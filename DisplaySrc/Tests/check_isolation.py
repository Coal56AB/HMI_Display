"""Verify that resource generation and the simulator work without a parent project."""
from pathlib import Path
import argparse,os,shutil,subprocess,sys,tempfile
ROOT=Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser();p.add_argument('--display-dir',type=Path,default=ROOT);a=p.parse_args()
with tempfile.TemporaryDirectory(prefix='display-isolation-') as work:
 module=Path(work)/'IndependentDisplay'
 shutil.copytree(a.display_dir,module,ignore=shutil.ignore_patterns('.build','Release','__pycache__','.git'))
 assert not (module.parent/'Firmware').exists()
 for script in ['Scripts/generate_rasters.py','Scripts/build_native_renderer.py','Tests/check_rendering.py','Tests/check_review.py','Tests/check_storage.py','Tests/check_desktop.py']:
  subprocess.run([sys.executable,str(module/script)],cwd=module,check=True,env={**os.environ,'PYTHONUTF8':'1'})
print('Module isolation PASS: resources, native renderer and desktop work outside the parent repository')
