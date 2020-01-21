#include "main.h"

int main(int argc, char *argv[]) {
  int err;
  struct screencast_context ctx = (struct screencast_context){0};
  ctx.simple_frame = (struct simple_frame){0};
  ctx.simple_frame.damage = &(struct damage){0};

  err = wlr_screencopy_init(&ctx);
  if (err) {
    goto end;
  }

  int output_id;
  struct wayland_output *output, *tmp_o;
  wl_list_for_each_reverse_safe(output, tmp_o, &ctx.output_list, link) {
    printf("Capturable output: %s Model: %s: ID: %i\n", output->make,
           output->model, output->id);
    output_id = output->id;
  }

  int c = 0;
  extern char *optarg;
  char *endstring;
  static char usage[] = "usage: %s [-l] [-o output_id\n]";

  while ((c = getopt(argc, argv, "lo:")) != -1) {
    switch (c) {

    case 'l':
      exit(EXIT_SUCCESS);
      break;

    case 'o':
      if (!optarg) {
        printf("The -o option requires a valid integer argument\n");
        fprintf(stderr, usage, argv[0]);
        exit(EXIT_FAILURE);
      }
      output_id = strtol(optarg, &endstring, 10);
      if (endstring == optarg || *endstring != '\0') {
        printf("The -o option requires a valid integer argument\n");
        fprintf(stderr, usage, argv[0]);
        exit(EXIT_FAILURE);
      }
      break;

    case '?':
      fprintf(stderr, usage, argv[0]);
      exit(EXIT_FAILURE);

    default:
      break;
    }
  }

  output = wlr_find_output(&ctx, NULL, output_id);
  if (!output) {
    printf("Unable to find output with ID %i!\n", output_id);
    return 1;
  }

  ctx.target_output = output;
  ctx.framerate = output->framerate;
  ctx.with_cursor = true;

  printf("wl_display fd: %d\n", wl_display_get_fd(ctx.display));

  pthread_mutex_init(&ctx.lock, NULL);

  wlr_register_cb(&ctx);

  pthread_create(&ctx.pwr_thread, NULL, pwr_start, &ctx);

  /* Run capture */
  while (wl_display_dispatch(ctx.display) != -1 && !ctx.err && !ctx.quit)
    ;

  pthread_join(ctx.pwr_thread, NULL);

  return 0;

end:
  wlr_screencopy_uninit(&ctx);
  return err;
}
