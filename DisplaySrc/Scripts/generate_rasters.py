"""Bake lossless ready-to-blit UI backgrounds into the existing PCH1 image.

Each 320x8 strip is deduplicated and encoded as RGB565 PackBits: low 7 bits
are count-1, high bit is a repeated color, otherwise literal colors follow.
The original vectors/font region remains for fonts and host regression tests.
"""
from pathlib import Path
import os, re, struct, subprocess, zlib, json

ROOT=Path(__file__).resolve().parents[1]

def encode(words):
    out=bytearray();i=0
    while i<len(words):
        run=1
        while i+run<len(words) and run<128 and words[i+run]==words[i]:run+=1
        if run>=3:
            out+=struct.pack('<BH',0x80|run-1,words[i]);i+=run
        else:
            start=i;i+=run
            while i<len(words) and i-start<128:
                if i+2<len(words) and words[i]==words[i+1]==words[i+2]:break
                i+=1
            n=i-start;out.append(n-1);out+=struct.pack('<'+'H'*n,*words[start:i])
    return out

def main():
    os.chdir(ROOT);(ROOT/'.build').mkdir(exist_ok=True)
    cc=os.environ.get('CC','C:/mingw64/bin/gcc.exe')
    os.environ['PATH']=str(Path(cc).parent)+os.pathsep+os.environ['PATH']
    sources=[str(p) for p in Path('Src').glob('*.c') if p.name!='telemetry.c']+['ThirdParty/tinf/tinflate.c']
    subprocess.run([cc,'-std=c99','-Os','-Wall','-Wextra','-Werror','-IInclude','-IThirdParty/tinf',
        *sources,'Scripts/raster_export.c','-o','.build/raster_export.exe'],check=True)
    subprocess.run(['.build/raster_export.exe','.build/backgrounds.rgb565'],check=True)
    raw=Path('.build/backgrounds.rgb565').read_bytes();assert len(raw)==54*320*480*2
    layout=Path('Include/hmi_assets_layout_full.h');text=layout.read_text()
    raster_header=Path('Include/hmi_raster_layout.h')
    image=bytearray(Path('Assets/hmi_assets.bin').read_bytes())
    # Repeatable even when the input already contains a previous raster tail.
    base=int(re.search(r'#define UI_FONT_BITS .*?HMI_ASSET_BASE\+(\d+)u',text)[1])+int(re.search(r'UI_FONT_BITMAP_BYTES (\d+)u',Path('Src/font_full.inc').read_text())[1])
    assert base<=len(image)
    image=image[:base];table=len(image);image+=bytes(54*60*4)
    unique={};total=0
    for index,at in enumerate(range(0,len(raw),5120)):
        pixels=raw[at:at+5120]
        if pixels not in unique:
            unique[pixels]=len(image);image+=encode(struct.unpack('<2560H',pixels))
        struct.pack_into('<I',image,table+index*4,unique[pixels]);total+=1
    help_source=Path('Src/hmi_help_text.h').read_text(encoding='utf-8')
    groups=re.findall(r'help_lines_\d+\[\]=\{(.*?)\};',help_source,re.S)
    offsets=[];lengths=[];counts=[]
    for group in groups:
        lines=[json.loads(x) for x in re.findall(r'"(?:[^"\\]|\\.)*"',group)]
        counts.append(len(lines));row=[];sizes=[]
        for line in lines:
            raw_line=line.encode('utf-8')+b'\0';assert len(raw_line)<=128
            row.append(len(image));sizes.append(len(raw_line));image+=raw_line
        offsets.append(row);lengths.append(sizes)
    def array(rows):return ',\n'.join('{'+','.join(str(x) for x in row)+'}' for row in rows)
    Path('Src/hmi_help_flash.h').write_text('#pragma once\nstatic const unsigned help_counts[]={'+','.join(map(str,counts))+'};\nstatic const uint32_t help_offsets[4][40]={'+array(offsets)+'};\nstatic const uint8_t help_lengths[4][40]={'+array(lengths)+'};\n')
    value_source=Path('Src/hmi_ui.c').read_text(encoding='utf-8')
    pairs=re.findall(r'\{NAME\(("(?:[^"\\]|\\.)*")\),UNIT\(("(?:[^"\\]|\\.)*")\)',value_source)
    offsets=[];lengths=[]
    for pair in pairs:
        row=[];sizes=[]
        for literal in pair:
            raw_text=json.loads(literal).encode('utf-8')+b'\0';assert len(raw_text)<=96
            row.append(len(image));sizes.append(len(raw_text));image+=raw_text
        offsets.append(row);lengths.append(sizes)
    Path('Src/hmi_value_flash.h').write_text('#pragma once\nstatic const uint32_t value_offsets[][2]={'+array(offsets)+'};\nstatic const uint8_t value_lengths[][2]={'+array(lengths)+'};\n')
    assert len(image)<=0x128000, f'Assets overlap persistent journal: {len(image)} bytes'
    crc=zlib.crc32(image[16:]);struct.pack_into('<4sIII',image,0,b'PCH1',1,len(image),crc)
    text=re.sub(r'HMI_ASSET_LENGTH \d+u',f'HMI_ASSET_LENGTH {len(image)}u',text)
    text=re.sub(r'HMI_ASSET_CRC 0x[0-9a-f]+u',f'HMI_ASSET_CRC 0x{crc:08x}u',text)
    layout.write_text(text);Path('Assets/hmi_assets.bin').write_bytes(image)
    raster_header.write_text(f'#pragma once\n#define HMI_RASTER_BASE_LENGTH {base}u\n#define HMI_RASTER_TABLE_OFFSET {table}u\n#define HMI_RASTER_SCENES 54u\n')
    print(f'Raster backgrounds: {len(unique)} unique / {total} strips; image {len(image)} / 2097152 bytes; CRC {crc:08x}')

if __name__=='__main__':main()
