#include "display_module.h"
#include "hmi_ui.h"
#include "hmi_storage.h"
#include "hmi_gfx.h"
#include "hmi_assets_layout_full.h"
#include "telemetry.h"
#include "journal_app.h"
#include "board.h"
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
    display_transport_send(b,sizeof(b));
}

void telemetry_send(const uint8_t *p,uint16_t n){display_transport_send(p,n);}
void telemetry_reset(void){display_system_reset();}
void display_init(DisplayWriteRect write){hmi_ui_init(&app_ui,(HmiDisplay){write,0});app_ui.state.telemetry_flags=192;app_ui.state.power_state=HMI_POWER_OFF;telemetry_init(&app_ui);journal_app_init(&app_ui);}
void display_render(void){hmi_ui_render(&app_ui);}
void display_touch(int16_t x,int16_t y,uint8_t down,uint32_t now){
    HmiEvent e;uint8_t selected=app_ui.state.selected_control;
    hmi_ui_touch(&app_ui,x,y,down,now);
    while(hmi_ui_poll_event(&app_ui,&e)){journal_app_event(&e);module_event(&e);}
    if(selected!=app_ui.state.selected_control){telemetry_selected(now);memset(&e,0,sizeof(e));e.type=HMI_EVENT_CUSTOM;e.id=400;e.value=app_ui.state.selected_control;journal_app_event(&e);module_event(&e);}
}
void display_poll(uint32_t now){telemetry_poll(&app_ui,now);journal_app_tick(now);}
void display_receive(uint8_t b){telemetry_byte(b);}
void display_receive_error(void){telemetry_rx_error();}
int display_assets_init(DisplayReadAssets read){return hmi_storage_init(read,0);}
unsigned display_assets_error(void){return (unsigned)hmi_storage_error();}
uint32_t display_assets_size(void){return HMI_ASSET_LENGTH;}
void display_asset_loads_reset(void){hmi_storage_loads=0;}
unsigned display_asset_loads(void){return hmi_storage_loads;}
void display_console_begin(unsigned y){ui_begin_rect(0,y,320,4,0);ui_set_clip(0,0,320,480);}
void display_console_pixel(int x,int y,unsigned size,uint16_t c){ui_fill_rect(x,y,size,size,c);}
const uint16_t *display_console_pixels(void){return ui_strip_data();}
void hmi_storage_progress(unsigned done,unsigned total){board_boot_progress(4,done*100u/total);}
void journal_init_progress(unsigned done,unsigned total){board_boot_progress(5,done*100u/total);}
