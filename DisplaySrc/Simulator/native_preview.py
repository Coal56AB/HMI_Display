import ctypes as C
import json,struct,time
from pathlib import Path
from PySide6 import QtCore,QtGui,QtWidgets
from uart_protocol import telemetry,packet,graph_chunk,graph_column
from uart_server import Snapshot
class Event(C.Structure):
    _fields_=[('sequence',C.c_uint32),('type',C.c_int),('id',C.c_uint16),('value',C.c_float),('flags',C.c_uint32),('data',C.c_uint8*8)]
class NativePreview(QtWidgets.QWidget):
    def __init__(self,desk):
        super().__init__();self.desk=desk;self.image=None;self.seq=0;self.last_clock='';self.graph_key=None;self.cursor=1;self.capture=None
        import sys
        root=Path(getattr(sys,'_MEIPASS',Path(__file__).resolve().parents[1]))
        dll=root/'hmi_renderer.dll' if hasattr(sys,'_MEIPASS') else root/'Release/Native/hmi_renderer.dll'
        assets=root/'hmi_assets.bin' if hasattr(sys,'_MEIPASS') else root/'Assets/hmi_assets.bin'
        self.lib=C.CDLL(str(dll));self.lib.renderer_init.argtypes=[C.c_char_p];self.lib.renderer_frame.restype=C.POINTER(C.c_uint16)
        if not self.lib.renderer_init(str(assets).encode()):raise RuntimeError('C renderer: incompatible assets')
        self.setMinimumSize(400,600);self.setMouseTracking(True)
    def send(self,raw):
        self.lib.renderer_packet(raw,len(raw),int(time.monotonic()*1000)&0xffffffff)
    def update_snapshot(self,s):
        self.seq+=1;self.send(telemetry(self.seq,Snapshot(s)))
        self.send(packet(5,struct.pack('<IB3f',self.seq,s['pendingMask']|(s['warningMask']<<3),*s['pending'])))
        self.send(packet(9,struct.pack('<I12B',self.seq,*s['visual'])))
        g=s['graph'];self.send(packet(11,struct.pack('<II',self.seq,g.get('offsetMs',0))));key=(g['page'],g.get('revision'),g.get('division'))
        if self.lib.renderer_graph()&512 and ((self.lib.renderer_graph()&256) or key!=self.graph_key) and g.get('division')==self.lib.renderer_division():
            if self.lib.renderer_graph()&256:
                if self.capture is None or self.cursor>=240 or self.capture['page']!=g['page'] or self.capture.get('revision')!=g.get('revision') or self.capture.get('division')!=g.get('division'):
                    self.capture=g;self.cursor=1
                else:self.cursor+=1
                capture=self.capture;point=self.cursor-1
                self.send(graph_column(self.seq,capture['page'],self.cursor,[v[point] for v in capture['channels']],capture['ranges'],capture['scales']))
            else:
                for ch,values in enumerate(g['channels']):
                    for start in range(0,240,47):self.send(graph_chunk(self.seq,g['page'],ch,start,values[start:start+47],g['ranges'][ch],g['scales'][ch],g['cursor']))
            self.graph_key=key
        a=s.get('auto',{})
        if a.get('active') or a.get('done'):self.send(packet(8,struct.pack('<IBB5f',self.seq,a['progress'],1 if a['active'] else 2,*a['values'])))
        clock=time.strftime('%H:%M')
        if clock!=self.last_clock:self.last_clock=clock;self.lib.renderer_clock(clock.encode())
        self.image=QtGui.QImage(C.string_at(self.lib.renderer_frame(),320*480*2),320,480,640,QtGui.QImage.Format.Format_RGB16).copy();self.update()
        e=Event()
        while self.lib.renderer_event(C.byref(e)):
            data=dict(sequence=e.sequence,type=e.type,id=e.id,value=e.value,flags=e.flags,data=list(e.data))
            self.desk.js('window.PchUart.fieldEvent('+json.dumps(data)+')')
        if not self.desk.link.serial:
            value=self.lib.renderer_graph()
            self.desk.link.graph=dict(page=value&255,flags=(value>>8)&3,division=self.lib.renderer_division())
    def target(self):
        scale=min(self.width()/320,self.height()/480)
        return QtCore.QRectF((self.width()-320*scale)/2,(self.height()-480*scale)/2,320*scale,480*scale)
    def paintEvent(self,e):
        p=QtGui.QPainter(self);p.fillRect(self.rect(),QtGui.QColor('#080f13'))
        if self.image:p.drawImage(self.target(),self.image)
    def touch(self,e,down):
        r=self.target();x=int((e.position().x()-r.x())*320/r.width());y=int((e.position().y()-r.y())*480/r.height())
        self.lib.renderer_touch(x,y,down,int(time.monotonic()*1000)&0xffffffff)
        self.image=QtGui.QImage(C.string_at(self.lib.renderer_frame(),320*480*2),320,480,640,QtGui.QImage.Format.Format_RGB16).copy();self.update()
    def mousePressEvent(self,e):self.touch(e,1)
    def mouseReleaseEvent(self,e):self.touch(e,0)
