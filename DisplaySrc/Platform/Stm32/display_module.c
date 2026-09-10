#include "module_services.h"
#include "hmi_ui.h"
#include "hmi_storage.h"
#include "hmi_gfx.h"
#include "hmi_assets_layout_full.h"
#include "telemetry.h"
#include "journal_app.h"

#include <string.h>
#if HMI_ASSET_LENGTH > 0x128000u
#error Resources overlap module journal
#endif
static HmiUi app_ui;
static void put32(uint8_t *b,uint32_t value){unsigned i;for(i=0;i<4;i++)b[i]=(uint8_t)(value>>(i*8));}
/* Example wire frame: A5 5A, version, payload size, sequence, type, id,
 * float32 value, flags, data[8], CRC16-CCITT (little endian). No struct padding. */
static void module_event(const HmiEvent *e){
    uint8_t b[30]={0xa5,0x5a,1,24};uint32_t value;uint16_t crc=0xffff;unsigned i,j;
    memcpy(&value,&e->value,4);put32(b+4,e->sequence);b[8]=(uint8_t)e->type;b[9]=0;
    b[10]=(uint8_t)e->id;b[11]=(uint8_t)(e->id>>8);put32(b+12,value);put32(b+16,e->flags);memcpy(b+20,e->data,8);
    for(i=0;i<28;i++){crc^=(uint16_t)b[i]<<8;for(j=0;j<8;j++)crc=(uint16_t)((crc<<1)^((crc&0x8000)?0x1021:0));}
    b[28]=(uint8_t)crc;b[29]=(uint8_t)(crc>>8);
    module_platform->send(b,sizeof(b));
}

const DisplayPlatform *module_platform;
void telemetry_send(const uint8_t *p,uint16_t n){module_platform->send(p,n);}
void telemetry_reset(void){module_platform->reset();}
void display_init(const DisplayPlatform *platform){
    module_platform=platform;
    hmi_ui_init(&app_ui,(HmiDisplay){platform->write_rect,0});
    app_ui.state.telemetry_flags=192;app_ui.state.power_state=HMI_POWER_OFF;
    telemetry_init(&app_ui);journal_app_init(&app_ui);
}
void display_event(const DisplayEvent *event){
    if(event->type==DISPLAY_RX_BYTE){telemetry_byte(event->byte);return;}
    if(event->type==DISPLAY_RX_ERROR){telemetry_rx_error();return;}
    HmiEvent e;uint8_t selected=app_ui.state.selected_control;
    hmi_ui_touch(&app_ui,event->x,event->y,event->down,event->now_ms);
    while(hmi_ui_poll_event(&app_ui,&e)){journal_app_event(&e);module_event(&e);}
    if(selected!=app_ui.state.selected_control){telemetry_selected(event->now_ms);memset(&e,0,sizeof(e));e.type=HMI_EVENT_CUSTOM;e.id=400;e.value=app_ui.state.selected_control;journal_app_event(&e);module_event(&e);}
}
void display_step(uint32_t now){telemetry_poll(&app_ui,now);journal_app_tick(now);hmi_ui_render(&app_ui);}
static int validate(const DisplayPlatform *platform){module_platform=platform;return hmi_storage_init(platform->read_assets,0);}
static unsigned error(void){return (unsigned)hmi_storage_error();}
static unsigned loads(void){return hmi_storage_loads;}
const DisplayModule display_module={DISPLAY_API_VERSION,HMI_ASSET_LENGTH,validate,error,loads};
void hmi_storage_progress(unsigned done,unsigned total){if(total)module_platform->boot_progress(4,done*100u/total);}
void journal_init_progress(unsigned done,unsigned total){if(total)module_platform->boot_progress(5,done*100u/total);}
