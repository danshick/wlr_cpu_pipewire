//common includes
#include <errno.h>
#include <stdio.h>
#include <sys/mman.h>

//wlr includes
#include <fcntl.h>
#include <limits.h>
#include <png.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/param.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#include <wayland-client-protocol.h>
#include "wlr-screencopy-unstable-v1-client-protocol.h"

//pipewire includes
#include <time.h>
#include <spa/support/type-map.h>
#include <spa/param/format-utils.h>
#include <spa/param/video/format-utils.h>
#include <spa/param/props.h>
#include <pipewire/pipewire.h>

//pthread includes
#include <pthread.h>

//TODO: stop hardcoding
#define WIDTH 1600
#define HEIGHT 900
#define FRAMERATE 25
#define BUFFERS 1
#define ALIGN 16

//Disableable logger
//#define logger(...) printf(__VA_ARGS__)
#define logger(...)

static struct {
	uint32_t width;
	uint32_t height;
  uint32_t size;
  uint32_t stride;
	bool y_invert;
	uint64_t tv_sec;
  uint32_t tv_nsec;
	enum wl_shm_format format;

	struct wl_buffer *buffer;
	void *data;
} local_frame;

struct pwr_type {
	struct spa_type_media_type media_type;
	struct spa_type_media_subtype media_subtype;
	struct spa_type_format_video format_video;
	struct spa_type_video_format video_format;
	uint32_t meta_cursor;
};

static inline void init_type(struct pwr_type *type, struct pw_type *map)
{
	pw_type_get(map, SPA_TYPE__MediaType, &type->media_type);
	pw_type_get(map, SPA_TYPE__MediaSubtype, &type->media_subtype);
	pw_type_get(map, SPA_TYPE_FORMAT__Video, &type->format_video);
	pw_type_get(map, SPA_TYPE__VideoFormat, &type->video_format);
	pw_type_get(map, SPA_TYPE_META__Cursor, &type->meta_cursor);
};

struct screencast_context {

  // pipewire
  struct pwr_type type;
	
	struct pw_main_loop *loop;
	struct spa_source *event;
	struct pw_core *core;
	struct pw_type *t;
	struct pw_remote *remote;
	struct spa_hook remote_listener;
	struct pw_stream *stream;
	struct spa_hook stream_listener;
	uint32_t seq;
	struct spa_video_info_raw pwr_format;
	
	bool stream_state;

	pthread_t pwr_thread;

  // wlroots

  struct wl_display *display;
  struct wl_list output_list;
  struct wl_registry *registry;
  struct zwlr_screencopy_manager_v1 *screencopy_manager;
	struct wl_shm *shm;

  // main frame callback
	struct zwlr_screencopy_frame_v1 *frame_callback;

  // target output
  struct wayland_output *target_output;
  bool with_cursor;

	// frame
	struct zwlr_screencopy_frame_v1 *wlr_frame;

	// frame mutex
	pthread_mutex_t lock;

  // if something happens during capture
	int err;
	bool quit;
};


struct wayland_output {
	struct wl_list link;
	uint32_t id;
	struct wl_output *output;
	char *make;
	char *model;
	int width;
	int height;
  float framerate;
};

struct screencast_context ctx;

//
//
//
//### utilities ###
//
//
//

static char *strdup(const char *src) {
    char *dst = malloc(strlen (src) + 1);  // Space for length plus nul
    if (dst == NULL) return NULL;          // No memory
    strcpy(dst, src);                      // Copy the characters
    return dst;                            // Return the new string
}

static void wlr_frame_free(struct screencast_context *ctx);
static void wlr_register_cb(struct screencast_context *ctx);

//
//
//
//### pipewire ###
///
///
///

static void writeFrameData(void *pwFramePointer, void *wlrFramePointer, uint32_t height, uint32_t stride, bool inverted){

  if(!inverted){
    memcpy(pwFramePointer, wlrFramePointer, height * stride);
    return;
  }

  for (size_t i = 0; i < (size_t)height; ++i) {
    void *flippedWlrRowPointer = wlrFramePointer + ((height - i - 1) * stride);
    void *pwRowPointer = pwFramePointer + (i * stride);
    memcpy(pwRowPointer, flippedWlrRowPointer, stride);
	}
  return ;

}

