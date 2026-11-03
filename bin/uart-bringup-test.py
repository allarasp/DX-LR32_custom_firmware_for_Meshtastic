"""Capture a physical 115200-8N1 UART boot and command/response exchange."""
import argparse
import json
import os
from pathlib import Path
import select
import termios
import time
import fcntl

parser = argparse.ArgumentParser()
parser.add_argument('--port', required=True)
parser.add_argument('--timeout', type=float, default=180)
args = parser.parse_args()
root = Path(__file__).resolve().parents[1]
logdir = root/'logs/bringup'
logdir.mkdir(parents=True, exist_ok=True)
fd = os.open(args.port, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
fcntl.ioctl(fd, termios.TIOCEXCL)
original = termios.tcgetattr(fd)
settings = termios.tcgetattr(fd)
settings[0] = settings[1] = settings[3] = 0
settings[2] = termios.CLOCAL | termios.CREAD | termios.CS8
settings[4] = settings[5] = termios.B115200
settings[6][termios.VMIN] = 0
settings[6][termios.VTIME] = 0
termios.tcsetattr(fd, termios.TCSANOW, settings)
termios.tcflush(fd, termios.TCIOFLUSH)
events = []
raw = bytearray()
def event(direction, data):
    item = dict(monotonic=time.monotonic(), direction=direction, ascii=data.decode('ascii',errors='backslashreplace'), hex=data.hex())
    events.append(item)
    print(direction, repr(data), flush=True)
    (logdir/'uart-transcript.json').write_text(json.dumps(events,indent=2)+'\n')
def receive_until(expected, timeout):
    collected = bytearray()
    deadline = time.monotonic()+timeout
    while time.monotonic() < deadline:
        if select.select([fd],[],[],min(0.5,max(0,deadline-time.monotonic())))[0]:
            data = os.read(fd,4096)
            if data:
                collected.extend(data); raw.extend(data); event('RX',data)
                (logdir/'uart-rx.bin').write_bytes(raw)
                if expected in collected: return bytes(collected)
    raise TimeoutError(f'No {expected!r}; received {bytes(collected)!r}')
try:
    print(f'UART READY: {args.port}, 115200 8N1; waiting for physical boot banner',flush=True)
    receive_until(b'DXLR32 CUSTOM FW BOOT\r\n',args.timeout)
    for command, expected in [
        (b'PING\r\n',b'PONG\r\n'),
        (b'VERSION\r\n',b'DXLR32 UARTDIAG 0.1.1\r\n'),
        (b'STATUS\r\n',b'STATUS CLK=22120000 BAUD=115200 RADIO=UNINITIALIZED\r\n'),
        *[(b'PING\r\n',b'PONG\r\n')]*5,
    ]:
        event('TX',command)
        assert os.write(fd,command) == len(command)
        termios.tcdrain(fd)
        response = receive_until(expected,2)
        assert response == expected, f'Unexpected serial response: {response!r}'
    print('PHYSICAL UART TEST PASSED: boot banner, VERSION, STATUS, six PING/PONG exchanges',flush=True)
finally:
    termios.tcsetattr(fd,termios.TCSANOW,original)
    os.close(fd)
