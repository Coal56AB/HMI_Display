"""Editor session using the same control desk and model as the standalone simulator."""
import json
from .desktop_simulator import Desk
from .native_preview import NativePreview
from .editor_runtime import PchRuntime


class Session:
    def __init__(self, context):
        self.runtime=PchRuntime(context.module,context)
        self.paused=False;self.busy=False;self.closed=False
        self.pending_ms=0
        try:
            self.desk=Desk(lambda desk:NativePreview(desk,self.runtime),embedded=True)
        except Exception:
            self.runtime.close()
            raise
        self.widget=self.desk.takeCentralWidget()
        self.widget.setParent(None)
        self.preview=self.desk.preview
        self.widget.setMinimumWidth(490)

    def set_paused(self, paused):
        self.paused=paused
        self.widget.setEnabled(not paused)

    def advance(self, milliseconds, connected):
        if self.closed or self.paused:return
        self.runtime.advance(milliseconds)
        self.pending_ms+=milliseconds
        if not self.desk.ready or self.busy:return
        self.busy=True
        milliseconds=self.pending_ms
        self.pending_ms=0
        flags=self.runtime.lib.renderer_graph()
        request=dict(page=flags&255,flags=(flags>>8)&3,division=self.runtime.lib.renderer_division())
        code='window.PchUart.setRequest('+json.dumps(request)+');window.PchHostClock.step('+str(milliseconds)+');JSON.stringify({snapshot:window.PchUart.snapshot(),values:{state:Sim.chargeState,run:Sim.inverterOn||window.PchUart.activeDischarge,dc:Sim.dc,t:Sim.chargeElapsed,selected:Sim.selected}})'
        def done(text):
            self.busy=False
            if self.closed or self.paused or not text:return
            data=json.loads(text)
            self.desk.values(json.dumps(data['values']))
            self.desk.status.setText('Встроенный экран: связь включена' if connected else 'Встроенный экран: связь разорвана')
            if connected:self.preview.update_snapshot(data['snapshot'])
            else:self.runtime.events()
        self.desk.js(code,done)

    def close(self):
        if self.closed:return
        self.closed=True
        self.desk.close()
        self.preview.deleteLater()
        self.widget.deleteLater()
        self.desk.deleteLater()
        self.runtime.close()


def create_session(context):
    return Session(context)
