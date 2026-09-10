"""Measure C objects without host executable/CRT/framebuffer overhead."""
import argparse
import json
from pathlib import Path
import subprocess

ROOT=Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--baseline',type=Path)
p.add_argument('--cc',default='gcc')
p.add_argument('--size',default='size')
args=p.parse_args()
results={}
sources=['hmi_gfx','hmi_scene','hmi_scene_generated','hmi_dirty','hmi_dynamic']
for name,root,files in [('current',ROOT,sources+['hmi_ui'])]+([('baseline',args.baseline,sources)] if args.baseline else []):
    folder=ROOT/'.build'/('size-'+name);folder.mkdir(parents=True,exist_ok=True)
    entries={}
    for stem in files:
        obj=folder/(stem+'.o')
        subprocess.run([args.cc,'-std=c99','-Os','-Wall','-Wextra','-Werror','-I'+str(root/'Include'),'-c',str(root/'Src'/(stem+'.c')),'-o',str(obj)],check=True)
        sections={}
        for line in subprocess.check_output([args.size,'-A',str(obj)],text=True).splitlines():
            words=line.split()
            if len(words)>=2 and words[0].startswith('.'):
                sections[words[0]]=int(words[1])
        entries[stem]={
            'code_const':sum(v for k,v in sections.items() if k.startswith(('.text','.rdata','.rodata'))),
            'ram':sum(v for k,v in sections.items() if k.startswith(('.data','.bss'))),
            'sections':sections}
    results[name]={'objects':entries,'code_const':sum(e['code_const'] for e in entries.values()),'ram':sum(e['ram'] for e in entries.values())}
(ROOT/'.build/size_report.json').write_text(json.dumps(results,indent=2)+'\n')
for name,r in results.items():
    print(name, 'code+const:',r['code_const'],'static RAM:',r['ram'])
    for stem,v in r['objects'].items():print(' ',stem,v['code_const'],v['ram'])
