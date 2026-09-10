"""Inspect exact exported geometry when extending the touch map."""
import gzip
import json
from pathlib import Path
import re
import sys
sys.stdout.reconfigure(encoding='utf-8')
d = json.loads(gzip.decompress((Path(__file__).resolve().parents[1] / 'Assets/asset_source.json.gz').read_bytes()))
strings = bytes(d['strings'])
for first,count,name in re.findall(r'\{(\d+)u,(\d+)u,"([^"]+)"\}',d['scenes_c']):
    if name not in sys.argv[1:]:
        continue
    print('\nSCENE',name)
    lines=[]
    for block in d['scene_blocks'][int(first):int(first)+int(count)]:
        for primitive in d['block_commands'][d['block_offsets'][block]:d['block_offsets'][block+1]]:
            start,end = d['primitive_offsets'][primitive:primitive+2]
            p = d['primitive_words'][start:end]
            if p[0] == 13:
                text = strings[p[8]:].split(b'\0')[0].decode('utf-8')
                lines.append(('TEXT',p[4:8],text))
            elif p[0] in (2,7,8,9) and p[6]>=35 and p[7]>=20:
                lines.append(('BOX',p[4:8]))
    for line in lines[-75:]:
        print(*line)
