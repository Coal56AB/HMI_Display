#include <stdint.h>
#include <stddef.h>

#define UI_WIDTH 320
#define UI_HEIGHT 480
#include "hmi_gfx.h"
#include "hmi_plot.h"

#if defined(__GNUC__) || defined(__clang__)
#define UI_EXPORT __attribute__((visibility("default")))
#else
#define UI_EXPORT
#endif

/* Единственный пиксельный буфер; по умолчанию 2048 RGB565 = 4096 байт. */
/* Live plot strips use only the first 712 pixels. Their exact history and
 * comparison bits occupy the unused tail of the SAME rendering buffer.
 * A larger ordinary strip invalidates that history before overwriting it. */
static union {
    uint16_t pixels[HMI_RENDER_BUFFER_PIXELS];
    HmiPlotWorkspace plot;
} ui_work;
#define ui_strip ui_work.pixels
HmiPlotWorkspace *ui_plot_workspace(void){return &ui_work.plot;}
uint32_t ui_working_buffer_bytes(void){return (uint32_t)sizeof(ui_work);}
static uint8_t ui_text_input[384];
static int16_t strip_x0,strip_y0;
static int16_t strip_w,strip_h;
static int16_t clip_x0,clip_y0,clip_x1,clip_y1;

#include "hmi_storage.h"
#if HMI_LITE
#include "font_lite.inc"
#else
#include "font_full.inc"
#endif

/* Replacement regions identify text anchors, not opaque pixel erasers.
 * The scene restores its geometry while omitting the old exported text. */
static int16_t text_regions[32][4];
static unsigned text_region_count;
static int text_home,text_layers_active;
static int live_graph_background;
void ui_set_live_graph_background(int enabled){live_graph_background=enabled;}
int ui_live_graph_background(void){return live_graph_background;}
void ui_text_layers_begin(int home){text_region_count=0;text_home=home;text_layers_active=1;}
void ui_text_layers_end(void){text_region_count=0;text_home=0;text_layers_active=0;live_graph_background=0;}
int ui_text_layers_home(void){return text_home;}
void ui_draw_header_help(void){
    if(!text_layers_active||!ui_rect_visible(201,4,22,28))return;
    ui_set_clip(201,4,22,28);ui_circle(211,17,8,44503u,0);
    ui_text_cstr(208,22,12,61342u,"?",0);
}
void ui_text_replace(int x,int y,int w,int h){
    if(text_region_count<32u){int16_t *r=text_regions[text_region_count++];
        r[0]=(int16_t)x;r[1]=(int16_t)y;r[2]=(int16_t)(x+w);r[3]=(int16_t)(y+h);}
}
int ui_static_text_hidden(int x,int baseline){
    unsigned i;for(i=0;i<text_region_count;i++){const int16_t *r=text_regions[i];
        if(x>=r[0]&&x<r[2]&&baseline>=r[1]&&baseline<r[3])return 1;}
    return 0;
}

int ui_draw_raster_scene(unsigned scene){
#if HMI_EXTERNAL_ASSETS
#include "hmi_raster_layout.h"
    unsigned band,last;
    if(!text_layers_active||scene>=HMI_RASTER_SCENES)return 0;
    if(live_graph_background&&scene>=11u&&scene<=17u)scene+=36u;
    band=(unsigned)strip_y0/8u;last=(unsigned)(strip_y0+strip_h-1)/8u;
    for(;band<=last;band++){
        uint32_t offset,pos=0;
        const uint8_t *stream;
        hmi_read_copy(&offset,(const void *)(uintptr_t)(HMI_ASSET_BASE+HMI_RASTER_TABLE_OFFSET+(scene*60u+band)*4u),4);
        stream=(const uint8_t *)(uintptr_t)(HMI_ASSET_BASE+offset);
        while(pos<2560u){
            uint8_t token=hmi_read_u8(stream++);unsigned count=(token&127u)+1u,left=count;
            uint16_t color=0;
            if(count>2560u-pos)return 0;
            if(token&128u){color=hmi_read_u16((const uint16_t *)stream);stream+=2;}
            while(left){
                int x=(int)(pos%320u),y=(int)(band*8u+pos/320u);
                unsigned n=320u-(unsigned)x;if(n>left)n=left;
                int x0=x<strip_x0?strip_x0:x,x1=x+(int)n;
                if(x1>strip_x0+strip_w)x1=strip_x0+strip_w;
                if(y>=strip_y0&&y<strip_y0+strip_h&&x0<x1){
                    uint16_t *dst=&ui_strip[(y-strip_y0)*strip_w+x0-strip_x0];
                    if(token&128u){for(int xx=x0;xx<x1;xx++)*dst++=color;}
                    else hmi_read_copy(dst,stream+(x0-x)*2,(uint32_t)(x1-x0)*2u);
                }
                if(!(token&128u))stream+=n*2u;
                pos+=n;left-=n;
            }
        }
    }
    return 1;
#else
    (void)scene;return 0;
#endif
}

