import sys,os
from pathlib import Path
MODULE=Path(__file__).resolve().parents[1]
os.chdir(MODULE)
(MODULE/".build").mkdir(exist_ok=True)
sys.path.insert(0,str(MODULE/"Simulator"))
"""Check boot asset failures and progress with the actual C storage reader."""
import os,subprocess
from pathlib import Path
root=Path(__file__).resolve().parents[1];os.chdir(root)
lib=root
source=root/'.build/storage_check.c'
source.write_text(r'''#include "hmi_storage.h"
#include "hmi_assets_layout_full.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
static unsigned mode,first,last,calls;static unsigned char *image;
void hmi_scene_cache_reset(void){} void hmi_font_cache_reset(void){}
void hmi_storage_progress(unsigned done,unsigned total){assert(total==HMI_ASSET_LENGTH);if(!calls)first=done;else assert(done>=last);last=done;calls++;}
static int reader(uint32_t at,void *dst,uint32_t n,void *u){(void)u;if(mode==1)return 0;memcpy(dst,image+at,n);return 1;}
static void check(unsigned m,unsigned error){mode=m;calls=first=last=0;assert(hmi_storage_init(reader,0)==(error==0));assert(hmi_storage_error()==(int)error);assert(calls&&first==0);assert((last==HMI_ASSET_LENGTH)==(error==0));}
int main(int argc,char **argv){FILE *f;unsigned char saved;(void)argc;f=fopen(argv[1],"rb");assert(f);image=malloc(HMI_ASSET_LENGTH);assert(image);assert(fread(image,1,HMI_ASSET_LENGTH,f)==HMI_ASSET_LENGTH);fclose(f);
check(0,0);check(1,1);saved=image[0];image[0]=0;check(0,2);image[0]=saved;
image[8]^=1;check(0,3);image[8]^=1;image[100]^=1;check(0,4);image[100]^=1;check(0,0);free(image);puts("Storage PASS: valid, read failure, missing header, incompatible assets, CRC failure; 100% only on success");return 0;}
''',encoding='utf-8')
env=os.environ.copy();env['PATH']='C:/mingw64/bin;'+env['PATH']
subprocess.run(['C:/mingw64/bin/gcc.exe','-std=c99','-Os','-Wall','-Wextra','-Werror','-DHMI_EXTERNAL_ASSETS=1','-I'+str(lib/'Include'),str(lib/'Src/hmi_storage.c'),str(source),'-o','.build/storage_check.exe'],check=True,env=env)
subprocess.run(['.build/storage_check.exe',str(lib/'Assets/hmi_assets.bin')],check=True,env=env)
