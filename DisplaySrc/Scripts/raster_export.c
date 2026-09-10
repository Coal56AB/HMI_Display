/* Host-only: bake static backgrounds with exactly the runtime replacement
 * anchors. No dynamic values are baked. Production never allocates a frame. */
#include "hmi_ui.h"
#include "hmi_gfx.h"
#include <stdio.h>
#include <stdlib.h>
void hmi_draw_generated_scene(HmiSceneId id,uint16_t y,uint16_t h);
static void discard(uint16_t x,uint16_t y,uint16_t w,uint16_t h,const uint16_t *p,uint16_t stride,void *u){
    (void)x;(void)y;(void)w;(void)h;(void)p;(void)stride;(void)u;
}
#include "raster_context.h"
int main(int argc,char **argv){
    HmiUi ui;unsigned id,y;FILE *out;if(argc!=2)return 2;
    out=fopen(argv[1],"wb");if(!out)return 2;
    for(id=0;id<HMI_SCENE_COUNT+7u;id++){
        unsigned source=id<HMI_SCENE_COUNT?id:id-36u;
        raster_context(&ui,source);
        if(id>=HMI_SCENE_COUNT)ui.state.telemetry_flags=128u;
        hmi_ui_prepare_text_layers(&ui);
        ui_set_live_graph_background(id>=HMI_SCENE_COUNT);
        for(y=0;y<480;y+=4){
            ui_begin_rect(0,(int)y,320,4,0);
            hmi_draw_generated_scene((HmiSceneId)source,(uint16_t)(y&~15u),16);
            if(fwrite(ui_strip_data(),2,1280,out)!=1280)return 3;
        }
        ui_text_layers_end();
    }
    return fclose(out)!=0;
}