uint32_t ui_font_data_bytes(void){
    return (uint32_t)(sizeof(UI_FONTS)+UI_GLYPH_TABLE_BYTES+UI_FONT_BITMAP_BYTES+sizeof(UI_CODEPOINTS));
}

static uint16_t mix565(uint16_t a, uint16_t b, int n, int d) {
    uint32_t ar=(a>>11)&31, ag=(a>>5)&63, ab=a&31;
    uint32_t br=(b>>11)&31, bg=(b>>5)&63, bb=b&31;
    return (uint16_t)((((ar*(d-n)+br*n)/d)<<11)|(((ag*(d-n)+bg*n)/d)<<5)|((ab*(d-n)+bb*n)/d));
}

static uint16_t over565(uint16_t background, uint16_t foreground, int alpha) {
    if (alpha <= 0) return background;
    if (alpha >= 15) return foreground;
    return mix565(background, foreground, alpha, 15);
}

static void px(int x, int y, uint16_t c) {
    if (x < strip_x0 || x >= strip_x0 + strip_w || y < strip_y0 || y >= strip_y0 + strip_h || x<clip_x0 || x>=clip_x1 || y<clip_y0 || y>=clip_y1) return;
    ui_strip[(y-strip_y0)*strip_w+(x-strip_x0)]=c;
}

static void px_alpha(int x, int y, uint16_t c, int alpha) {
    if (x < strip_x0 || x >= strip_x0 + strip_w || y < strip_y0 || y >= strip_y0 + strip_h || x<clip_x0 || x>=clip_x1 || y<clip_y0 || y>=clip_y1 || alpha<=0) return;
    uint16_t *dst=&ui_strip[(y-strip_y0)*strip_w+(x-strip_x0)];
    *dst=over565(*dst,c,(alpha+8)>>4);
}

UI_EXPORT uint32_t ui_strip_ptr(void) { return (uint32_t)(uintptr_t)ui_strip; }
const uint16_t *ui_strip_data(void) { return ui_strip; }
void ui_replace_color_rect(int x,int y,int w,int h,uint16_t from,uint16_t to){
    int x0=x,x1=x+w,y0=y,y1=y+h,xx,yy;
    if(x0<strip_x0)x0=strip_x0;
    if(x1>strip_x0+strip_w)x1=strip_x0+strip_w;
    if(y0<strip_y0)y0=strip_y0;
    if(y1>strip_y0+strip_h)y1=strip_y0+strip_h;
    if(x0<clip_x0)x0=clip_x0;
    if(x1>clip_x1)x1=clip_x1;
    if(y0<clip_y0)y0=clip_y0;
    if(y1>clip_y1)y1=clip_y1;
    for(yy=y0;yy<y1;yy++)for(xx=x0;xx<x1;xx++){
        uint16_t *p=&ui_strip[(yy-strip_y0)*strip_w+xx-strip_x0];
        if(*p==from)*p=to;
    }
}
UI_EXPORT uint32_t ui_text_ptr(void) { return (uint32_t)(uintptr_t)ui_text_input; }
UI_EXPORT int ui_strip_bytes(void) { return (int)HMI_RENDER_BUFFER_BYTES; }
UI_EXPORT int ui_strip_height(void) { return strip_h; }
UI_EXPORT int ui_buffer_width(void) { return strip_w; }
UI_EXPORT int ui_buffer_height(void) { return strip_h; }
int ui_rect_visible(int x,int y,int w,int h){
    return w>0&&h>0&&x<strip_x0+strip_w&&x+w>strip_x0&&
           y<strip_y0+strip_h&&y+h>strip_y0;
}

