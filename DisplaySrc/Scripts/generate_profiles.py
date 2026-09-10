"""Generate full SPI-NOR image and compact internal-Flash resource profile."""
import gzip
import importlib.util
import json
from pathlib import Path
import re
import struct
import sys
import subprocess
import tempfile
import zlib
import zopfli.zlib

ROOT=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('pack',ROOT/'Scripts/pack_assets.py')
pack=importlib.util.module_from_spec(spec);spec.loader.exec_module(pack)

def lz(data):
    # Spend time on the host to leave room for dirty-region code in 64 KiB.
    # Same raw DEFLATE format and the same tinf decoder on the MCU.
    result=zopfli.zlib.compress(bytes(data),numiterations=15)[2:-4]
    assert zlib.decompress(result,wbits=-15)==data
    return result

def main():
    original=json.loads(gzip.decompress((ROOT/'Assets/asset_source.json.gz').read_bytes()))
    for profile in ('full','lite'):
        d=json.loads(json.dumps(original))
        if profile=='lite':
            words=d['primitive_words'];offsets=d['primitive_offsets']
            primitives=[words[a:b] for a,b in zip(offsets,offsets[1:])]
            # Remove only baked demonstration waveforms, not live graph rendering.
            def keep(p):
                if p[0] in (4,5,6) and abs(p[6]-p[4])<=2 and abs(p[7]-p[5])<=2:return False
                if p[0] in (4,5,6) and 30<=p[4]<=285 and 30<=p[6]<=285 and 100<=p[5]<=286 and 100<=p[7]<=286 and p[8] not in (12841,12809):return False
                # One-pixel shaded icon fragments are decoration in the lite UI.
                if p[0]==2 and p[6]==1 and p[7]==1:return False
                return True
            used={i for i in d['block_commands'] if keep(primitives[i])}
            mapping={old:new for new,old in enumerate(sorted(used))}
            flat=[];starts=[0]
            for old in sorted(used):flat.extend(primitives[old]);starts.append(len(flat))
            commands=[];blocks=[0]
            for a,b in zip(d['block_offsets'],d['block_offsets'][1:]):
                commands.extend(mapping[i] for i in d['block_commands'][a:b] if i in used);blocks.append(len(commands))
            d.update(primitive_words=flat,primitive_offsets=starts,block_commands=commands,block_offsets=blocks)
            fonts=[d['UI_FONTS'][i:i+5] for i in range(0,len(d['UI_FONTS']),5)]
            font=next(f for f in fonts if f[0]==10 and f[4]==0)
            d['UI_GLYPHS']=d['UI_GLYPHS'][font[2]*7:(font[2]+font[3])*7]
            font[2]=0;d['UI_FONTS']=font
            # Two-bit coverage retains antialiasing with a smaller alphabet.
            def alpha(n):return min(15,((n+2)//5)*5)
            d['UI_FONT_BITS']=[alpha(b&15)|(alpha(b>>4)<<4) for b in d['UI_FONT_BITS']]
        with tempfile.TemporaryDirectory() as tmp:
            tmp=Path(tmp)
            for folder in ('Assets','Src','Include'):(tmp/folder).mkdir()
            (tmp/'Assets/asset_source.json.gz').write_bytes(gzip.compress(json.dumps(d).encode()))
            pack.ROOT=tmp;sys.argv=['pack_assets.py'];pack.main()
            scene=(tmp/'Src/hmi_scene_generated.c').read_text()
            font=(tmp/'Src/font_data.inc').read_text()
        blob=bytearray(16);layout=[];sizes={}
        def add_array(match):
            declaration,kind,name,body=match.groups()
            nums=[int(n,16) if 'x' in n else int(n) for n in re.findall(r'0x[0-9a-fA-F]+|-?\d+',body)]
            if name in ('UI_FONTS','UI_CODEPOINTS'):return match[0]
            if kind not in ('uint8_t','uint16_t','ui_glyph_t'):return match[0]
            while len(blob)%2:blob.append(0)
            offset=len(blob);size=2 if kind=='uint16_t' else 1
            for n in nums:blob.extend((n&((1<<(8*size))-1)).to_bytes(size,'little'))
            sizes[name]=len(nums)*size
            layout.append(f'#define {name} ((const {kind} *)(uintptr_t)(HMI_ASSET_BASE+{offset}u))')
            return '#if !HMI_STORAGE_VIRTUAL\n'+match[0]+'\n#endif'
        pattern=r'((?:static )?const) (uint8_t|uint16_t|ui_glyph_t) (\w+)\[\] = \{(.*?)\};'
        scene=re.sub(pattern,add_array,scene,flags=re.S)
        font=re.sub(pattern,add_array,font,flags=re.S)
        count=len(d['UI_GLYPHS'])//7
        font += f'\n#define UI_GLYPH_TABLE_BYTES {count*8}u\n#define UI_FONT_BITMAP_BYTES {sizes["UI_FONT_BITS"]}u\n'
        scene=re.sub(r'const uint32_t hmi_generated_data_bytes = .*?;',
                     'const uint32_t hmi_generated_data_bytes = '+str(sum(v for k,v in sizes.items() if k.startswith('hmi_generated_')))+'u+sizeof(hmi_generated_scenes);',scene)
        # Macros need glyph typedef first. Scene macros can be included immediately.
        font=font.replace('static const ui_font_t UI_FONTS[]',f'#include "hmi_assets_layout_{profile}.h"\nstatic const ui_font_t UI_FONTS[]',1)
        scene=scene.replace('#include "hmi_scene_generated.h"',f'#include "hmi_scene_generated.h"\n#include "hmi_assets_layout_{profile}.h"',1)
        crc=zlib.crc32(blob[16:]);blob[:16]=struct.pack('<4sIII',b'PCH1',1,len(blob),crc)
        header='#pragma once\n#include "hmi_storage.h"\n'+f'#define HMI_ASSET_LENGTH {len(blob)}u\n#define HMI_ASSET_CRC 0x{crc:08x}u\n#if HMI_STORAGE_VIRTUAL\n'+'\n'.join(layout)+'\n#endif\n'
        (ROOT/f'Include/hmi_assets_layout_{profile}.h').write_text(header)
        (ROOT/f'Src/hmi_scene_{profile}.inc').write_text(scene)
        (ROOT/f'Src/font_{profile}.inc').write_text(font)
        if profile=='full':
            (ROOT/'Assets/hmi_assets.bin').write_bytes(blob)
        else:
            offsets=[];compressed=bytearray()
            for at in range(0,len(blob),4096):offsets.append(len(compressed));compressed.extend(lz(blob[at:at+4096]))
            offsets.append(len(compressed))
            (ROOT/'Src/hmi_assets_lite.inc').write_text(pack.emit('uint32_t','lite_offsets',offsets,True)+pack.emit('uint8_t','lite_bytes',compressed,True))
            (ROOT/'Assets/hmi_assets_lite.bin').write_bytes(blob)
            print(f'LITE internal compressed resources: {len(compressed)+len(offsets)*4} bytes (raw {len(blob)})')
    (ROOT/'Src/hmi_scene_generated.c').write_text('#include "hmi_config.h"\n#if HMI_LITE\n#include "hmi_scene_lite.inc"\n#else\n#include "hmi_scene_full.inc"\n#endif\n')

    subprocess.run([sys.executable,str(ROOT/'Scripts/generate_rasters.py')],check=True)

if __name__=='__main__':main()
