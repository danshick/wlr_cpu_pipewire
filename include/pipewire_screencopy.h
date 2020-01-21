#include <stdio.h>

#include <spa/support/type-map.h>
#include <spa/param/format-utils.h>
#include <spa/param/video/format-utils.h>
#include <spa/param/props.h>
#include <pipewire/pipewire.h>

#include "wlr_screencopy.h"

#include "screencast_common.h"

#define BUFFERS 1
#define ALIGN 16

void *pwr_start(void *data);