UI_EXPORT void ui_begin(int y0, int rows, uint16_t color) {
    ui_begin_rect(0,y0,UI_WIDTH,rows,color);
}

UI_EXPORT void ui_begin_rect(int x,int y,int width,int height,uint16_t color) {
    uint32_t pixels;
    if(x<0){width+=x;x=0;}if(y<0){height+=y;y=0;}
    if(x+width>UI_WIDTH)width=UI_WIDTH-x;
    if(y+height>UI_HEIGHT)height=UI_HEIGHT-y;
    if(width<1)width=1;
    if(height<1)height=1;
    pixels=(uint32_t)width*(uint32_t)height;
    if(pixels>HMI_RENDER_BUFFER_PIXELS)height=(int)(HMI_RENDER_BUFFER_PIXELS/(uint32_t)width);
    if((uint32_t)width*(uint32_t)height>HMI_PLOT_DIFF_PIXELS)hmi_plot_forget();
    strip_x0=(int16_t)x;strip_y0=(int16_t)y;strip_w=(int16_t)width;strip_h=(int16_t)height;
    clip_x0=0;clip_y0=0;clip_x1=UI_WIDTH;clip_y1=UI_HEIGHT;
    {int n=strip_w*strip_h;int i;for(i=0;i<n;i++)ui_strip[i]=color;}
}

UI_EXPORT void ui_set_clip(int x,int y,int w,int h){clip_x0=(int16_t)(x<0?0:x);clip_y0=(int16_t)(y<0?0:y);clip_x1=(int16_t)(x+w>UI_WIDTH?UI_WIDTH:x+w);clip_y1=(int16_t)(y+h>UI_HEIGHT?UI_HEIGHT:y+h);}

UI_EXPORT void ui_fill_rect(int x, int y, int w, int h, uint16_t c) {
    if(w<=0||h<=0)return;
    int x0=x<clip_x0?clip_x0:x,x1=x+w>clip_x1?clip_x1:x+w,y0=y<strip_y0?strip_y0:y,y1=y+h>strip_y0+strip_h?strip_y0+strip_h:y+h;
    if(y0<clip_y0)y0=clip_y0;
    if(y1>clip_y1)y1=clip_y1;
    if(x0<strip_x0)x0=strip_x0;
    if(x1>strip_x0+strip_w)x1=strip_x0+strip_w;
    for(int yy=y0;yy<y1;yy++){uint16_t *p=&ui_strip[(yy-strip_y0)*strip_w+(x0-strip_x0)];for(int xx=x0;xx<x1;xx++)*p++=c;}
}

UI_EXPORT void ui_fill_gradient_v(int x,int y,int w,int h,uint16_t top,uint16_t bottom){
    if(w<=0||h<=0)return;
    int y0=y<strip_y0?strip_y0:y,y1=y+h>strip_y0+strip_h?strip_y0+strip_h:y+h;
    for(int yy=y0;yy<y1;yy++)ui_fill_rect(x,yy,w,1,mix565(top,bottom,yy-y,h>1?h-1:1));
}

UI_EXPORT void ui_line(int x0,int y0,int x1,int y1,uint16_t c){
    if(y0==y1){int left=x0<x1?x0:x1;ui_fill_rect(left,y0,(x0<x1?x1:x0)-left+1,1,c);return;}
    if(x0==x1){int top=y0<y1?y0:y1;ui_fill_rect(x0,top,1,(y0<y1?y1:y0)-top+1,c);return;}
    int dx=x1>x0?x1-x0:x0-x1,sx=x0<x1?1:-1,dy=y1>y0?y0-y1:y1-y0,sy=y0<y1?1:-1,err=dx+dy;
    for(;;){px(x0,y0,c);if(x0==x1&&y0==y1)break;int e2=err<<1;if(e2>=dy){err+=dy;x0+=sx;}if(e2<=dx){err+=dx;y0+=sy;}}
}

