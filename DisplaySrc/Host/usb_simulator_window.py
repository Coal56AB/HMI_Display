"""Native Qt window; no browser, HTTP server or network listener."""
import base64
from pathlib import Path
from serial.tools import list_ports
from PySide6.QtCore import Qt, QTimer
from PySide6.QtWidgets import (QMainWindow, QWidget, QVBoxLayout, QHBoxLayout,
    QLabel, QPushButton, QComboBox, QGroupBox, QProgressBar, QSpinBox,
    QFileDialog, QSlider)


class SimulatorWindow(QMainWindow):
    def __init__(self, session, port=None):
        super().__init__()
        self.session=session
        self.setWindowTitle('USB — Симулятор основного контроллера')
        self.resize(870,620)
        central=QWidget();self.setCentralWidget(central);layout=QVBoxLayout(central)
        intro=QLabel('Проверка экрана ESP32: MIDI, шесть моторов и команды с тача.\n'
                     'Подключи USB к ПК и нажми RESET на ESP. Данные STM32 по UART имеют приоритет.')
        intro.setWordWrap(True);layout.addWidget(intro)
        row=QHBoxLayout();layout.addLayout(row)
        self.ports=QComboBox();self.ports.setMinimumWidth(330);row.addWidget(self.ports,1)
        self.refresh_button=self.button(row,'Обновить',self.refresh_ports)
        self.connect_button=self.button(row,'Подключить',self.connect_port)
        self.disconnect_button=self.button(row,'Отключить',lambda:self.command('disconnect'))
        self.connection=QLabel();layout.addWidget(self.connection)
        self.error=QLabel();self.error.setWordWrap(True);self.error.setStyleSheet('color: #b62936;')
        self.error.setTextInteractionFlags(Qt.TextSelectableByMouse);layout.addWidget(self.error)
        group=QGroupBox('Мелодия');layout.addWidget(group);box=QVBoxLayout(group)
        row=QHBoxLayout();box.addLayout(row)
        self.button(row,'Тестовая мелодия',lambda:self.command('demo'))
        self.button(row,'Открыть MIDI…',self.open_midi)
        self.play_button=self.button(row,'Играть',lambda:self.command('play'))
        self.button(row,'Стоп всё',lambda:self.command('stop'))
        self.title=QLabel();box.addWidget(self.title)
        self.position=QSlider(Qt.Horizontal);self.position.sliderReleased.connect(self.seek)
        box.addWidget(self.position);self.time=QLabel();box.addWidget(self.time)
        row=QHBoxLayout();layout.addLayout(row);self.voices=[]
        for i in range(6):
            group=QGroupBox(f'M{i+1}');row.addWidget(group);column=QVBoxLayout(group)
            bar=QProgressBar();bar.setOrientation(Qt.Vertical);bar.setRange(0,127)
            bar.setTextVisible(False);bar.setMinimumHeight(140);column.addWidget(bar,0,Qt.AlignHCenter)
            label=QLabel('—');label.setAlignment(Qt.AlignCenter);column.addWidget(label)
            self.voices.append((bar,label))
        row=QHBoxLayout();layout.addLayout(row);row.addWidget(QLabel('Нота MIDI:'))
        self.note=QSpinBox();self.note.setRange(0,127);self.note.setValue(60);row.addWidget(self.note)
        self.button(row,'Note On',lambda:self.command('note',note=self.note.value(),down=True))
        self.button(row,'Note Off',lambda:self.command('note',note=self.note.value(),down=False));row.addStretch()
        self.actions=QLabel();self.actions.setWordWrap(True);layout.addWidget(self.actions)
        self.diagnostics=QLabel();self.diagnostics.setWordWrap(True);layout.addWidget(self.diagnostics)
        self.refresh_ports()
        if port:
            index=self.ports.findData(port)
            if index<0:self.ports.addItem(port,port);index=self.ports.count()-1
            self.ports.setCurrentIndex(index)
            QTimer.singleShot(0,self.connect_port)
        self.timer=QTimer(self);self.timer.timeout.connect(self.update_state);self.timer.start(100)
        self.update_state()

    def button(self,layout,text,callback):
        button=QPushButton(text);button.clicked.connect(callback);layout.addWidget(button);return button

    def refresh_ports(self):
        previous=self.ports.currentData();self.ports.clear();preferred=-1
        for port in list_ports.comports():
            self.ports.addItem(f'{port.device} — {port.description}',port.device)
            if port.vid==0x303a:preferred=self.ports.count()-1
        index=self.ports.findData(previous)
        if index>=0:self.ports.setCurrentIndex(index)
        elif preferred>=0:self.ports.setCurrentIndex(preferred)

    def connect_port(self):
        port=self.ports.currentData()
        if not port:self.error.setText('COM-порт не найден. Подключи ESP по USB и нажми «Обновить».');return
        self.command('connect',port=port)

    def command(self,command,**data):
        try:
            self.session.command(dict(command=command,**data));self.error.clear()
        except Exception as exc:self.error.setText(str(exc))
        self.update_state()

    def open_midi(self):
        name,_=QFileDialog.getOpenFileName(self,'Открыть MIDI','','MIDI (*.mid *.midi)')
        if not name:return
        try:
            path=Path(name)
            if path.stat().st_size>8000000:raise ValueError('Файл больше 8 МБ')
            self.command('midi',name=path.name,data=base64.b64encode(path.read_bytes()).decode('ascii'))
        except Exception as exc:self.error.setText(str(exc))

    def seek(self):
        with self.session.lock:self.session.action(2,0,self.position.value())
        self.update_state()

    def update_state(self):
        s=self.session.status();connected=bool(s['port'])
        self.connection.setText(f"Порт открыт: {s['port']} · отправлено {s['bytes']} байт" if connected else 'Не подключён')
        self.connect_button.setEnabled(not connected);self.disconnect_button.setEnabled(connected)
        self.ports.setEnabled(not connected);self.refresh_button.setEnabled(not connected)
        if s['error']:self.error.setText(s['error'])
        self.title.setText(s['title']);self.play_button.setText('Пауза' if s['playing'] else 'Играть')
        self.position.setMaximum(max(1,s['duration']-1))
        if not self.position.isSliderDown():self.position.setValue(s['position'])
        def timestamp(ms):return f'{ms//60000}:{ms//1000%60:02d}'
        self.time.setText(f"{timestamp(s['position'])} / {timestamp(s['duration'])}")
        for (bar,label),motor in zip(self.voices,s['motors']):
            bar.setValue(motor['note'] if motor['active'] else 0)
            label.setText(f"{motor['note']}\n{motor['frequency']:.1f} Гц" if motor['active'] else '—')
        self.actions.setText(f"Команд с экранчика: {s['actions']}\n{s['last_action']}")
        d=s['diagnostics']
        if d:
            self.diagnostics.setText(f"Тач: опросов {d[0]}, шумных {d[1]}, сбросов {d[2]}, событий {d[3]} · "
                f"палец {'нажат' if d[5] else 'отпущен'} · X={d[6]} Y={d[7]}\n"
                f"Макс. цикл ESP: {d[4]} мс · отчёт {s['diagnostics_age']:.1f} с назад")
        else:self.diagnostics.setText('Диагностика тача: ожидается отчёт ESP')

    def closeEvent(self,event):
        self.timer.stop();self.session.close();event.accept()
