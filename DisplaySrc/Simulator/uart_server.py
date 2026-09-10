"""Local UART gateway for the user's original HTML simulator (Python + pyserial)."""
import argparse
from collections import deque
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import json
import math
from pathlib import Path
import struct
import threading
import time
import webbrowser
from uart_protocol import Decoder, FIELDS, packet, telemetry, graph, graph_chunk, graph_column

ROOT = Path(__file__).resolve().parent


class Snapshot:
    def __init__(self, data):
        self.data = data
        self.mode, self.selected, self.time = int(data['mode']), int(data['selected']), float(data['time'])

    def snapshot(self):
        return int(self.data['state']), int(self.data['flags']), dict(self.data['values'])


class Link:
    def __init__(self):
        self.lock = threading.RLock()
        self.serial = None
        self.decoder = Decoder()
        self.latest = None
        self.last_exchange = 0
        self.pending = None
        self.sequence = self.acks = 0
        self.jobs = deque()
        self.events = []
        self.status = 'Локальная симуляция · UART не подключён'
        self.graph = dict(page=0, flags=1, division=200)
        self.sweep_frame = None
        self.sweep_cursor = 1
        self.graph_offset = None
        self.graph_revision = None
        self.next_cycle = 0
        self.last_telemetry = 0
        self.last_auto = 0
        self.last_clock = 0
        self.last_kind = ''
        self.alive = True

    def connect(self, port):
        import serial
        with self.lock:
            self.disconnect()
            self.serial = serial.Serial(port, 115200, timeout=0, write_timeout=.1)
            self.serial.reset_input_buffer()
            self.decoder = Decoder()
            self.status = f'{port}: ожидание ответа дисплея'

    def disconnect(self):
        with self.lock:
            if self.serial:
                self.serial.close()
            self.serial = self.pending = None
            self.jobs.clear()
            self.status = 'Локальная симуляция · UART не подключён'

    def exchange(self, data):
        values = data.get('values', {})
        if set(FIELDS) - values.keys() or any(not isinstance(values[k], (int, float)) or
                                              not math.isfinite(values[k]) or abs(values[k]) > 100000 for k in FIELDS):
            raise ValueError('Invalid telemetry values')
        if data.get('mode') not in (0, 1, 2) or data.get('selected') not in (0, 1, 2) or data.get('state') not in range(6):
            raise ValueError('Invalid state')
        with self.lock:
            self.latest = data
            self.last_exchange = time.monotonic()
            result = dict(connected=self.serial is not None, status=self.status, acks=self.acks,
                          graph=self.graph.copy(), events=self.events[:])
            self.events.clear()
            return result

    def pump(self, now):
        with self.lock:
            if not self.serial:
                return
            try:
                for kind, payload in self.decoder.feed(self.serial.read(min(4096, self.serial.in_waiting))):
                    if kind == 1:
                        seq, event, _, ident, value, flags, raw = struct.unpack('<IBBHfI8s', payload)
                        self.events.append(dict(sequence=seq, type=event, id=ident, value=value, flags=flags, data=list(raw)))
                        self.events = self.events[-128:]
                    else:
                        seq, status, page, flags, division = struct.unpack('<IBBBI', payload)
                        if self.pending and seq == self.pending[0]:
                            self.pending = None
                            self.acks += 1
                            if 0 <= page < 5 and 2 <= division <= 5000:
                                if page != self.graph['page'] or division != self.graph['division'] or flags != self.graph['flags']:
                                    self.jobs.clear()
                                    stopping = (self.graph['flags'] & 1) and not (flags & 1) and page == self.graph['page'] and division == self.graph['division']
                                    if not stopping:self.graph_revision = None
                                    self.sweep_frame = None
                                self.graph = dict(page=page, flags=flags, division=division)
                            self.status = f'На связи · ACK {self.acks} · CRC ошибок {self.decoder.errors}'
                            if status == 1:
                                self.status = 'Дисплей отклонил пакет: неверные поля'
                if self.pending:
                    if now - self.pending[2] >= 2:
                        if self.pending[3] >= 3:
                            raise OSError('Нет ответа дисплея')
                        if self.serial.write(self.pending[1]) != len(self.pending[1]):
                            raise OSError('Неполная запись UART')
                        self.pending[2], self.pending[3] = now, self.pending[3] + 1
                    return
                if not self.latest or now - self.last_exchange > 1:
                    self.jobs.clear()
                    return
                if not self.jobs and now >= self.next_cycle:
                    if now-self.last_telemetry>=.25:self.jobs.extend([('telemetry', None), ('visual', None)])
                    g = self.latest.get('graph', {})
                    if (self.graph['flags'] & 2) and ((self.graph['flags'] & 1) or g.get('revision') != self.graph_revision) and g.get('page') == self.graph['page'] and g.get('division',self.graph['division'])==self.graph['division']:
                        if self.graph_revision!=g.get('revision'):self.sweep_frame=None
                        self.graph_revision = g.get('revision')
                        if self.graph_offset!=g.get('offsetMs',0):
                            self.graph_offset=g.get('offsetMs',0);self.jobs.append(('axis',self.graph_offset))
                        if self.graph['flags'] & 1:
                            if self.sweep_frame is None or self.sweep_cursor>=240:
                                self.sweep_frame=g;self.sweep_cursor=1
                            else:self.sweep_cursor+=1
                            capture=self.sweep_frame;point=self.sweep_cursor-1
                            self.jobs.append(('column',(capture['page'],self.sweep_cursor,[v[point] for v in capture['channels']],capture['ranges'],capture['scales'])))
                        else:
                            for ch, values in enumerate(g.get('channels', [])[:4]):
                                if len(values) == 240:
                                    for start in range(0, 240, 47):
                                        self.jobs.append(('chunk', (g['page'], ch, start, values[start:start+47], g['ranges'][ch], g['scales'][ch], g.get('cursor', 0))))
                                elif len(values) <= 48:
                                    self.jobs.append(('graph', (g['page'], ch, values, g['ranges'][ch], g['scales'][ch])))
                    self.next_cycle = now + .005
                if not self.jobs:
                    return
                self.sequence = (self.sequence + 1) & 0xffffffff
                if now-self.last_clock >= 10:
                    kind, payload = 'clock', None
                elif self.latest.get('auto', {}).get('active', False) and now-self.last_auto>=.5 or self.latest.get('auto', {}).get('done', False) and now-self.last_auto>=1:
                    kind,payload='auto',self.latest['auto']
                elif self.last_kind not in ('telemetry', 'clock') and now-self.last_telemetry >= .25:
                    kind, payload = 'telemetry', None
                elif self.last_kind == 'telemetry':
                    kind, payload = 'draft', None
                else:
                    kind, payload = self.jobs.popleft()
                self.last_kind = kind
                if kind == 'clock':
                    self.last_clock = now
                    raw = packet(6, struct.pack('<III', self.sequence, int(time.time()-time.timezone), 0))
                elif kind == 'auto':
                    self.last_auto=now
                    raw=packet(8,struct.pack('<IBB5f',self.sequence,int(payload['progress']),1 if payload.get('active') else 2,*payload['values']))
                elif kind == 'telemetry':
                    self.last_telemetry = now
                    raw = telemetry(self.sequence, Snapshot(self.latest))
                elif kind == 'draft':
                    pending = self.latest.get('pending', [self.latest['values'][name] for name in
                                                          ('modulation_set', 'rotation_set', 'current_limit_set')])
                    raw = packet(5, struct.pack('<IB3f', self.sequence, self.latest.get('pendingMask', 0) | (self.latest.get('warningMask', 0)<<3), *pending))
                elif kind == 'visual':
                    raw = packet(9, struct.pack('<I12B', self.sequence, *self.latest.get('visual', [0]*12)))
                elif kind == 'axis':
                    raw=packet(11,struct.pack('<II',self.sequence,payload))
                elif kind == 'column':
                    raw = graph_column(self.sequence,*payload)
                elif kind == 'chunk':
                    raw = graph_chunk(self.sequence, *payload)
                else:
                    raw = graph(self.sequence, *payload)
                if self.serial.write(raw) != len(raw):
                    raise OSError('Неполная запись UART')
                self.pending = [self.sequence, raw, now, 0]
            except Exception as exc:
                self.disconnect()
                self.status = 'UART: ' + str(exc)

    def run(self):
        while self.alive:
            self.pump(time.monotonic())
            time.sleep(.002)


