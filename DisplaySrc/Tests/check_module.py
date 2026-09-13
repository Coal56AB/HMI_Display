"""Check the full platform API adapter in portrait and rotated configurations."""
from pathlib import Path
import json, os, shutil, subprocess
root = Path(__file__).resolve().parents[1]
spec = json.loads((root / 'module.json').read_text())['firmware']
cc = os.environ.get('CC') or shutil.which('gcc')
if not cc:
    raise SystemExit('Set CC or put GCC in PATH')
build = root / '.build'
build.mkdir(exist_ok=True)
results = []
for rotated in (0, 1):
    exe = build / ('test_module_%d.exe' % rotated)
    command = [cc, '-std=c99', '-Os', '-Wall', '-Wextra', '-Werror', '-DHMI_ROTATED_DISPLAY=%d' % rotated,
               '-I' + str(root.parent / 'Firmware/Api')]
    command += ['-I' + str(root / path) for path in spec['includes']]
    command += ['-D' + value for value in spec['defines']]
    command += [str(root / path) for path in spec['sources']]
    command += [str(root / 'Tests/test_module.c'), '-o', str(exe)]
    subprocess.run(command, check=True)
    results.append(subprocess.check_output([str(exe)], cwd=str(root)))
assert results[0] == results[1], results
print('Module PASS: portrait/rotated frames match; navigation, touch cancellation, resources and journal')
