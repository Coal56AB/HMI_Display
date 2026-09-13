"""Export the shared module manifest for the ESP-IDF CMake 3.16 toolchain."""
import json
from pathlib import Path
module = Path(__file__).resolve().parents[1]
manifest = json.loads((module / 'module.json').read_text(encoding='utf-8'))
spec = manifest['firmware']
print('set(pch_assets [[%s]])' % (module / manifest['assets']).as_posix())
for field, target in [('sources', 'hmi_sources'), ('includes', 'hmi_includes'), ('defines', 'hmi_defines')]:
    for value in spec[field]:
        if field != 'defines':
            value = (module / value).as_posix()
        print('list(APPEND %s [[%s]])' % (target, value))
