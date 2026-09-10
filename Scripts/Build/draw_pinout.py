"""Draw the 26-pin LCD connector in the orientation of the supplied rear photo.

Module signals: https://www.lcdwiki.com/3.5inch_RPi_Display
Blue Pill destinations are read from the actual CubeMX configuration.
Requires Pillow; writes a PNG and an editable SVG with identical geometry.
"""
from pathlib import Path
from html import escape
import os
from PIL import Image, ImageDraw, ImageFont

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / 'docs'
W, H = 1600, 1560
image = Image.new('RGB', (W, H), '#f5f7fb')
draw = ImageDraw.Draw(image)
svg = [f'<svg xmlns="http://www.w3.org/2000/svg" width="{W}" height="{H}" viewBox="0 0 {W} {H}">']
fonts = Path(os.environ.get('PINOUT_FONT_DIR', 'C:/Windows/Fonts'))
palette = {'power': '#b42335', 'ground': '#344054', 'nc': '#8a94a6',
           'gpio': '#087f5b', 'spi': '#1769c2', 'irq': '#7c3aed', 'rail': '#ad6700'}

def rect(x, y, w, h, fill, radius=0, stroke=None, width=1):
    draw.rounded_rectangle((x,y,x+w,y+h), radius=radius, fill=fill, outline=stroke, width=width)
    svg.append(f'<rect x="{x}" y="{y}" width="{w}" height="{h}" rx="{radius}" fill="{fill}" stroke="{stroke or "none"}" stroke-width="{width}"/>')

def line(x1,y1,x2,y2,color,width=2):
    draw.line((x1,y1,x2,y2),fill=color,width=width)
    svg.append(f'<path d="M{x1} {y1} L{x2} {y2}" stroke="{color}" stroke-width="{width}"/>')

def text(x,y,s,size=24,color='#172b4d',bold=False,anchor='start'):
    font = ImageFont.truetype(str(fonts / ('segoeuib.ttf' if bold else 'segoeui.ttf')),size)
    length=draw.textlength(s,font=font)
    xx=x-length/2 if anchor=='middle' else x-length if anchor=='end' else x
    draw.text((xx,y),s,font=font,fill=color,anchor='ls')
    svg.append(f'<text x="{x}" y="{y}" font-family="Segoe UI,Arial,sans-serif" font-size="{size}" font-weight="{700 if bold else 400}" text-anchor="{anchor}" fill="{color}">{escape(s)}</text>')

ioc = dict(row.split('=',1) for row in (ROOT/'Firmware/BluePillHMI/BluePillHMI.ioc').read_text().splitlines() if '=' in row)
def pin(label):
    matches=[key.split('.')[0] for key,value in ioc.items() if value==label and key.endswith(('.GPIO_Label','.Signal'))]
    assert len(matches)==1,(label,matches)
    return matches[0]

# Physical pin numbers, not Raspberry Pi BCM numbers or STM32 pin names.
data = {}
for n in [3,5,7,8,10,12,13,15,16]:data[n]=('NC','—','nc')
for n in [6,9,14,20,25]:data[n]=('GND','GND','ground')
for n in [2,4]:data[n]=('+5 V','5V','power')
for n in [1,17]:data[n]=('3V3 *','—','rail')
for n,label,target,kind in [(11,'TP_IRQ / PENIRQ','TOUCH_IRQ','irq'),(18,'DC / LCD_RS','LCD_DC','gpio'),
    (19,'MOSI','SPI1_MOSI','spi'),(21,'MISO','SPI1_MISO','spi'),(22,'RST','LCD_RST','gpio'),
    (23,'SCK','SPI1_SCK','spi'),(24,'CS — дисплей','LCD_CS','gpio'),(26,'T_CS — сенсор','TOUCH_CS','gpio')]:
    data[n]=(label,pin(target),kind)
assert set(data)==set(range(1,27))

rect(0,0,W,H,'#f5f7fb')
text(70,80,'3.5 inch RPi LCD · разъём 2 × 13',44,bold=True)
text(70,125,'Полная нумерация контактов + твои подключения к Blue Pill',27)
text(70,172,'Вид на гнёзда с обратной стороны дисплея — как на присланном фото.',25)
text(70,210,'Ориентир: +5 V вверху слева, T_CS внизу слева. Плата продолжается вправо →',23)
text(70,259,'СИГНАЛ ДИСПЛЕЯ',18,color='#667085',bold=True)
text(515,259,'BLUE PILL',18,color='#667085',bold=True)
text(976,259,'СИГНАЛ ДИСПЛЕЯ',18,color='#667085',bold=True)
text(1375,259,'BLUE PILL',18,color='#667085',bold=True)
rect(695,276,210,933,'#172b4d',20)
text(750,304,'чётные',16,color='#cbd5e1',anchor='middle')
text(850,304,'нечётные',16,color='#cbd5e1',anchor='middle')

for row in range(13):
    y=340+row*68
    for n,left in [(2*(row+1),True),(2*row+1,False)]:
        label,dest,kind=data[n];color=palette[kind]
        x=750 if left else 850
        rect(x-25,y-25,50,50,'#ffffff',7,color,3)
        text(x,y+9,str(n),25,color,bold=True,anchor='middle')
        if left:
            rect(62,y-28,618,56,'#ffffff',10)
            line(655,y,x-27,y,color,3)
            text(80,y+8,label,25,color,bold=True)
            text(525,y+8,dest,23,color,bold=True)
        else:
            rect(924,y-28,614,56,'#ffffff',10)
            line(x+27,y,949,y,color,3)
            text(975,y+8,label,25,color,bold=True)
            text(1375,y+8,dest,23,color,bold=True)

legend=[('spi','SPI1'),('gpio','Обычный GPIO'),('irq','Вход касания'),('power','Питание'),('nc','NC: не используется')]
x=70
for kind,label in legend:
    rect(x,1245,16,16,palette[kind],4)
    text(x+25,1261,label,23,palette[kind]);x+={'spi':160,'gpio':280,'irq':270,'power':205,'nc':300}[kind]
text(70,1320,f"TP_IRQ: при касании — низкий уровень; в проекте подключён к {pin('TOUCH_IRQ')}.",24,bold=True)
text(70,1365,'* 3V3 (1, 17) указаны по типовой документации; здесь к Blue Pill не подключены.',23)
text(70,1404,'Питание по твоему фото: +5 V и GND. GPIO STM32 работают с уровнями 3,3 V.',23)
text(70,1452,'Типовая распиновка LCD Wiki. Для твоей платы не подтверждена: прозвонка GND не совпала.',21,color='#667085')
text(70,1490,'Физические номера разъёма не равны номерам GPIO Raspberry Pi или STM32.',21,color='#667085')
svg.append('</svg>')
OUT.mkdir(exist_ok=True)
(OUT/'lcd-connector-pinout.svg').write_text('\n'.join(svg),encoding='utf-8')
image.save(OUT/'lcd-connector-pinout.png')
print(OUT/'lcd-connector-pinout.png')
