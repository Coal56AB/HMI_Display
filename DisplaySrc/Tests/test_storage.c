#include "hmi_storage.h"
#include "hmi_assets_layout_full.h"
#include <assert.h>
#include <stdio.h>
static FILE *image;
static int corrupt,fail;
static int read_image(uint32_t at,void *dest,uint32_t n,void *user){
    (void)user;if(fail)return 0;
    if(fseek(image,(long)at,SEEK_SET)||fread(dest,1,n,image)!=n)return 0;
    if(corrupt&&at<=100&&at+n>100)((uint8_t *)dest)[100-at]^=1;
    return 1;
}
int main(void){
    unsigned i;uint8_t expected[37],actual[37];
    image=fopen("Assets/hmi_assets.bin","rb");assert(image);
    assert(hmi_storage_init(read_image,0));
    for(i=0;i+sizeof(actual)<HMI_ASSET_LENGTH;i+=509){
        assert(read_image(i,expected,sizeof(expected),0));
        hmi_read_copy(actual,(void *)(uintptr_t)(HMI_ASSET_BASE+i),sizeof(actual));
        assert(!memcmp(expected,actual,sizeof(actual)));
    }
    corrupt=1;assert(!hmi_storage_init(read_image,0));
    corrupt=0;fail=1;assert(!hmi_storage_init(read_image,0));assert(hmi_storage_error());
    fail=0;assert(hmi_storage_init(read_image,0));
    fclose(image);puts("storage: page boundaries, CRC corruption, read failure and remount PASS");
    return 0;
}
