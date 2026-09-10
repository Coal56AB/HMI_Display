#include "storage_fixture.h"
#include "hmi.h"
#include "hmi_gfx.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static uint16_t frame[320u*480u]; /* Host tests only. */
static FILE *raw;
static void collect(uint16_t x,uint16_t y,uint16_t w,uint16_t h,
                    const uint16_t *p,uint16_t stride,void *user){
    unsigned row;(void)user;assert(x+w<=320u&&y+h<=480u&&stride>=w);
    for(row=0;row<h;row++)memcpy(frame+(y+row)*320u+x,p+row*stride,w*2u);
}
static void result(const char *label,const uint16_t *p,unsigned count){
    uint64_t hash=UINT64_C(14695981039346656037);unsigned i;
    for(i=0;i<count;i++){
        unsigned char b[2]={(unsigned char)p[i],(unsigned char)(p[i]>>8)};
        hash^=b[0];hash*=UINT64_C(1099511628211);hash^=b[1];hash*=UINT64_C(1099511628211);
        if(raw)assert(fwrite(b,1,2,raw)==2);
    }
    printf("%s %08lx%08lx\n",label,(unsigned long)(hash>>32),(unsigned long)(hash&0xffffffffu));
}
int main(int argc,char **argv){
    fixture_init();
    HmiState s;unsigned id,mode,px,bold,cp,cp_index;char label[100];
    static const uint16_t codepoints[]={
#include "glyph_codepoints.inc"
    };
    static const int16_t samples[]={-100,-5,30,90,25,0,110};
    static const HmiJournalItem journal[]={
        {"2026-09-07 12:34:56","fault","Перегрев"},
        {"2026-09-07 12:35:00","warn","Ток"},
        {"2026-09-07 12:36:00","info","Пуск"}};
    if(argc>1){raw=fopen(argv[1],"wb");assert(raw);}
    for(mode=0;mode<3;mode++)for(id=0;id<HMI_SCENE_COUNT;id++){
        hmi_state_defaults(&s);s.scene_override=(HmiSceneId)id;
        if(mode){
            s.dynamic_values=1;s.clock="23:59";s.output_current=3.74f;s.output_power=1.23f;
            s.output_voltage=219;s.motor_load=72;s.slip=-1.5f;s.regulation_active=1;
            s.precharge_seconds=2.4f;s.mains_frequency=49.9f;s.mains_voltage=231;
            if(id>=HMI_SCENE_GRAPHS_DQD&&id<=HMI_SCENE_GRAPHS_SPEED){
                s.page=HMI_PAGE_GRAPHS;s.graph[0]=(HmiGraphChannel){samples,7,-100,100,65535,1,"Ток","А"};
            }else if(id==HMI_SCENE_JOURNAL_PAGE1||id==HMI_SCENE_JOURNAL_PAGE2){
                s.page=HMI_PAGE_JOURNAL;s.journal=journal;s.journal_count=3;
            }
        }
        hmi_init();
        if(mode==2){ /* Non-aligned narrow dirty rectangle, unchanged surroundings. */
            memset(frame,0x5a,sizeof(frame));hmi_invalidate((HmiRect){11,13,137,419});
            hmi_render_dirty(&s,collect,NULL);
        }else hmi_render_full(&s,collect,NULL);
        snprintf(label,sizeof(label),"scene-%u-%s",mode,hmi_generated_scenes[id].name);
        result(label,frame,320u*480u);
    }
    /* Every supported alphabet glyph, both weights, all exact and fallback sizes.
     * Space/ASCII, Cyrillic and extra symbols are independent of font storage. */
    for(bold=0;bold<2;bold++)for(px=7;px<=20;px++)for(cp_index=0;cp_index<sizeof(codepoints)/sizeof(codepoints[0]);cp_index++){
        char text[4]={0};unsigned band;
        cp=codepoints[cp_index];
        if(cp<128)text[0]=(char)cp;
        else if(cp<2048){text[0]=(char)(0xc0|(cp>>6));text[1]=(char)(0x80|(cp&63));}
        else {text[0]=(char)(0xe0|(cp>>12));text[1]=(char)(0x80|((cp>>6)&63));text[2]=(char)(0x80|(cp&63));}
        for(band=0;band<2;band++){
            ui_begin_rect(0,(int)band*16,40,16,0x2345);ui_set_clip(2,1,36,30);
            ui_text_fit_cstr(3,24,(int)px,0xfedc,text,(int)bold,25);
            snprintf(label,sizeof(label),"glyph-%u-%u-%u-%u",bold,px,cp,band);
            result(label,ui_strip_data(),640);
        }
    }
    if(raw)assert(fclose(raw)==0);
    return 0;
}
