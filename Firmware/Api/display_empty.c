/* Optional empty module. Remove this source from the build when connecting a GUI. */
#include "display_renderer.h"
DISPLAY_RENDER_STORAGE(render_storage,480*2,0);
static DisplayRenderer renderer;
static void paint(DisplayCanvas *canvas,void *user) {
    (void)user;
    display_canvas_fill(canvas,0,0,480,320,0);
}
const DisplayModule display_module={DISPLAY_API_VERSION,0,0,0,0};
void display_init(const DisplayPlatform *platform){
    display_renderer_init(&renderer,platform,480,320,render_storage,0);
    /* No initial invalidation: keep the platform boot terminal visible.
     * A GUI calls display_renderer_full/invalidate when its content changes. */
}
void display_event(const DisplayEvent *event){(void)event;}
void display_step(uint32_t now_ms){
    (void)now_ms;
    display_renderer_step(&renderer,paint,0,0);
}
