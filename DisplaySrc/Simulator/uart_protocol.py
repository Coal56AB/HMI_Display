"""PCH UART v1. Little endian; CRC-16/CCITT-FALSE, including header."""
import struct

FIELDS = ('mains_voltage mains_frequency dc_bus_voltage precharge_current '
          'precharge_seconds temperature_rectifier temperature_precharge temperature_dc '
          'temperature_inverter temperature_motor modulation_set modulation_actual '
          'rotation_set rotation_actual current_limit_set current_limit_actual '
          'output_voltage output_current output_power rotor_frequency stator_frequency '
          'slip motor_load').split()


def crc16(data):
    crc = 0xffff
    for byte in data:
        crc ^= byte << 8
        for _ in range(8):
            crc = ((crc << 1) ^ (0x1021 if crc & 0x8000 else 0)) & 0xffff
    return crc


def packet(kind, payload):
    data = bytes((0xa5, 0x5a, kind, len(payload))) + payload
    return data + struct.pack('<H', crc16(data))


class Decoder:
    def __init__(self):
        self.buffer = bytearray()
        self.errors = 0

    def feed(self, data):
        self.buffer.extend(data)
        result = []
        while len(self.buffer) >= 4:
            if self.buffer[:2] != b'\xa5\x5a':
                del self.buffer[0]
                continue
            kind, size = self.buffer[2:4]
            if (kind, size) not in ((1, 24), (4, 11)):
                del self.buffer[0]
                self.errors += 1
                continue
            length = size + 6
            if len(self.buffer) < length:
                break
            raw = self.buffer[:length]
            if crc16(raw[:-2]) != struct.unpack('<H', raw[-2:])[0]:
                del self.buffer[0]
                self.errors += 1
                continue
            result.append((kind, bytes(raw[4:-2])))
            del self.buffer[:length]
        return result


def telemetry(seq, model):
    state, flags, values = model.snapshot()
    # Match the panel's useful precision; avoid six-digit noise in its %g fields.
    for name, value in values.items():
        precision = 0 if name.startswith('temperature_') else 3 if name == 'output_power' else 2
        values[name] = round(value, precision)
    return packet(2, struct.pack('<IBBBBI23f', seq, state, model.mode,
                                model.selected, flags, int(model.time * 1000) & 0xffffffff,
                                *(values[k] for k in FIELDS)))


def graph(seq, page, channel, values, limits=None, scale=10):
    points = [max(-32768, min(32767, round(x * scale))) for x in values]
    low, high = min([0] + points), max([10] + points)
    margin = max(1, (high - low) // 10)
    low, high = max(-32768, low - margin), min(32767, high + margin)
    if limits:
        low, high = [max(-32768, min(32767, round(x * scale))) for x in limits]
    return packet(3, struct.pack('<IBBhhBB', seq, page, channel, low, high, len(points), {10:0,100:1,1000:2}[scale])
                  + struct.pack('<' + 'h' * len(points), *points))


def graph_chunk(seq, page, channel, start, values, limits, scale, cursor=0):
    points = [max(-32768, min(32767, round(x * scale))) for x in values]
    low, high = [max(-32768, min(32767, round(x * scale))) for x in limits]
    return packet(7, struct.pack('<IBBBBBBhh', seq, page, channel, start, len(points),
                                {10:0,100:1,1000:2}[scale], cursor, low, high)
                  + struct.pack('<' + 'h' * len(points), *points))


def graph_column(seq, page, cursor, values, ranges, scales):
    """One new live column, all channels; history windows use TYPE 7."""
    count=min(4,len(values));raw=bytearray(struct.pack('<IBBB',seq,page,cursor,count))
    for i in range(4):raw.append(2 if i<count and scales[i]==1000 else 1 if i<count and scales[i]==100 else 0)
    for i in range(4):
        scale=scales[i] if i<count else 10;limits=ranges[i] if i<count else [-1,1]
        lo=max(-32768,min(32766,round(limits[0]*scale)));hi=max(lo+1,min(32767,round(limits[1]*scale)))
        raw.extend(struct.pack('<hh',lo,hi))
    for i in range(4):raw.extend(struct.pack('<h',max(-32768,min(32767,round(values[i]*scales[i]))) if i<count else 0))
    return packet(10,raw)