static void pwr_on_event(void *data, uint64_t expirations){
	struct screencast_context *ctx = data;
  struct pw_buffer *pw_buf;
  struct spa_buffer *spa_buf;
	struct spa_meta_header *h;
	struct spa_data *d;

	if(!ctx->stream_state){
		wlr_frame_free(ctx);
		return ;
	}
  
	logger("pw event fired\n");

  if ((pw_buf = pw_stream_dequeue_buffer(ctx->stream)) == NULL) {
		printf("out of buffers\n");
		return;
	}
  
	spa_buf = pw_buf->buffer;
	d = spa_buf->datas;
	if ((d[0].data) == NULL)
			return;
	if ((h = spa_buffer_find_meta(spa_buf, ctx->t->meta.Header))) {
		h->pts = -1;
		h->flags = 0;
		h->seq = ctx->seq++;
		h->dts_offset = 0;
	}

	d[0].type = ctx->t->data.MemPtr;
	d[0].maxsize = local_frame.size;
	//d[0].data = local_frame.data;
	d[0].mapoffset = 0;
	d[0].chunk->size = local_frame.size;
	d[0].chunk->stride = local_frame.stride;
	d[0].chunk->offset = 0;
	d[0].flags = 0;
	d[0].fd = -1;

  writeFrameData(d[0].data, local_frame.data, local_frame.height, local_frame.stride, local_frame.y_invert);

	logger("************** \n");
	logger("pointer: %p\n", d[0].data);
	logger("size: %d\n", d[0].maxsize);
	logger("stride: %d\n", d[0].chunk->stride);
	logger("width: %d\n", local_frame.width);
	logger("height: %d\n", local_frame.height);
	logger("y_invert: %d\n", local_frame.y_invert);
	logger("************** \n");

  pw_stream_queue_buffer(ctx->stream, pw_buf);

	wlr_frame_free(ctx);
}


static void pwr_handle_stream_state_changed(void *data, enum pw_stream_state old, enum pw_stream_state state,
				    const char *error) {
	struct screencast_context *ctx = data;

	printf("stream state: \"%s\"\n", pw_stream_state_as_string(state));
  printf("node id: %d\n", pw_stream_get_node_id(ctx->stream));

	switch (state) {
    case PW_STREAM_STATE_PAUSED:
			ctx->stream_state = false;
      break;
    case PW_STREAM_STATE_STREAMING:
			ctx->stream_state = true; 
      break;
    default:
			ctx->stream_state = false;
      break;
  }
}

static void pwr_handle_stream_format_changed(void *data, const struct spa_pod *format) {
	struct screencast_context *ctx = data;
	struct pw_stream *stream = ctx->stream;
	struct pw_type *t = ctx->t;
	uint8_t params_buffer[1024];
	struct spa_pod_builder b = SPA_POD_BUILDER_INIT(params_buffer, sizeof(params_buffer));
	const struct spa_pod *params[2];

	if (format == NULL) {
		pw_stream_finish_format(stream, 0, NULL, 0);
		return;
	}
	spa_format_video_raw_parse(format, &ctx->pwr_format, &ctx->type.format_video);

	params[0] = spa_pod_builder_object(&b,
		t->param.idBuffers, t->param_buffers.Buffers,
		":", t->param_buffers.size,    "i", local_frame.size,
		":", t->param_buffers.stride,  "i", local_frame.stride,
		":", t->param_buffers.buffers, "iru", BUFFERS,
			SPA_POD_PROP_MIN_MAX(1, 32),
		":", t->param_buffers.align,   "i", ALIGN);

	params[1] = spa_pod_builder_object(&b,
		t->param.idMeta, t->param_meta.Meta,
		":", t->param_meta.type, "I", t->meta.Header,
		":", t->param_meta.size, "i", sizeof(struct spa_meta_header));

	pw_stream_finish_format(stream, 0, params, 2);
}

static const struct pw_stream_events pwr_stream_events = {
	PW_VERSION_STREAM_EVENTS,
	.state_changed = pwr_handle_stream_state_changed,
	.format_changed = pwr_handle_stream_format_changed,
};


