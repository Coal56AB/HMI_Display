"""Check cap height and special marks of the internal boot console font."""
from pathlib import Path
import re
s=(Path(__file__).resolve().parents[2]/'Firmware/BluePillHMI/App/boot_font.h').read_text(encoding='utf-8')
cps=list(map(int,re.search(r'boot_codepoints\[\]=\{([^}]+)',s)[1].split(',')))
rows=[list(map(int,r.split(','))) for r in re.findall(r'\{([\d,]+)\}',s[s.index('boot_glyphs'):])]
assert len(cps)==len(rows)
for cp,row in zip(cps,rows):
    assert len(row)==12
    if chr(cp).isalpha() and cp not in (1044,1049,1062):
        ys=[i for i,n in enumerate(row) if n]
        assert min(ys)==2 and max(ys)==8,(chr(cp),ys)
print('Boot font PASS: all capital bodies share rows 2..8; accent/descenders are explicit exceptions')
