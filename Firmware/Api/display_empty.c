/* Optional empty module. Remove this source from the build when connecting a GUI. */
#include "display_api.h"
const DisplayModule display_module={DISPLAY_API_VERSION,0,0,0,0};
void display_init(const DisplayPlatform *platform){(void)platform;}
void display_event(const DisplayEvent *event){(void)event;}
void display_step(uint32_t now_ms){(void)now_ms;}