UI_EXPORT void ui_line_aa(int x0,int y0,int x1,int y1,uint16_t c){
    int dx=x1-x0,dy=y1-y0,adx=dx<0?-dx:dx,ady=dy<0?-dy:dy;
    if(dx==0||dy==0){ui_line(x0,y0,x1,y1,c);return;}
    if(adx>=ady){
        if(x0>x1){int t=x0;x0=x1;x1=t;t=y0;y0=y1;y1=t;dx=x1-x0;dy=y1-y0;}
        int yfp=y0*256,step=(dy*256)/dx;
        for(int x=x0;x<=x1;x++,yfp+=step){int y=yfp>>8,frac=yfp&255;px_alpha(x,y,c,255-frac);px_alpha(x,y+1,c,frac);}
    }else{
        if(y0>y1){int t=x0;x0=x1;x1=t;t=y0;y0=y1;y1=t;dx=x1-x0;dy=y1-y0;}
        int xfp=x0*256,step=(dx*256)/dy;
        for(int y=y0;y<=y1;y++,xfp+=step){int x=xfp>>8,frac=xfp&255;px_alpha(x,y,c,255-frac);px_alpha(x+1,y,c,frac);}
    }
}

UI_EXPORT void ui_line_width(int x0,int y0,int x1,int y1,int width,uint16_t c){
    if(width<=1){ui_line(x0,y0,x1,y1,c);return;}
    if(width>6)width=6;
    int dx=x1>x0?x1-x0:x0-x1,dy=y1>y0?y1-y0:y0-y1;
    int first=-(width/2),last=first+width-1;
    for(int o=first;o<=last;o++){
        if(dx>=dy)ui_line(x0,y0+o,x1,y1+o,c);
        else ui_line(x0+o,y0,x1+o,y1,c);
    }
}

UI_EXPORT void ui_rect(int x,int y,int w,int h,uint16_t c){if(w<=0||h<=0)return;ui_line(x,y,x+w-1,y,c);ui_line(x,y+h-1,x+w-1,y+h-1,c);ui_line(x,y,x,y+h-1,c);ui_line(x+w-1,y,x+w-1,y+h-1,c);}

static int inside_round(int xx,int yy,int w,int h,int r){
    if(r<=0)return 1;
    int cx=xx<r?r-1:(xx>=w-r?w-r:xx),cy=yy<r?r-1:(yy>=h-r?h-r:yy),dx=xx-cx,dy=yy-cy;return dx*dx+dy*dy<=r*r;
}

UI_EXPORT void ui_fill_round_rect(int x,int y,int w,int h,int r,uint16_t c){
    if(w<=0||h<=0)return;
    if(r<0)r=0;
    if(r>w/2)r=w/2;
    if(r>h/2)r=h/2;
    int y0=y<strip_y0?strip_y0:y,y1=y+h>strip_y0+strip_h?strip_y0+strip_h:y+h;
    for(int yy=y0;yy<y1;yy++)for(int xx=0;xx<w;xx++)if(inside_round(xx,yy-y,w,h,r))px(x+xx,yy,c);
}

UI_EXPORT void ui_round_rect(int x,int y,int w,int h,int r,uint16_t c){
    if(w<=1||h<=1)return;
    int y0=strip_y0-y;if(y0<0)y0=0;if(y0<clip_y0-y)y0=clip_y0-y;
    int y1=strip_y0+strip_h-y;if(y1>h)y1=h;if(y1>clip_y1-y)y1=clip_y1-y;
    for(int yy=y0;yy<y1;yy++)for(int xx=0;xx<w;xx++){
        int outer=inside_round(xx,yy,w,h,r);
        int inner=xx>0&&yy>0&&xx<w-1&&yy<h-1&&inside_round(xx-1,yy-1,w-2,h-2,r>0?r-1:0);
        if(outer&&!inner)px(x+xx,y+yy,c);
    }
}

