"""PySide2 asset uploader for Python 3.7; launch with START.cmd."""
import struct
import sys
import time
import json
import argparse
from pathlib import Path

import serial
from serial.tools.list_ports import comports
from PySide2 import QtCore, QtWidgets


parser=argparse.ArgumentParser()
parser.add_argument('--display-dir',type=Path,default=Path(__file__).resolve().parents[2]/'DisplaySrc')
options,qt_args=parser.parse_known_args()
MODULE=options.display_dir.resolve()
MANIFEST=json.loads((MODULE/'module.json').read_text(encoding='utf-8'))
sys.argv=[sys.argv[0]]+qt_args

class Upload(QtCore.QThread):
    progress = QtCore.Signal(str, int, int, float)
    result = QtCore.Signal(bool, str)

    def __init__(self, port, manual, parent=None):
        super().__init__(parent)
        self.port, self.manual = port, manual

    def check_cancel(self):
        if self.isInterruptionRequested():
            raise RuntimeError('Загрузка остановлена. Перед использованием дисплея загрузите ассеты заново.')

    def receive(self, port, expected, timeout, stage):
        started = time.monotonic()
        while time.monotonic() - started < timeout:
            self.check_cancel()
            reply = port.read(1)
            if reply == expected:
                return
            if reply:
                raise RuntimeError(stage + ': неожиданный ответ платы ' + reply.hex())
            if expected == b'K':
                self.progress.emit(stage, 0, 0, time.monotonic() - started)
        raise RuntimeError(stage + ': плата не ответила за {} с'.format(timeout))

    def run(self):
        try:
            path = MODULE / MANIFEST['assets']
            data = path.read_bytes()
            if not data or len(data)>2*1024*1024:
                raise RuntimeError('Пустой образ ресурсов или размер больше внешней Flash.')
            with serial.Serial(self.port, 115200, timeout=0.2, write_timeout=3) as port:
                port.reset_input_buffer()
                started = time.monotonic()
                command = b'PCHW' if self.manual else b'PCHR'
                stage = 'Нажмите RESET на плате' if self.manual else 'Ожидание платы'
                while time.monotonic() - started < 60:
                    self.check_cancel()
                    self.progress.emit(stage, 0, 0, time.monotonic() - started)
                    port.write(command)
                    if port.read(1) == b'R':
                        break
                else:
                    raise RuntimeError('Нет ответа платы. Проверьте ExternalFlash, TX → PA10, RX → PA9 и режим Run в Keil.')
                port.write(struct.pack('<I', len(data)))
                sectors = (len(data) + 4095) // 4096
                started = time.monotonic()
                self.progress.emit('Стирание Flash', 0, sectors, 0)
                for i in range(sectors):
                    self.receive(port, b'e', 5, 'Стирание Flash')
                    self.progress.emit('Стирание Flash', i + 1, sectors, time.monotonic() - started)
                self.receive(port, b'D', 5, 'Подготовка Flash')
                started = last = time.monotonic()
                self.progress.emit('Запись ассетов', 0, len(data), 0)
                for offset in range(0, len(data), 256):
                    self.check_cancel()
                    chunk = data[offset:offset + 256]
                    if port.write(chunk) != len(chunk):
                        raise RuntimeError('Неполная запись в COM-порт.')
                    self.receive(port, b'p', 5, 'Запись по адресу {}'.format(offset))
                    now = time.monotonic()
                    done = offset + len(chunk)
                    if now - last >= 0.1 or done == len(data):
                        self.progress.emit('Запись ассетов', done, len(data), now - started)
                        last = now
                self.progress.emit('Проверка CRC', 0, 0, 0)
                self.receive(port, b'K', 30, 'Проверка CRC')
            self.result.emit(True, 'Готово. Ассеты записаны, CRC подтверждён платой.')
        except Exception as exc:
            self.result.emit(False, str(exc))


