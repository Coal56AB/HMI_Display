#include "../../Firmware/ESP32/main/hmi_boot_render.h"
#include <stdio.h>
static uint16_t screen[480*320],reference[480*320];
static unsigned writes,pixels,width,height;
static void capture(uint16_t x,uint16_t y,uint16_t w,uint16_t h,const uint16_t *p,uint16_t stride,void *user){
    (void)user;assert(x+w<=width&&y+h<=height&&stride>=w&&w*h<=960);
    for(unsigned j=0;j<h;j++)for(unsigned i=0;i<w;i++)screen[(y+j)*width+x+i]=p[j*stride+i];
    writes++;pixels+=(unsigned)w*h;
}
int main(void){
    DisplayPlatform platform={0};platform.write_rect=capture;
    width=320;height=480;platform.width=width;platform.height=height;
    hmi_boot_draw(&platform,320,480,"LCD READY",5,0);assert(pixels==320*480);memcpy(reference,screen,sizeof(screen));
    width=480;height=320;platform.width=width;platform.height=height;pixels=0;
    hmi_boot_draw(&platform,320,480,"LCD READY",5,0);assert(pixels==320*480);
    for(unsigned y=0;y<480;y++)for(unsigned x=0;x<320;x++)assert(reference[y*320+x]==screen[x*480+479-y]);
    pixels=0;hmi_boot_draw(&platform,320,480,"READY",100,1);assert(pixels==320*56);
    for(unsigned y=255;y<263;y++)for(unsigned x=40;x<280;x++)assert(screen[x*480+479-y]==0x655f);
    hmi_boot_draw(&platform,480,320,"LCD READY",5,0);
    pixels=0;hmi_boot_draw(&platform,480,320,"READY",150,1);assert(pixels==480*56);
    for(unsigned y=175;y<183;y++)for(unsigned x=120;x<360;x++)assert(screen[y*480+x]==0x655f);
    printf("Boot PASS: portrait rotation, initial full frame, bounded updates and landscape template (%u writes)\n",writes);
    return 0;
}
