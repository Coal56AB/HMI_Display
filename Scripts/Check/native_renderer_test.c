#include "../../Firmware/Api/display_renderer.h"
#include <stdio.h>

#define W 48
#define H 32
DISPLAY_RENDER_STORAGE(storage,96,W*H/2);
static DisplayRenderer renderer;
static uint16_t desired[H][W],screen[W*H];
static DisplayPlatform platform;
static const uint16_t colors[]={0,0xffff,0xf800,0x001f};
static unsigned writes,pixel_count;
static int strict;
static DisplayRect first;
static void paint(DisplayCanvas *c,void *user) {
    (void)user;
    if(!writes)first=c->clip;
    for(int y=0;y<c->clip.h;++y)for(int x=0;x<c->clip.w;++x)
        c->pixels[y*c->clip.w+x]=desired[c->clip.y+y][c->clip.x+x];
}
static void capture(uint16_t x,uint16_t y,uint16_t w,uint16_t h,
                    const uint16_t *pixels,uint16_t stride,void *user) {
    (void)user;
    assert(w && h && stride>=w && x+w<=platform.width && y+h<=platform.height);
    ++writes;pixel_count+=(unsigned)w*h;
    for(unsigned yy=0;yy<h;++yy)for(unsigned xx=0;xx<w;++xx) {
        unsigned pos=(y+yy)*platform.width+x+xx;
        uint16_t value=pixels[yy*stride+xx];
        if(strict)assert(screen[pos]!=value);
        screen[pos]=value;
    }
}
static void verify(void) {
    for(unsigned y=0;y<H;++y)for(unsigned x=0;x<W;++x) {
        unsigned pos=platform.width==W?y*W+x:x*H+H-1-y;
        assert(screen[pos]==desired[y][x]);
    }
}
static void drain(DisplayRenderStyle *s) {
    unsigned iterations=0;
    while(display_renderer_step(&renderer,paint,0,s))assert(++iterations<=W*H);
    verify();
}
static void setup(unsigned rotation) {
    platform=(DisplayPlatform){0};platform.version=DISPLAY_API_VERSION;
    platform.width=rotation?H:W;platform.height=rotation?W:H;platform.write_rect=capture;
    display_renderer_init(&renderer,&platform,W,H,storage,W*H/2);
    memset(desired,0,sizeof(desired));memset(screen,0x55,sizeof(screen));
    writes=pixel_count=0;strict=0;
}
static void test_modes(void) {
    for(unsigned rotation=0;rotation<2;++rotation)for(unsigned dir=0;dir<4;++dir) {
        setup(rotation);
        DisplayRenderStyle style={(DisplayDrawDirection)dir,{0,0,W,H},colors,4};
        for(unsigned y=0;y<H;++y)for(unsigned x=0;x<W;++x)desired[y][x]=colors[(x+y)%4];
        display_renderer_full(&renderer);
        display_renderer_step(&renderer,paint,0,&style);
        if(dir==DRAW_TOP_TO_BOTTOM)assert(first.y==0 && first.h==2);
        if(dir==DRAW_BOTTOM_TO_TOP)assert(first.y==H-2 && first.h==2);
        if(dir==DRAW_LEFT_TO_RIGHT)assert(first.x==0 && first.w==3);
        if(dir==DRAW_RIGHT_TO_LEFT)assert(first.x==W-3 && first.w==3);
        drain(&style);assert(pixel_count==W*H);
        strict=1;writes=pixel_count=0;
        display_renderer_full(&renderer);drain(&style);assert(!writes);
        uint32_t random=123456;
        for(unsigned n=0;n<500;++n) {
            random=random*1664525u+1013904223u;
            unsigned x=random%W,y=(random>>8)%H;
            desired[y][x]=colors[(random>>16)%4];
            display_renderer_invalidate(&renderer,(int)x-1,(int)y-1,3,3);
            if(n%7==0)display_renderer_invalidate(&renderer,0,0,W,H);
            drain(&style);
        }
        /* A colour outside the palette must never be mistaken for unchanged. */
        desired[0][0]=0x1234;display_renderer_invalidate(&renderer,0,0,1,1);drain(&style);
        strict=0;writes=0;display_renderer_invalidate(&renderer,0,0,1,1);drain(&style);assert(writes==1);
        desired[0][0]=colors[0];display_renderer_full(&renderer);drain(&style);
        /* Painting another view invalidates the cache, even with no pending work. */
        display_renderer_step(&renderer,paint,0,0);
        pixel_count=0;display_renderer_full(&renderer);drain(&style);assert(pixel_count==W*H);
        uint16_t reversed[]={0x001f,0xf800,0xffff,0};style.palette=reversed;
        pixel_count=0;display_renderer_full(&renderer);drain(&style);assert(pixel_count==W*H);
        /* Same paint callback without a cache must produce the same complete image. */
        style.palette=0;pixel_count=0;display_renderer_full(&renderer);drain(&style);assert(pixel_count==W*H);
    }
}
static void test_queue(void) {
    setup(0);
    display_renderer_invalidate(&renderer,-5,-5,8,8);
    assert(renderer.count==1 && renderer.pending[0].w==3 && renderer.pending[0].h==3);
    display_renderer_clear(&renderer);
    for(unsigned i=0;i<DISPLAY_RENDER_QUEUE;++i)
        display_renderer_invalidate(&renderer,(i%16)*3,(i/16)*3,1,1);
    assert(renderer.count==DISPLAY_RENDER_QUEUE);
    display_renderer_invalidate(&renderer,47,31,1,1);
    assert(renderer.count==1 && renderer.pending[0].w==W && renderer.pending[0].h==H);
    drain(0);
    /* New damage to an already completed slice must be queued and painted. */
    display_renderer_full(&renderer);display_renderer_step(&renderer,paint,0,0);
    desired[0][0]=colors[2];display_renderer_invalidate(&renderer,0,0,1,1);drain(0);
    DisplayRenderStyle odd={DRAW_RIGHT_TO_LEFT,{1,1,7,5},colors,4};
    display_renderer_full(&renderer);drain(&odd);
    writes=0;display_renderer_invalidate(&renderer,1,1,7,5);drain(&odd);assert(!writes);
    display_renderer_clear(&renderer);assert(!display_renderer_pending(&renderer));
}
int main(void) {
    test_modes();test_queue();
    puts("Renderer: four directions, rotation, exact deltas, palette/cache lifecycle and queue passed");
    return 0;
}
