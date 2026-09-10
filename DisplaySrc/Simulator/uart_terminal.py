"""Run with Python 3 + tkinter; install pyserial for a real COM connection."""
import argparse
import struct
import time
import tkinter as tk
from tkinter import ttk
from inverter_sim import Simulator
from uart_protocol import Decoder, telemetry, graph


class Terminal:
    def __init__(self, root):
        self.root, self.model = root, Simulator()
        self.serial = None
        self.decoder = Decoder()
        self.sequence = 0
        self.pending = None
        self.graph_page, self.graph_running, self.division_ms = 0, True, 200
        self.graph_visible = False
        self.last_kind = 0
        self.graph_queue = []
        self.next_telemetry = self.next_graph = 0
        self.last = time.monotonic()
        self.accumulator = 0.0
        self.last_draw = 0
        self.acks = 0
        root.title('PCH-1000 · UART терминал / симулятор контроллера')
        root.geometry('1080x810')
        root.minsize(920, 720)
        root.protocol('WM_DELETE_WINDOW', self.close)
        shell = ttk.Frame(root, padding=12)
        shell.pack(fill='both', expand=True)
        link = ttk.Frame(shell)
        link.pack(fill='x')
        ttk.Label(link, text='UART 115200 · 8N1').pack(side='left', padx=(0, 12))
        self.port = ttk.Combobox(link, width=15)
        self.port.pack(side='left')
        ttk.Button(link, text='Обновить порты', command=self.ports).pack(side='left', padx=5)
        self.connect_button = ttk.Button(link, text='Подключить', command=self.connect)
        self.connect_button.pack(side='left')
        self.link_status = tk.StringVar(value='Без подключения · локальная симуляция')
        ttk.Label(link, textvariable=self.link_status).pack(side='left', padx=12)
        ttk.Label(shell, text='Симулятор для проверки дисплея. D/Q — тестовые сигналы; силовыми GPIO эта программа не управляет.').pack(anchor='w', pady=(10, 6))
        settings = ttk.LabelFrame(shell, text='Модель DC-звена', padding=8)
        settings.pack(fill='x')
        self.config = {}
        for i, (key, name, value) in enumerate((('mains', 'Сеть, В', 220), ('capacitance', 'Ёмкость, мФ', 3),
                ('resistance', 'Предзаряд, Ом', 150), ('leak_resistance', 'Утечка, кОм', 100),
                ('discharge_watts', 'Разряд инв., Вт', 30))):
            cell = ttk.Frame(settings)
            cell.pack(side='left', padx=6)
            ttk.Label(cell, text=name).pack(anchor='w')
            var = tk.StringVar(value=str(value))
            ttk.Entry(cell, width=12, textvariable=var).pack()
            self.config[key] = var
        ttk.Button(settings, text='Применить', command=self.configure).pack(side='left', padx=10, pady=10)
        controls = ttk.LabelFrame(shell, text='Управление', padding=8)
        controls.pack(fill='x', pady=8)
        self.charge_button = ttk.Button(controls, text='Заряд', width=14, command=self.toggle_charge)
        self.charge_button.pack(side='left', padx=4)
        self.inverter_discharge = tk.BooleanVar(value=False)
        ttk.Checkbutton(controls, text='Разряд через инвертор', variable=self.inverter_discharge).pack(side='left', padx=(4, 24))
        self.run_button = ttk.Button(controls, text='Пуск', width=14, command=self.toggle_run)
        self.run_button.pack(side='left', padx=4)
        encoder = ttk.Frame(shell)
        encoder.pack(fill='x')
        ttk.Label(encoder, text='Энкодер:').pack(side='left')
        self.selection = ttk.Combobox(encoder, state='readonly', width=20,
                                     values=('Модуляция, %', 'Скорость ротора, Гц', 'Ограничение тока, %'))
        self.selection.current(0)
        self.selection.pack(side='left', padx=8)
        self.selection.bind('<<ComboboxSelected>>', lambda _: self.command(f'select {self.selection.current()}'))
        ttk.Button(encoder, text='− шаг', command=lambda: self.command('enc -1')).pack(side='left')
        ttk.Button(encoder, text='+ шаг', command=lambda: self.command('enc 1')).pack(side='left', padx=4)
        knob = ttk.Label(encoder, text=' ↶ Колесо мыши ↷ ', relief='ridge', padding=8)
        knob.pack(side='left', padx=8)
        knob.bind('<MouseWheel>', lambda e: self.command(f'enc {1 if e.delta > 0 else -1}'))
        self.values = tk.StringVar()
        ttk.Label(encoder, textvariable=self.values).pack(side='left', padx=8)
        self.state = tk.StringVar()
        ttk.Label(shell, textvariable=self.state, font=('Segoe UI', 12)).pack(anchor='w', pady=10)
        graph_bar = ttk.Frame(shell)
        graph_bar.pack(fill='x')
        ttk.Label(graph_bar, text='Осциллограммы ПК (страница дисплея выбирается на самом дисплее):').pack(side='left')
        self.preview_page = ttk.Combobox(graph_bar, state='readonly', width=15,
                                       values=('Мощность', 'Скорость', 'Ограничение', 'D тест', 'Q тест'))
        self.preview_page.current(0)
        self.preview_page.pack(side='left', padx=8)
        self.canvas = tk.Canvas(shell, background='#101e27', highlightthickness=0, height=230)
        self.canvas.pack(fill='both', expand=True, pady=7)
        self.log_widget = tk.Text(shell, height=6, state='disabled', wrap='word')
        self.log_widget.pack(fill='x')
        command_bar = ttk.Frame(shell)
        command_bar.pack(fill='x', pady=(8, 0))
        ttk.Label(command_bar, text='Команда:').pack(side='left', padx=(0, 8))
        self.entry = ttk.Entry(command_bar)
        self.entry.pack(side='left', fill='x', expand=True)
        self.entry.bind('<Return>', self.enter)
        ttk.Button(command_bar, text='Выполнить', command=self.enter).pack(side='left', padx=8)
        self.ports()
        self.log('charge · start · stop · discharge passive/inverter · select 0/1/2 · enc N · set N')
        root.after(10, self.tick)

    def log(self, text):
        self.log_widget.configure(state='normal')
        self.log_widget.insert('end', time.strftime('%H:%M:%S ') + text + '\n')
        if int(self.log_widget.index('end-1c').split('.')[0]) > 300:
            self.log_widget.delete('1.0', '50.0')
        self.log_widget.see('end')
        self.log_widget.configure(state='disabled')

    def ports(self):
        try:
            from serial.tools.list_ports import comports
            values = [p.device for p in comports()]
            self.port['values'] = values
            if values and not self.port.get():
                self.port.set(values[0])
        except ImportError:
            self.link_status.set('Для UART установите pyserial; симуляция доступна')

    def configure(self):
        import math
        try:
            if self.model.state != 'off' or self.model.voltage > 1:
                raise ValueError('Изменение модели доступно после полного разряда')
            values = {k: float(v.get().replace(',', '.')) for k, v in self.config.items()}
            bounds = {'mains': (1, 500), 'capacitance': (.01, 100), 'resistance': (1, 100000),
                      'leak_resistance': (.001, 100000), 'discharge_watts': (.01, 10000)}
            for key, value in values.items():
                if not math.isfinite(value) or not bounds[key][0] <= value <= bounds[key][1]:
                    raise ValueError(f'{key}: допустимо {bounds[key]}')
            values['capacitance'] /= 1000
            values['leak_resistance'] *= 1000
            for key, value in values.items():
                setattr(self.model, key, value)
            self.log('Параметры модели применены')
        except ValueError as exc:
            self.log(str(exc))

    def command(self, text):
        try:
            self.log(self.model.command(text))
            self.selection.current(self.model.selected)
            self.next_telemetry = 0
            self.update_buttons()
        except (ValueError, IndexError) as exc:
            self.log('Команда отклонена: ' + str(exc))

    def toggle_charge(self):
        if self.model.state in ('charge', 'ready', 'run'):
            self.command('discharge inverter' if self.inverter_discharge.get() else 'discharge passive')
        else:
            self.command('charge')

    def toggle_run(self):
        self.command('stop' if self.model.state == 'run' else 'start')

    def update_buttons(self):
        self.charge_button.configure(text='Разряд' if self.model.state in ('charge', 'ready', 'run') else 'Заряд')
        self.run_button.configure(text='Стоп' if self.model.state == 'run' else 'Пуск',
                                  state='normal' if self.model.state in ('ready', 'run') else 'disabled')

    def enter(self, _=None):
        self.command(self.entry.get())
        self.entry.delete(0, 'end')

    def disconnect(self, reason='Отключено'):
        if self.serial:
            self.serial.close()
        self.serial, self.pending = None, None
        self.graph_queue.clear()
        self.connect_button.configure(text='Подключить')
        self.link_status.set(reason)

    def connect(self):
        if self.serial:
            self.disconnect()
            return
        try:
            import serial
            self.serial = serial.Serial(self.port.get(), 115200, timeout=0, write_timeout=.05)
            self.serial.reset_input_buffer()
            self.decoder = Decoder()
            self.pending = None
            self.graph_queue.clear()
            self.next_telemetry = self.next_graph = 0
            self.connect_button.configure(text='Отключить')
            self.link_status.set('Ожидание ответа дисплея…')
            self.log(f'Открыт {self.port.get()}, 115200 8N1')
        except Exception as exc:
            self.disconnect('Не удалось подключиться')
            self.log(str(exc))

    def send(self, data, now):
        if self.serial.write(data) != len(data):
            raise OSError('Неполная запись UART')
        self.pending = [self.sequence, data, now, 0]
        self.last_kind = data[2]

    def exchange(self, now):
        if not self.serial:
            return
        try:
            for kind, payload in self.decoder.feed(self.serial.read(min(4096, self.serial.in_waiting))):
                if kind == 1:
                    self.log(self.model.event(payload))
                    self.selection.current(self.model.selected)
                    self.next_telemetry = 0
                elif kind == 4:
                    seq, status, page, running, division = struct.unpack('<IBBBI', payload)
                    if self.pending and seq == self.pending[0]:
                        self.pending = None
                        self.acks += 1
                        if page in range(5) and 10 <= division <= 60000:
                            if page != self.graph_page or division != self.division_ms or (running & 3) != 3:
                                self.graph_queue.clear()
                            self.graph_page, self.graph_running, self.division_ms = page, bool(running & 1), division
                            self.graph_visible = bool(running & 2)
                        self.link_status.set(f'На связи · ACK {self.acks} · CRC ошибок {self.decoder.errors}')
                        if status == 1:
                            self.log('Дисплей отклонил пакет: неверные поля')
            if self.pending:
                if now - self.pending[2] >= 2:
                    if self.pending[3] >= 3:
                        raise OSError('Нет ответа дисплея: проверьте прошивку и RX/TX')
                    data = self.pending[1]
                    if self.serial.write(data) != len(data):
                        raise OSError('Неполная повторная запись UART')
                    self.pending[2], self.pending[3] = now, self.pending[3] + 1
                return
            self.sequence = (self.sequence + 1) & 0xffffffff
            if self.graph_running and self.graph_visible and not self.graph_queue and now >= self.next_graph:
                self.graph_queue = list(enumerate(self.model.graph_window(self.graph_page, self.division_ms)))
                self.next_graph = now + .5
            if now >= self.next_telemetry and (self.last_kind != 2 or not self.graph_queue):
                self.send(telemetry(self.sequence, self.model), now)
                self.next_telemetry = now + .1
            else:
                if self.graph_queue and self.graph_running and self.graph_visible:
                    channel, points = self.graph_queue.pop(0)
                    self.send(graph(self.sequence, self.graph_page, channel, points), now)
        except Exception as exc:
            self.log('UART: ' + str(exc))
            self.disconnect('Связь потеряна · локальная симуляция продолжается')

    def draw(self):
        self.update_buttons()
        _, _, d = self.model.snapshot()
        names = {'off': 'СТОП', 'charge': 'ЗАРЯД ЧЕРЕЗ 150 Ом', 'ready': 'ГОТОВ · резистор шунтирован',
                 'run': 'РАБОТА', 'passive': 'СЕТЬ ОТКЛЮЧЕНА · самостоятельный разряд',
                 'discharge': 'РАЗРЯД ЧЕРЕЗ ИНВЕРТОР · переходный процесс'}
        if self.model.state == 'charge':
            names['charge'] = f'ЗАРЯД ЧЕРЕЗ {self.model.resistance:g} Ом'
        self.state.set(f"{names[self.model.state]}    DC {self.model.voltage:.1f} В    "
                       f"I предз. {self.model.precharge_current:.2f} А    t зар. {self.model.charge_time:.2f} с")
        a, b, c = self.model.setpoints
        self.values.set(f'{a:g} %  /  {b:g} Гц  /  {c:g} %')
        cv = self.canvas
        cv.delete('all')
        width, height = max(100, cv.winfo_width()), max(100, cv.winfo_height())
        page = max(0, self.preview_page.current())
        data = self.model.graph_window(page, self.division_ms)
        labels = [('U, В', 'I, А', 'P, кВт', 'DC, В'), ('Ротор, Гц', 'Статор, Гц', 'Задание, Гц', 'Нагрузка, %'),
                  ('Лимит, %', 'Ток, %', 'Модуляция, %', 'Задание, %'),
                  ('Id, А', 'Ud, В', 'Id зад., А', 'Ud зад., В'), ('Iq, А', 'Uq, В', 'Iq зад., А', 'Uq зад., В')][page]
        for ch, color in enumerate(('#40c8ed', '#f5c45b', '#8fdb88', '#f1859d')):
            x, y = (ch % 2) * width / 2, (ch // 2) * height / 2
            w, h = width / 2, height / 2
            lo, hi = min([0] + data[ch]), max([.01] + data[ch])
            cv.create_text(x + 10, y + 12, text=f'{labels[ch]}   {data[ch][-1]:.2f}   [{lo:.1f}…{hi:.1f}]', anchor='w', fill=color)
            coords = []
            for i, val in enumerate(data[ch]):
                coords.extend((x + 12 + i * (w - 24) / 47, y + h - 10 - (val - lo) / (hi - lo) * (h - 36)))
            cv.create_line(*coords, fill=color, width=2)

    def tick(self):
        now = time.monotonic()
        self.accumulator += min(now - self.last, 1.0)
        self.last = now
        while self.accumulator >= .01:
            self.model.step(.01)
            self.accumulator -= .01
        self.exchange(now)
        if now - self.last_draw > .1:
            self.draw()
            self.last_draw = now
        self.root.after(10, self.tick)

    def close(self):
        self.disconnect()
        self.root.destroy()


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--smoke-test', action='store_true', help='Create UI and exit after 1 s')
    args = parser.parse_args()
    root = tk.Tk()
    app = Terminal(root)
    if args.smoke_test:
        root.after(1000, app.close)
    root.mainloop()
