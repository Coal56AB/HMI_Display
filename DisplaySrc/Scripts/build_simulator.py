"""Build this module's desktop simulator; no parent repository is required."""
from pathlib import Path
import subprocess,sys,os
ROOT=Path(__file__).resolve().parents[1]
def main():
 os.environ['PYTHONUTF8']='1'
 subprocess.run([sys.executable,str(ROOT/'Scripts/generate_rasters.py')],check=True)
 subprocess.run([sys.executable,str(ROOT/'Scripts/build_native_renderer.py')],check=True)
 work=ROOT/'.build/desktop';work.mkdir(parents=True,exist_ok=True)
 args=[sys.executable,'-m','PyInstaller','--noconfirm','--distpath',str(ROOT/'Release/Simulator'),'--workpath',str(work/'work'),str(ROOT/'Scripts/simulator.spec')]
 subprocess.run(args,check=True,cwd=ROOT)
if __name__=='__main__':main()
