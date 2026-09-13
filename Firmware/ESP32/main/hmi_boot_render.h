#ifndef HMI_BOOT_RENDER_H
#define HMI_BOOT_RENDER_H
#include "display_renderer.h"
/* Logical boot geometry belongs to the module; LCD geometry belongs to the board. */
static inline void hmi_boot_draw(const DisplayPlatform *platform,unsigned width,unsigned height,
                                 const char *label,unsigned percent,int painted) {
    assert((width==320&&height==480)||(width==480&&height==320));
    static const char letters[]="ABCDEFGHIJKLMNOPQRSTUVWXYZ";
    static const uint8_t glyphs[][5]={
        {0x7e,0x11,0x11,0x11,0x7e},{0x7f,0x49,0x49,0x49,0x36},{0x3e,0x41,0x41,0x41,0x22},
        {0x7f,0x41,0x41,0x22,0x1c},{0x7f,0x49,0x49,0x49,0x41},{0x7f,9,9,9,1},
        {0x3e,0x41,0x49,0x49,0x7a},{0x7f,8,8,8,0x7f},{0,0x41,0x7f,0x41,0},
        {0x20,0x40,0x41,0x3f,1},{0x7f,8,0x14,0x22,0x41},{0x7f,0x40,0x40,0x40,0x40},
        {0x7f,2,12,2,0x7f},{0x7f,4,8,16,0x7f},{0x3e,0x41,0x41,0x41,0x3e},
        {0x7f,9,9,9,6},{0x3e,0x41,0x51,0x21,0x5e},{0x7f,9,0x19,0x29,0x46},
        {0x46,0x49,0x49,0x49,0x31},{1,1,0x7f,1,1},{0x3f,0x40,0x40,0x40,0x3f},
        {0x1f,0x20,0x40,0x20,0x1f},{0x3f,0x40,0x38,0x40,0x3f},{0x63,0x14,8,0x14,0x63},
        {7,8,0x70,8,7},{0x61,0x51,0x49,0x45,0x43}};
    if(percent>100u)percent=100u;
    static uint16_t rows[480*2],rotated[480*2];
    static DisplayRenderer renderer;
    renderer.platform=platform;renderer.width=width;renderer.height=height;
    renderer.rotated=rotated;
    const unsigned top=height/2-28,bar_x=width/2-120,text_x=width/2-80;
    for(unsigned y=painted?top:0;y<(painted?top+56:height);y+=2) {
        for(unsigned dy=0;dy<2;++dy)for(unsigned x=0;x<width;++x) {
            const unsigned yy=y+dy;
            uint16_t color=0x08c5;
            if(yy>=top+43&&yy<top+51&&x>=bar_x&&x<bar_x+240)color=x<bar_x+percent*240/100?0x655f:0x2188;
            if(yy>=top+8&&yy<top+22&&x>=text_x) {
                unsigned ch=(x-text_x)/12,col=(x-text_x)%12;
                if(ch<strlen(label)&&col<10) {
                    const char *g=strchr(letters,label[ch]);
                    if(g&&(glyphs[g-letters][col/2]&(1u<<((yy-top-8)/2))))color=0xef7f;
                }
            }
            rows[dy*width+x]=color;
        }
        display_renderer_write(&renderer,0,(int)y,(int)width,2,rows,width);
    }
}
#endif
