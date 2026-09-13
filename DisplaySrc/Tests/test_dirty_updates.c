#include "storage_fixture.h"
#include "hmi.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static uint16_t actual[320u*480u],expected[320u*480u];
static void collect(uint16_t x,uint16_t y,uint16_t w,uint16_t h,const uint16_t *p,uint16_t stride,void *user){
    unsigned row;uint16_t *frame=(uint16_t *)user;
    for(row=0;row<h;row++)memcpy(frame+(y+row)*320u+x,p+row*stride,w*2u);
}
int main(void){
    fixture_init();
    unsigned scene;HmiState s;
    for(scene=0;scene<HMI_SCENE_COUNT;scene++){
        hmi_state_defaults(&s);s.scene_override=(HmiSceneId)scene;
        hmi_init();hmi_render_full(&s,collect,expected);memset(actual,0,sizeof(actual));
        for(unsigned y=0;y<480;y+=37)for(unsigned x=0;x<320;x+=73){
            HmiRect r={(uint16_t)x,(uint16_t)y,(uint16_t)(x+73>320?320-x:73),(uint16_t)(y+37>480?480-y:37)};
            hmi_invalidate(r);hmi_render_dirty(&s,collect,actual);
        }
        assert(memcmp(actual,expected,sizeof(actual))==0);
    }
    hmi_state_defaults(&s);hmi_render_full(&s,collect,actual);
    {HmiState next=s;next.clock="23:59";hmi_diff_and_invalidate(&s,&next);hmi_render_dirty(&next,collect,actual);
     assert(hmi_dirty_pixel_count()<153600);hmi_render_full(&next,collect,expected);assert(memcmp(actual,expected,sizeof(actual))==0);}
    hmi_state_defaults(&s);s.dynamic_values=1;s.telemetry_flags=128;s.precharge_seconds=0;
    hmi_render_full(&s,collect,actual);
    {static const float seconds[]={9.9f,99.9f,100.0f,999.9f,1000.0f,99999.9f,1.0f};
     for(unsigned i=0;i<sizeof(seconds)/sizeof(seconds[0]);i++){
        HmiState next=s;next.precharge_seconds=seconds[i];hmi_diff_and_invalidate(&s,&next);
        hmi_render_dirty(&next,collect,actual);hmi_render_full(&next,collect,expected);
        assert(memcmp(actual,expected,sizeof(actual))==0);s=next;
     }}
    puts("dirty: all 47 scenes tiled reconstruction and clock transition PASS");return 0;
}
