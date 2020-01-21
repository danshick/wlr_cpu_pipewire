# wlr_cpu_pipewire

## Building

Depends on pipewire 0.2

Currently, the height, width, framerate, and alignment data is hardcoded, and must be correct in order for this to function properly.

    meson build
    ninja -C build

## Running

    ./build/wlr_cpu_pipewire

## Tooling

In order to test this, it is recommended that you use gstreamer, as shown in the test_me script. That requires gstreamer, pipewire 0.2 built with the gstreamer plugins, and gstreamer-plugins-good.

## Todo

- [x] Support output selection
- [x] Support dynamic width/height
- [x] Support copy_with_damage to reduce CPU load
- [ ] Add a timer to reregister the frame callbacks to cap the framerate
- [x] Refactoring