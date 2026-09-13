/* Portable, allocation-free rendering shared by GUI modules and templates.
 * Header-only so module.json, Keil, ESP-IDF and native simulators use the same code. */
#ifndef DISPLAY_RENDERER_H
#define DISPLAY_RENDERER_H
#include "display_api.h"
#include <string.h>
#include <assert.h>

typedef struct { int16_t x, y, w, h; } DisplayRect;
typedef struct { DisplayRect clip; uint16_t *pixels; } DisplayCanvas;
typedef enum {
    DRAW_TOP_TO_BOTTOM, DRAW_BOTTOM_TO_TOP,
    DRAW_LEFT_TO_RIGHT, DRAW_RIGHT_TO_LEFT
} DisplayDrawDirection;
/* Optional exact cache for an area with up to 15 RGB565 colours.
 * Colours outside the palette are always sent. NULL palette disables caching. */
typedef struct {
    DisplayDrawDirection direction;
    DisplayRect cache_area;
    const uint16_t *palette;
    unsigned palette_count;
} DisplayRenderStyle;
typedef void (*DisplayPaint)(DisplayCanvas *, void *);

#define DISPLAY_RENDER_QUEUE 32u
typedef struct {
    const DisplayPlatform *platform;
    unsigned width, height, capacity, cache_bytes;
    uint16_t *pixels, *rotated;
    uint8_t *changed, *cache;
    DisplayRect pending[DISPLAY_RENDER_QUEUE];
    unsigned count;
    DisplayRect cache_area;
    uint16_t palette[15];
    unsigned palette_count;
    uint8_t cache_valid;
} DisplayRenderer;
/* Storage size is chosen by the application; buffer layout stays here.
 * cache_size=0 costs one dummy byte and enables no image cache. */
#define DISPLAY_RENDER_STORAGE(name, pixel_count, cache_size) \
    static struct { \
        uint16_t pixels[pixel_count], rotated[pixel_count]; \
        uint8_t changed[((pixel_count)+7)/8], cache[(cache_size)?(cache_size):1]; \
    } name
#define display_renderer_init(r, platform, width, height, storage, cache_size) \
    display_renderer_setup(r, platform, width, height, \
        (unsigned)(sizeof((storage).pixels)/sizeof(uint16_t)), \
        (storage).pixels, (storage).rotated, (storage).changed, (storage).cache, cache_size)