static void pwr_handle_state_changed(void *data, enum pw_remote_state old, enum pw_remote_state state, const char *error)
{
  struct screencast_context *ctx = data;
	struct pw_remote *remote = ctx->remote;

	switch (state) {
	case PW_REMOTE_STATE_ERROR:
		printf("remote error: %s\n", error);
		pw_main_loop_quit(ctx->loop);
		break;

	case PW_REMOTE_STATE_CONNECTED:
	{
		const struct spa_pod *params[1];
		uint8_t buffer[1024];
		struct spa_pod_builder b = SPA_POD_BUILDER_INIT(buffer, sizeof(buffer));

		logger("remote state: \"%s\"\n", pw_remote_state_as_string(state));

		ctx->stream = pw_stream_new(remote,
				"wlr_screeencopy",
				pw_properties_new(
					"media.class", "Video/Source",
					PW_NODE_PROP_MEDIA, "Video",
					PW_NODE_PROP_CATEGORY, "Source",
					PW_NODE_PROP_ROLE, "Screen",
					NULL));

		params[0] = spa_pod_builder_object(&b,
			ctx->t->param.idEnumFormat, ctx->t->spa_format,
			"I", ctx->type.media_type.video,
			"I", ctx->type.media_subtype.raw,
			":", ctx->type.format_video.format,    "I", ctx->type.video_format.BGRA,
			":", ctx->type.format_video.size,      "Rru", &SPA_RECTANGLE(WIDTH, HEIGHT),
				SPA_POD_PROP_MIN_MAX(&SPA_RECTANGLE(1, 1),
						     						 &SPA_RECTANGLE(4096, 4096)),
			":", ctx->type.format_video.framerate, "F", &SPA_FRACTION(FRAMERATE, 1));

		pw_stream_add_listener(ctx->stream,
				       &ctx->stream_listener,
				       &pwr_stream_events,
				       ctx);

		pw_stream_connect(ctx->stream,
				  PW_DIRECTION_OUTPUT,
				  NULL,
				  PW_STREAM_FLAG_DRIVER |
				  PW_STREAM_FLAG_MAP_BUFFERS,
				  params, 1);
    
		break;
	}
	default:
		logger("remote state: \"%s\"\n", pw_remote_state_as_string(state));
		break;
	}
}

static const struct pw_remote_events pwr_remote_events = {
	PW_VERSION_REMOTE_EVENTS,
	.state_changed = pwr_handle_state_changed,
};


void *pwr_start(void *data){

  struct screencast_context *ctx = data;
	
  pw_init(NULL, NULL);

	/* create a main loop */
	ctx->loop = pw_main_loop_new(NULL);
	ctx->core = pw_core_new(pw_main_loop_get_loop(ctx->loop), NULL);
	ctx->t = pw_core_get_type(ctx->core);
	ctx->remote = pw_remote_new(ctx->core, NULL, 0);

	init_type(&ctx->type, ctx->t);

	/* make an event to signal frame ready */
	ctx->event = pw_loop_add_event(pw_main_loop_get_loop(ctx->loop), pwr_on_event, ctx);

	pw_remote_add_listener(ctx->remote, &ctx->remote_listener, &pwr_remote_events, ctx);
	pw_remote_connect(ctx->remote);

	/* run the loop, this will trigger the callbacks */
	pw_main_loop_run(ctx->loop);

	pw_core_destroy(ctx->core);
	pw_main_loop_destroy(ctx->loop);
  return NULL;
}

//
//
//
//### wlroots ###
//
//
//

static void wlr_frame_free(struct screencast_context *ctx) {

	zwlr_screencopy_frame_v1_destroy(ctx->wlr_frame);
	munmap(local_frame.data, local_frame.size);
	wl_buffer_destroy(local_frame.buffer);
	logger("wlr frame destroyed\n");
	pthread_mutex_unlock(&ctx->lock);

}

static struct wl_buffer *create_shm_buffer(struct screencast_context *ctx, enum wl_shm_format fmt,
		int width, int height, int stride, void **data_out) {
	int size = stride * height;

	const char shm_name[] = "/wlroots-screencopy";
	int fd = shm_open(shm_name, O_RDWR | O_CREAT | O_EXCL, S_IRUSR | S_IWUSR);
	if (fd < 0) {
		fprintf(stderr, "shm_open failed\n");
		return NULL;
	}
	shm_unlink(shm_name);

	int ret;
	while ((ret = ftruncate(fd, size)) == EINTR) {
		// No-op
	}
	if (ret < 0) {
		close(fd);
		fprintf(stderr, "ftruncate failed\n");
		return NULL;
	}

	void *data = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	if (data == MAP_FAILED) {
		fprintf(stderr, "mmap failed: %m\n");
		close(fd);
		return NULL;
	}

	struct wl_shm_pool *pool = wl_shm_create_pool(ctx->shm, fd, size);
	close(fd);
	struct wl_buffer *buffer = wl_shm_pool_create_buffer(pool, 0, width, height,
		stride, fmt);
	wl_shm_pool_destroy(pool);

	*data_out = data;
	return buffer;
}

