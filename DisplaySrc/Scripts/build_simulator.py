"""Build this module's desktop simulator; no parent repository is required."""
from pathlib import Path
import subprocess,sys,os
ROOT=Path(__file__).resolve().parents[1]
def main():
 os.environ['PYTHONUTF8']='1'
 subprocess.run([sys.executable,str(ROOT/'Scripts/generate_rasters.py')],check=True)
 subprocess.run([sys.executable,str(ROOT/'Scripts/build_native_renderer.py')],check=True)
 work=ROOT/'.build/desktop';work.mkdir(parents=True,exist_ok=True)
 args=[sys.executable,'-m','PyInstaller','--noconfirm','--windowed','--name','PCH_Simulator','--distpath',str(ROOT/'Release/Simulator'),'--workpath',str(work/'work'),'--specpath',str(work)]
 for path in ['Simulator/uart_simulator.html','Simulator/uart_bridge.js','Release/Native/renderer-build.json','Assets/hmi_assets.bin']:
  args+=['--add-data',str(ROOT/path)+os.pathsep+'.']
 args+=['--add-binary',str(ROOT/'Release/Native/hmi_renderer.dll')+os.pathsep+'.',str(ROOT/'Simulator/desktop_simulator.py')]
 subprocess.run(args,check=True,cwd=ROOT)
if __name__=='__main__':main()
