"""Run the firmware upload loop against an immediate-sender UART model."""
from pathlib import Path
import os,subprocess
root=Path(__file__).resolve().parents[2];os.chdir(root)
s=(root/'Firmware/BluePillHMI/App/board.c').read_text(encoding='utf-8')
import re
length='12345' # Exercise a final partial page without depending on any GUI module.
loop=s[s.index('void board_assets_boot(void){'):s.index('\n#else\nvoid board_assets_boot')]
header=r'''#include <stdint.h>
#include <assert.h>
#include <string.h>
#include <stdio.h>
#define HMI_ASSET_LENGTH 1196668u

#define FLASH_CS_GPIO_Port 0
#define FLASH_CS_Pin 0
#define LOADER_LENGTH 1
#define LOADER_BAD_LENGTH 2
#define LOADER_ERASE 3
#define LOADER_PROGRAM 4
#define LOADER_VERIFY 5
#define LOADER_DONE 6
static struct {struct {unsigned stage;}loader;struct {unsigned offset,ok;}storage;}app_debug;
static unsigned pending,received,pages;static int hspi1;
static void board_boot_progress(unsigned stage,unsigned percent){(void)stage;assert(percent<=100);assert(!pending);}
static int boot_request(void){return 1;}
static void flash_probe(void){} static void flash_wait(void){} static void flash_command(unsigned c){(void)c;}
static void pin(int a,int b,int c){(void)a;(void)b;(void)c;}
static void tx(void *s,const void *p,unsigned n){(void)s;(void)p;(void)n;assert(!pending);}
static void board_boot_error(unsigned e){(void)e;assert(0);}
static int validate(const void *p){(void)p;assert(!pending);return 1;}
static int board_platform;
static const struct {unsigned assets_size;int (*validate)(const void*);}display_module={HMI_ASSET_LENGTH,validate};
static void ack(char c){assert(!pending);if(c=='R')pending=4;else if(c=='D'||c=='p'){unsigned left=HMI_ASSET_LENGTH-received;pending=left>256?256:left;}}
static void receive(uint8_t *d,uint16_t n,uint32_t t){(void)t;assert(pending==n);pending=0;if(app_debug.loader.stage==LOADER_LENGTH){uint32_t size=HMI_ASSET_LENGTH;memcpy(d,&size,4);}else{memset(d,0,n);received+=n;pages++;}}
'''
import tempfile
work=tempfile.TemporaryDirectory(prefix='display-check-');os.chdir(work.name);Path('build').mkdir()
Path('build/loader_order.c').write_text(header.replace('1196668',length)+loop+'\nint main(void){board_assets_boot();assert(received==HMI_ASSET_LENGTH);assert(pages==(HMI_ASSET_LENGTH+255)/256);puts("Loader PASS: immediate sender, no LCD/SPI work while UART data pending, all pages received");return 0;}\n',encoding='utf-8')
env=os.environ.copy();env['PATH']='C:/mingw64/bin;'+env['PATH']
subprocess.run(['C:/mingw64/bin/gcc.exe','-std=c99','-Wall','-Wextra','-Werror','build/loader_order.c','-o','build/loader_order.exe'],check=True,env=env)
subprocess.run(['build/loader_order.exe'],check=True,env=env)

os.chdir(root);work.cleanup()
