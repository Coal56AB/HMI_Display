"""Desktop USB simulator and background serial transport for the ESP32 screen."""
import argparse
import base64
import struct
import threading
import time
import serial
from controller_simulator import Controller
from protocol import Parser, Actions, ACTION, title_packet
from live_midi import LiveMidi


class Session:
    def __init__(self):
        self.lock=threading.RLock();self.live=LiveMidi();self.model=Controller(self.live.event);self.parser=Parser()
        self.actions=Actions(self.action);self.port=None;self.port_name='';self.error=''
        self.tx=bytearray();self.reset_history=True;self.last_title=0;self.stopped=threading.Event()
        self.received_actions=0;self.last_action='';self.sent_bytes=0
        self.diagnostics=None;self.diagnostics_at=0
        self.link_diagnostics=None
        self.worker=threading.Thread(target=self.run,daemon=True);self.worker.start()

    def action(self,a,m,v):
        result=self.model.action(a,m,v)
        self.received_actions+=1;self.last_action='action=%s motor=%s value=%s → %s'%(a,m+1,v,result)
        if a in (1,2):self.reset_history=True
        return result

    def close_port(self):
        if self.port:
            try:
                # Finish any partial USB frame before the explicit disconnect.
                self.tx.extend(self.live.disconnect_packet())
                deadline=time.monotonic()+.25
                while self.tx and time.monotonic()<deadline:
                    n=self.port.write(self.tx[:1024]);del self.tx[:n]
            except Exception:
                pass # STM32's MIDI watchdog also stops on loss of the heartbeat.
            finally:self.port.close()
        self.port=None;self.port_name='';self.tx.clear()

    def connect(self,name):
        self.close_port()
        # Do not toggle BOOT/RESET when opening a USB device serial port.
        port=serial.Serial(port=None,baudrate=921600,timeout=0,write_timeout=.2)
        port.dtr=False;port.rts=False;port.port=name
        try:port.open()
        except Exception as exc:
            port.close()
            raise RuntimeError(f"Не удалось открыть {name}: {exc}. Закрой монитор порта и другие программы, использующие этот COM-порт.") from exc
        self.port=port;self.port_name=name;self.parser=Parser();self.actions=Actions(self.action)
        self.error='';self.reset_history=True;self.last_title=0
        self.live.begin();self.model.stop_notes()
        self.link_diagnostics=None
        self.diagnostics=None;self.diagnostics_at=0

    def run(self):
        last=time.monotonic();last_send=last
        while not self.stopped.wait(.005):
            now=time.monotonic();delta=round((now-last)*1000);last=now
            with self.lock:
                try:
                    self.model.advance(delta)
                    if not self.port:
                        self.model.changes=[];self.live.begin();continue
                    for command,seq,payload in self.parser.feed(self.port.read(min(4096,self.port.in_waiting))):
                        if command==ACTION:self.tx.extend(self.actions.receive(seq,payload))
                        elif command==0x57:self.live.ack(seq,payload,now)
                        elif command==0x60 and len(payload)==32:
                            self.diagnostics=struct.unpack('<8I',payload);self.diagnostics_at=now
                        elif command==0x61 and len(payload)==24:
                            self.link_diagnostics=struct.unpack('<6I',payload)
                    self.tx.extend(self.live.poll(now))
                    if self.model.changes or now-last_send>=.05:
                        self.tx.extend(self.model.packets(self.reset_history));self.reset_history=False;last_send=now
                    if now-self.last_title>=1:
                        self.tx.extend(title_packet(self.model.title));self.last_title=now
                    if len(self.tx)>32768:raise RuntimeError('USB не успевает принимать данные')
                    if self.tx:
                        n=self.port.write(self.tx[:1024]);del self.tx[:n];self.sent_bytes+=n
                except Exception as exc:
                    self.error=str(exc);self.close_port();self.model.playing=False
                    self.model.changes=[];self.model.stop_notes()

    def status(self):
        with self.lock:
            return dict(self.model.status(),port=self.port_name,error=self.error,
                        actions=self.received_actions,last_action=self.last_action,bytes=self.sent_bytes,
                        midi_ack=bool(self.port and self.live.acknowledged),link_diagnostics=self.link_diagnostics,
                        diagnostics=self.diagnostics,diagnostics_age=time.monotonic()-self.diagnostics_at)

    def command(self,data):
        with self.lock:
            cmd=data['command']
            if cmd=='connect':self.connect(str(data['port']))
            elif cmd=='disconnect':
                self.model.playing=False;self.model.stop_notes();self.close_port()
            elif cmd=='demo':self.model.demo();self.reset_history=True;self.last_title=0
            elif cmd=='midi':
                self.model.load_midi(base64.b64decode(data['data'],validate=True),str(data['name']))
                self.reset_history=True;self.last_title=0
            elif cmd=='play':self.model.action(1,0,0);self.reset_history=True
            elif cmd=='stop':self.model.action(0,0,0);self.reset_history=True
            elif cmd=='note':
                pitch=int(data['note'])
                if not 0<=pitch<=127:raise ValueError('Нота должна быть 0–127')
                self.model.note('on' if data['down'] else 'off',0,pitch,100 if data['down'] else 0)
            else:raise ValueError('Неизвестная команда')

    def close(self):
        self.stopped.set();self.worker.join(2)
        with self.lock:self.close_port()


def main():
    import sys
    from PySide6.QtWidgets import QApplication
    from usb_simulator_window import SimulatorWindow
    parser=argparse.ArgumentParser()
    parser.add_argument('--port')
    args=parser.parse_args()
    app=QApplication(sys.argv[:1])
    app.setApplicationName('USB — Симулятор контроллера')
    session=Session()
    try:
        window=SimulatorWindow(session,args.port)
        window.show()
        return app.exec()
    finally:session.close()

if __name__=='__main__':raise SystemExit(main())
