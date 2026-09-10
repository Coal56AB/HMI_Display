import sys,os
from pathlib import Path
MODULE=Path(__file__).resolve().parents[1]
os.chdir(MODULE)
(MODULE/".build").mkdir(exist_ok=True)
sys.path.insert(0,str(MODULE/"Simulator"))
"""Runtime C rendering invariants, using the same DLL and assets as the desktop."""
import ctypes as C,struct,sys
from pathlib import Path
from uart_protocol import packet,graph_column
root=Path(__file__).resolve().parents[1]
l=C.CDLL(str(root/'Release/Native/hmi_renderer.dll'));l.renderer_init.argtypes=[C.c_char_p]
assert l.renderer_init(str(root/'Assets/hmi_assets.bin').encode())
def send(kind,payload,tick=1000):
 raw=packet(kind,payload);l.renderer_packet(raw,len(raw),tick)
def validate():
 at=l.renderer_validate_frame();assert not at,('frame mismatch',(at-1)%320,(at-1)//320)
for group in (0,1,2,3):
 for mask in (0,1,2,4,7,0,255):l.renderer_settings(group,mask);validate()
# State color changes must work independently of numeric readings / charging.
for state in (0,1,2,3,1,0):
 send(9,struct.pack('<I12B',1,*([state]*12)));validate()
send(2,struct.pack('<IBBBBI23f',1,5,0,0,8,0,*([0.0]*23)))
send(9,struct.pack('<I12B',1,3,1,*([0]*10)))
l.renderer_dialog(12);validate()
# A graph update may touch only its changed columns, apart from the 1 Hz statistics.
l.renderer_page(1)
for i in range(1,241):
 raw=graph_column(241-i,0,i,[(i%40-20)/10]*3+[311],[[-3,3]]*3+[[0,360]],[100,100,100,10])
 l.renderer_packet(raw,len(raw),1001)
 pixels=l.renderer_pixels()
 if i>1:assert pixels<=4*178,('too much plot output',i,pixels)
 at=l.renderer_validate_plot();assert not at,('plot mismatch',i,at%320,at//320)
print('Rendering PASS: hidden fields, four explicit states, dialogs, 240 columns; <= 712 pixels per live update after initialization')
