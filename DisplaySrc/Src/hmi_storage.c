#include "hmi_storage.h"
#if HMI_STORAGE_VIRTUAL
#if HMI_LITE
#include "hmi_assets_layout_lite.h"
#include "hmi_assets_lite.inc"
#else
#include "hmi_assets_layout_full.h"
#endif
#if HMI_LITE
#include "tinf.h"
#define PAGE_SIZE 4096u
#define SLOTS 1u
#else
#define PAGE_SIZE 64u
#define SLOTS 32u
#endif
static uint8_t cache[SLOTS][PAGE_SIZE];
static uint32_t tags[SLOTS];
static uint32_t ages[SLOTS],age;
static uint32_t last_slot;
static HmiAssetReader read_asset;
static void *reader_user;
static int failed;
uint32_t hmi_storage_loads;
#if HMI_LITE
/* Retain small pieces of interleaved tables even after their 4 KiB
 * decompression page is evicted. Same asset format, no extra Flash. */
#define LINE_SIZE 64u
#define LINE_SLOTS 48u
static uint8_t lines[LINE_SLOTS][LINE_SIZE];
static uint32_t line_tags[LINE_SLOTS],line_ages[LINE_SLOTS],line_age;
static uint32_t last_line;
#endif

static uint8_t byte_at(uint32_t address){
    uint32_t page=address/PAGE_SIZE,slot,i;
    if(address>=HMI_ASSET_LENGTH){failed=1;return 0;}
    if(tags[last_slot]==page)return cache[last_slot][address%PAGE_SIZE];
    for(slot=0;slot<SLOTS;slot++)if(tags[slot]==page)break;
    if(slot==SLOTS){
        slot=0;
        for(i=1;i<SLOTS;i++)if(ages[i]<ages[slot])slot=i;
    }
    if(++age==0){for(i=0;i<SLOTS;i++)ages[i]=0;age=1;}
    ages[slot]=age;
    if(tags[slot]!=page){
        hmi_storage_loads++;
        uint32_t remaining=HMI_ASSET_LENGTH-page*PAGE_SIZE,n=remaining<PAGE_SIZE?remaining:PAGE_SIZE;
#if HMI_LITE
        unsigned out=n;
        if(tinf_uncompress(cache[slot],&out,lite_bytes+lite_offsets[page],
                           lite_offsets[page+1]-lite_offsets[page])!=TINF_OK||out!=n){failed=1;memset(cache[slot],0,n);}
#else
        if(!read_asset||!read_asset(page*PAGE_SIZE,cache[slot],n,reader_user)){failed=1;memset(cache[slot],0,n);}
#endif
        tags[slot]=page;
    }
    last_slot=slot;
    return cache[slot][address%PAGE_SIZE];
}
#if HMI_LITE
static uint8_t cached_byte(uint32_t address){
    uint32_t tag=address/LINE_SIZE,slot,i,base=tag*LINE_SIZE;
    if(address>=HMI_ASSET_LENGTH){failed=1;return 0;}
    if(line_tags[last_line]==tag)return lines[last_line][address%LINE_SIZE];
    for(slot=0;slot<LINE_SLOTS;slot++)if(line_tags[slot]==tag)break;
    if(slot==LINE_SLOTS){
        slot=0;
        for(i=1;i<LINE_SLOTS;i++)if(line_ages[i]<line_ages[slot])slot=i;
        /* A cache line never crosses a decompression page. */
        for(i=0;i<LINE_SIZE&&base+i<HMI_ASSET_LENGTH;i++)lines[slot][i]=byte_at(base+i);
        line_tags[slot]=tag;
    }
    if(++line_age==0){for(i=0;i<LINE_SLOTS;i++)line_ages[i]=0;line_age=1;}
    line_ages[slot]=line_age;
    last_line=slot;
    return lines[slot][address%LINE_SIZE];
}
#else
#define cached_byte byte_at
#endif
uint8_t hmi_read_u8(const void *source){
    uintptr_t p=(uintptr_t)source;
    if(p>=HMI_ASSET_BASE&&p<HMI_ASSET_BASE+0x01000000u)return cached_byte((uint32_t)(p-HMI_ASSET_BASE));
    return *(const uint8_t *)source;
}
uint16_t hmi_read_u16(const uint16_t *p){const uint8_t *b=(const uint8_t *)p;return (uint16_t)(hmi_read_u8(b)|((uint16_t)hmi_read_u8(b+1)<<8));}
void hmi_read_copy(void *destination,const void *source,uint32_t length){
    uint8_t *d=(uint8_t *)destination;const uint8_t *s=(const uint8_t *)source;
#if HMI_EXTERNAL_ASSETS
    uintptr_t p=(uintptr_t)source;
    if(p>=HMI_ASSET_BASE&&p<HMI_ASSET_BASE+0x01000000u){
        uint32_t at=(uint32_t)(p-HMI_ASSET_BASE);
        while(length){
            uint32_t n=PAGE_SIZE-at%PAGE_SIZE;if(n>length)n=length;
            if(at>=HMI_ASSET_LENGTH||n>HMI_ASSET_LENGTH-at){failed=1;memset(d,0,length);return;}
            (void)byte_at(at);
            memcpy(d,cache[last_slot]+at%PAGE_SIZE,n);d+=n;at+=n;length-=n;
        }
        return;
    }
    memcpy(destination,source,length);(void)d;(void)s;
#else
    while(length--)*d++=hmi_read_u8(s++);
#endif
}
int hmi_storage_error(void){return failed;}
__attribute__((weak)) void hmi_storage_progress(unsigned done,unsigned total){(void)done;(void)total;}
int hmi_storage_init(HmiAssetReader reader,void *user){
    unsigned i;uint32_t crc=0xffffffffu;uint8_t header[16];
    extern void hmi_scene_cache_reset(void);
    hmi_scene_cache_reset();
    last_slot=0;
#if HMI_EXTERNAL_ASSETS
    {extern void hmi_font_cache_reset(void);hmi_font_cache_reset();}
#endif
    read_asset=reader;reader_user=user;failed=0;age=0;hmi_storage_loads=0;
    for(i=0;i<SLOTS;i++){tags[i]=0xffffffffu;ages[i]=0;}
#if HMI_LITE
    line_age=0;
    last_line=0;
    for(i=0;i<LINE_SLOTS;i++){line_tags[i]=0xffffffffu;line_ages[i]=0;}
#endif
    hmi_storage_progress(0,HMI_ASSET_LENGTH);
    hmi_read_copy(header,(const void *)(uintptr_t)HMI_ASSET_BASE,16);
    if(failed)return 0;
    if(memcmp(header,"PCH1",4)||header[4]!=1){failed=2;return 0;}
    {uint32_t length,expected;memcpy(&length,header+8,4);memcpy(&expected,header+12,4);
     if(length!=HMI_ASSET_LENGTH||expected!=HMI_ASSET_CRC){failed=3;return 0;}}
    for(i=16;i<HMI_ASSET_LENGTH;i++){
        if((i&16383u)==0)hmi_storage_progress(i,HMI_ASSET_LENGTH);
        unsigned bit;crc^=byte_at(i);for(bit=0;bit<8;bit++)crc=(crc>>1)^((crc&1u)?0xedb88320u:0u);
    }
    if(failed)return 0;
    if((crc^0xffffffffu)!=HMI_ASSET_CRC){failed=4;return 0;}
    hmi_storage_progress(HMI_ASSET_LENGTH,HMI_ASSET_LENGTH);
    return 1;
}
#endif
