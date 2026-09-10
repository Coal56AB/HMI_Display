"""Run the actual firmware renderer with XPT2046-equivalent touch coordinates."""
import argparse,ctypes as C,math,os,struct,subprocess,sys,time
from pathlib import Path
root=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(root/'DisplaySrc/Host'))
from protocol import Parser,Actions,ACTION,encode,state_packet,title_packet,MIDI_EVENT,MIDI_CLOCK
parser=argparse.ArgumentParser();parser.add_argument('--screenshot');parser.add_argument('--page',choices=['overview','midi','song','settings','motor'],default='overview');args=parser.parse_args()
if args.screenshot:os.environ.setdefault('QT_QPA_PLATFORM','offscreen')
from PySide2.QtCore import Qt,QTimer
from PySide2.QtGui import QImage,QPainter
from PySide2.QtWidgets import QApplication,QWidget
libfile=root/'.build/music_box.dll'
if not libfile.exists():subprocess.run([sys.executable,str(root/'DisplaySrc/Scripts/test.py')],check=True)
lib=C.CDLL(str(libfile))
WR=C.CFUNCTYPE(None,C.c_uint16,C.c_uint16,C.c_uint16,C.c_uint16,C.POINTER(C.c_uint16),C.c_uint16,C.c_void_p)
READ=C.CFUNCTYPE(C.c_int,C.c_uint32,C.c_void_p,C.c_uint32,C.c_void_p)
FR=C.CFUNCTYPE(C.c_int,C.c_uint32,C.c_void_p,C.c_uint32)
FW=C.CFUNCTYPE(C.c_int,C.c_uint32,C.c_void_p,C.c_uint32)
FE=C.CFUNCTYPE(C.c_int,C.c_uint32)
NOW=C.CFUNCTYPE(C.c_uint32)
SEND=C.CFUNCTYPE(None,C.POINTER(C.c_uint8),C.c_uint16)
RESET=C.CFUNCTYPE(None)
PROGRESS=C.CFUNCTYPE(None,C.c_uint,C.c_uint)
class Platform(C.Structure):_fields_=[('version',C.c_uint32),('width',C.c_uint16),('height',C.c_uint16),('write',WR),('assets',READ),('read',FR),('save',FW),('erase',FE),('clock',NOW),('send',SEND),('reset',RESET),('progress',PROGRESS)]
class Event(C.Structure):_fields_=[('type',C.c_int),('now',C.c_uint32),('x',C.c_int16),('y',C.c_int16),('down',C.c_uint8),('byte',C.c_uint8)]
lib.display_init.argtypes=[C.POINTER(Platform)];lib.display_event.argtypes=[C.POINTER(Event)];lib.display_step.argtypes=[C.c_uint32]
class Panel(QWidget):
    def __init__(self):
        super().__init__();self.setWindowTitle('Music Motor · 480 × 320');self.setFixedSize(960,640)
        self.image=QImage(480,320,QImage.Format_RGB32);self.image.fill(0)
        self.clock=0;self.flash=bytes([255])*8;self.out=[];self.parser=Parser();self.actions=Actions(self.action)
        self.position=8000;self.playing=True;self.sleep=self.reset=self.micro=0;self.last_state=0;self.last_note=0
        self.motors=[dict(enabled=True,active=True,direction=False,note=69-i*4,frequency=440*2**(-i/3)) for i in range(6)]
        self.callbacks=(WR(self.write),READ(),FR(self.read),FW(self.save),FE(self.erase),NOW(lambda:self.clock),SEND(self.send),RESET(),PROGRESS())
        self.platform=Platform(2,320,480,*self.callbacks);lib.display_init(C.byref(self.platform));self.feed(title_packet('Композиция 01'));self.state()
        for t in range(0,8000,500):
            for i in range(6):
                pitch=76-i*5+[0,4,7,4,2,5,9,7][t//500%8]
                self.feed(encode(MIDI_EVENT,struct.pack('<IBBB',t,i,pitch,100)))
                self.feed(encode(MIDI_EVENT,struct.pack('<IBBB',t+350,i,pitch,0)))
        self.feed(encode(MIDI_CLOCK,struct.pack('<I',8000)));self.flush()
        if args.page!='overview':
            x,y={'midi':(125,290),'song':(210,290),'settings':(295,290),'motor':(45,70)}[args.page];self.touch(x,y,1);self.touch(x,y,0);self.flush()
        self.timer=QTimer(self);self.timer.timeout.connect(self.tick);self.timer.start(10)
    def write(self,x,y,w,h,pixels,stride,user):
        for b in range(h):
            for a in range(w):
                c=pixels[b*stride+a];r=((c>>11)&31)*255//31;g=((c>>5)&63)*255//63;blue=(c&31)*255//31
                self.image.setPixel(y+b,319-x-a,0xff000000|r<<16|g<<8|blue)
    def read(self,at,data,n):C.memmove(data,self.flash,n);return 1
    def save(self,at,data,n):self.flash=C.string_at(data,n);return 1
    def erase(self,at):self.flash=bytes([255])*8;return 1
    def send(self,data,n):self.out.append(C.string_at(data,n))
    def feed(self,data):
        for b in data:lib.display_event(C.byref(Event(1,self.clock,0,0,0,b)))
        lib.display_step(self.clock)
    def state(self):self.feed(state_packet(self.motors,self.position,93000,playing=self.playing,midi=True,sleep=self.sleep,reset=self.reset,micro=self.micro))
    def action(self,a,m,v):
        if m>5:return 2
        if a==0:self.playing=False;self.position=0;[motor.update(active=False) for motor in self.motors]
        elif a==1:
            self.playing=not self.playing
            for motor in self.motors:motor.update(active=self.playing,enabled=True)
        elif a==2:self.position=min(92999,v)
        elif a==3:self.motors[m]['frequency']=v/1000;self.motors[m]['note']=round(69+12*math.log2(v/440000))
        elif a==4:self.motors[m]['enabled']=bool(v);self.motors[m]['active']&=bool(v)
        elif a==5:self.motors[m].update(active=bool(v),enabled=True)
        elif a==6:self.motors[m]['direction']=bool(v)
        elif a==7:self.micro=v
        elif a==8:self.sleep=v;self.playing=False;[motor.update(active=False) for motor in self.motors]
        elif a==9:self.reset=1;self.playing=False;[motor.update(active=False,enabled=False) for motor in self.motors]
        elif a==10:self.reset=self.sleep=0
        else:return 2
        return 0
    def touch(self,x,y,down):lib.display_event(C.byref(Event(0,self.clock,319-y,x,down,0)))
    def mousePressEvent(self,e):self.touch(e.x()//2,e.y()//2,1)
    def mouseMoveEvent(self,e):
        if e.buttons()&Qt.LeftButton:self.touch(e.x()//2,e.y()//2,1)
    def mouseReleaseEvent(self,e):self.touch(e.x()//2,e.y()//2,0)
    def paintEvent(self,e):q=QPainter(self);q.drawImage(self.rect(),self.image)
    def flush(self):
        for _ in range(250):lib.display_step(self.clock)
    def tick(self):
        self.clock+=10
        if self.playing:self.position=(self.position+10)%93000
        for packet in self.out:
            for command,seq,payload in self.parser.feed(packet):
                if command==ACTION:self.feed(self.actions.receive(seq,payload))
        self.out.clear()
        if self.clock-self.last_state>=100:self.last_state=self.clock;self.state()
        for _ in range(12):lib.display_step(self.clock)
        self.update()
app=QApplication([]);panel=Panel()
if args.screenshot:
    panel.flush();panel.image.save(args.screenshot);print(args.screenshot)
else:
    panel.show();sys.exit(app.exec_())