class Window(QtWidgets.QWidget):
    def __init__(self):
        super().__init__()
        self.worker = None
        self.closing = False
        self.setWindowTitle('Дисплей — загрузка ресурсов')
        self.resize(620, 340)
        layout = QtWidgets.QVBoxLayout(self)
        title = QtWidgets.QLabel('Загрузка ассетов в дисплей')
        title.setStyleSheet('font-size: 20px; font-weight: 600;')
        layout.addWidget(title)
        note = QtWidgets.QLabel('USB–UART · 115200 бод · ExternalFlash\nЗагрузка образа занимает несколько минут.')
        layout.addWidget(note)
        ports = QtWidgets.QHBoxLayout()
        self.port = QtWidgets.QComboBox()
        self.port.setEditable(True)
        ports.addWidget(self.port, 1)
        self.refresh = QtWidgets.QPushButton('Обновить порты')
        self.refresh.clicked.connect(self.scan)
        ports.addWidget(self.refresh)
        layout.addLayout(ports)
        self.manual = QtWidgets.QCheckBox('Ручной RESET (для старой прошивки)')
        layout.addWidget(self.manual)
        self.status = QtWidgets.QLabel('Выберите COM-порт и нажмите «Загрузить ассеты».')
        self.status.setWordWrap(True)
        layout.addWidget(self.status)
        self.bar = QtWidgets.QProgressBar()
        self.bar.setValue(0)
        layout.addWidget(self.bar)
        self.details = QtWidgets.QLabel(' ')
        layout.addWidget(self.details)
        buttons = QtWidgets.QHBoxLayout()
        self.start = QtWidgets.QPushButton('Загрузить ассеты')
        self.start.clicked.connect(self.upload)
        buttons.addWidget(self.start)
        self.cancel = QtWidgets.QPushButton('Остановить')
        self.cancel.setEnabled(False)
        self.cancel.clicked.connect(self.stop)
        buttons.addWidget(self.cancel)
        self.terminal = QtWidgets.QPushButton('Открыть терминал')
        self.terminal.clicked.connect(self.open_terminal)
        buttons.addWidget(self.terminal)
        layout.addLayout(buttons)
        self.process = QtCore.QProcess(self)
        self.process.finished.connect(self.terminal_finished)
        self.process.errorOccurred.connect(self.terminal_error)
        self.scan()

    def scan(self):
        previous = self.port.currentText().split(' — ', 1)[0].strip()
        self.port.clear()
        for port in comports():
            self.port.addItem('{} — {}'.format(port.device, port.description or 'Без описания'), port.device)
            self.port.setItemData(self.port.count() - 1, port.description, QtCore.Qt.ToolTipRole)
        if previous:
            index = self.port.findData(previous)
            if index >= 0:
                self.port.setCurrentIndex(index)
            else:
                self.port.setCurrentText(previous)

    def upload(self):
        port = self.port.currentText().split(' — ', 1)[0].strip()
        if not port:
            self.status.setText('Подключите USB–UART и выберите COM-порт.')
            return
        self.worker = Upload(port, self.manual.isChecked(), self)
        self.worker.progress.connect(self.update_progress)
        self.worker.result.connect(self.show_result)
        self.worker.finished.connect(self.finished)
        for widget in (self.port, self.refresh, self.manual, self.start, self.terminal):
            widget.setEnabled(False)
        self.cancel.setEnabled(True)
        self.worker.start()

    def update_progress(self, stage, done, total, elapsed):
        self.status.setText(stage)
        self.bar.setRange(0, 1000 if total else 0)
        if total:
            self.bar.setValue(1000 * done // total)
        text = 'Прошло: {:.0f} с'.format(elapsed)
        if total and done and elapsed:
            rate = done / elapsed
            text += '  ·  Осталось: ≈{:.0f} с'.format((total - done) / rate)
            if stage == 'Запись ассетов':
                text += '  ·  {:.1f} КиБ/с  ·  {:.0f}/{:.0f} КиБ'.format(rate / 1024, done / 1024, total / 1024)
            else:
                text += '  ·  {}/{} секторов'.format(done, total)
        self.details.setText(text)

    def show_result(self, success, message):
        self.bar.setRange(0, 1000)
        self.bar.setValue(1000 if success else 0)
        self.status.setText(message)
        if not success:
            self.details.setText('Устраните причину и повторите загрузку.')

    def finished(self):
        self.worker.deleteLater()
        self.worker = None
        for widget in (self.port, self.refresh, self.manual, self.start, self.terminal):
            widget.setEnabled(True)
        self.cancel.setEnabled(False)
        if self.closing:
            self.close()

    def stop(self):
        if self.worker:
            self.worker.requestInterruption()
            self.cancel.setEnabled(False)

    def open_terminal(self):
        self.terminal.setEnabled(False)
        launcher=MODULE/MANIFEST['simulator']
        self.process.start('cmd.exe', ['/d','/c',str(launcher)])

    def terminal_finished(self, code, _status):
        self.terminal.setEnabled(True)
        self.start.setEnabled(True)
        if code:
            self.status.setText('Терминал завершился с ошибкой: ' + bytes(self.process.readAllStandardError()).decode('utf-8', 'replace')[-600:])

    def terminal_error(self, _error):
        self.status.setText('Ошибка запуска терминала: ' + self.process.errorString())
        self.terminal.setEnabled(True)
        self.start.setEnabled(True)

    def closeEvent(self, event):
        if self.worker:
            self.closing = True
            self.stop()
            event.ignore()
        elif self.process.state() != QtCore.QProcess.NotRunning:
            self.status.setText('Сначала закройте окно терминала.')
            event.ignore()
        else:
            event.accept()


if __name__ == '__main__':
    app = QtWidgets.QApplication(sys.argv)
    app.setStyle('Fusion')
    window = Window()
    window.show()
    sys.exit(app.exec_())
