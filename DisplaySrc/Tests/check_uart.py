import sys,os
from pathlib import Path
MODULE=Path(__file__).resolve().parents[1]
os.chdir(MODULE)
(MODULE/".build").mkdir(exist_ok=True)
sys.path.insert(0,str(MODULE/"Simulator"))
"""Host integration check: Python packets -> real firmware C parser -> ACKs."""
import os, struct, subprocess
from pathlib import Path
from uart_protocol import packet, graph_chunk, Decoder, crc16
ROOT=Path(__file__).resolve().parents[1]
LIB=ROOT
def main():
    os.chdir(ROOT)
    build=ROOT/'.build';build.mkdir(exist_ok=True)
    source=build/'uart_parser_check.c'
    source.write_text(r'''#include "telemetry.h"
#include <assert.h>
#include <stdio.h>
#include <fcntl.h>
#include <io.h>
static unsigned resets;
void telemetry_reset(void){resets++;}
void telemetry_send(const uint8_t *p,uint16_t n){fwrite(p,1,n,stdout);}
int main(void){
 HmiUi ui;int c;unsigned now=0;_setmode(_fileno(stdin),_O_BINARY);_setmode(_fileno(stdout),_O_BINARY);
 hmi_ui_init(&ui,(HmiDisplay){0});telemetry_init(&ui);ui.state.param_section=HMI_PARAM_AUTO_RUNNING;
 while((c=getchar())!=EOF){telemetry_byte((uint8_t)c);telemetry_poll(&ui,++now);}
 assert(ui.state.param_section==HMI_PARAM_AUTO_DONE);assert(ui.auto_progress==100);assert(ui.parameters[HMI_VALUE_LM]==142);assert(ui.state.graph_cursor==120);
 assert(ui.state.pending_mask==3);assert(ui.state.pending_setpoints[0]==67);
 assert(ui.state.graph[0].sample_count==240);assert(ui.state.graph[0].samples[239]==239);
 assert(ui.state.graph_scale[0]==100);assert(resets==0);
 assert(ui.state.dc_bus_voltage==311);telemetry_poll(&ui,now+10001);
 assert(ui.state.power_state==HMI_POWER_READY);assert(ui.state.telemetry_flags&64);assert(!(ui.state.telemetry_flags&1));
 return 0;
}
''',encoding='utf-8')
    exe=build/'uart_parser_check.exe'
    env=os.environ.copy();env['PATH']='C:/mingw64/bin;'+env['PATH']
    subprocess.run(['C:/mingw64/bin/gcc.exe','-std=c99','-Os','-Wall','-Wextra','-Werror',
      '-I'+str(LIB/'Include'),'-I'+str(LIB/'ThirdParty/tinf'),'-IPlatform/Stm32',
      *map(str,(LIB/'Src').glob('*.c')),str(LIB/'ThirdParty/tinf/tinflate.c'),
      str(source),'-o',str(exe)],check=True,env=env)
    assert crc16(b'123456789')==0x29b1
    values=[0.0]*23;values[2]=311
    frames=[packet(2,struct.pack('<IBBBBI23f',1,2,0,0,28,100,*values)),
            packet(5,struct.pack('<IB3f',2,3,67,26,125))]
    for start in range(0,240,47):
      f=graph_chunk(3+start,0,0,start,[i/100 for i in range(start,min(start+47,240))],[-3,3],100,cursor=120)
      frames.extend([f,f])  # retries must be idempotent
    frames.extend([packet(8,struct.pack('<IBB5f',400,25,1,.84,.71,4.3,4.3,142)),packet(8,struct.pack('<IBB5f',401,100,2,.84,.71,4.3,4.3,142))])
    bad=bytearray(frames[0]);bad[-1]^=1
    stream=bytes(bad)+b'noise'+b''.join(frames)
    result=subprocess.run([str(exe)],input=stream,capture_output=True,env=env)
    assert result.returncode==0,result.stderr.decode(errors='replace')
    decoder=Decoder();acks=[]
    for byte in result.stdout:acks+=decoder.feed(bytes([byte]))
    assert len(acks)==len(frames),(len(acks),len(frames))
    assert all(kind==4 and data[4]==0 for kind,data in acks),acks
    print('UART PASS: CRC rejection, byte fragmentation, telemetry, drafts, 240-point chunks, duplicate ACK retry, timeout')
if __name__=='__main__':main()
