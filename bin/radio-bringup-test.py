"""Physical non-RF radio diagnostic test; requires an already connected UART."""
import argparse
import json
import os
from pathlib import Path
import select
import termios
import time
import fcntl

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--port', required=True)
parser.add_argument('--wait-boot', action='store_true')
parser.add_argument('--diagnose', action='store_true', help='capture all radio failures; still exit nonzero')
parser.add_argument('--timeout', type=float, default=60)
parser.add_argument('--log-dir', type=Path, default=Path('logs/radio-bringup/uart'))
args = parser.parse_args()
args.log_dir.mkdir(parents=True, exist_ok=True)
fd = os.open(args.port, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
fcntl.ioctl(fd, termios.TIOCEXCL)
original = termios.tcgetattr(fd)
settings = termios.tcgetattr(fd)
settings[0] = settings[1] = settings[3] = 0
settings[2] = termios.CLOCAL | termios.CREAD | termios.CS8
settings[4] = settings[5] = termios.B115200
settings[6][termios.VMIN] = settings[6][termios.VTIME] = 0
termios.tcsetattr(fd, termios.TCSANOW, settings)
termios.tcflush(fd, termios.TCIOFLUSH)
events = []
pending = bytearray()
def event(direction, data):
    events.append(dict(monotonic=time.monotonic(), direction=direction,
                       ascii=data.decode('ascii', errors='backslashreplace'), hex=data.hex()))
    print(direction, repr(data), flush=True)
    (args.log_dir/'transcript.json').write_text(json.dumps(events, indent=2)+'\n')
def line(timeout):
    deadline = time.monotonic()+timeout
    while True:
        if b'\n' in pending:
            end = pending.index(10)+1
            result = bytes(pending[:end]); del pending[:end]
            return result
        remaining = deadline-time.monotonic()
        if remaining <= 0: raise TimeoutError(f'UART line timeout; pending={pending!r}')
        if select.select([fd],[],[],min(remaining,0.5))[0]:
            data = os.read(fd,4096)
            if data: event('RX',data); pending.extend(data)
try:
    print('UART READY: 115200 8N1', flush=True)
    failures = []
    if args.wait_boot:
        assert line(args.timeout) == b'DXLR32 CUSTOM FW BOOT\r\n'
        boot = line(3)
        if not boot.startswith(b'RADIO_INIT OK '):
            failures.append(boot)
            if not args.diagnose: raise AssertionError(boot)
    for command, expected in [
        ('PING','PONG'), ('VERSION','DXLR32 RADIODIAG 0.2.0'),
        ('STATUS','STATUS CLK=22120000 BAUD=115200 RADIO=READY_RC'),
        ('RADIO_STATUS','RADIO_STATUS OK '), ('RADIO_ERRORS','RADIO_ERRORS OK '),
        ('RADIO_TEST','RADIO_TEST OK '), ('RADIO_RESET','RADIO_RESET OK '),
        ('RADIO_TEST','RADIO_TEST OK '), ('PING','PONG')]:
        data = (command+'\r\n').encode()
        event('TX',data)
        assert os.write(fd,data) == len(data)
        termios.tcdrain(fd)
        reply = line(3)
        valid = reply.startswith(expected.encode())
        if command == 'RADIO_TEST':
            valid = valid and b'gfsk=0x00 lora=0x01 restored=0x00' in reply
        if not valid:
            failures.append(reply)
            if not args.diagnose or command in ('PING','VERSION'): raise AssertionError(reply)
    if failures:
        raise AssertionError(f'Physical radio test failed: {len(failures)} failing responses; see transcript')
    print('PHYSICAL RADIO/UART TEST PASSED (no RF operations)', flush=True)
finally:
    termios.tcsetattr(fd,termios.TCSANOW,original)
    os.close(fd)