UI_EXPORT void ui_circle(int cx,int cy,int r,uint16_t c,int filled){
    if(r<=0)return;
    int x0=-r,x1=r,y0=-r,y1=r,rr=r*r,ri=(r-1)*(r-1);
    if(x0<strip_x0-cx)x0=strip_x0-cx;
    if(x1>=strip_x0+strip_w-cx)x1=strip_x0+strip_w-cx-1;
    if(y0<strip_y0-cy)y0=strip_y0-cy;
    if(y1>=strip_y0+strip_h-cy)y1=strip_y0+strip_h-cy-1;
    if(x0<clip_x0-cx)x0=clip_x0-cx;
    if(x1>=clip_x1-cx)x1=clip_x1-cx-1;
    if(y0<clip_y0-cy)y0=clip_y0-cy;
    if(y1>=clip_y1-cy)y1=clip_y1-cy-1;
    for(int y=y0;y<=y1;y++)for(int x=x0;x<=x1;x++){int d=x*x+y*y;
    if(!filled&&d==rr&&(x==0||y==0))continue;
    if(d<=rr&&(filled||d>=ri))px(cx+x,cy+y,c);}
}

UI_EXPORT void ui_circle_width(int cx,int cy,int r,int width,uint16_t c){
    if(width<=1){ui_circle(cx,cy,r,c,0);return;}
    if(width>6)width=6;
    for(int i=0;i<width&&r-i>0;i++)ui_circle(cx,cy,r-i,c,0);
}

static int edge2(int ax,int ay,int bx,int by,int pxv,int pyv){return(pxv-ax)*(by-ay)-(pyv-ay)*(bx-ax);}

UI_EXPORT void ui_fill_triangle(int x0,int y0,int x1,int y1,int x2,int y2,uint16_t c){
    int minx=x0<x1?(x0<x2?x0:x2):(x1<x2?x1:x2),maxx=x0>x1?(x0>x2?x0:x2):(x1>x2?x1:x2);
    int miny=y0<y1?(y0<y2?y0:y2):(y1<y2?y1:y2),maxy=y0>y1?(y0>y2?y0:y2):(y1>y2?y1:y2);
    int area=edge2(x0,y0,x1,y1,x2,y2);if(area==0)return;
    if(minx<clip_x0)minx=clip_x0;
    if(maxx>=clip_x1)maxx=clip_x1-1;
    if(miny<clip_y0)miny=clip_y0;
    if(maxy>=clip_y1)maxy=clip_y1-1;
    if(miny<strip_y0)miny=strip_y0;
    if(maxy>=strip_y0+strip_h)maxy=strip_y0+strip_h-1;
    for(int y=miny;y<=maxy;y++)for(int x=minx;x<=maxx;x++){
        int a=edge2(x0,y0,x1,y1,x,y),b=edge2(x1,y1,x2,y2,x,y),d=edge2(x2,y2,x0,y0,x,y);
        if((area>0&&a>=0&&b>=0&&d>=0)||(area<0&&a<=0&&b<=0&&d<=0))px(x,y,c);
    }
}

static uint32_t utf8_next(const uint8_t *s,int len,int *i){
    uint32_t c=s[(*i)++];if(c<128)return c;
    if((c&0xe0)==0xc0&&*i<len){uint32_t b1=s[(*i)++];return((c&31)<<6)|(b1&63);}
    if((c&0xf0)==0xe0&&*i+1<len){uint32_t b1=s[(*i)++],b2=s[(*i)++];return((c&15)<<12)|((b1&63)<<6)|(b2&63);}
    return'?';
}
static const ui_font_t *font_for(int pxv,int bold){const ui_font_t *best=&UI_FONTS[0];int bd=1000;for(int i=0;i<UI_FONT_COUNT;i++){if((int)UI_FONTS[i].bold!=(bold?1:0))continue;int d=(int)UI_FONTS[i].px-pxv;if(d<0)d=-d;if(d<bd){bd=d;best=&UI_FONTS[i];}}return best;}
#if HMI_EXTERNAL_ASSETS
/* Metrics are reused for measuring and drawing; keep them out of the SPI
 * bitmap cache so text does not evict its own glyph descriptions. */
