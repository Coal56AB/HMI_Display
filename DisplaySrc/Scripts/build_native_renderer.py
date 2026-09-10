from pathlib import Path
import os,subprocess,shutil,hashlib,json
root=Path(__file__).resolve().parents[1]
out=root/'Release/Native';out.mkdir(parents=True,exist_ok=True)
cc=os.environ.get('CC') or shutil.which('gcc') or 'C:/mingw64/bin/gcc.exe'
env=os.environ.copy();env['PATH']=str(Path(cc).parent)+os.pathsep+env['PATH']
sources=list((root/'Src').glob('*.c'))+[root/'ThirdParty/tinf/tinflate.c',root/'Simulator/native_renderer.c']
subprocess.run([cc,'-shared','-static-libgcc','-std=c99','-Os','-Wall','-Wextra','-Werror','-DHMI_EXTERNAL_ASSETS=1','-I'+str(root/'Include'),'-I'+str(root/'ThirdParty/tinf'),*map(str,sources),'-o',str(out/'hmi_renderer.dll')],check=True,env=env)
h=hashlib.sha256()
for p in sorted(sources+list((root/'Include').glob('*'))):
 if p.is_file():h.update(p.name.encode());h.update(p.read_bytes())
(out/'renderer-build.json').write_text(json.dumps({'rendererSha256':h.hexdigest(),'assetsSha256':hashlib.sha256((root/'Assets/hmi_assets.bin').read_bytes()).hexdigest()},indent=2))
