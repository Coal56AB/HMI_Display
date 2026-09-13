"""Exercise the serial scheduler with UART bandwidth and ACK latency."""
import struct
import sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "Simulator"))
from uart_server import Link
from uart_protocol import FIELDS, packet

class Port:
    def __init__(self):
        self.now = 0
        self.ready = 0
        self.response = b""
        self.division = 200
        self.parts = {}
        self.frames = []
        self.columns = 0
    @property
    def in_waiting(self):
        return len(self.response) if self.now >= self.ready else 0
    def read(self, size):
        result, self.response = self.response[:size], self.response[size:]
        return result
    def write(self, raw):
        kind, payload = raw[2], raw[4:-2]
        seq = struct.unpack_from("<I", payload)[0]
        if kind == 10:
            self.columns += 1
            assert 1 <= payload[5] <= 240

        if kind == 7:
            _, page, channel, start, count, scale, cursor, low, high = struct.unpack_from("<IBBBBBBhh", payload)
            if start == 0: self.parts[channel] = []
            assert len(self.parts[channel]) == start
            self.parts[channel].extend(v / 1000 for v in struct.unpack_from("<" + "h" * count, payload, 14))
            if start + count == 240:
                self.frames.append((self.now, channel, self.parts[channel][:]))
        self.response = packet(4, struct.pack("<IBBBI", seq, 0, 0, 3, self.division))
        self.ready = self.now + (len(raw) + len(self.response)) * 10 / 115200 + .003
        return len(raw)
    def close(self): pass

link, port = Link(), Port()
link.serial = port
now = 1.0
for division in (200, 50, 5):
    port.division = division
    end = now + 3
    next_snapshot = 0
    baseline = len(port.frames)
    while now < end:
        port.now = now
        if now >= next_snapshot:
            span = link.graph["division"] * .005
            points = [now - span + i * span / 239 for i in range(240)]
            link.exchange(dict(mode=0, selected=0, state=2, flags=0, time=now,
                values=dict.fromkeys(FIELDS, 0), graph=dict(page=0, division=link.graph["division"],
                channels=[points]*4, ranges=[[-10,30]]*4, scales=[1000]*4, cursor=240, bucket=int(now*240/span), revision=0)))
            link.last_exchange = now
            next_snapshot = now + .1
        link.pump(now)
        now += .005
    fresh = port.frames[baseline:]
    assert len(fresh) >= 4, (division, len(fresh), link.status)
    for channel in range(4):
        stamp, _, points = next(f for f in reversed(fresh) if f[1] == channel)
        assert abs(points[-1] - points[0] - division * .005) < .002
        assert 0 <= stamp - points[-1] < 1, "Serial queue replays stale history"
        assert all(a <= b for a,b in zip(points, points[1:])), "Repeated curve"
print("UART graph scheduler: fresh windows, 200/50/5 ms/div, bandwidth and ACK delay PASS")

from graph_stream import GraphStream
stream=GraphStream()
def data(bucket):
    return dict(page=0,revision=0,division=1000,bucket=bucket,
                channels=[[float(i) for i in range(240)]]*4,ranges=[[-1,240]]*4,scales=[10]*4)
assert stream.columns(data(200),budget=12) is None
updates=stream.columns(data(202),budget=12)
assert [c[1] for c in updates[-2:]] == [202,203]
assert stream.columns(data(202),budget=12)==[]
stream.columns(data(239))
updates=stream.columns(data(241))
assert [c[1] for c in updates[-2:]] == [1,2]
assert len(updates)==2
assert updates[1][2]==[1.0]*4
constant=data(250);constant["channels"]=[[311.0]*240]*4
assert all(c[2]==[311.0]*4 for c in stream.columns(constant))
print("Incremental columns, duplicate snapshots, wrap and no injected zero samples PASS")
