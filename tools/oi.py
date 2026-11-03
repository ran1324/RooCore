"""OI stream decoder. Packet definitions from iRobot's Create 2 OI spec."""
import struct

FIELDS = {
    7: ('bumps_wheeldrops',1,False), 8: ('wall',1,False),
    9: ('cliff_left',1,False), 10: ('cliff_front_left',1,False),
    11: ('cliff_front_right',1,False), 12: ('cliff_right',1,False),
    13: ('virtual_wall',1,False), 14: ('overcurrents',1,False),
    15: ('dirt_detect',1,False), 17: ('infrared',1,False),
    18: ('buttons',1,False), 21: ('charging_state',1,False),
    22: ('voltage_mv',2,False), 23: ('current_ma',2,True),
    24: ('temperature_c',1,True), 25: ('charge_mah',2,False),
    26: ('capacity_mah',2,False), 35: ('oi_mode',1,False),
    43: ('encoder_left',2,False), 44: ('encoder_right',2,False),
}
HEADER = struct.Struct('!4sIIII')

def delta16(new, old):
    return (new - old + 32768) % 65536 - 32768

class Stream:
    def __init__(self):
        self.buffer = bytearray()
        self.errors = 0

    def feed(self, data):
        self.buffer.extend(data)
        frames = []
        while self.buffer:
            if self.buffer[0] != 19:
                del self.buffer[0]
                continue
            if len(self.buffer) < 2:
                break
            # These profiles cannot produce more than 64 payload bytes.
            length = self.buffer[1]
            if not 1 <= length <= 64:
                self.errors += 1
                del self.buffer[0]
                continue
            total = length + 3
            if len(self.buffer) < total:
                break
            raw = bytes(self.buffer[:total])
            if sum(raw) % 256:
                self.errors += 1
                del self.buffer[0]
                continue
            values, pos = {}, 2
            try:
                while pos < total-1:
                    name, size, signed = FIELDS[raw[pos]]
                    pos += 1
                    if pos+size > total-1 or name in values:
                        raise ValueError('Invalid packet layout')
                    values[name] = int.from_bytes(raw[pos:pos+size], 'big', signed=signed)
                    pos += size
            except (KeyError, ValueError):
                self.errors += 1
            else:
                frames.append((raw.hex(), values))
            del self.buffer[:total]
        return frames

class Receiver:
    def __init__(self, session):
        self.session = session
        self.last_seq = None
        self.last_overruns = 0
        self.stream = Stream()
        self.segment = 0
        self.gaps = 0

    def feed(self, packet):
        if len(packet) < HEADER.size:
            raise ValueError('Short transport header')
        magic, session, seq, ms, overruns = HEADER.unpack(packet[:HEADER.size])
        if magic != b'RC01' or session != self.session:
            raise ValueError('Wrong protocol or session')
        if self.last_seq is not None:
            distance = (seq-self.last_seq) & 0xffffffff
            if distance == 0 or distance > 0x7fffffff:
                return []  # Duplicate or reordered UDP datagram.
            missing = distance-1
        else:
            missing = seq
        if missing or overruns != self.last_overruns:
            self.gaps += missing
            self.segment += 1
            self.stream.buffer.clear()
        self.last_seq, self.last_overruns = seq, overruns
        return [dict(device_ms=ms, seq=seq, segment=self.segment,
                     raw_frame=raw, **values)
                for raw, values in self.stream.feed(packet[HEADER.size:])]

class CaptureDecoder:
    """Decode v2 TCP records and never bridge a disconnect, reboot or loss event."""
    def __init__(self):
        self.stream = Stream()
        self.key = None
        self.last_seq = None
        self.overruns = 0
        self.dropped = 0
        self.segment = 0
        self.missing_records = 0

    def disconnect(self):
        self.stream.buffer.clear()
        self.key = None
        self.last_seq = None
        self.segment += 1

    @staticmethod
    def uint(record, name):
        value = record.get(name)
        if type(value) is not int or not 0 <= value <= 0xffffffff:
            raise ValueError('Invalid ' + name)
        return value

    def feed(self, record):
        kind = record.get('type')
        if kind not in ('serial','status'):
            raise ValueError('Unknown record type')
        boot, capture, ms, overruns, dropped = [self.uint(record,k) for k in
            ('boot','capture','ms','overruns','dropped')]
        data = b''
        seq = None
        if kind == 'serial':
            seq = self.uint(record,'seq')
            raw = record.get('hex')
            if not isinstance(raw,str) or not 2 <= len(raw) <= 512 or len(raw)%2:
                raise ValueError('Invalid hex length')
            try:
                data = bytes.fromhex(raw)
            except ValueError:
                raise ValueError('Invalid hex data') from None
            if len(data)*2 != len(raw):
                raise ValueError('Whitespace in hex data')
        key = boot,capture
        if key != self.key:
            self.disconnect()
            self.key = key
            self.overruns, self.dropped = 0,0
        gap = overruns != self.overruns or dropped != self.dropped
        self.overruns, self.dropped = overruns,dropped
        if seq is not None:
            distance = ((seq-self.last_seq)&0xffffffff) if self.last_seq is not None else seq+1
            if distance == 0 or distance > 0x7fffffff:
                raise ValueError('Duplicate or backwards sequence')
            self.missing_records += distance-1
            gap = gap or distance != 1
            self.last_seq = seq
        if gap:
            self.segment += 1
            self.stream.buffer.clear()
        before_errors = self.stream.errors
        frames = self.stream.feed(data)
        if before_errors != self.stream.errors:
            self.segment += 1
        return [dict(boot=boot,capture=capture,device_ms=ms,seq=seq,
                     segment=self.segment,raw_frame=raw,**values) for raw,values in frames]