static void wlr_frame_buffer(void *data, struct zwlr_screencopy_frame_v1 *frame, uint32_t format,
		uint32_t width, uint32_t height, uint32_t stride) {
  struct screencast_context *ctx = data;
	
	pthread_mutex_lock(&ctx->lock);

  logger("wlr buffer event handler\n");
	ctx->wlr_frame = frame;
	local_frame.width = width;
	local_frame.height = height;
	local_frame.stride = stride;
	local_frame.size = stride * height;
	local_frame.format = format;
	local_frame.buffer = 
		create_shm_buffer(ctx, format, width, height, stride, &local_frame.data);
	if (local_frame.buffer == NULL) {
		fprintf(stderr, "failed to create buffer\n");
		exit(EXIT_FAILURE);
	}
	
	zwlr_screencopy_frame_v1_copy(frame, local_frame.buffer);
	pthread_mutex_unlock(&ctx->lock);
}

static void wlr_frame_flags(void *data,
		struct zwlr_screencopy_frame_v1 *frame, uint32_t flags) {
  struct screencast_context *ctx = data;

	pthread_mutex_lock(&ctx->lock);

	logger("wlr flags event handler\n");
  local_frame.y_invert = flags & ZWLR_SCREENCOPY_FRAME_V1_FLAGS_Y_INVERT;

	pthread_mutex_unlock(&ctx->lock);

}

static void wlr_frame_ready(void *data, struct zwlr_screencopy_frame_v1 *frame,
		uint32_t tv_sec_hi, uint32_t tv_sec_lo, uint32_t tv_nsec) {
  struct screencast_context *ctx = data;

	pthread_mutex_lock(&ctx->lock);

  logger("wlr ready event handler\n");

  local_frame.tv_sec = ((((uint64_t)tv_sec_hi) << 32) | tv_sec_lo);
  local_frame.tv_nsec = tv_nsec;
  
  if (!ctx->quit && !ctx->err) {
	  pw_loop_signal_event(pw_main_loop_get_loop(ctx->loop), ctx->event);
		//sleep(1);
		wlr_register_cb(ctx);
	}

}

static void wlr_frame_failed(void *data, struct zwlr_screencopy_frame_v1 *frame) {
	struct screencast_context *ctx = data;

  logger("wlr failed event handler\n");

  wlr_frame_free(ctx);
	ctx->err = true;
}

static const struct zwlr_screencopy_frame_v1_listener wlr_frame_listener = {
	.buffer = wlr_frame_buffer,
	.flags = wlr_frame_flags,
	.ready = wlr_frame_ready,
	.failed = wlr_frame_failed
};

static void wlr_register_cb(struct screencast_context *ctx) {
	ctx->frame_callback = zwlr_screencopy_manager_v1_capture_output(
			ctx->screencopy_manager, ctx->with_cursor, ctx->target_output->output);

	zwlr_screencopy_frame_v1_add_listener(ctx->frame_callback,
			&wlr_frame_listener, ctx);
		logger("wlr callbacks registered\n");
}

static void wlr_output_handle_geometry(void *data, struct wl_output *wl_output,
		int32_t x, int32_t y, int32_t phys_width, int32_t phys_height,
		int32_t subpixel, const char *make, const char *model,
		int32_t transform) {
	struct wayland_output *output = data;
	output->make = strdup(make);
	output->model = strdup(model);
}

static void wlr_output_handle_mode(void *data, struct wl_output *wl_output,
		uint32_t flags, int32_t width, int32_t height, int32_t refresh) {
	if (flags & WL_OUTPUT_MODE_CURRENT) {
		struct wayland_output *output = data;
		output->width = width;
		output->height = height;
    output->framerate = (float) refresh * 1000;
	}
}

static void wlr_output_handle_done(void* data, struct wl_output *wl_output) {
	/* Nothing to do */
}

static void wlr_output_handle_scale(void* data, struct wl_output *wl_output,
		int32_t factor) {
	/* Nothing to do */
}

static const struct wl_output_listener wlr_output_listener = {
	.geometry = wlr_output_handle_geometry,
	.mode = wlr_output_handle_mode,
	.done = wlr_output_handle_done,
	.scale = wlr_output_handle_scale,
};

