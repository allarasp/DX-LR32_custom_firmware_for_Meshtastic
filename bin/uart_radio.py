#!/usr/bin/env python3
"""DX-LR32 binary UART controller; standard library only, never starts RF."""
import argparse
import binascii
import fcntl
import json
import os
from pathlib import Path
import select
import struct
import termios
import time

VERSION = 1
MAX_PAYLOAD = 272
COMMANDS = dict(version=1, caps=2, status=3, frequency=4, lora=5, packet=6,
                power=7, apply=8, start_rx=9, stop_rx=10, send=11, errors=12,
                reset=13, config=14, mode=15, stats=16, readback=17, xosc=18, rf_switch=19, ping=20, arm_tx=21, radio_stats=22, transport_stats=23)
STATUS = ('OK', 'VERSION', 'FRAME', 'CRC', 'TIMEOUT', 'UNKNOWN', 'LENGTH',
          'RANGE', 'COMBINATION', 'UNSUPPORTED', 'RADIO_ERROR', 'NOT_READY',
          'READBACK', 'DISABLED', 'OVERFLOW', 'DUPLICATE')

def encode(command, seq, payload=b'', kind=0, version=VERSION, bad_crc=False, allow_oversize=False):
    if len(payload) > MAX_PAYLOAD and not allow_oversize:
        raise ValueError('payload exceeds protocol maximum')
    body = struct.pack('<BBBHH', version, kind, command, seq, len(payload)) + payload
    crc = binascii.crc_hqx(body, 0xffff) ^ bool(bad_crc)
    return b'\x7e' + escape(body + struct.pack('<H', crc)) + b'\x7e'

def escape(data):
    return bytes(v for b in data for v in ((0x7d, b ^ 0x20) if b in (0x7e, 0x7d) else (b,)))

def describe(command, reply):
    if not reply:
        return {}
    if command == 1 and len(reply) == 5:
        return dict(firmware=list(reply[1:4]), protocol=reply[4])
    if command == 2 and len(reply) == 76:
        return dict(features=struct.unpack_from('<I',reply,2)[0], max_packet=struct.unpack_from('<H',reply,6)[0],
                    max_payload=struct.unpack_from('<H',reply,8)[0],
                    module_band=list(struct.unpack_from('<II',reply,10)), policy_band=list(struct.unpack_from('<II',reply,18)),
                    hardware_power_max=reply[26], policy_power_max=reply[27], duty_percent=reply[28],
                    bandwidths=list(struct.unpack_from('<10I',reply,30)))
    if command == 3 and len(reply) == 28:
        return dict(mode=reply[1], raw_status=reply[2], device_errors=struct.unpack_from('<H',reply,3)[0],
                    active_valid=bool(reply[5]), dirty=bool(reply[6]),
                    applied_mask=struct.unpack_from('<H',reply,7)[0], readback_mask=struct.unpack_from('<H',reply,9)[0],
                    generation=struct.unpack_from('<I',reply,11)[0], uptime_ms=struct.unpack_from('<I',reply,15)[0])
    if command == 14 and len(reply) == 51:
        keys=('frequency_hz','bandwidth_hz','preamble','sync_word','sf','coding_rate_denominator','ldro','header','crc','invert_iq','payload_length','requested_power_dbm')
        return dict(pending=dict(zip(keys,struct.unpack_from('<IIHHBBBBBBBb',reply,1))),
                    active_snapshot=dict(zip(keys,struct.unpack_from('<IIHHBBBBBBBb',reply,21))),
                    active_valid=bool(reply[41]), dirty=bool(reply[42]), generation=struct.unpack_from('<I',reply,47)[0])
    if command == 18 and len(reply) == 18:
        return dict(flags=struct.unpack_from('<H',reply,1)[0], pre_status=reply[3], xosc_status=reply[4], final_status=reply[5],
                    pre_errors=struct.unpack_from('<H',reply,6)[0], xosc_errors=struct.unpack_from('<H',reply,8)[0],
                    final_errors=struct.unpack_from('<H',reply,10)[0], first_failure_phase=reply[12], first_hal_error=reply[13],
                    elapsed_ms=struct.unpack_from('<I',reply,14)[0])
    if command == 19 and len(reply) == 14:
        return dict(state=reply[1], last_error=reply[2], activation_enabled=bool(reply[3]), direction_mask=reply[4],
                    open_drain_mask=reply[5], pull_bits=struct.unpack_from('<H',reply,6)[0],
                    alternate_bits=struct.unpack_from('<I',reply,8)[0], input_mask=reply[12], latch_mask=reply[13])
    if command == 23 and len(reply) == 11:
        return dict(dropped_output_frames=struct.unpack_from('<I',reply,1)[0],tx_stalls=struct.unpack_from('<I',reply,5)[0],tx_queue_free=struct.unpack_from('<H',reply,9)[0])
    if command == 22 and len(reply) == 37:
        return dict(zip(('rx_packets','rx_rejected','rx_read_errors','rx_timeouts','tx_attempts',
                         'tx_done','tx_errors','cleanup_errors','duplicate_tx_requests'),struct.unpack_from('<9I',reply,1)))
    if command == 17 and len(reply) == 6:
        return dict(packet_type=reply[1], sync_word=struct.unpack_from('<H',reply,2)[0], iq_register=reply[4], tx_modulation_register=reply[5])
    return {}

