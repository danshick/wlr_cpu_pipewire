#include <errno.h>
#include <stdio.h>
#include <sys/mman.h>

#include "screencast_common.h"
#include "pipewire_screencopy.h"
#include "wlr_screencopy.h"

struct screencast_context ctx;