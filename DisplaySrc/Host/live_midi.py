"""Reliable raw MIDI input: PC -> ESP USB -> STM32, with end-to-end ACKs."""
from collections import deque
import struct
from protocol import encode


class LiveMidi:
    def __init__(self):
        self.sequence = 0
        self.begin()

    def begin(self):
        self.events = deque()
        self.pending = b''
        self.sent = self.started = self.last = None
        self.acknowledged = False
        self.event('reset', 0, 0, 0)

    def event(self, kind, channel, note, velocity):
        if kind == 'reset':
            self.events.clear()
        if len(self.events) >= 4096:
            raise RuntimeError('Очередь MIDI переполнена: STM32 не успевает принимать ноты')
        self.events.append((channel, {'on':0,'off':1,'cc':2,'reset':4}[kind], note, velocity))

    def poll(self, now):
        if self.pending and now-self.started >= 1.0:
            raise RuntimeError('Нет подтверждения MIDI от STM32. Проверь связь ESP–STM32 и обнови прошивку ESP.')
        if not self.pending and (self.events or self.last is None or now-self.last >= .1):
            payload = bytearray([1])
            while self.events and len(payload)+10 <= 201:
                channel,kind,note,velocity = self.events.popleft()
                payload.extend(struct.pack('<IBBBBBB', int(now*1000)&0xffffffff,
                    channel, kind|128, note, velocity, 0, 0))
            self.sequence = (self.sequence+1)&255
            self.pending = encode(0x56,payload,self.sequence)
            self.started = now
            self.sent = None
        if self.pending and (self.sent is None or now-self.sent >= .1):
            self.sent = now
            return self.pending
        return b''

    def ack(self, sequence, payload, now):
        if not self.pending or sequence != self.sequence or len(payload)!=1:
            return
        if payload[0]:
            reason = 'STM32 занята Studio, сохранением или воспроизведением с платы' if payload[0]==12 else f'ошибка STM32 {payload[0]}'
            raise RuntimeError('MIDI не принят: '+reason)
        self.pending = b''
        self.last = now
        self.acknowledged = True

    def disconnect_packet(self):
        self.sequence = (self.sequence+1)&255
        return encode(0x56,b'\0',self.sequence)
