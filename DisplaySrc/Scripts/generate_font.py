"""Regenerate the checked-in 9x16 Cyrillic font; Python 3.7 + PySide2."""
import os
from pathlib import Path
os.environ.setdefault('QT_QPA_PLATFORM', 'offscreen')
from PySide2.QtWidgets import QApplication
from PySide2.QtGui import QImage, QPainter, QFont, QColor, QFontDatabase
app = QApplication([])
QFontDatabase.addApplicationFont('C:/Windows/Fonts/consola.ttf')
font = QFont('Consolas'); font.setPixelSize(14)
points = list(range(32,127)) + list(range(0x410,0x450)) + [0x401,0x451]
rows = []
for code in points:
    im = QImage(9,16,QImage.Format_RGB32); im.fill(QColor('black'))
    painter = QPainter(im); painter.setFont(font); painter.setPen(QColor('white'))
    painter.drawText(0,12,chr(code)); painter.end()
    rows.append([sum((1<<x) for x in range(9) if QColor(im.pixel(x,y)).red()>100) for y in range(16)])
target = Path(__file__).resolve().parents[1]/'src/font.h'
target.write_text('/* Generated 9x16 bitmap, ASCII + Cyrillic. */\nstatic const uint16_t font_rows[][16]={\n'+
    ',\n'.join('{'+','.join(str(v) for v in row)+'}' for row in rows)+'\n};\n',encoding='utf8')
print(target)
