"""Build an alternate module with unchanged platform C sources and Keil template."""
from pathlib import Path
import hashlib,json,os,subprocess,sys,tempfile
ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'Scripts/Build'))
from configure_keil import configure
files=[p for p in (ROOT/'Firmware').rglob('*') if p.suffix in ('.c','.h','.uvprojx')]
before={p:hashlib.sha256(p.read_bytes()).hexdigest() for p in files}
for p in (ROOT/'DisplaySrc/Platform').rglob('*.[ch]'):
    text=p.read_text(encoding='utf-8')
    assert '"board.h"' not in text and '"main.h"' not in text and 'HAL_' not in text,p
with tempfile.TemporaryDirectory(prefix='display-contract-') as tmp:
    module=Path(tmp)
    (module/'minimal.c').write_text("""#include "display_api.h"
static const DisplayPlatform *api;static unsigned dirty=1;static uint16_t color=0x1234;
const DisplayModule display_module={DISPLAY_API_VERSION,0,0,0,0};
void display_init(const DisplayPlatform *p){api=p;}
void display_event(const DisplayEvent *e){if(e->type==DISPLAY_TOUCH&&e->down){color^=0xffff;dirty=1;}}
void display_step(uint32_t now){(void)now;if(dirty){api->write_rect(0,0,1,1,&color,1,0);dirty=0;}}
""")
    (module/'module.json').write_text(json.dumps({'apiVersion':2,'firmware':{'sources':['minimal.c'],'includes':['.'],'defines':[]}}))
    project=configure(module,module/'build/alternate.uvprojx')
    env=dict(os.environ,__COMPAT_LAYER='RunAsInvoker')
    startup=subprocess.STARTUPINFO();startup.dwFlags|=subprocess.STARTF_USESHOWWINDOW;startup.wShowWindow=0
    log=module/'compile.log';result=subprocess.run([os.environ.get('KEIL_UV4','E:/Keil_v5/UV4/UV4.exe'),'-j0','-b',str(project),'-tExternalFlash','-o'+str(log)],env=env,startupinfo=startup)
    text=log.read_text(errors='replace')
    assert result.returncode in (0,1) and '0 Error(s)' in text,text
assert before=={p:hashlib.sha256(p.read_bytes()).hexdigest() for p in files}
print('Platform boundary PASS: alternate GUI builds; all platform sources and project are byte-identical')
