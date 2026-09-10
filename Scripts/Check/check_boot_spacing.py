from pathlib import Path
import os,subprocess
os.environ["PATH"]="C:/mingw64/bin"+os.pathsep+os.environ["PATH"]
root=Path(__file__).resolve().parents[2];os.chdir(root)
s=Path('Firmware/BluePillHMI/App/board.c').read_text(encoding='utf-8')
a=s.index('void board_boot_progress(');b=s.index('\nvoid board_boot_error(',a);c=s.index('\n}',b)+2
code='''#include <stdint.h>
#include <string.h>
#include <assert.h>
static unsigned boot_line_count;
static int positions[16],n;
static void boot_text(int x,int y,const char*t,unsigned z,uint16_t col){(void)x;(void)z;(void)col;if(strlen(t)>5&&n<16)positions[n++]=y;}
static void boot_begin(unsigned y){(void)y;n=0;}
static uint16_t boot_pixels[320];
static void board_write_rect(int a,int b,int c,int d,const uint16_t*p,int e,int f){(void)a;(void)b;(void)c;(void)d;(void)p;(void)e;(void)f;}
static void Error_Handler(void){}
'''+s[a:c]+'''\nint main(void){board_boot_progress(0,100);board_boot_progress(1,100);board_boot_progress(4,100);board_boot_progress(5,100);board_boot_progress(6,100);assert(boot_line_count==5);for(int i=1;i<6;i++)assert(positions[i]==40+(i-1)*20);board_boot_error(3);for(int i=0;i<4;i++)assert(positions[i]==140+i*20);return 0;}'''
import tempfile
work=tempfile.TemporaryDirectory(prefix='display-check-');os.chdir(work.name);Path('build').mkdir()
p=Path('build/boot_spacing_test.c');p.write_text(code,encoding='utf-8');subprocess.run(['C:/mingw64/bin/gcc.exe','-std=c99','-Wall','-Wextra','-Werror',str(p),'-o','build/boot_spacing_test.exe'],check=True);subprocess.run(['build/boot_spacing_test.exe'],check=True);print('Boot spacing PASS: skipped stages leave no gaps; stage and error lines use 20 px pitch')

os.chdir(root);work.cleanup()
