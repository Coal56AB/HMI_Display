"""Launch MusicMotorStudio with a physical touch panel on a second serial port.
The panel never opens the motor UART; the existing Client owns that connection.
"""
import argparse,struct,sys,time
from pathlib import Path
from protocol import Parser,Actions,ACTION,encode,state_packet,title_packet,MIDI_EVENT,MIDI_CLOCK

class StudioLink:
    def __init__(self,window,port):
        import serial
        from PySide2.QtCore import QTimer
        self.window=window
        self.serial=serial.Serial(port,115200,timeout=0,write_timeout=.05)
        self.parser=Parser();self.actions=Actions(self.action);self.last_state=0;self.last_title=None;self.last_title_at=0
        self.started=time.monotonic();self.notes=[None]*6;self.alive=True
        self.timer=QTimer(window);self.timer.timeout.connect(self.poll);self.timer.start(5)
    def write(self,data):
        if self.serial.write(data)!=len(data):raise OSError('Incomplete panel UART write')
    def action(self,a,m,v):
        from protocol.wire import Command as C
        w=self.window
        if a>10 or m>5:return 2
        if not w.client.connected:return 12
        if a==0:w.stop_all();return 0
        if a==1:w.toggle_main_playback();return 0
        if a==2:
            if not w.song.notes:return 10
            w.seek_main_cursor(v);return 0
        if w.player.state!='stopped':return 9
        if a in (3,4,5,6) and not w.config['installed_mask']&(1<<m):return 5
        status=w.latest_status or {}
        if a==5 and v:
            if status.get('reset'):return 3
            if status.get('sleep'):return 4
        if a==3:
            if not 20000<=v<=4000000:return 2
            w.manual_command(m,'frequency',v/1000)
        elif a in (4,5,6):
            if v>1:return 2
            if a==4:w.manual_command(m,'enable',v)
            elif a==6:w.manual_command(m,'direction',v)
            elif v:
                w.manual_command(m,'enable',True);w.manual_command(m,'start',None)
            else:w.manual_command(m,'stop',None)
        elif a==7:
            if v not in (0,1,2,3,7):return 2
            w.config['microstep_raw']=v;w.common_command(C.MICROSTEP,v)
        elif a==8:
            if v>1:return 2
            w.common_command(C.SLEEP,v)
        elif a==9:w.reset_drivers()
        elif a==10:w.activate_drivers()
        return 0
    def poll(self):
        if not self.alive:return
        try:
            for command,seq,payload in self.parser.feed(self.serial.read(min(4096,self.serial.in_waiting))):
                if command==ACTION:self.write(self.actions.receive(seq,payload))
            t=time.monotonic()
            if t-self.last_state<.1:return
            self.last_state=t;w=self.window;s=w.latest_status or {}
            motors=s.get('motors',[dict(enabled=False,active=False,direction=False,note=255,frequency=440) for _ in range(6)])
            position=w.player.position if w.player.state!='stopped' else w.main_cursor_ms
            duration=w.allocation.duration_ms if w.allocation else 0
            self.write(state_packet(motors,position,duration,connected=w.client.connected,playing=w.player.state in ('playing','preparing'),paused=w.player.state=='paused',midi=bool(w.allocation and w.client.connected),sleep=s.get('sleep',0),reset=s.get('reset',0),micro=s.get('raw',0),mask=w.config['installed_mask'],known_midi=bool(w.allocation and w.player.state in ('playing','preparing'))))
            title=getattr(w.song,'title','Композиция')
            if title!=self.last_title or t-self.last_title_at>=2:
                self.write(title_packet(title));self.last_title=title;self.last_title_at=t
            # Actual observed motor notes, no fabricated events. Also usable by a live MIDI adapter.
            at=int((t-self.started)*1000)&0xffffffff
            for i,m in enumerate(motors):
                pitch=m['note'] if m['active'] and w.client.connected and m['note']<128 else None
                if pitch!=self.notes[i]:
                    if self.notes[i] is not None:self.write(encode(MIDI_EVENT,struct.pack('<IBBB',at,i,self.notes[i],0)))
                    if pitch is not None:self.write(encode(MIDI_EVENT,struct.pack('<IBBB',at,i,pitch,100)))
                    self.notes[i]=pitch
            self.write(encode(MIDI_CLOCK,struct.pack('<I',at)))
        except Exception as exc:
            self.close();self.window.statusBar().showMessage('Панель: '+str(exc),15000)
    def close(self):
        self.alive=False;self.timer.stop();self.serial.close()

def main():
    parser=argparse.ArgumentParser();parser.add_argument('--display-port',required=True);parser.add_argument('--motor-port');parser.add_argument('--studio-dir',type=Path,default=Path(__file__).resolve().parents[3]);args=parser.parse_args()
    # Rename the local protocol import before adding Studio's protocol package.
    sys.path.insert(0,str(args.studio_dir/'desktop'))
    sys.modules.pop('protocol',None)
    from PySide2.QtWidgets import QApplication
    from app.main_window import MainWindow
    app=QApplication(sys.argv);window=MainWindow();link=StudioLink(window,args.display_port)
    if args.motor_port:window.client.connect_device(args.motor_port,115200,simulation=False)
    app.aboutToQuit.connect(link.close);window.show();return app.exec_()
if __name__=='__main__':sys.exit(main())

