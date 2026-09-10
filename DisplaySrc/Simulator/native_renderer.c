/* Desktop adapter only. Production renderer and UART parser are compiled unchanged. */
#include "hmi_ui.h"
#include "hmi_storage.h"
#include "telemetry.h"
#include <stdio.h>
#include <string.h>
#define API __declspec(dllexport)
static HmiUi ui;static uint16_t frame[320*480];static FILE *assets;
static uint32_t now_ms;
static int read_asset(uint32_t at,void *d,uint32_t n,void *u){(void)u;return fseek(assets,at,SEEK_SET)==0&&fread(d,1,n,assets)==n;}
static void flush(uint16_t x,uint16_t y,uint16_t w,uint16_t h,const uint16_t *p,uint16_t stride,void *u){(void)u;for(unsigned row=0;row<h;row++)memcpy(frame+(y+row)*320+x,p+row*stride,w*2);}
void telemetry_send(const uint8_t *p,uint16_t n){(void)p;(void)n;}
void telemetry_reset(void){}
API int renderer_init(const char *path){assets=fopen(path,"rb");if(!assets||!hmi_storage_init(read_asset,0))return 0;hmi_ui_init(&ui,(HmiDisplay){flush,0});telemetry_init(&ui);return 1;}
API void renderer_packet(const uint8_t *p,unsigned n,unsigned tick){now_ms=tick;for(unsigned i=0;i<n;i++){telemetry_byte(p[i]);telemetry_poll(&ui,tick);}}
API const uint16_t *renderer_frame(void){hmi_ui_render(&ui);return frame;}
API void renderer_touch(int x,int y,int down,unsigned tick){hmi_ui_touch(&ui,x,y,(uint8_t)down,tick);}
API int renderer_event(HmiEvent *e){return hmi_ui_poll_event(&ui,e);}
API unsigned renderer_graph(void){return ui.state.graph_page|(ui.graph_running<<8)|((ui.state.page==HMI_PAGE_GRAPHS?1u:0u)<<9);}
API unsigned renderer_division(void){return ui.graph_time_ms;}
API void renderer_clock(const char *text){if(strlen(text)!=5)return;memcpy(ui.clock,text,6);hmi_invalidate((HmiRect){256,1,58,33});}

API void renderer_page(unsigned page){hmi_ui_dispatch(&ui,HMI_ACTION_PAGE,(int16_t)page);}
API unsigned renderer_validate_plot(void){
    static uint16_t before[320*480];hmi_ui_render(&ui);memcpy(before,frame,sizeof(frame));hmi_invalidate_all();hmi_ui_render(&ui);
    for(unsigned y=106;y<284;y++)for(unsigned x=30;x<286;x++)if(before[y*320+x]!=frame[y*320+x])return y*320+x;
    return 0;
}

API void renderer_settings(unsigned group,unsigned mask){if(group<4){ui.settings[group]=mask;hmi_invalidate_all();}}
API void renderer_section(unsigned section){hmi_ui_dispatch(&ui,HMI_ACTION_SECTION,(int16_t)section);}
API void renderer_dialog(unsigned dialog){hmi_ui_dispatch(&ui,HMI_ACTION_DIALOG,(int16_t)dialog);}

API unsigned renderer_validate_frame(void){
    static uint16_t before[320*480];hmi_ui_render(&ui);memcpy(before,frame,sizeof(frame));hmi_invalidate_all();hmi_ui_render(&ui);
    for(unsigned i=0;i<320*480;i++)if(before[i]!=frame[i])return i+1;
    return 0;
}
API unsigned renderer_pixels(void){hmi_ui_render(&ui);return hmi_dirty_pixel_count();}

API unsigned renderer_cursor(void){return ui.state.graph_cursor;}
API unsigned renderer_valid_count(void){return ui.state.graph_valid_count;}
API void renderer_action(int action,int arg){hmi_ui_dispatch(&ui,(HmiAction)action,(int16_t)arg);}

API unsigned renderer_offset(void){return ui.state.graph_offset_ms;}