static struct {uint16_t key;ui_glyph_t glyph;} glyph_cache[128];
void hmi_font_cache_reset(void){memset(glyph_cache,0,sizeof(glyph_cache));}
#endif
static const ui_glyph_t *glyph_for(const ui_font_t *f,uint32_t cp){
    static ui_glyph_t glyph;int i;
    /* Fast candidates for the generated ASCII/Cyrillic layout. Verify the
     * candidate so reordered or reduced font tables retain the fallback. */
    i=cp>=32&&cp<=126?(int)cp-32:cp>=1040&&cp<=1071?(int)cp-1040+96:
      cp>=1072&&cp<=1103?(int)cp-1072+129:-1;
    if(i<0||i>=f->count||UI_CODEPOINTS[i]!=cp)
        for(i=0;i<f->count;i++)if(UI_CODEPOINTS[i]==cp)break;
    if(i==f->count)for(i=0;i<f->count;i++)if(UI_CODEPOINTS[i]=='?')break;
    if(i==f->count)return 0;
#if HMI_EXTERNAL_ASSETS
    {unsigned key=f->first+(unsigned)i,slot=key%128u;
     if(glyph_cache[slot].key!=key+1u){
         hmi_read_copy(&glyph_cache[slot].glyph,UI_GLYPHS+key,sizeof(glyph));
         glyph_cache[slot].key=(uint16_t)(key+1u);
     }
     return &glyph_cache[slot].glyph;}
#else
    hmi_read_copy(&glyph,UI_GLYPHS+f->first+i,sizeof(glyph));return &glyph;
#endif
}

UI_EXPORT int ui_measure_text(int pxv,int len){const ui_font_t *f=font_for(pxv,0);int i=0,w=0;if(len>384)len=384;while(i<len){const ui_glyph_t *g=glyph_for(f,utf8_next(ui_text_input,len,&i));if(g)w+=g->advance;}return w;}

#if HMI_EXTERNAL_ASSETS
static void draw_glyph(const ui_glyph_t *g,int cursor,int baseline,uint16_t c){
    uint32_t off=(uint32_t)g->off0|((uint32_t)g->off1<<8)|((uint32_t)(g->off2&127u)<<16);
    int x=cursor+g->xoff,y=baseline+g->yoff,x0=x,x1=x+g->w,y0=y,y1=y+g->h;
    int run=0,alpha=0;
    if(x0<strip_x0)x0=strip_x0;
    if(x1>strip_x0+strip_w)x1=strip_x0+strip_w;
    if(y0<strip_y0)y0=strip_y0;
    if(y1>strip_y0+strip_h)y1=strip_y0+strip_h;
    if(x0<clip_x0)x0=clip_x0;
    if(x1>clip_x1)x1=clip_x1;
    if(y0<clip_y0)y0=clip_y0;
    if(y1>clip_y1)y1=clip_y1;
    if(x0>=x1||y0>=y1)return;
    if(g->off2&128u){
        /* Skip entire RLE runs above the tile, then decode only visible rows.
         * No division/remainder per pixel; no reads below the tile. */
        int skip=(y0-y)*g->w;
        while(skip){
            uint8_t token=hmi_read_u8(UI_FONT_BITS+off++);
            run=(token>>4)+1;alpha=token&15;
            if(run>skip){run-=skip;skip=0;}else{skip-=run;run=0;}
        }
        for(int yy=y0;yy<y1;yy++)for(int xx=x;xx<x+g->w;xx++){
            if(!run){uint8_t token=hmi_read_u8(UI_FONT_BITS+off++);run=(token>>4)+1;alpha=token&15;}
            run--;
            if(alpha&&xx>=x0&&xx<x1){
                uint16_t *dst=&ui_strip[(yy-strip_y0)*strip_w+xx-strip_x0];
                *dst=over565(*dst,c,alpha);
            }
        }
    }else{
        for(int yy=y0;yy<y1;yy++){
            int pixel=(yy-y)*g->w+x0-x;uint8_t packed=0;
            uint16_t *dst=&ui_strip[(yy-strip_y0)*strip_w+x0-strip_x0];
            for(int xx=x0;xx<x1;xx++,pixel++,dst++){
                if(xx==x0||!(pixel&1))packed=hmi_read_u8(UI_FONT_BITS+off+(pixel>>1));
                alpha=(packed>>((pixel&1)*4))&15;
                if(alpha)*dst=over565(*dst,c,alpha);
            }
        }
    }
}
#else
static void draw_glyph(const ui_glyph_t *g,int cursor,int baseline,uint16_t c){
    uint32_t off=(uint32_t)g->off0|((uint32_t)g->off1<<8)|((uint32_t)(g->off2&127u)<<16);int run=0,alpha=0;
    int x=cursor+g->xoff,y=baseline+g->yoff;
    if(x+g->w<=strip_x0||x>=strip_x0+strip_w||y+g->h<=strip_y0||y>=strip_y0+strip_h)return;
    for(int pixel=0;pixel<g->w*g->h;pixel++){
        if(g->off2&128u){
            if(!run){uint8_t token=hmi_read_u8(UI_FONT_BITS+off++);run=(token>>4)+1;alpha=token&15;}
            run--;
        }else alpha=(hmi_read_u8(UI_FONT_BITS+off+(pixel>>1))>>((pixel&1)*4))&15;
        if(alpha){
            int gx=x+pixel%g->w,gy=y+pixel/g->w;
            if(gx>=strip_x0&&gx<strip_x0+strip_w&&gy>=strip_y0&&gy<strip_y0+strip_h&&gx>=clip_x0&&gx<clip_x1&&gy>=clip_y0&&gy<clip_y1){
                uint16_t *dst=&ui_strip[(gy-strip_y0)*strip_w+(gx-strip_x0)];*dst=over565(*dst,c,alpha);
            }
        }
    }
}

