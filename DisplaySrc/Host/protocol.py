"""MusicBox HMI UART v1. Transport is independent of Qt and pyserial."""
import struct
import time
from collections import deque

STATE, TITLE, MIDI_EVENT, MIDI_CLOCK, ACTION, ACK = (0x40,0x41,0x42,0x43,0x50,0x51)

def crc16(data):
    crc=0xffff
    for b in data:
        crc ^= b<<8
        for _ in range(8):
            crc=((crc<<1)^0x1021 if crc&0x8000 else crc<<1)&0xffff
    return crc

def encode(command,payload=b'',seq=0):
    if len(payload)>240: raise ValueError('Payload too large')
    body=bytes((len(payload),seq&255,command))+payload
    return b'\xa5\x5a'+body+struct.pack('<H',crc16(body))

class Parser:
    def __init__(self): self.buffer=bytearray(); self.last=0
    def feed(self,data):
        now=time.monotonic()
        if now-self.last>.1:self.buffer.clear()
        if data:self.last=now
        self.buffer.extend(data);frames=[]
        while len(self.buffer)>=3:
            if self.buffer[:2]!=b'\xa5\x5a' or self.buffer[2]>240:
                del self.buffer[0];continue
            n=self.buffer[2]+7
            if len(self.buffer)<n:break
            b=self.buffer[:n]
            if crc16(b[2:-2])!=struct.unpack('<H',b[-2:])[0]:
                del self.buffer[0];continue
            del self.buffer[:n];frames.append((b[4],b[3],bytes(b[5:-2])))
        return frames

def state_packet(motors,position=0,duration=0,connected=True,playing=False,paused=False,midi=False,sleep=0,reset=0,micro=0,mask=63,known_midi=False):
    flags=int(connected)|int(playing)*2|int(paused)*4|int(midi)*8|int(known_midi)*32
    data=struct.pack('<BBBBBBII',1,flags,sleep,reset,micro,mask,max(0,int(position)),max(0,int(duration)))
    for motor in motors:
        motor_flags=int(motor['enabled'])|int(motor['active'])*2|int(motor['direction'])*4
        data+=struct.pack('<BBI',motor_flags,motor.get('note',255),round(motor['frequency']*1000))
    if len(data)!=50: raise ValueError('Exactly six motors required')
    return encode(STATE,data)

def title_packet(title):
    data=title.encode('utf8')[:48].decode('utf8','ignore').encode('utf8')
    return encode(TITLE,data)

class Actions:
    """Deduplicate retries without re-executing toggles; cache rejected commands too."""
    def __init__(self,callback):self.callback=callback;self.recent=deque(maxlen=16)
    def receive(self,seq,payload):
        now=time.monotonic()
        for oldseq,oldpayload,result,at in self.recent:
            if seq==oldseq and payload==oldpayload and now-at<2:
                return encode(ACK,bytes((result,)),seq)
        if len(payload)!=6:result=2
        else:
            action,motor,value=struct.unpack('<BBI',payload)
            result=self.callback(action,motor,value)
        self.recent.append((seq,payload,result,now))
        return encode(ACK,bytes((result,)),seq)

