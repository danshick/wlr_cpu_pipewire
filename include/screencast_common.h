#include <pthread.h>

#include <pipewire/pipewire.h>
#include <spa/param/video/format-utils.h>

#include <libdrm/drm_fourcc.h>

#include <wayland-client-protocol.h>

// Disableable logger
//#define logger(...) printf(__VA_ARGS__)
#define logger(...)

#ifndef SCREENCAST_COMMON_H
#define SCREENCAST_COMMON_H

struct damage {
  uint32_t x;
  uint32_t y;
  uint32_t width;
  uint32_t height;
};

struct simple_frame {
  uint32_t width;
  uint32_t height;
  uint32_t size;
  uint32_t stride;
  bool y_invert;
  uint64_t tv_sec;
  uint32_t tv_nsec;
  enum wl_shm_format format;
  struct damage *damage;

  struct wl_buffer *buffer;
  void *data;
};

struct pwr_type {
  struct spa_type_media_type media_type;
  struct spa_type_media_subtype media_subtype;
  struct spa_type_format_video format_video;
  struct spa_type_video_format video_format;
  uint32_t meta_cursor;
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
  uint32_t framerate;
  bool with_cursor;

  // frame
  struct zwlr_screencopy_frame_v1 *wlr_frame;
  struct simple_frame simple_frame;

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

#endif /* SCREENCAST_COMMON_H */

uint32_t pipewire_from_wl_shm(void *data);
char *strdup(const char *src);