static inline void display_renderer_setup(DisplayRenderer *r, const DisplayPlatform *p,
        unsigned width, unsigned height, unsigned capacity, uint16_t *pixels,
        uint16_t *rotated, uint8_t *changed, uint8_t *cache, unsigned cache_bytes) {
    /* Each slice must accommodate at least one whole logical row or column. */
    assert(width && height && width<=32767 && height<=32767);
    assert(capacity>=width && capacity>=height);
    assert(p && ((p->width==width && p->height==height) ||
                 (p->width==height && p->height==width)));
    memset(r,0,sizeof(*r));
    r->platform=p;r->width=width;r->height=height;r->capacity=capacity;
    r->pixels=pixels;r->rotated=rotated;r->changed=changed;r->cache=cache;
    r->cache_bytes=cache_bytes;
}
static inline unsigned display_renderer_pending(const DisplayRenderer *r) { return r->count; }
static inline void display_renderer_clear(DisplayRenderer *r) { r->count=0;r->cache_valid=0; }
static inline void display_renderer_invalidate(DisplayRenderer *r, int x,int y,int w,int h) {
    if(x<0){w+=x;x=0;} if(y<0){h+=y;y=0;}
    if(x+w>(int)r->width)w=(int)r->width-x;
    if(y+h>(int)r->height)h=(int)r->height-y;
    if(w<=0 || h<=0)return;
    for(unsigned i=0;i<r->count;++i) {
        DisplayRect *q=&r->pending[i];
        if(x>=q->x && y>=q->y && x+w<=q->x+q->w && y+h<=q->y+q->h)return;
        /* Never rewind the slice currently being painted. */
        if(i && x<q->x+q->w && x+w>q->x && y<q->y+q->h && y+h>q->y) {
            int x0=x<q->x?x:q->x,y0=y<q->y?y:q->y;
            int x1=x+w>q->x+q->w?x+w:q->x+q->w;
            int y1=y+h>q->y+q->h?y+h:q->y+q->h;
            *q=(DisplayRect){(int16_t)x0,(int16_t)y0,(int16_t)(x1-x0),(int16_t)(y1-y0)};
            return;
        }
    }
    if(r->count==DISPLAY_RENDER_QUEUE) {
        r->count=1;r->pending[0]=(DisplayRect){0,0,(int16_t)r->width,(int16_t)r->height};
    } else r->pending[r->count++]=(DisplayRect){(int16_t)x,(int16_t)y,(int16_t)w,(int16_t)h};
}
static inline void display_renderer_full(DisplayRenderer *r) {
    r->count=0;display_renderer_invalidate(r,0,0,(int)r->width,(int)r->height);
}
static inline void display_canvas_fill(DisplayCanvas *c,int x,int y,int w,int h,uint16_t color) {
    int x1=x+w,y1=y+h;
    if(x<c->clip.x)x=c->clip.x;
    if(y<c->clip.y)y=c->clip.y;
    if(x1>c->clip.x+c->clip.w)x1=c->clip.x+c->clip.w;
    if(y1>c->clip.y+c->clip.h)y1=c->clip.y+c->clip.h;
    for(int yy=y;yy<y1;++yy)for(int xx=x;xx<x1;++xx)
        c->pixels[(yy-c->clip.y)*c->clip.w+xx-c->clip.x]=color;
}
static inline int display_renderer_same_area(DisplayRect a,DisplayRect b) {
    return a.x==b.x && a.y==b.y && a.w==b.w && a.h==b.h;
}
static inline void display_renderer_style(DisplayRenderer *r,const DisplayRenderStyle *s) {
    DisplayRect a=s->cache_area;
    if(!s->palette || !s->palette_count || s->palette_count>15 ||
       a.x<0 || a.y<0 || a.w<=0 || a.h<=0 ||
       a.x+a.w>(int)r->width || a.y+a.h>(int)r->height ||
       ((unsigned)a.w*a.h+1)/2>r->cache_bytes) {
        r->cache_valid=0;return;
    }
    if(!r->cache_valid || !display_renderer_same_area(r->cache_area,a) ||
       r->palette_count!=s->palette_count ||
       memcmp(r->palette,s->palette,s->palette_count*sizeof(uint16_t))) {
        memset(r->cache,255,((unsigned)a.w*a.h+1)/2);
        r->cache_area=a;r->palette_count=s->palette_count;
        memcpy(r->palette,s->palette,s->palette_count*sizeof(uint16_t));
        r->cache_valid=1;
    }
}
static inline void display_renderer_write(DisplayRenderer *r,int x,int y,int w,int h,
                                         const uint16_t *pixels,unsigned stride) {
    if(r->platform->width==r->width && r->platform->height==r->height) {
        r->platform->write_rect((uint16_t)x,(uint16_t)y,(uint16_t)w,(uint16_t)h,pixels,(uint16_t)stride,0);
    } else {
        for(int yy=0;yy<h;++yy)for(int xx=0;xx<w;++xx)
            r->rotated[xx*h+h-1-yy]=pixels[yy*stride+xx];
        r->platform->write_rect((uint16_t)(r->height-y-h),(uint16_t)x,
            (uint16_t)h,(uint16_t)w,r->rotated,(uint16_t)h,0);
    }
}
static inline int display_renderer_changed(DisplayRenderer *r,unsigned i) {
    return (r->changed[i/8]>>(i%8))&1;
}
static inline void display_renderer_delta(DisplayRenderer *r,DisplayRect c) {
    memset(r->changed,0,((unsigned)c.w*c.h+7)/8);
    for(int yy=0;yy<c.h;++yy)for(int xx=0;xx<c.w;++xx) {
        unsigned i=(unsigned)yy*c.w+xx,code=15;
        int x=c.x+xx,y=c.y+yy,changed=1;
        DisplayRect a=r->cache_area;
        if(x>=a.x && x<a.x+a.w && y>=a.y && y<a.y+a.h) {
            unsigned index=(unsigned)(y-a.y)*a.w+x-a.x,shift=(index&1)*4;
            for(unsigned k=0;k<r->palette_count;++k)if(r->pixels[i]==r->palette[k]){code=k;break;}
            changed=code==15 || ((r->cache[index/2]>>shift)&15u)!=code;
            r->cache[index/2]=(uint8_t)((r->cache[index/2]&~(15u<<shift))|(code<<shift));
        }
        if(changed)r->changed[i/8]|=(uint8_t)(1u<<(i%8));
    }
    /* Merge only changed pixels; no unchanged pixel enters a transmitted rectangle. */
    for(int xx=0;xx<c.w;++xx)for(int yy=0;yy<c.h;) {
        if(!display_renderer_changed(r,(unsigned)yy*c.w+xx)){++yy;continue;}
        int end=yy+1,right=xx+1;
        while(end<c.h && display_renderer_changed(r,(unsigned)end*c.w+xx))++end;
        while(right<c.w) {
            int row;
            for(row=yy;row<end && display_renderer_changed(r,(unsigned)row*c.w+right);++row){}
            if(row!=end)break;
            ++right;
        }
        display_renderer_write(r,c.x+xx,c.y+yy,right-xx,end-yy,r->pixels+yy*c.w+xx,(unsigned)c.w);
        for(int col=xx;col<right;++col)for(int row=yy;row<end;++row) {
            unsigned i=(unsigned)row*c.w+col;r->changed[i/8]&=(uint8_t)~(1u<<(i%8));
        }
        yy=end;
    }
}
/* One bounded slice per call. Paint must fill the clip and must not mutate this
 * renderer's queue; dispatch UI events between calls, never during paint/write. */
