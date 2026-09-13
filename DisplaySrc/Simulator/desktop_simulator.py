"""Native Windows control desk with the reference HMI embedded beside it."""
import json
import sys
import threading
from pathlib import Path
from PySide6 import QtCore, QtWidgets, QtGui
from PySide6.QtWebEngineWidgets import QWebEngineView
if __package__:
    from .native_preview import NativePreview
    from .uart_server import Link, ThreadingHTTPServer, handler
else:
    from native_preview import NativePreview
    from uart_server import Link, ThreadingHTTPServer, handler


class Desk(QtWidgets.QMainWindow):
    def __init__(self, preview_factory=None, embedded=False):
        super().__init__()
        self.link = Link()
        self.server = ThreadingHTTPServer(('127.0.0.1', 0), handler(self.link))
        threading.Thread(target=self.server.serve_forever, daemon=True).start()
        threading.Thread(target=self.link.run, daemon=True).start()
        self.setWindowTitle('ПЧ-1000 — стенд управления и UART')
        self.resize(1180, 850)
        shell = QtWidgets.QWidget()
        self.setCentralWidget(shell)
        layout = QtWidgets.QHBoxLayout(shell)
        panel = QtWidgets.QWidget()
        panel.setMinimumWidth(490)
        left = QtWidgets.QVBoxLayout(panel)
        layout.addWidget(panel, 1)
        self.view = QWebEngineView()
        self.view.setMinimumWidth(430)
        self.view.setFixedSize(1,1)
        left.addWidget(self.view)
        self.preview=preview_factory(self) if preview_factory else NativePreview(self)
        if not embedded:layout.addWidget(self.preview,1)
        self.embedded=embedded
        self.ready = False
        self.status = QtWidgets.QLabel('Инициализация модели…')
        left.addWidget(self.status)
        linkbox = QtWidgets.QGroupBox('Подключение к дисплею · 115200 / 8N1')
        row = QtWidgets.QHBoxLayout(linkbox)
        self.port = QtWidgets.QComboBox()
        row.addWidget(self.port, 1)
        refresh = QtWidgets.QPushButton('Обновить')
        refresh.clicked.connect(self.ports)
        row.addWidget(refresh)
        self.connect = QtWidgets.QPushButton('Подключить')
        self.connect.clicked.connect(self.connect_port)
        row.addWidget(self.connect)
        left.addWidget(linkbox)
        linkbox.setVisible(not embedded)
        power = QtWidgets.QGroupBox('Силовая часть · 150 Ом / 3 мФ')
        grid = QtWidgets.QGridLayout(power)
        self.charge = QtWidgets.QPushButton('ЗАРЯД')
        self.run = QtWidgets.QPushButton('ПУСК')
        self.charge.clicked.connect(lambda: self.js('Sim.toggleCharge()'))
        self.run.clicked.connect(lambda: self.js('Sim.toggleInverter()'))
        grid.addWidget(self.charge, 0, 0)
        grid.addWidget(self.run, 0, 1)
        discharge = QtWidgets.QCheckBox('Разряд через инвертор')
        discharge.setChecked(True)
        discharge.toggled.connect(lambda v: self.element('uart-active-discharge', 'checked', v))
        grid.addWidget(discharge, 0, 2)
        self.meters = QtWidgets.QLabel('DC: 0 В · заряд: 0 с')
        grid.addWidget(self.meters, 1, 0, 1, 3)
        left.addWidget(power)
        controls = QtWidgets.QGroupBox('Уставки и энкодер')
        grid = QtWidgets.QGridLayout(controls)
        self.setpoints = []
        for i, (name, unit, maximum, initial) in enumerate([
            ('Модуляция', '%', 100, 65), ('Частота ротора', 'Гц', 1000, 25), ('Лимит тока', '%', 300, 125)
        ]):
            select = QtWidgets.QPushButton(name)
            select.clicked.connect(lambda _, n=i: self.js(f'Sim.select({json.dumps(["mod", "freq", "limit"][n])})'))
            grid.addWidget(select, i, 0)
            spin = QtWidgets.QDoubleSpinBox()
            spin.setRange(0, maximum)
            spin.setDecimals(1)
            spin.setSuffix(' ' + unit)
            spin.setValue(initial)
            self.setpoints.append(spin)
            grid.addWidget(spin, i, 1)
            apply = QtWidgets.QPushButton('Применить')
            apply.clicked.connect(lambda _, n=i: self.apply(n))
            grid.addWidget(apply, i, 2)
        for col, (text, action) in enumerate([('−', 'Encoder.adjust(-1)'), ('Энкодер: применить', 'Encoder.apply()'), ('+', 'Encoder.adjust(1)')]):
            button = QtWidgets.QPushButton(text)
            button.clicked.connect(lambda _, code=action: self.js(code))
            grid.addWidget(button, 3, col)
        next_button = QtWidgets.QPushButton('Следующий параметр')
        next_button.clicked.connect(lambda: self.js('Encoder.next()'))
        grid.addWidget(next_button, 4, 0, 1, 3)
        mode = QtWidgets.QComboBox()
        mode.addItems(['U/F const', 'Скалярное управление', 'Векторное управление'])
        mode.currentIndexChanged.connect(lambda n: self.js(f'DriveSettings.control={json.dumps(["uf", "scalar", "vector"][n])};Sim.publishTelemetry();Sim.updateUI()'))
        grid.addWidget(mode, 5, 0, 1, 3)
        left.addWidget(controls)
        network = QtWidgets.QGroupBox('Сеть и разряд — испытание предупреждений и аварий')
        form = QtWidgets.QFormLayout(network)
        for name, value, low, high, code in [
            ('Напряжение сети, В', 220, 0, 300, 'Sim.mainsVoltage=VALUE'),
            ('Частота сети, Гц', 50, 0, 100, 'Sim.mainsFrequency=VALUE'),
            ('Утечка, кОм', 100, .1, 100000, 'document.getElementById("uart-leak").value=VALUE'),
            ('Мощность разряда, Вт', 30, .1, 1000, 'document.getElementById("uart-power").value=VALUE')
        ]:
            spin = QtWidgets.QDoubleSpinBox()
            spin.setRange(low, high)
            spin.setValue(value)
            spin.valueChanged.connect(lambda v, template=code: self.js(template.replace('VALUE', str(v)) + ';Sim.publishTelemetry();Sim.updateUI()'))
            form.addRow(name, spin)
        left.addWidget(network)
        thresholds=QtWidgets.QGroupBox('Уставки сети: предупреждение / авария');thresholds.setCheckable(True);thresholds.setChecked(False)
        limits=QtWidgets.QFormLayout(thresholds)
        for key,title,value in [('vWarnLow','U предупреждение ниже, В',198),('vWarnHigh','U предупреждение выше, В',242),('vFaultLow','U авария ниже, В',180),('vFaultHigh','U авария выше, В',260),('fWarnLow','f предупреждение ниже, Гц',49),('fWarnHigh','f предупреждение выше, Гц',51),('fFaultLow','f авария ниже, Гц',45),('fFaultHigh','f авария выше, Гц',55)]:
            spin=QtWidgets.QDoubleSpinBox();spin.setRange(0,400);spin.setValue(value)
            spin.valueChanged.connect(lambda v,k=key:self.js(f'window.PchUart.network.{k}={v};Sim.publishTelemetry()'))
            limits.addRow(title,spin)
        left.addWidget(thresholds)

        left.addStretch()
        if not embedded:left.addWidget(QtWidgets.QLabel('Справа — интерактивный референс экрана. Заряд запускается только кнопкой.'))
        if embedded:
            from PySide6.QtWebEngineCore import QWebEngineScript
            script=QWebEngineScript();script.setName('editor-clock')
            script.setInjectionPoint(QWebEngineScript.DocumentCreation)
            script.setWorldId(QWebEngineScript.MainWorld)
            script.setSourceCode('window.PchHostClock={now:0};')
            self.view.page().scripts().insert(script)
        self.view.loadFinished.connect(self.loaded)
        self.view.setUrl(QtCore.QUrl(f'http://127.0.0.1:{self.server.server_port}/?desktop=1'))
        self.timer = QtCore.QTimer(self)
        self.timer.timeout.connect(self.update_status)
        if not embedded:self.timer.start(20)
        self.ports()

    def js(self, code, callback=None):
        if self.ready:
            self.view.page().runJavaScript('(()=>{const {Sim,Encoder,Graph,DriveSettings}=window.PchReference;return eval('+json.dumps(code)+')})()', callback or (lambda _: None))

    def element(self, name, prop, value):
        self.js(f'document.getElementById({json.dumps(name)}).{prop}={json.dumps(value)}')

    def loaded(self, ok):
        self.ready = ok
        self.js("document.head.insertAdjacentHTML('beforeend', '<style>#uart-tools,.debug-panel,.encoder-panel,.encoder-hint,.mcu-panel,.perf-panel{display:none!important}body{margin:0!important;overflow:auto!important}#stage{position:relative!important;left:0!important;top:0!important;width:max-content!important;margin:0 auto!important;padding:12px!important;transform:none!important}#display-module{position:relative!important;transform:none!important;margin:0 auto!important}</style>')")

    def apply(self, i):
        key = ['mod', 'freq', 'limit'][i]
        value = self.setpoints[i].value() / (100 if i == 2 else 1)
        self.js(f'Sim.select({json.dumps(key)});Encoder.pending.{key}={value};Encoder.apply()')

    def ports(self):
        from serial.tools import list_ports
        selected = self.port.currentText()
        self.port.clear()
        self.port.addItems([p.device for p in list_ports.comports()])
        self.port.setCurrentText(selected)

    def connect_port(self):
        try:
            if self.link.serial:
                self.link.disconnect()
            else:
                self.link.connect(self.port.currentText())
        except Exception as exc:
            QtWidgets.QMessageBox.warning(self, 'UART', str(exc))

    def update_status(self):
        self.connect.setText('Отключить' if self.link.serial else 'Подключить')
        self.status.setText(self.link.status)
        self.js('JSON.stringify(window.PchUart.snapshot())',self.render_snapshot)
        self.js('JSON.stringify({state:Sim.chargeState,run:Sim.inverterOn||window.PchUart.activeDischarge,dc:Sim.dc,t:Sim.chargeElapsed,selected:Sim.selected})', self.values)

    def render_snapshot(self,text):
        if text:self.preview.update_snapshot(json.loads(text))

    def values(self, text):
        if not text:
            return
        data = json.loads(text)
        self.charge.setText('РАЗРЯД' if data['state'] in ('charging', 'charged') else 'ЗАРЯД')
        self.run.setText('СТОП' if data['run'] else 'ПУСК')
        self.meters.setText(f"DC: {data['dc']:.1f} В · заряд: {data['t']:.1f} с · выбран: {dict(mod='модуляция',freq='частота',limit='лимит тока').get(data['selected'],data['selected'])}")

    def closeEvent(self, event):
        self.timer.stop()
        self.link.alive = False
        self.link.disconnect()
        self.server.shutdown()
        self.server.server_close()
        event.accept()


if __name__ == '__main__':
    app = QtWidgets.QApplication(sys.argv)
    app.setStyle('Fusion')
    QtGui.QFontDatabase.addApplicationFont('C:/Windows/Fonts/segoeui.ttf')
    app.setFont(QtGui.QFont('Segoe UI',10))
    window = Desk()
    window.show()
    sys.exit(app.exec())
