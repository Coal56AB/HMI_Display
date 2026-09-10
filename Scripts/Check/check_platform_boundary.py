"""Compile the common platform with a minimal replacement GUI, then restore module selection."""
from pathlib import Path
import json,os,subprocess,sys,tempfile
ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'Scripts/Build'))
from configure_keil import configure
for p in (ROOT/'Firmware/BluePillHMI/App').glob('*.[ch]'):
 text=p.read_text(encoding='utf-8')
 assert '#include "hmi_' not in text and 'HmiUi' not in text and 'HmiEvent' not in text,p
project=ROOT/'Firmware/BluePillHMI/MDK-ARM/BluePillHMI.uvprojx'
original=project.read_bytes()
with tempfile.TemporaryDirectory(prefix='display-contract-') as tmp:
 module=Path(tmp);(module/'display_module.h').write_bytes((ROOT/'DisplaySrc/Include/display_module.h').read_bytes())
 (module/'minimal.c').write_text('''#include "display_module.h"
static uint16_t pixels[320*4];static DisplayWriteRect output;static unsigned line,dirty=1;static uint16_t color=0x1234;
void display_init(DisplayWriteRect write){output=write;}
void display_render(void){if(!dirty)return;for(unsigned i=0;i<1280;i++)pixels[i]=color;for(unsigned y=0;y<480;y+=4)output(0,y,320,4,pixels,320,0);dirty=0;}
void display_touch(int16_t x,int16_t y,uint8_t down,uint32_t now){(void)x;(void)y;(void)now;if(down){color^=0xffff;dirty=1;}}
void display_poll(uint32_t now){(void)now;}
void display_receive(uint8_t b){(void)b;}
void display_receive_error(void){}
int display_assets_init(DisplayReadAssets read){(void)read;return 1;}
unsigned display_assets_error(void){return 0;}
uint32_t display_assets_size(void){return 16;}
void display_asset_loads_reset(void){}
unsigned display_asset_loads(void){return 0;}
void display_console_begin(unsigned y){line=y;for(unsigned i=0;i<1280;i++)pixels[i]=0;}
void display_console_pixel(int x,int y,unsigned size,uint16_t c){for(unsigned dy=0;dy<size;dy++)for(unsigned dx=0;dx<size;dx++)if(x+(int)dx>=0&&x+(int)dx<320&&y+(int)dy>=(int)line&&y+(int)dy<(int)line+4)pixels[(y+dy-line)*320+x+dx]=c;}
const uint16_t *display_console_pixels(void){return pixels;}
''')
 (module/'module.json').write_text(json.dumps({'apiVersion':1,'firmware':{'sources':['minimal.c'],'includes':['.'],'defines':[]}}))
 try:
  configure(module);env=os.environ.copy();env['__COMPAT_LAYER']='RunAsInvoker'
  startup=subprocess.STARTUPINFO();startup.dwFlags|=subprocess.STARTF_USESHOWWINDOW;startup.wShowWindow=0
  log=module/'compile.log';result=subprocess.run([os.environ.get('KEIL_UV4','E:/Keil_v5/UV4/UV4.exe'),'-j0','-b',str(project),'-tExternalFlash','-o'+str(log)],env=env,startupinfo=startup)
  text=log.read_text(errors='replace')
  assert result.returncode in (0,1) and '0 Error(s)' in text,text
  print('Platform boundary PASS: alternate GUI and touch handler compile without any PCH source')
 finally:project.write_bytes(original)