static struct wayland_output *wlr_find_output(struct screencast_context *ctx,
		struct wl_output *out, uint32_t id) {
	struct wayland_output *output, *tmp;
	wl_list_for_each_safe(output, tmp, &ctx->output_list, link) {
		if ((output->output == out) || (output->id == id)) {
			return output;
		}
	}
	return NULL;
}

static void wlr_remove_output(struct wayland_output *out) {
	wl_list_remove(&out->link);
}

static void wlr_registry_handle_add(void *data, struct wl_registry *reg,
		uint32_t id, const char *interface, uint32_t ver) {
	struct screencast_context *ctx = data;

	if (!strcmp(interface, wl_output_interface.name)) {
		struct wayland_output *output = malloc(sizeof(*output));

		output->id = id;
		output->output = wl_registry_bind(reg, id, &wl_output_interface, 1);

		wl_output_add_listener(output->output, &wlr_output_listener, output);
		wl_list_insert(&ctx->output_list, &output->link);
	}

	if (!strcmp(interface, zwlr_screencopy_manager_v1_interface.name)) {
		ctx->screencopy_manager = wl_registry_bind(reg, id,
				&zwlr_screencopy_manager_v1_interface, 1);
	}

	if (strcmp(interface, wl_shm_interface.name) == 0) {
		ctx->shm = wl_registry_bind(reg, id, &wl_shm_interface, 1);
	}
}

static void wlr_registry_handle_remove(void *data, struct wl_registry *reg,
		uint32_t id) {
	wlr_remove_output(wlr_find_output((struct screencast_context *)data, NULL, id));
}

static const struct wl_registry_listener wlr_registry_listener = {
	.global = wlr_registry_handle_add,
	.global_remove = wlr_registry_handle_remove,
};

static int wlr_screencopy_init(struct screencast_context *ctx) {
  //connect to wayland display WAYLAND_DISPLAY or 'wayland-0' if not set
	ctx->display = wl_display_connect(NULL);
	if (!ctx->display) {
		printf("Failed to connect to display!\n");
		return -1;
	}

  //retrieve list of outputs
	wl_list_init(&ctx->output_list);

  //retrieve registry
	ctx->registry = wl_display_get_registry(ctx->display);
	wl_registry_add_listener(ctx->registry, &wlr_registry_listener, ctx);

	wl_display_roundtrip(ctx->display);
	wl_display_dispatch(ctx->display);

	//make sure our wlroots supports screencopy protocol
	if (!ctx->shm) {
		printf("Compositor doesn't support %s!\n",
				"wl_shm");
		return -1;
	}

  //make sure our wlroots supports screencopy protocol
	if (!ctx->screencopy_manager) {
		printf("Compositor doesn't support %s!\n",
				zwlr_screencopy_manager_v1_interface.name);
		return -1;
	}

	return 0;
}

static void wlr_screencopy_uninit(struct screencast_context *ctx) {
	struct wayland_output *output, *tmp_o;
	wl_list_for_each_safe(output, tmp_o, &ctx->output_list, link) {
  	wl_list_remove(&output->link);
	}

	if (ctx->screencopy_manager) {
		zwlr_screencopy_manager_v1_destroy(ctx->screencopy_manager);
	}
}

int main(int argc, char *argv[]) {
  int err;
  struct screencast_context ctx = (struct screencast_context){ 0 };

  err = wlr_screencopy_init(&ctx);
  if (err) {
    goto end;
  }
  
  int output_id;
  struct wayland_output *output, *tmp_o;
	wl_list_for_each_reverse_safe(output, tmp_o, &ctx.output_list, link) {
		printf("Capturable output: %s Model: %s: ID: %i\n",
				output->make, output->model, output->id);
    output_id = output->id;
	}

  output = wlr_find_output(&ctx, NULL, output_id);
	if (!output) {
		printf("Unable to find output with ID %i!\n", output_id);
		return 1;
	}
  
  ctx.target_output = output;
	ctx.with_cursor = true;

  printf("wl_display fd: %d\n", wl_display_get_fd(ctx.display));

	pthread_mutex_init(&ctx.lock, NULL);

 	wlr_register_cb(&ctx);
  
  pthread_create(&ctx.pwr_thread, NULL, pwr_start, &ctx);

	/* Run capture */
	while (wl_display_dispatch(ctx.display) != -1 && !ctx.err && !ctx.quit);
  
  pthread_join(ctx.pwr_thread, NULL);

  return 0;

  end:
    wlr_screencopy_uninit(&ctx);
    return err;
}