#endif

UI_EXPORT void ui_text_weight(int x,int baseline,int pxv,uint16_t c,int len,int bold){
    const ui_font_t *f=font_for(pxv,bold);int i=0,cursor=x;if(len>384)len=384;
    while(i<len){
        const ui_glyph_t *g=glyph_for(f,utf8_next(ui_text_input,len,&i));if(!g)continue;
        draw_glyph(g,cursor,baseline,c);
        cursor+=g->advance;
    }
}

UI_EXPORT void ui_text_weight_fit(int x,int baseline,int pxv,uint16_t c,int len,int bold,int target_width){
    const ui_font_t *f=font_for(pxv,bold);int i=0,total=0,count=0;
    if(len>384)len=384;
    while(i<len){const ui_glyph_t *g=glyph_for(f,utf8_next(ui_text_input,len,&i));if(g){total+=g->advance;count++;}}
    /* Never stretch glyph bitmaps to match a DOM line.  Stretching made the
     * same letter acquire a different raster depending on its fractional
     * position inside a word (especially visible on Cyrillic А at 7-9 px).
     * Keep every glyph pixel-identical and distribute the small width
     * correction through advances only. */
    int natural_cursor=0;i=0;
    while(i<len){
        const ui_glyph_t *g=glyph_for(f,utf8_next(ui_text_input,len,&i));if(!g)continue;
        int cursor=x+natural_cursor;
        if(count>1&&total>0&&target_width>0)cursor=x+(natural_cursor*target_width+total/2)/total;
        draw_glyph(g,cursor,baseline,c);
        natural_cursor+=g->advance;
    }
}

UI_EXPORT void ui_text(int x,int baseline,int pxv,uint16_t c,int len){ui_text_weight(x,baseline,pxv,c,len,0);}

static int cstr_copy(const char *s){
    int n=0;
    if(!s)return 0;
    while(n<(int)sizeof(ui_text_input)){uint8_t c=hmi_read_u8(s+n);if(!c)break;ui_text_input[n++]=c;}
    return n;
}

int ui_measure_cstr(int pxv,const char *utf8){return ui_measure_text(pxv,cstr_copy(utf8));}
void ui_text_cstr(int x,int baseline,int pxv,uint16_t c,const char *utf8,int bold){
    int n=cstr_copy(utf8);ui_text_weight(x,baseline,pxv,c,n,bold);
}
void ui_text_fit_cstr(int x,int baseline,int pxv,uint16_t c,const char *utf8,int bold,int target_width){
    int n=cstr_copy(utf8);ui_text_weight_fit(x,baseline,pxv,c,n,bold,target_width);
}

/* The display callback sends the current rectangle through SPI or a parallel
 * controller driver. No full framebuffer and no dependency on a specific LCD. */

void ui_dim_content(void){
    for(int y=0;y<strip_h;y++)if(y+strip_y0>=35&&y+strip_y0<429)
        for(int x=0;x<strip_w;x++){uint16_t *p=&ui_strip[y*strip_w+x];*p=(uint16_t)((*p>>1)&0x7befu);}
}