class Decoder:
    def __init__(self):
        self.body = bytearray()
        self.collecting = self.escaped = self.discard = False

    def feed(self, data):
        frames = []
        for b in data:
            if b == 0x7e:
                if self.collecting and not self.discard and not self.escaped and len(self.body) >= 9:
                    v, kind, cmd, seq, length = struct.unpack_from('<BBBHH', self.body)
                    if length <= MAX_PAYLOAD and len(self.body) == length + 9 and binascii.crc_hqx(self.body[:-2], 0xffff) == struct.unpack_from('<H', self.body, len(self.body)-2)[0]:
                        frames.append((v, kind, cmd, seq, bytes(self.body[7:-2])))
                self.body.clear()
                self.collecting = True
                self.escaped = self.discard = False
            elif self.collecting and not self.discard:
                if self.escaped:
                    if b not in (0x5d, 0x5e):
                        self.discard = True
                        continue
                    b ^= 0x20
                    self.escaped = False
                elif b == 0x7d:
                    self.escaped = True
                    continue
                if len(self.body) >= MAX_PAYLOAD + 9:
                    self.discard = True
                else:
                    self.body.append(b)
        return frames

class Link:
    def __init__(self, port, logfile=None):
        self.fd = os.open(port, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
        self.original = termios.tcgetattr(self.fd)
        try:
            fcntl.ioctl(self.fd, termios.TIOCEXCL)
            settings = termios.tcgetattr(self.fd)
            settings[0] = settings[1] = settings[3] = 0
            settings[2] = termios.CLOCAL | termios.CREAD | termios.CS8
            settings[4] = settings[5] = termios.B115200
            settings[6][termios.VMIN] = settings[6][termios.VTIME] = 0
            termios.tcsetattr(self.fd, termios.TCSANOW, settings)
            termios.tcflush(self.fd, termios.TCIOFLUSH)
        except BaseException:
            os.close(self.fd)
            raise
        self.decoder = Decoder()
        self.seq = 0
        self.logfile = logfile
        self.events = []
        self.pending = []

    def close(self):
        try:
            termios.tcsetattr(self.fd, termios.TCSANOW, self.original)
        except termios.error:
            pass  # USB unplug may invalidate termios; preserve the original failure.
        finally:
            os.close(self.fd)

    def log(self, direction, **fields):
        record = dict(time=time.monotonic(), direction=direction, **fields)
        self.events.append(record)
        print(json.dumps(record), flush=True)
        if self.logfile:
            self.logfile.parent.mkdir(parents=True, exist_ok=True)
            self.logfile.write_text(json.dumps(self.events, indent=2)+'\n')

    def write(self, frame):
        self.log('TX', wire=frame.hex())
        deadline = time.monotonic()+2
        while frame:
            remaining = deadline-time.monotonic()
            if remaining <= 0 or not select.select([], [self.fd], [], remaining)[1]:
                raise TimeoutError('UART write deadline')
            count = os.write(self.fd, frame)
            frame = frame[count:]
        termios.tcdrain(self.fd)

    def receive(self, command, seq, timeout=2, kind=1):
        deadline = time.monotonic()+timeout
        while True:
            for _ in range(len(self.pending)):
                frame = self.pending.pop(0)
                if frame[:4] == (VERSION, kind, command, seq):
                    return frame[4]
                self.log('UNMATCHED', version=frame[0], kind=frame[1], command=frame[2], seq=frame[3], payload=frame[4].hex())
            remaining = deadline-time.monotonic()
            if remaining <= 0:
                raise TimeoutError(f'no reply command={command} sequence={seq}')
            if select.select([self.fd], [], [], min(remaining, .5))[0]:
                data = os.read(self.fd, 4096)
                if not data:
                    raise OSError('serial device disconnected')
                for frame in self.decoder.feed(data):
                    self.log('RX', version=frame[0], kind=frame[1], command=frame[2], seq=frame[3], payload=frame[4].hex())
                    self.pending.append(frame)

    def request(self, command, payload=b'', expected=0, **encoding):
        self.seq = (self.seq+1) & 0xffff
        self.write(encode(command, self.seq, payload, **encoding))
        reply = self.receive(command, self.seq, timeout=10 if command == 8 else 2)
        self.log('REPLY', command=command, status=STATUS[reply[0]] if reply and reply[0]<len(STATUS) else 'INVALID', decoded=describe(command,reply))
        if not reply or reply[0] != expected:
            raise AssertionError(f'command={command} expected={STATUS[expected]}, reply={reply.hex()}')
        return reply


def configure(link, frequency=433875000, sf=11, bandwidth=250000, cr=5, ldro=0,
              preamble=16, sync=0x24b4, header=0, crc=1, iq=0, length=255, power=-9):
    link.request(4, struct.pack('<I', frequency))
    link.request(5, struct.pack('<BIBB', sf, bandwidth, cr, ldro))
    link.request(6, struct.pack('<HHBBBB', preamble, sync, header, crc, iq, length))
    link.request(7, struct.pack('<b', power))
    reply = link.request(8)
    assert struct.unpack_from('<HH', reply, 1) == (15, 15), reply
    rb = link.request(17)
    assert rb[1] == 1 and struct.unpack_from('<H', rb, 2)[0] == sync, rb
    assert bool(rb[4] & 4) == (not iq), rb
    assert bool(rb[5] & 4) == (bandwidth != 500000), rb
    state = link.request(14)
    expected = struct.pack('<IIHHBBBBBBBb', frequency, bandwidth, preamble, sync,
                           sf, cr, ldro, header, crc, iq, length, power)
    assert state[1:21] == state[21:41] == expected and state[41:43] == b'\x01\x00', state
    return state


def self_test(link):
    version = link.request(1)
    assert version[:3] == bytes([0, 0, 3]) and version[-1] == 1
    if version[3] >= 1:
        assert link.request(20) == b'\0PONG'
    caps = link.request(2)
    assert struct.unpack_from('<IIII', caps, 10) == (433000000, 532000000, 433000000, 434000000), caps
    assert not (struct.unpack_from('<I', caps, 2)[0] & 0xf00), caps
    link.request(13)
    assert link.request(14)[41] == 0
    configure(link)
    first = link.request(14)[21:41]
    for hz, error in [(868000000, 9), (432999999, 9), (532000001, 9), (500000000, 7)]:
        link.request(4, struct.pack('<I', hz), expected=error)
    link.request(7, b'\x0b', expected=7)
    link.request(5, struct.pack('<BIBB', 4, 250000, 5, 0), expected=7)
    link.request(5, struct.pack('<BIBB', 12, 125000, 5, 0))
    link.request(8, expected=8)
    assert link.request(14)[21:41] == first
    configure(link, frequency=433500123, sf=9, bandwidth=125000, cr=8,
              preamble=24, sync=0x1424, header=1, crc=0, iq=1, length=42, power=-9)
    link.request(1, bad_crc=True, expected=3)
    link.request(1, version=99, expected=1)
    link.request(1, b'x', expected=6)
    link.request(0xff, expected=5)
    link.request(11, (b'\x00\x7d\x7e'*91)[:MAX_PAYLOAD], expected=13)
    link.request(9, expected=13)
    for mode in (1, 2, 3, 4):
        link.request(15, bytes([mode]), expected=13)
    link.request(10)
    link.request(15, b'\0')
    link.request(1, b'\0'*273, expected=14, allow_oversize=True)
    # A complete request header with no CRC/closing delimiter must time out.
    link.seq = (link.seq+1) & 0xffff
    header = struct.pack('<BBBHH', 1, 0, 4, link.seq, 4)
    link.write(b'\x7e'+escape(header+b'\x01'))
    assert link.receive(4, link.seq)[0] == 4
    link.seq = (link.seq+1) & 0xffff
    header = struct.pack('<BBBHH', 1, 0, 1, link.seq, 0)
    link.write(b'\x7e'+escape(header)+b'\x7d\x01\x7e')
    assert link.receive(1, link.seq)[0] == 2
    link.seq = 0x7e7c  # Next request has escaped sequence octets.
    link.request(1)
    link.request(12)
    link.request(16)
    configure(link)
    status = link.request(3)
    assert status[1] == 0 and status[2] >> 4 == 2 and status[3:5] == b'\0\0' and status[5] == 1, status
    link.log('RESULT', result='PASS: physical binary UART, dynamic config/readbacks, rejection and recovery; no RF')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--port', help='defaults to the single CH340 by-id port')
    parser.add_argument('--log', type=Path, default=Path('logs/protocol-stage/physical.json'))
    parser.add_argument('--wait-boot', action='store_true')
    parser.add_argument('--timeout', type=float, default=180)
    parser.add_argument('--frequency', type=int, default=433875000)
    parser.add_argument('--sf', type=int, default=11)
    parser.add_argument('--bandwidth', type=int, default=250000)
    parser.add_argument('--cr', type=int, choices=range(5,9), default=5)
    parser.add_argument('--ldro', type=int, choices=(0,1), default=0)
    parser.add_argument('--preamble', type=int, default=16)
    parser.add_argument('--sync', type=lambda x: int(x,0), default=0x24b4)
    parser.add_argument('--header', type=int, choices=(0,1), default=0)
    parser.add_argument('--crc', type=int, choices=(0,1), default=1)
    parser.add_argument('--iq', type=int, choices=(0,1), default=0)
    parser.add_argument('--length', type=int, default=255)
    parser.add_argument('--power', type=int, default=-9)
    parser.add_argument('command', choices=('self-test','configure','version','caps','status','config','errors','reset','stats','readback','xosc','rf_switch','ping','radio_stats','transport_stats'), default='status', nargs='?')
    args = parser.parse_args()
    ports = list(Path('/dev/serial/by-id').glob('usb-1a86_*'))
    if not args.port and len(ports) != 1:
        parser.error('specify --port; exactly one CH340 by-id port was not found')
    link = Link(args.port or str(ports[0]), args.log)
    try:
        if args.wait_boot:
            print('UART READY; waiting for binary BOOT event', flush=True)
            boot = link.receive(0x80, 0, args.timeout, kind=2)
            assert boot[:2] == b'\x00\x03' and boot[3] == 0, boot
        else:
            link.write(b'\x7e')  # synchronize after an interrupted host session
        if args.command == 'self-test':
            self_test(link)
        elif args.command == 'configure':
            configure(link, **{key:getattr(args,key) for key in ('frequency','sf','bandwidth','cr','ldro','preamble','sync','header','crc','iq','length','power')})
        elif args.command == 'xosc':
            capabilities = link.request(2)
            if not (struct.unpack_from('<I',capabilities,2)[0] & 0x20):
                raise RuntimeError('firmware does not advertise XOSC diagnostic support')
            before = link.request(14)
            pins_before = link.request(19)
            result = link.request(18)
            assert len(result)==18 and struct.unpack_from('<H',result,1)[0]==255, result
            assert result[3]>>4==2 and result[4]>>4==3 and result[5]>>4==2, result
            assert result[6:12]==b'\0'*6, result
            after = link.request(14)
            assert before==after, 'successful XOSC test changed config/validity/generation'
            pins_after = link.request(19)
            assert pins_before[:12]==pins_after[:12] and pins_before[13:]==pins_after[13:], 'RF switch configuration/latch changed'
            link.request(3)
            link.log('RESULT',result='PASS: physical XOSC entry and return to RC; configuration preserved; RF outputs disabled')
        elif args.command == 'ping':
            assert link.request(20) == b'\0PONG'
        else:
            link.request(COMMANDS[args.command])
    finally:
        link.close()

if __name__ == '__main__':
    main()
