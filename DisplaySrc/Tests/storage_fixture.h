#include "hmi_storage.h"
#if HMI_STORAGE_VIRTUAL
#include <assert.h>
#include <stdio.h>
#if HMI_EXTERNAL_ASSETS
static int fixture_read(uint32_t at,void *dest,uint32_t n,void *user){
    FILE *f=(FILE *)user;return fseek(f,(long)at,SEEK_SET)==0&&fread(dest,1,n,f)==n;
}
#endif
static void fixture_init(void){
#if HMI_EXTERNAL_ASSETS
    FILE *f=fopen("Assets/hmi_assets.bin","rb");assert(f);assert(hmi_storage_init(fixture_read,f));
#else
    assert(hmi_storage_init(0,0));
#endif
}
#else
static void fixture_init(void){}
#endif
