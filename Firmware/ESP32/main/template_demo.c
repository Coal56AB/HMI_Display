/* Standalone hardware check: six colour bars and a touch crosshair. */
#include "display_renderer.h"
DISPLAY_RENDER_STORAGE(render_storage,480*16,0);
static DisplayRenderer renderer;
static int cursor_x = -1, cursor_y = -1;
static const uint16_t colours[] = {0xf800,0x07e0,0x001f,0xffff,0x07ff,0xffe0};
const DisplayModule display_module = {DISPLAY_API_VERSION, 0, 0, 0, 0};
void display_init(const DisplayPlatform *platform) {
    display_renderer_init(&renderer,platform,480,320,render_storage,0);
    cursor_x=cursor_y=-1;
    display_renderer_full(&renderer);
}
static void invalidate_cursor(void) {
    if(cursor_x>=0)display_renderer_invalidate(&renderer,cursor_x-8,cursor_y-8,17,17);
}
void display_event(const DisplayEvent *event) {
    if (event->type != DISPLAY_TOUCH && event->type != DISPLAY_TOUCH_CANCEL) return;
    const int down=event->type==DISPLAY_TOUCH && event->down;
    const int x = down ? event->x : -1, y = down ? event->y : -1;
    if (x != cursor_x || y != cursor_y) {
        invalidate_cursor();cursor_x=x;cursor_y=y;invalidate_cursor();
    }
}
static void paint(DisplayCanvas *canvas,void *user) {
    (void)user;
    for(int i=0;i<6;++i)display_canvas_fill(canvas,i*80,0,80,320,colours[i]);
    if(cursor_x>=0) {
        display_canvas_fill(canvas,cursor_x-8,cursor_y-1,17,3,0);
        display_canvas_fill(canvas,cursor_x-1,cursor_y-8,3,17,0);
    }
}
void display_step(uint32_t now) {
    (void)now;
    display_renderer_step(&renderer,paint,0,0);
}
