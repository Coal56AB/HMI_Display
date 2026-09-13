#pragma once
extern "C" {
#include "display_api.h"
}

// Optional application hooks, called by the UI task. The platform supplies
// no-op defaults. Configure services before resource validation and GUI init.
void hmi_module_configure(DisplayPlatform *platform);
// Return false to keep the application's startup error visible and stop the UI.
bool hmi_module_start();
// Called after display_init; applications may start their own worker tasks here.
void hmi_module_run();
void hmi_module_tick(uint32_t now);
