"""Virtual six-motor controller for the physical ESP32 screen; no motor output."""
import io
import math
import struct
from bisect import bisect_right
from protocol import encode, state_packet, title_packet, MIDI_EVENT, MIDI_CLOCK


class Controller:
    def __init__(self, midi_sink=None):
        self.midi_sink = midi_sink
        self.title = 'MIDI'
        self.events = []
        self.event_times = []
        self.duration = 16000
        self.position = self.clock = 0
        self.playing = self.paused = False
        self.loop = True
        self.sleep = self.reset = self.micro = 0
        self.motors = [dict(enabled=False, active=False, direction=False, note=60+i,
                            frequency=440*2**((60+i-69)/12)) for i in range(6)]
        self.keys = {}
        self.sustain = set()
        self.changes = []
        self.replaying = False
        self.cursor = 0
        self.demo()

    def demo(self):
        events = []
        def note(at,length,channel,pitch,velocity=100):
            events.extend([(at,'on',channel,pitch,velocity),(at+length,'off',channel,pitch,0)])
        # Original 16-bar study at 120 BPM: independent rhythms, rests and registers.
        roots=[48,45,53,43,48,45,50,43,48,53,45,43,53,50,43,48]
        tune=[0,4,7,11,9,7,4,2, 4,7,12,11,7,4,2,0]
        rhythm=[(0,330),(500,160),(750,160),(1000,430),(1625,230)]
        for bar,root in enumerate(roots):
            at=bar*2000
            # Bass: long root, syncopated fifth, occasional walking pickup.
            note(at,650,0,root-12,110)
            note(at+1000,360,0,root-5,95)
            if bar%2:note(at+1750,190,0,root-10,85)
            # Lead changes every bar; every fourth bar leaves a breathing space.
            for j,(offset,length) in enumerate(rhythm):
                if bar%4==3 and j==3:continue
                pitch=root+12+tune[(bar*3+j)%len(tune)]
                note(at+offset,length,1,pitch,95+j*4)
            # Arpeggio enters off the beat and moves between voices/registers.
            for j in range(8):
                if (bar+j)%5==0:continue
                note(at+125+j*250,110+(j%3)*35,2,root+[0,7,12,4][(j+bar)%4],70+j*3)
            # Countermelody appears in the second half, with its own phrase length.
            if bar>=8:
                note(at+375,520,3,root+19,85)
                if bar%3:note(at+1375,390,3,root+16,80)
            # Short accents and occasional held harmony exercise mixed note lengths.
            for offset in (500,1500):note(at+offset,70,4,root+24+(bar%3)*2,105)
            if bar%4 in (0,2):note(at+250,1250,5,root+4,65)
        self.load(events, 32500, 'Ритм, бас и мелодия')

    def load_midi(self, data, title):
        import mido
        midi = mido.MidiFile(file=io.BytesIO(data))
        events, at = [], 0.0
        for msg in midi:
            at += msg.time * 1000
            if msg.type in ('note_on','note_off'):
                kind = 'on' if msg.type == 'note_on' and msg.velocity else 'off'
                events.append((round(at),kind,msg.channel,msg.note,msg.velocity))
            elif msg.type == 'control_change':
                events.append((round(at),'cc',msg.channel,msg.control,msg.value))
            if len(events) > 200000: raise ValueError('Слишком много событий MIDI')
        if not events: raise ValueError('В MIDI нет нот/контроллеров')
        self.load(events, max(1000,round(at)+500), title)

    def load(self, events, duration, title):
        self.stop_notes()
        self.events = sorted(events, key=lambda e:e[0])
        self.event_times = [e[0] for e in self.events]
        self.duration = duration
        self.title = title[:48]
        self.position = self.clock = self.cursor = 0
        self.playing = self.paused = False
        self.changes = []

    def change(self, voice, pitch, velocity):
        if self.replaying: return
        self.changes.append((int(self.clock)&0xffffffff,voice,pitch,velocity))
        if len(self.changes) > 512: raise RuntimeError('Симулятор не успевает передавать MIDI')

    def off_voice(self, voice, disable=False):
        m = self.motors[voice]
        if m['active']:
            if m['note'] < 128:self.change(voice,m['note'],0)
        m['active'] = False
        if disable: m['enabled'] = False
        for key in list(self.keys):
            if self.keys[key][0] == voice: del self.keys[key]

    def stop_notes(self):
        if self.midi_sink and not self.replaying:self.midi_sink('reset',0,0,0)
        for i in range(6): self.off_voice(i,True)
        self.sustain.clear()

    def note(self, kind, channel, pitch, velocity=100):
        if self.midi_sink and not self.replaying:self.midi_sink(kind,channel,pitch,velocity)
        key = (channel,pitch)
        if kind == 'cc':
            if pitch == 64:
                if velocity >= 64: self.sustain.add(channel)
                else:
                    self.sustain.discard(channel)
                    for k, (v,down) in list(self.keys.items()):
                        if k[0] == channel and not down: self.off_voice(v)
            elif pitch in (120,123):
                for k,(v,_) in list(self.keys.items()):
                    if k[0] == channel: self.off_voice(v)
            elif pitch == 121: self.note('cc',channel,64,0)
            return
        if kind == 'off' or not velocity:
            if key in self.keys:
                v,_ = self.keys[key]
                if channel in self.sustain: self.keys[key] = (v,False)
                else: self.off_voice(v)
            return
        if key in self.keys: self.off_voice(self.keys[key][0])
        self.paused = False
        free = next((i for i,m in enumerate(self.motors) if not m['active']), None)
        if free is None:
            free = next(iter(self.keys.values()), (0,False))[0]
            self.off_voice(free)
        frequency = 440*2**((pitch-69)/12)
        while frequency < 20: frequency *= 2
        while frequency > 4000: frequency /= 2
        self.sleep = self.reset = 0
        self.motors[free].update(enabled=True,active=True,note=pitch,frequency=frequency)
        self.keys[key] = (free,True)
        self.change(free,pitch,velocity)

    def seek(self, position):
        self.stop_notes()
        self.position = min(max(0,int(position)),self.duration-1)
        self.clock = self.position
        self.cursor = bisect_right(self.event_times,self.position)
        if self.playing:
            self.replaying = True
            try:
                for event in self.events[:self.cursor]:
                    self.clock=event[0];self.note(*event[1:])
            finally: self.replaying = False;self.clock=self.position
            if self.midi_sink:
                for channel in self.sustain:self.midi_sink('cc',channel,64,127)
                for (channel,pitch),(_,down) in self.keys.items():
                    self.midi_sink('on',channel,pitch,100)
                    if not down:self.midi_sink('off',channel,pitch,0)
        self.changes = []  # Sender reconstructs held voices after resetting the clock.

    def advance(self, delta):
        delta = max(0,min(int(delta),1000))
        if not self.playing:
            self.clock += delta
            return
        target = min(self.position+delta,self.duration)
        while self.cursor < len(self.events) and self.events[self.cursor][0] <= target:
            event = self.events[self.cursor]
            self.clock += max(0,event[0]-self.position); self.position = event[0]
            self.note(*event[1:]); self.cursor += 1
        self.clock += max(0,target-self.position); self.position = target
        if self.position >= self.duration:
            self.stop_notes()
            if self.loop: self.position = self.cursor = 0
            else: self.playing = False

    def action(self, action, motor, value):
        if motor >= 6 or action > 10: return 2
        m = self.motors[motor]
        if action == 0:
            self.playing = self.paused = False; self.position = self.cursor = 0
            self.stop_notes(); self.sleep = self.reset = 1
        elif action == 1:
            if self.playing:
                self.playing=False;self.paused=True;self.stop_notes()
            else:
                self.sleep=self.reset=0;self.playing=True;self.paused=False;self.seek(self.position)
        elif action == 2: self.seek(value)
        elif action == 3:
            if not 20000 <= value <= 4000000: return 2
            if self.playing: return 1
            active=m['active'];self.off_voice(motor)
            m.update(frequency=value/1000,note=round(69+12*math.log2(value/440000)))
            m['active']=active
            if active:self.change(motor,m['note'],100)
        elif action in (4,5,6):
            if value not in (0,1): return 2
            if self.playing: return 1
            if action == 4:
                if not value:self.off_voice(motor)
                m['enabled']=bool(value)
            elif action == 5:
                if value:
                    if self.sleep or self.reset:return 1
                    if not m['active']:
                        m.update(enabled=True,active=True);self.change(motor,m['note'],100)
                else:self.off_voice(motor)
            else:
                if m['active']:return 1
                m['direction']=bool(value)
        elif action == 7:
            if value not in (0,1,2,3,7):return 2
            if any(x['active'] for x in self.motors):return 1
            self.micro=value
        elif action == 8:
            if value not in (0,1):return 2
            self.sleep=value
            if value:self.playing=False;self.stop_notes()
        elif action == 9:
            self.reset=1;self.playing=False;self.stop_notes()
        elif action == 10:self.sleep=self.reset=0
        return 0

    def packets(self, reset_history=False):
        packets=[]
        if reset_history:
            # Force the firmware's history-clear path, even when reconnecting at the same time.
            packets.append(encode(MIDI_CLOCK,struct.pack('<I',0xffffffff)))
            packets.append(encode(MIDI_CLOCK,struct.pack('<I',int(self.clock)&0xffffffff)))
            self.changes=[(int(self.clock)&0xffffffff,i,m['note'],100) for i,m in enumerate(self.motors) if m['active']]
        packets.append(encode(MIDI_CLOCK,struct.pack('<I',int(self.clock)&0xffffffff)))
        packets.append(state_packet(self.motors,self.position,self.duration,playing=self.playing,
                                    paused=self.paused,midi=True,sleep=self.sleep,reset=self.reset,micro=self.micro))
        for at,voice,pitch,velocity in self.changes:
            packets.append(encode(MIDI_EVENT,struct.pack('<IBBB',at,voice,pitch,velocity)))
        self.changes=[]
        return b''.join(packets)

    def status(self):
        return dict(title=self.title,playing=self.playing,paused=self.paused,position=self.position,
                    duration=self.duration,motors=[dict(m) for m in self.motors],sleep=self.sleep,reset=self.reset,micro=self.micro)
