#include "display_api.h"
#include "journal_store.h"
#include "hmi_ui.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static uint16_t frame[320*480];
static uint8_t flash[2*JOURNAL_BANK];
static FILE *assets;
static unsigned now,sent,last_id;
static uint32_t ticks(void){return now;}
static void reset(void){assert(0);}
static int read_assets(uint32_t at,void *data,uint32_t n,void *user){
    (void)user;return !fseek(assets,(long)at,SEEK_SET)&&fread(data,1,n,assets)==n;
}
static int read_flash(uint32_t at,void *data,uint32_t n){
    assert(at>=JOURNAL_BASE&&at-JOURNAL_BASE+n<=sizeof(flash));memcpy(data,flash+at-JOURNAL_BASE,n);return 1;
}
static int write_flash(uint32_t at,const void *data,uint32_t n){
    const uint8_t *p=data;assert(at>=JOURNAL_BASE&&at-JOURNAL_BASE+n<=sizeof(flash));
    for(unsigned i=0;i<n;i++)flash[at-JOURNAL_BASE+i]&=p[i];
    return 1;
}
static int erase_flash(uint32_t at){assert(!(at&4095));memset(flash+at-JOURNAL_BASE,255,4096);return 1;}
static void send(const uint8_t *p,uint16_t n){assert(n==30&&p[0]==0xa5&&p[1]==0x5a);sent++;last_id=p[10]|p[11]<<8;}
static void flush(uint16_t x,uint16_t y,uint16_t w,uint16_t h,const uint16_t *p,uint16_t stride,void *user){
    (void)user;assert(stride>=w&&w*h<=2560);
#if HMI_ROTATED_DISPLAY
    assert(x+w<=480&&y+h<=320);
    for(unsigned j=0;j<h;j++)for(unsigned i=0;i<w;i++)frame[(479-x-i)*320+y+j]=p[j*stride+i];
#else
    assert(x+w<=320&&y+h<=480);
    for(unsigned j=0;j<h;j++)for(unsigned i=0;i<w;i++)frame[(y+j)*320+x+i]=p[j*stride+i];
#endif
}
static void touch(int x,int y,int down){
#if HMI_ROTATED_DISPLAY
    DisplayEvent event={DISPLAY_TOUCH,now,(int16_t)(479-y),(int16_t)x,(uint8_t)down,0};
#else
    DisplayEvent event={DISPLAY_TOUCH,now,(int16_t)x,(int16_t)y,(uint8_t)down,0};
#endif
    display_event(&event);
}
static void hash(void){uint32_t h=2166136261u;for(unsigned i=0;i<320*480;i++){h^=frame[i];h*=16777619u;}printf("%08lx\n",(unsigned long)h);}
int main(void){
    DisplayPlatform p={DISPLAY_API_VERSION,
#if HMI_ROTATED_DISPLAY
        480,320,
#else
        320,480,
#endif
        flush,read_assets,read_flash,write_flash,erase_flash,ticks,send,reset,0};
    assets=fopen("Assets/hmi_assets.bin","rb");assert(assets);memset(flash,255,sizeof(flash));
    assert(display_module.validate(&p));display_init(&p);display_step(now);hash();
    now=100;touch(200,455,1);now+=30;touch(200,455,0);assert(sent==1&&last_id==HMI_PAGE_PARAMETERS);display_step(now);hash();
    now=200;touch(40,455,1);now+=30;
    {DisplayEvent cancel={DISPLAY_TOUCH_CANCEL,now,0,0,0,0};display_event(&cancel);}
    touch(40,455,0);assert(sent==1);display_step(now);hash();
    now=300;touch(40,455,1);now+=30;touch(40,455,0);assert(sent==2&&last_id==HMI_PAGE_HOME);display_step(now);hash();
    assert(journal_used()>0&&!journal_error());fclose(assets);return 0;
}
