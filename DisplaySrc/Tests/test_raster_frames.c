#include "hmi_ui.h"
#include "hmi_gfx.h"
#include "storage_fixture.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static uint16_t frame[320u*480u]; /* Host only. */
static void discard(uint16_t x,uint16_t y,uint16_t w,uint16_t h,const uint16_t *p,uint16_t stride,void *u){
    (void)x;(void)y;(void)w;(void)h;(void)p;(void)stride;(void)u;
}
#include "../Scripts/raster_context.h"
static void collect(uint16_t x,uint16_t y,uint16_t w,uint16_t h,const uint16_t *p,uint16_t stride,void *u){
    unsigned row;(void)u;
    for(row=0;row<h;row++)memcpy(frame+(y+row)*320u+x,p+row*stride,w*2u);
}
int main(int argc,char **argv){
    HmiUi ui;unsigned id,mode;FILE *out;if(argc!=2)return 2;
    fixture_init();out=fopen(argv[1],"wb");assert(out);
    for(mode=0;mode<3;mode++)for(id=0;id<HMI_SCENE_COUNT;id++){
        raster_context(&ui,id);ui.display=(HmiDisplay){collect,0};
        if(mode){
            static const int16_t samples[]={-90,30,80,-20};
            ui.state.telemetry_flags=(uint8_t)(128u|1u|(mode==2?16u:4u));
            for(unsigned ch=0;ch<4;ch++)ui.state.graph[ch]=(HmiGraphChannel){samples,4,-100,100,(uint16_t)(0x1234u+ch),1,"I","A"};
            strcpy(ui.edit,"123.45");ui.edit_error=(uint8_t)(mode==2);
            ui.state.clock="09:17";ui.state.mains_voltage=227;ui.state.output_voltage=192;
            ui.state.output_current=3.71f;ui.state.motor_load=67;ui.state.slip=-1.25f;
            ui.state.precharge_seconds=1.3f;ui.state.power_state=HMI_POWER_CHARGE;
            for(unsigned j=0;j<4;j++){ui.units[1][j]=1;ui.units[2][j]=1;}
            if(mode==2){ui.settings[0]=0;ui.settings[1]=0;ui.settings[2]=0;ui.settings[3]=0;}
        }
        hmi_ui_render(&ui);assert(fwrite(frame,2,320u*480u,out)==320u*480u);
    }
    return fclose(out)!=0;
}
