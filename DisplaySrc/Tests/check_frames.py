import sys,os
from pathlib import Path
MODULE=Path(__file__).resolve().parents[1]
os.chdir(MODULE)
(MODULE/".build").mkdir(exist_ok=True)
sys.path.insert(0,str(MODULE/"Simulator"))
"""Compare static/glyph upstream and reviewed controller-driven dynamic frames."""
from pathlib import Path
import subprocess
import sys

root = Path(__file__).resolve().parents[1]
exe = root / '.build/test_full_frames'
if sys.platform == 'win32':
    exe = exe.with_suffix('.exe')
actual = subprocess.check_output([str(exe)], text=True).splitlines()
expected = (root / 'Tests/baseline_hashes.txt').read_text(encoding='utf-8-sig').splitlines()
if actual != expected:
    differences = [(a,b) for a,b in zip(expected,actual) if a!=b]
    raise SystemExit(f'Frame regression: {len(differences)} mismatches, lengths {len(expected)}/{len(actual)}; {differences[:3]}')
print(f'Renderer regression: {len(actual)} hashes PASS (47 scenes x 3 variants + glyphs)')