def handler(link):
    class Handler(BaseHTTPRequestHandler):
        def log_message(self, *_):
            pass

        def reply(self, value, status=200):
            data = json.dumps(value, ensure_ascii=False).encode('utf-8')
            self.send_response(status)
            self.send_header('Content-Type', 'application/json; charset=utf-8')
            self.send_header('Content-Length', str(len(data)))
            self.end_headers()
            self.wfile.write(data)

        def do_GET(self):
            if self.path == '/api/ports':
                try:
                    from serial.tools.list_ports import comports
                    self.reply([dict(port=p.device, label=p.description) for p in comports()])
                except ImportError:
                    self.reply({'error': 'Установите pyserial: py -3 -m pip install pyserial==3.5'}, 500)
                return
            path = self.path.split('?')[0]
            files = {'/': ('uart_simulator.html', 'text/html; charset=utf-8'),
                     '/uart_bridge.js': ('uart_bridge.js', 'text/javascript; charset=utf-8')}
            if path not in files:
                self.reply({'error': 'Not found'}, 404)
                return
            name, mime = files[path]
            data = (ROOT / name).read_bytes()
            if name.endswith('.html'):
                data = data.replace(b'</body>', b'<script src="/uart_bridge.js"></script></body>')
            self.send_response(200)
            self.send_header('Content-Type', mime)
            self.send_header('Cache-Control', 'no-store')
            self.send_header('Content-Length', str(len(data)))
            self.end_headers()
            self.wfile.write(data)

        def do_POST(self):
            expected = 'http://' + self.headers.get('Host', '')
            if self.headers.get('Origin', expected) != expected or 'application/json' not in self.headers.get('Content-Type', ''):
                self.reply({'error': 'Invalid origin/content type'}, 403)
                return
            try:
                size = int(self.headers.get('Content-Length', 0))
                if not 0 < size <= 100000:
                    raise ValueError('Invalid body size')
                data = json.loads(self.rfile.read(size))
                if self.path == '/api/connect':
                    link.connect(data['port'])
                    self.reply({'ok': True})
                elif self.path == '/api/disconnect':
                    link.disconnect()
                    self.reply({'ok': True})
                elif self.path == '/api/exchange':
                    self.reply(link.exchange(data))
                else:
                    self.reply({'error': 'Not found'}, 404)
            except Exception as exc:
                self.reply({'error': str(exc)}, 400)
    return Handler


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--port', type=int, default=0)
    parser.add_argument('--no-browser', action='store_true')
    args = parser.parse_args()
    link = Link()
    server = ThreadingHTTPServer(('127.0.0.1', args.port), handler(link))
    address = f'http://127.0.0.1:{server.server_port}/'
    output = ROOT.parent / '.build'
    output.mkdir(exist_ok=True)
    (output / 'uart-terminal-server.json').write_text(json.dumps({'url': address}), encoding='utf-8')
    threading.Thread(target=link.run, daemon=True).start()
    print(address, flush=True)
    if not args.no_browser:
        webbrowser.open(address)
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        link.alive = False
        link.disconnect()
        server.server_close()
