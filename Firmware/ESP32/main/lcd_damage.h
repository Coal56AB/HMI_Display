#pragma once
#include <stdint.h>
#include <algorithm>
// Cache signatures, not a 300 KB framebuffer. Partial tiles invalidate their signature.
class LcdDamage {
    uint64_t hashes[30*40]{};
    bool valid[30*40]{};
public:
    void invalidate(unsigned x,unsigned y,unsigned w,unsigned h) {
        if(!w||!h)return;
        for(unsigned yy=y/8;yy<=(y+h-1)/8;++yy)
            for(unsigned xx=x/16;xx<=(x+w-1)/16;++xx)valid[yy*30+xx]=false;
    }
    template<class Write> void draw(unsigned x,unsigned y,unsigned w,unsigned h,
                                   const uint16_t *pixels,unsigned stride,Write write) {
        for(unsigned yy=y;yy<y+h;) {
            unsigned hh=std::min(y+h-yy,8-yy%8),run=x;
            bool sending=false;
            for(unsigned xx=x;xx<x+w;) {
                unsigned ww=std::min(x+w-xx,16-xx%16),index=(yy/8)*30+xx/16;
                bool dirty=true;
                if(ww==16&&hh==8) {
                    uint64_t hash=14695981039346656037ull;
                    for(unsigned row=0;row<hh;++row)for(unsigned col=0;col<ww;++col)
                        hash=(hash^pixels[(yy-y+row)*stride+xx-x+col])*1099511628211ull;
                    dirty=!valid[index]||hashes[index]!=hash;
                    hashes[index]=hash;valid[index]=true;
                } else valid[index]=false;
                if(dirty&&!sending){run=xx;sending=true;}
                if(!dirty&&sending){write(run,yy,xx-run,hh,pixels+(yy-y)*stride+run-x,stride);sending=false;}
                xx+=ww;
            }
            if(sending)write(run,yy,x+w-run,hh,pixels+(yy-y)*stride+run-x,stride);
            yy+=hh;
        }
    }
};
