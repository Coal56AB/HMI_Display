"""Compile and exercise the same C renderer as the target firmware."""
import os, shutil, subprocess
from pathlib import Path
root=Path(__file__).resolve().parents[2]
gcc=os.environ.get('CC') or shutil.which('gcc') or 'C:/mingw64/bin/gcc.exe'
env=os.environ.copy();env['PATH']=str(Path(gcc).parent)+os.pathsep+env.get('PATH','')
out=root/'.build';out.mkdir(exist_ok=True)
args=[gcc,'-std=c99','-Wall','-Wextra','-Werror','-I'+str(root/'Firmware/Api')]
subprocess.run(args+[str(root/'DisplaySrc/tests/test_ui.c'),'-o',str(out/'test_ui.exe')],env=env,check=True)
subprocess.run([str(out/'test_ui.exe')],env=env,check=True)
subprocess.run(args+['-shared','-O2',str(root/'DisplaySrc/src/music_box.c'),'-o',str(out/'music_box.dll')],env=env,check=True)
