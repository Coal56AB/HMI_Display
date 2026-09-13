import sys,os
from pathlib import Path
MODULE=Path(__file__).resolve().parents[1]
os.chdir(MODULE)
(MODULE/".build").mkdir(exist_ok=True)
sys.path.insert(0,str(MODULE/"Simulator"))
import os,sys,json
from pathlib import Path
os.environ['QT_QPA_PLATFORM']='offscreen'
os.environ['QTWEBENGINE_CHROMIUM_FLAGS']='--disable-gpu'
sys.path.insert(0,str(Path('tools').resolve()))
from desktop_simulator import Desk
from PySide6 import QtWidgets,QtCore,QtGui
app=QtWidgets.QApplication([]);QtGui.QFontDatabase.addApplicationFont("C:/Windows/Fonts/segoeui.ttf");app.setFont(QtGui.QFont("Segoe UI",10));w=Desk();w.show();result={'done':False}
def finish(ok):
 print('Desktop smoke',ok,flush=True);result['done']=bool(ok);w.close();app.quit()
def after_load():
 w.js('JSON.stringify({state:Sim.chargeState,dc:Sim.dc,discharge:document.getElementById("uart-active-discharge").checked})',check)
def check(text):
 print('Initial',text,flush=True)
 if not text or (json.loads(text)['state']!='discharged' or not json.loads(text)['discharge']):finish(False);return
 w.grab().save(str(Path('.build/desktop-review.png').resolve()))
 w.js('Sim.mainsVoltage=190;Sim.publishTelemetry();JSON.stringify(window.PchUart.snapshot())',warning)
def warning(text):
 data=json.loads(text);assert data['visual'][0]==2 and data['state']==0
 w.js('Sim.mainsFrequency=60;Sim.publishTelemetry();JSON.stringify(window.PchUart.snapshot())',frequency_fault)
def frequency_fault(text):
 data=json.loads(text);assert data['state']==5 and data['visual'][1]==3 and not any(data['visual'][2:])
 w.js('Sim.mainsVoltage=220;Sim.mainsFrequency=50;Sim.toggleCharge()');QtCore.QTimer.singleShot(2600,charged)
def charged():
 w.js('JSON.stringify({state:Sim.chargeState,dc:Sim.dc})',verify)
def verify(text):
 print('Charged',text,flush=True)
 if not text or json.loads(text)['state']!='charged':finish(False);return
 w.js('Sim.toggleInverter();Encoder.next();JSON.stringify({run:Sim.inverterOn,selected:Sim.selected})',running)
def running(text):
 print('Running/encoder',text,flush=True)
 if not text or not json.loads(text)['run'] or json.loads(text)['selected']!='freq':finish(False);return
 QtCore.QTimer.singleShot(2500,stop_history)

def stop_history():
 w.js('Sim.toggleInverter()')
 w.preview.lib.renderer_page(1)
 w.preview.lib.renderer_touch(270,315,1,1000)
 w.preview.lib.renderer_touch(270,315,0,1100)
 QtCore.QTimer.singleShot(250,pan_history)

def pan_history():
 w.js('window.PchUart.fieldEvent({type:3,id:3,value:-1});JSON.stringify({offset:window.PchUart.snapshot().graph.offsetMs,collecting:Graph.collecting})',verify_history)

def verify_history(text):
 data=json.loads(text);print('History half-window',data,flush=True)
 assert data['offset']==500 and not data['collecting']
 w.js('Sim.toggleInverter();Sim.mainsVoltage=0;Sim.step(.1);Sim.publishTelemetry();JSON.stringify({run:Sim.inverterOn,fault:window.PchUart.snapshot().state})',faulted)
def faulted(text):
 print('Mains loss',text,flush=True)
 finish(bool(text) and not json.loads(text)['run'] and json.loads(text)['fault']==5)
QtCore.QTimer.singleShot(2500,after_load);QtCore.QTimer.singleShot(12000,lambda:finish(False))
app.exec();raise SystemExit(0 if result['done'] else 1)