static inline int display_renderer_step(DisplayRenderer *r,DisplayPaint paint,void *user,
                                        const DisplayRenderStyle *style) {
    static const DisplayRenderStyle normal={DRAW_TOP_TO_BOTTOM,{0,0,0,0},0,0};
    if(!style)style=&normal;
    display_renderer_style(r,style);
    if(!r->count || !r->platform->write_rect)return 0;
    DisplayRect *q=&r->pending[0],c=*q;
    int vertical=style->direction==DRAW_LEFT_TO_RIGHT || style->direction==DRAW_RIGHT_TO_LEFT;
    if(vertical) {
        unsigned columns=r->capacity/(unsigned)c.h;
        if(columns>32)columns=32;
        if(columns<(unsigned)c.w)c.w=(int16_t)columns;
        if(style->direction==DRAW_RIGHT_TO_LEFT)c.x=(int16_t)(q->x+q->w-c.w);
    } else {
        unsigned rows=r->capacity/(unsigned)c.w;
        if(rows<(unsigned)c.h)c.h=(int16_t)rows;
        if(style->direction==DRAW_BOTTOM_TO_TOP)c.y=(int16_t)(q->y+q->h-c.h);
    }
    DisplayCanvas canvas={c,r->pixels};paint(&canvas,user);
    if(r->cache_valid)display_renderer_delta(r,c);
    else display_renderer_write(r,c.x,c.y,c.w,c.h,r->pixels,(unsigned)c.w);
    if(vertical) {
        q->w-=c.w;if(style->direction==DRAW_LEFT_TO_RIGHT)q->x+=c.w;
    } else {
        q->h-=c.h;if(style->direction!=DRAW_BOTTOM_TO_TOP)q->y+=c.h;
    }
    if(!q->w || !q->h){--r->count;memmove(r->pending,r->pending+1,r->count*sizeof(DisplayRect));}
    return 1;
}
#endif
