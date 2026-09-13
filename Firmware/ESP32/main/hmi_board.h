#pragma once
extern "C" {
#include "display_api.h"
}
#include <stdint.h>
void hmi_lcd_init();
void hmi_board_init();
void hmi_boot_status(const char *label, unsigned percent, unsigned dwell_ms = 200);
void hmi_touch_poll(uint32_t now);
extern DisplayPlatform hmi_platform;
struct HmiDiagnostics {
    uint32_t touch_polls, touch_rejects, touch_cancels, touch_events, max_loop_ms;
    uint32_t rectangles, pixels, last_write_us, max_write_us;
    uint16_t raw_x, raw_y;
    int16_t touch_x, touch_y;
    uint8_t touch_down;
};
extern volatile HmiDiagnostics hmi_debug;
