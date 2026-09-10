import sys,os
from pathlib import Path
MODULE=Path(__file__).resolve().parents[1]
os.chdir(MODULE)
(MODULE/".build").mkdir(exist_ok=True)
sys.path.insert(0,str(MODULE/"Simulator"))
"""Regression coverage for the review: stop/freeze, late columns, date cancel and ROM bounds."""
import ctypes as C,struct
from pathlib import Path
from uart_protocol import packet,graph_column
root=Path(__file__).resolve().parents[1]
l=C.CDLL(str(root/'Release/Native/hmi_renderer.dll'));l.renderer_init.argtypes=[C.c_char_p]
assert l.renderer_init(str(root/'Assets/hmi_assets.bin').encode())
def send(t,p):
 raw=packet(t,p);l.renderer_packet(raw,len(raw),1000)
def state(run):send(2,struct.pack('<IBBBBI23f',1,3 if run else 2,0,0,25 if run else 24,0,*([0.0]*23)))
def column(c):
 raw=graph_column(c,0,c,[1,2,3,311],[[-6,6]]*3+[[0,360]],[100]*3+[10]);l.renderer_packet(raw,len(raw),1001)
state(True);l.renderer_page(1)
for c in range(1,121):
 column(c);assert l.renderer_cursor()==c
state(False)
assert not l.renderer_graph()&256 and l.renderer_valid_count()==120
assert l.renderer_validate_frame()==0
column(121)
assert l.renderer_cursor()==120 and l.renderer_valid_count()==120, 'late packet moved frozen graph'
send(11,struct.pack('<II',9,500))
assert l.renderer_offset()==500 and l.renderer_validate_frame()==0
state(True)
assert l.renderer_graph()&256 and l.renderer_valid_count()==0 and l.renderer_cursor()==0
l.renderer_page(3);l.renderer_action(23,610);l.renderer_action(23,612);l.renderer_action(9,0)
assert l.renderer_validate_frame()==0
l.renderer_action(23,611);assert l.renderer_validate_frame()==0
l.renderer_action(4,0);l.renderer_dialog(5);assert l.renderer_validate_frame()==0
print('Review PASS: left-to-right cursor, stop trims stale right side, late packets ignored, restart clears, date cancel and ROM redraw')
