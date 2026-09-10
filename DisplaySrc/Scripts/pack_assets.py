"""Losslessly pack the editor's legacy C tables. No third-party dependencies.

Import a fresh legacy export with --import-dir PATH; otherwise regenerate from
Assets/asset_source.json.gz (build-time input, never linked into firmware).
"""
import argparse
import collections
import gzip
import json
from pathlib import Path
import re
import itertools

ROOT = Path(__file__).resolve().parents[1]


def array(text, name):
    body = re.search(r'\b' + name + r'\[[^=]*=\s*\{(.*?)\};', text, re.S)[1]
    return [int(n, 16) if 'x' in n.lower() else int(n)
            for n in re.findall(r'0[xX][0-9a-fA-F]+|-?\d+', body)]


def emit(kind, name, values, static=False):
    return ('static ' if static else '') + 'const ' + kind + ' ' + name + '[] = {\n' + ''.join(
        ' ' + ','.join(str(v) for v in values[i:i+24]) + ',\n'
        for i in range(0, len(values), 24)) + '};\n'


def varint(n):
    out = []
    while n >= 128:
        out.append((n & 127) | 128)
        n >>= 7
    return out + [n]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--import-dir', type=Path)
    args = parser.parse_args()
    source = ROOT / 'Assets/asset_source.json.gz'
    if args.import_dir:
        scene = (args.import_dir / 'Src/hmi_scene_generated.c').read_text(encoding='utf-8')
        font = (args.import_dir / 'Src/font_data.inc').read_text(encoding='utf-8')
        names = ('primitive_words', 'primitive_offsets', 'clip_rects', 'block_commands',
                 'block_offsets', 'scene_blocks', 'strings')
        data = {n: array(scene, 'hmi_generated_' + n) for n in names}
        data['scenes_c'] = re.search(r'const HmiGeneratedScene.*?\n};', scene, re.S)[0]
        data['scene_header'] = (args.import_dir / 'Include/hmi_scene_generated.h').read_text(encoding='utf-8')
        for n in ('UI_FONTS', 'UI_GLYPHS', 'UI_FONT_BITS'):
            data[n] = array(font, n)
        source.parent.mkdir(exist_ok=True)
        source.write_bytes(gzip.compress(json.dumps(data, separators=(',', ':')).encode(), mtime=0))
    data = json.loads(gzip.decompress(source.read_bytes()))
    header = data['scene_header'].replace(
        'extern const uint16_t hmi_generated_primitive_words[];',
        'extern const uint8_t hmi_generated_primitive_data[];\nextern const uint16_t hmi_generated_templates[];\nextern const uint16_t hmi_generated_values[];')
    header = header.replace('extern const uint32_t hmi_generated_primitive_offsets[];', 'extern const uint8_t hmi_generated_primitive_offsets[];')
    header = header.replace('hmi_generated_clip_rects[][4]', 'hmi_generated_clip_rects[]')
    header = header.replace('extern const uint16_t hmi_generated_block_offsets[];',
                            'extern const uint16_t hmi_generated_block_offsets[];\nextern const uint16_t hmi_generated_block_bounds[];')
    (ROOT / 'Include/hmi_scene_generated.h').write_text(header, encoding='utf-8')

    words = data['primitive_words']
    offsets = data['primitive_offsets']
    primitives = [words[a:b] for a, b in zip(offsets, offsets[1:])]
    assert all(9 <= len(p) <= 11 and 2 <= p[0] <= 13 for p in primitives)
    bounds = []
    for start,end in zip(data['block_offsets'],data['block_offsets'][1:]):
        rows = [primitives[i] for i in data['block_commands'][start:end]]
        bounds.extend([min((p[2] for p in rows),default=0),
                       min(65535,max((p[2]+p[3] for p in rows),default=0))])
    # Lines dominate the export (sampled graphs). Store endpoint deltas and
    # bounding-box margins instead of four almost identical absolute values.
    for p in primitives:
        if p[0] in (4,5,6):
            p[2] = (p[2] - min(p[5],p[7])) & 65535
            p[3] = (p[3] - abs(p[7]-p[5])) & 65535
            p[6] = (p[6]-p[4]) & 65535
            p[7] = (p[7]-p[5]) & 65535
    groups = collections.defaultdict(list)
    color_index = {2:8, 3:8, 4:8, 5:8, 6:9, 7:8, 8:9, 9:9, 10:7, 11:8, 12:10, 13:7}
    for p in primitives:
        groups[(p[0], p[1], p[color_index[p[0]]])].append(p)
    templates, group_ids = [], {}
    for key, rows in groups.items():
        group_ids[key] = len(templates)
        templates.append([collections.Counter(col).most_common(1)[0][0] for col in zip(*rows)])
    # One shared value dictionary covers coordinates, sizes, colors and strings.
    counts = collections.Counter(v for p in primitives for v in p)
    dictionary = [v for v, _ in counts.most_common(255)]
    codes = {v:i for i,v in enumerate(dictionary)}
    stream, packed_offsets = [], []
    for p in primitives:
        tid = group_ids[(p[0], p[1], p[color_index[p[0]]])]
        base = templates[tid]
        mask = sum(1 << i for i, (a,b) in enumerate(zip(p, base)) if a != b)
        packed_offsets.extend(len(stream).to_bytes(3, 'little'))
        stream.extend(varint(tid) + varint(mask))
        for i, v in enumerate(p):
            if mask & (1 << i):
                stream.extend([codes[v]] if v in codes else [255, v & 255, v >> 8])
    assert len(stream) < 1 << 24
    flat_templates = [v for row in templates for v in row + [0] * (11-len(row))]
    result = '#include "hmi_scene_generated.h"\n/* Generated by Scripts/pack_assets.py; lossless shared primitive templates. */\n'
    arrays = [('uint8_t', 'primitive_data', stream), ('uint8_t', 'primitive_offsets', packed_offsets),
              ('uint16_t', 'templates', flat_templates), ('uint16_t', 'values', dictionary)]
    arrays += [('uint16_t', n, data[n]) for n in ('clip_rects','block_commands','block_offsets','scene_blocks')]
    arrays.append(('uint16_t', 'block_bounds', bounds))
    arrays.append(('uint8_t', 'strings', data['strings']))
    for kind, name, values in arrays:
        result += emit(kind, 'hmi_generated_' + name, values)
    result += data['scenes_c'] + '\n'
    result += 'const uint32_t hmi_generated_data_bytes = ' + '+'.join(
        'sizeof(hmi_generated_' + name + ')' for _,name,_ in arrays) + '+sizeof(hmi_generated_scenes);\n'
    (ROOT / 'Src/hmi_scene_generated.c').write_text(result, encoding='utf-8')

    glyphs = [data['UI_GLYPHS'][i:i+7] for i in range(0,len(data['UI_GLYPHS']),7)]
    fonts = [data['UI_FONTS'][i:i+5] for i in range(0,len(data['UI_FONTS']),5)]
    codepoints = [g[0] for g in glyphs[:fonts[0][3]]]
    assert all([g[0] for g in glyphs[f[2]:f[2]+f[3]]] == codepoints for f in fonts)
    bits, bitmap_ids, new_glyphs = [], {}, []
    for cp, off, w, h, xo, yo, advance in glyphs:
        raw = data['UI_FONT_BITS'][off:off+(w*h+1)//2]
        alpha = [(raw[i//2] >> (4*(i&1))) & 15 for i in range(w*h)]
        rle = []
        for value, run in itertools.groupby(alpha):
            count = len(list(run))
            while count:
                n = min(count,16)
                rle.append(((n-1)<<4) | value)
                count -= n
        compressed = len(rle) < len(raw)
        payload = tuple(rle if compressed else raw)
        key = (compressed, payload)
        if key not in bitmap_ids:
            bitmap_ids[key] = len(bits)
            bits.extend(payload)
        assert len(bits) < 0x800000
        offset = bitmap_ids[key] | (0x800000 if compressed else 0)
        new_glyphs.append((offset & 255,(offset>>8)&255,offset>>16,w,h,xo,yo,advance))
    result = '/* Generated by Scripts/pack_assets.py. All glyphs and 4-bit coverage preserved. */\n'
    result += 'typedef struct { uint8_t off0,off1,off2,w,h; int8_t xoff,yoff; uint8_t advance; } ui_glyph_t;\n'
    result += 'typedef struct { uint8_t px,ascent; uint16_t first,count; uint8_t bold; } ui_font_t;\n'
    result += 'static const ui_font_t UI_FONTS[] = {\n' + ''.join(
        ' {'+','.join(map(str,data['UI_FONTS'][i:i+5]))+'},\n' for i in range(0,len(data['UI_FONTS']),5)) + '};\n'
    result += 'static const ui_glyph_t UI_GLYPHS[] = {\n' + ''.join(
        ' {'+','.join(str(v)+'u' if j==0 else str(v) for j,v in enumerate(row))+'},\n' for row in new_glyphs) + '};\n'
    result += emit('uint8_t', 'UI_FONT_BITS', bits, True)
    result += emit('uint16_t', 'UI_CODEPOINTS', codepoints, True)
    result += '#define UI_FONT_COUNT ((int)(sizeof(UI_FONTS)/sizeof(UI_FONTS[0])))\n'
    (ROOT / 'Src/font_data.inc').write_text(result, encoding='utf-8')
    print(f'{len(primitives)} primitives, {len(templates)} templates; scene tables {sum(len(v)*(2 if k=="uint16_t" else 1) for k,_,v in arrays)} bytes')
    print(f'{len(glyphs)} glyphs retained; bitmap {len(data["UI_FONT_BITS"])} -> {len(bits)} bytes; glyph records {len(glyphs)*8} bytes')


if __name__ == '__main__':
    main()
