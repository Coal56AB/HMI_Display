/* Standalone hardware check: six colour bars and a touch crosshair. */
#include "display_api.h"
#include <string.h>
static const DisplayPlatform *p;
static uint16_t pixels[480 * 16];
static int cursor_x = -1, cursor_y = -1;
static unsigned next_y;
static const uint16_t colours[] = {0xf800,0x07e0,0x001f,0xffff,0x07ff,0xffe0};
const DisplayModule display_module = {DISPLAY_API_VERSION, 0, 0, 0, 0};
void display_init(const DisplayPlatform *platform) { p = platform; next_y = 0; }
void display_event(const DisplayEvent *event) {
    if (event->type != DISPLAY_TOUCH) return;
    const int x = event->down ? event->x : -1, y = event->down ? event->y : -1;
    if (x != cursor_x || y != cursor_y) { cursor_x = x; cursor_y = y; next_y = 0; }
}
void display_step(uint32_t now) {
    (void)now;
    if (next_y >= 320) return;
    for (unsigned y = 0; y < 16; ++y) for (unsigned x = 0; x < 480; ++x) {
        const int dy = (int)(next_y + y) - cursor_y, dx = (int)x - cursor_x;
        pixels[y * 480 + x] = cursor_x >= 0 &&
            ((dx >= -8 && dx <= 8 && dy >= -1 && dy <= 1) ||
             (dy >= -8 && dy <= 8 && dx >= -1 && dx <= 1)) ? 0 : colours[x / 80];
    }
    p->write_rect(0, (uint16_t)next_y, 480, 16, pixels, 480, 0);
    next_y += 16;
}
