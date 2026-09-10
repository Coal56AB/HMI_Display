"""Display test controller, not firmware for a power stage. Units: V/A/s/F/Hz."""
from collections import deque
import math


class Simulator:
    def __init__(self):
        self.capacitance = .003
        self.resistance = 150.0
        self.mains = 220.0
        self.leak_resistance = 100000.0
        self.discharge_watts = 30.0
        self.state = 'off'
        self.time = self.voltage = self.charge_time = self.speed = 0.0
        self.precharge_current = self.precharge_temp = 0.0
        self.setpoints = [65.0, 25.0, 125.0]
        self.selected = self.mode = 0
        self.nominal_current = 3.0
        self.history = deque(maxlen=32000)  # > 300 s at 100 Hz, maximum scope span
        self.note = 'Симулятор остановлен'

    def command(self, text):
        args = text.lower().split()
        if not args:
            return ''
        cmd = args[0]
        if cmd == 'charge':
            if self.state not in ('off', 'passive', 'discharge'):
                raise ValueError('Заряд доступен после отключения сети')
            self.state, self.charge_time = 'charge', 0.0
        elif cmd == 'start':
            if self.state != 'ready':
                raise ValueError('Для пуска сначала зарядите DC-звено')
            self.state = 'run'
        elif cmd == 'stop':
            if self.state == 'run':
                self.state = 'ready'
            elif self.state == 'discharge':
                self.state = 'passive'
        elif cmd == 'discharge':
            if len(args) != 2 or args[1] not in ('passive', 'inverter'):
                raise ValueError('discharge passive | discharge inverter')
            self.state = 'passive' if args[1] == 'passive' else 'discharge'
        elif cmd == 'select':
            value = int(args[1])
            if value not in (0, 1, 2):
                raise ValueError('select: 0 модуляция, 1 скорость, 2 лимит')
            self.selected = value
        elif cmd in ('enc', 'set'):
            value = float(args[1])
            if not math.isfinite(value):
                raise ValueError('Нужно конечное число')
            index = self.selected
            target = self.setpoints[index] + value * (0.5 if index == 1 else 1) if cmd == 'enc' else value
            self.setpoints[index] = max(0.0, min((100.0, 200.0, 200.0)[index], target))
        else:
            raise ValueError('Команды: charge, start, stop, discharge passive/inverter, select 0/1/2, enc N, set N')
        self.note = text
        return self.note

    def event(self, payload):
        import struct
        _, kind, _, ident, value, _, _ = struct.unpack('<IBBHfI8s', payload)
        if kind == 1 and ident < 3 and math.isfinite(value):
            self.selected = ident
            self.command(f'set {value}')
        elif kind == 1 and ident == 11 and 0.01 <= value <= 1000:
            self.nominal_current = value
        elif kind == 2 and ident == 10 and value in (0, 1, 2):
            self.mode = int(value)
        elif kind == 10 and ident == 400 and value in (0, 1, 2):
            self.selected = int(value)
        return f'Экран: событие {kind}, параметр {ident}, значение {value:g}'

    def step(self, dt):
        if not 0 < dt <= .1:
            raise ValueError('Integrator step must be in (0, .1]')
        self.time += dt
        target = self.mains * math.sqrt(2)
        self.precharge_current = 0.0
        if self.state == 'charge':
            before = self.voltage
            self.charge_time += dt
            self.voltage = target + (before - target) * math.exp(-dt / (self.resistance * self.capacitance))
            self.precharge_current = max(0, (target - self.voltage) / self.resistance)
            heat = .5 * self.capacitance * ((target - before)**2 - (target - self.voltage)**2)
            self.precharge_temp += max(0, heat) / 10.0  # illustrative resistor thermal mass: 10 J/K
            if self.voltage >= .95 * target:
                self.state = 'ready'  # close bypass contactor
                self.voltage = target
                self.precharge_current = 0
        elif self.state in ('ready', 'run'):
            self.voltage = target
        elif self.state == 'discharge':
            # Constant-power discharge, tapered below 40 V; leakage remains present.
            watts = self.discharge_watts * min(1, (self.voltage / 40)**2)
            energy = max(0, .5 * self.capacitance * self.voltage**2 - watts * dt)
            self.voltage = math.sqrt(2 * energy / self.capacitance)
        if self.state in ('passive', 'discharge', 'off'):
            self.voltage *= math.exp(-dt / (self.leak_resistance * self.capacitance))
            if self.voltage < 1:
                self.voltage = 0
                self.state = 'off'
        self.precharge_temp *= math.exp(-dt / 60)
        requested = self.setpoints[1] if self.state == 'run' else 0
        self.speed += max(-30 * dt, min(30 * dt, requested - self.speed))
        self.history.append((self.time, self.graph_values()))

    def snapshot(self):
        running = self.state == 'run'
        active = self.state == 'discharge'
        mains_on = self.state in ('charge', 'ready', 'run')
        modulation = self.setpoints[0] if running else 0
        amps = self.nominal_current * min(self.setpoints[2] / 100, .2 + self.speed / 100) if running else 0
        if active:
            amps = self.discharge_watts * min(1, (self.voltage / 40)**2) / max(1, self.voltage)
        output = self.voltage * modulation / 100 / math.sqrt(2) if running else self.voltage if active else 0
        state = {'off': 0, 'charge': 1, 'ready': 2, 'run': 3, 'passive': 0, 'discharge': 0}[self.state]
        flags = int(running or active) | 4 | (8 if mains_on else 0) | (16 if self.state in ('ready', 'run') else 0)
        flags |= 32 if self.state == 'passive' else 64 if active else 0
        data = dict(mains_voltage=self.mains if mains_on else 0, mains_frequency=50 if mains_on else 0,
                    dc_bus_voltage=self.voltage, precharge_current=self.precharge_current,
                    precharge_seconds=self.charge_time, temperature_rectifier=25 + amps,
                    temperature_precharge=25 + self.precharge_temp, temperature_dc=25,
                    temperature_inverter=25 + amps * 2, temperature_motor=25 + self.speed / 10,
                    modulation_set=self.setpoints[0], modulation_actual=modulation,
                    rotation_set=self.setpoints[1], rotation_actual=self.speed,
                    current_limit_set=self.setpoints[2], current_limit_actual=amps / self.nominal_current * 100,
                    output_voltage=output, output_current=amps, output_power=output * amps / 1000,
                    rotor_frequency=self.speed, stator_frequency=self.setpoints[1] if running else 0,
                    slip=max(0, (self.setpoints[1] - self.speed) / max(.01, self.setpoints[1]) * 100) if running else 0,
                    motor_load=amps / self.nominal_current * 100)
        return state, flags, data

    def graph_values(self):
        _, _, d = self.snapshot()
        # D/Q are illustrative quadrature signals, explicitly a test waveform.
        angle = self.time * 2 * math.pi * 2
        amps, volts = d['output_current'], d['output_voltage']
        return ((volts, amps, d['output_power'], self.voltage),
                (self.speed, d['stator_frequency'], self.setpoints[1], d['motor_load']),
                (self.setpoints[2], d['current_limit_actual'], d['modulation_actual'], self.setpoints[0]),
                (amps * math.cos(angle), volts * math.cos(angle), amps, volts),
                (amps * math.sin(angle), volts * math.sin(angle), amps, volts))

    def graph_window(self, page, division_ms):
        if not self.history:
            return [[0.0] * 48 for _ in range(4)]
        history = list(self.history)
        start = self.time - division_ms * .005  # five divisions
        result, at = [[] for _ in range(4)], 0
        for i in range(48):
            t = start + division_ms * .005 * i / 47
            while at + 1 < len(history) and history[at + 1][0] <= t:
                at += 1
            values = history[at][1][page] if t >= history[0][0] else (0, 0, 0, 0)
            for ch in range(4):
                result[ch].append(values[ch])
        return result
