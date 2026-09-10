"""Protocol and real Studio integration smoke test; no serial hardware required."""
import importlib.util,os,sys,struct,time
from pathlib import Path
os.environ['QT_QPA_PLATFORM']='offscreen'
root=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(root/'DisplaySrc/Host'))
from protocol import Parser,Actions,encode,state_packet,title_packet,STATE,ACTION,ACK
p=Parser();frame=encode(STATE,bytes(range(50)),3)
assert not p.feed(frame[:12]);assert p.feed(frame[12:])==[(STATE,3,bytes(range(50)))]
bad=bytearray(frame);bad[-1]^=1
assert p.feed(bytes(bad)+frame)==[(STATE,3,bytes(range(50)))]
calls=[];actions=Actions(lambda a,m,v:calls.append((a,m,v)) or 0)
payload=struct.pack('<BBI',1,0,0)
assert actions.receive(7,payload)==actions.receive(7,payload) and len(calls)==1
assert Parser().feed(title_packet('Композиция '*20))[0][2].decode('utf8')
# Load the HMI adapter while its private wire module is still resolved.
from studio import StudioLink
sys.modules.pop('protocol',None)
sys.path.insert(0,str(root.parent/'desktop'))
from PySide2.QtWidgets import QApplication
from app.main_window import MainWindow
from protocol.wire import Command
import serial
class Port:
    def __init__(self,*a,**k):self.input=bytearray();self.output=bytearray()
    @property
    def in_waiting(self):return len(self.input)
    def read(self,n):b=bytes(self.input[:n]);del self.input[:n];return b
    def write(self,b):self.output.extend(b);return len(b)
    def close(self):pass
serial.Serial=Port
app=QApplication([]);window=MainWindow(simulation=True,settings_path=root/'.build/test-settings.json')
window.client.connect_device(simulation=True)
end=time.monotonic()+.3
while time.monotonic()<end:app.processEvents()
link=StudioLink(window,'test');link.poll()
frames=Parser().feed(link.serial.output);assert any(c==STATE and len(b)==50 for c,seq,b in frames)
link.serial.output.clear();link.serial.input.extend(encode(ACTION,struct.pack('<BBI',0,0,0),19));link.poll()
assert any(c==ACK and seq==19 and b==b'\0' for c,seq,b in Parser().feed(link.serial.output))
assert link.action(3,1,19999)==2 and link.action(6,1,2)==2
link.close();window.client.close()
print('PASS: UART framing/CRC, UTF8, retry dedup, actual MusicMotorStudio state and STOP ACK')
