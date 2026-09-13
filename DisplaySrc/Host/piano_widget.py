"""Two chromatic octaves, with independent computer-key and mouse ownership."""
from PySide6.QtCore import Qt, QRectF, Signal
from PySide6.QtGui import QColor, QPainter
from PySide6.QtWidgets import QWidget


KEYS = 'ZSXDCVGBHNJM' + 'Q2W3ER5T6Y7U'
RUSSIAN = dict(zip('ЯЫЧВСМПИТЬОЛЙЦУКЕНГ', 'ZSXDCVGBHNJMQWERTYU'))
WHITE = (0, 2, 4, 5, 7, 9, 11)
NAMES = ('C', 'C♯', 'D', 'D♯', 'E', 'F', 'F♯', 'G', 'G♯', 'A', 'A♯', 'B')


class PianoWidget(QWidget):
    noteChanged = Signal(int, bool)

    def __init__(self, parent=None):
        super().__init__(parent)
        self.base = 48
        self.held = {}
        self.setFocusPolicy(Qt.StrongFocus)
        self.setMinimumSize(700, 180)
        self.setToolTip('Нажми здесь и играй. Z–M: нижняя октава, Q–U: верхняя. Esc: отпустить ноты.')

    def set_octave(self, octave):
        self.release_all()
        self.base = (octave + 1) * 12
        self.update()

    def hold(self, source, note):
        if source in self.held:
            return
        first = note not in self.held.values()
        self.held[source] = note
        if first:
            self.noteChanged.emit(note, True)
        self.update()

    def release(self, source):
        note = self.held.pop(source, None)
        if note is not None and note not in self.held.values():
            self.noteChanged.emit(note, False)
        self.update()

    def release_all(self):
        notes = set(self.held.values())
        self.held.clear()
        for note in notes:
            self.noteChanged.emit(note, False)
        self.update()

    def key_name(self, event):
        name = event.text().upper()
        name = RUSSIAN.get(name, name)
        if name in KEYS and len(name) == 1:
            return name
        # KeyRelease may carry no text; Qt's Latin key codes are layout independent.
        if 0 <= event.key() < 128:
            name = chr(event.key()).upper()
            if name in KEYS:
                return name
        return None

    def keyPressEvent(self, event):
        if event.key() == Qt.Key_Escape:
            self.release_all(); event.accept(); return
        name = self.key_name(event)
        if name and not event.modifiers() & (Qt.ControlModifier | Qt.AltModifier | Qt.MetaModifier):
            if not event.isAutoRepeat():
                self.hold(name, self.base + KEYS.index(name))
            event.accept()
        else:
            super().keyPressEvent(event)

    def keyReleaseEvent(self, event):
        name = self.key_name(event)
        if name:
            if not event.isAutoRepeat():
                self.release(name)
            event.accept()
        else:
            super().keyReleaseEvent(event)

    def focusOutEvent(self, event):
        self.release_all()
        super().focusOutEvent(event)

    def rectangles(self):
        width = self.width() / 14
        height = self.height() - 4
        whites, blacks = [], []
        count = 0
        for offset in range(24):
            if offset % 12 in WHITE:
                whites.append((offset, QRectF(count * width, 2, width, height)))
                count += 1
            else:
                blacks.append((offset, QRectF((count - .31) * width, 2, width * .62, height * .62)))
        return whites, blacks

    def paintEvent(self, event):
        painter = QPainter(self)
        whites, blacks = self.rectangles()
        for black, items in ((False, whites), (True, blacks)):
            for offset, rect in items:
                down = self.base + offset in self.held.values()
                painter.setPen(QColor('#64748b'))
                painter.setBrush(QColor('#38bdf8' if down else '#202938' if black else '#f8fafc'))
                painter.drawRoundedRect(rect.adjusted(1, 0, -1, -1), 3, 3)
                painter.setPen(QColor('white' if black and not down else '#142033'))
                label = KEYS[offset]
                if not black:
                    note = self.base + offset
                    label += '\n' + NAMES[note % 12] + str(note // 12 - 1)
                painter.drawText(rect.adjusted(0, 0, 0, -9), Qt.AlignHCenter | Qt.AlignBottom, label)
        if self.hasFocus():
            painter.setPen(QColor('#0284c7'))
            painter.setBrush(Qt.NoBrush)
            painter.drawRect(self.rect().adjusted(1, 1, -2, -2))

    def mouse_note(self, position):
        whites, blacks = self.rectangles()
        for offset, rect in blacks + whites:
            if rect.contains(position):
                return self.base + offset
        return None

    def mousePressEvent(self, event):
        if event.button() == Qt.LeftButton:
            self.setFocus()
            note = self.mouse_note(event.position())
            if note is not None:
                self.hold('mouse', note)

    def mouseMoveEvent(self, event):
        if event.buttons() & Qt.LeftButton:
            note = self.mouse_note(event.position())
            if note != self.held.get('mouse'):
                self.release('mouse')
                if note is not None:
                    self.hold('mouse', note)

    def mouseReleaseEvent(self, event):
        if event.button() == Qt.LeftButton:
            self.release('mouse')
