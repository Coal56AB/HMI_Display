"""Build the shared STM32 platform with the selected independent display module."""
from pathlib import Path
import argparse,json,os,shutil,subprocess,sys,tempfile
from configure_keil import configure,ROOT
p=argparse.ArgumentParser();p.add_argument('--display-dir',type=Path,default=ROOT/'DisplaySrc');p.add_argument('--keil',default=os.environ.get('KEIL_UV4','E:/Keil_v5/UV4/UV4.exe'));a=p.parse_args()
module=a.display_dir.resolve()
if not (module/'module.json').exists(): module=ROOT/'Firmware/Api'
spec=json.loads((module/'module.json').read_text())
if spec.get('resourceBuild'): subprocess.run([sys.executable,str(module/spec['resourceBuild'])],check=True,cwd=module)
project=configure(module)
env=os.environ.copy();env['__COMPAT_LAYER']='RunAsInvoker'
with tempfile.TemporaryDirectory(prefix='display-firmware-') as tmp:
 log=Path(tmp)/'build.log';startup=None
 if os.name=='nt':
  startup=subprocess.STARTUPINFO();startup.dwFlags|=subprocess.STARTF_USESHOWWINDOW;startup.wShowWindow=0
 result=subprocess.run([a.keil,'-j0','-b',str(project),'-tExternalFlash','-o'+str(log)],env=env,startupinfo=startup)
 text=log.read_text(errors='replace') if log.exists() else ''
 if result.returncode not in (0,1) or '0 Error(s)' not in text:print(text);raise SystemExit('Firmware build failed')
 out=ROOT/'Release'/spec['name'];out.mkdir(parents=True,exist_ok=True)
 shutil.copy2(project.parent/'ExternalFlash/ExternalFlash.hex',out/'display.hex')
 if spec.get('assets'): shutil.copy2(module/spec['assets'],out/'resources.bin')
 (out/'build.log').write_text(text)
 print(text[text.rfind('Program Size:'):]);print(out)
