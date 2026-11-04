#!/usr/bin/env python3
"""Physical v0.5.x UART regression; configures and briefly receives, never transmits."""
import argparse
from pathlib import Path
import struct
from uart_radio import Link, configure, encode

def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--port')
    ap.add_argument('--log',type=Path,default=Path('logs/live-controller/regression.json'))
    a=ap.parse_args()
    ports=list(Path('/dev/serial/by-id').glob('usb-1a86_*'))
    if not a.port and len(ports)!=1:ap.error('specify --port; exactly one CH340 is required')
    link=Link(a.port or str(ports[0]),a.log)
    try:
        link.write(b'\x7e');version=link.request(1);assert version[:3]==b'\0\0\5' and version[4]==1
        maximum=22 if version[3]>=1 else 10
        assert link.request(20)==b'\0PONG'
        caps=link.request(2);assert struct.unpack_from('<I',caps,2)[0]==0x7bff
        link.request(10)
        link.request(4,struct.pack('<I',868000000),expected=9)
        assert caps[26]==22 and caps[27]==maximum
        link.request(7,struct.pack('<b',maximum+1),expected=7)
        # Max-power configuration only: this utility never sends/arms TX.
        configure(link,power=maximum)
        assert struct.unpack_from('<b',link.request(14),40)[0]==maximum
        link.request(7,struct.pack('<b',-10),expected=7)
        # Configure varied PHY settings without activating TX; check available readbacks.
        for sf,bw,ldro,header,crc,iq,power,sync in ((7,125000,0,1,0,1,0,0x1424),(12,250000,1,0,1,0,10,0x1424),(11,250000,0,0,1,0,-9,0x24b4)):
            configure(link,frequency=433500000 if bw==125000 else 433875000,sf=sf,bandwidth=bw,cr=8 if sf==7 else 5,
                      ldro=ldro,preamble=16,sync=sync,header=header,crc=crc,iq=iq,length=32 if header else 255,power=power)
            rb=link.request(17);assert struct.unpack_from('<H',rb,2)[0]==sync
            assert ((rb[4]>>2)&1)==(0 if iq else 1)
            cfg=link.request(14);assert struct.unpack_from('<b',cfg,40)[0]==power and cfg[41]==1
        # Deliberately corrupt CRC, then recover on a valid frame.
        link.seq+=1;link.write(encode(20,link.seq,bad_crc=True))
        assert link.receive(20,link.seq)[0]==3
        assert link.request(20)==b'\0PONG'
        link.seq+=1;link.write(encode(20,link.seq)[:-2])
        assert link.receive(20,link.seq)[0]==4
        assert link.request(20)==b'\0PONG'
        # Burst within bounded queue capacity; every reply must remain intact/in order.
        seqs=list(range(link.seq+1,link.seq+33));link.seq=seqs[-1]
        link.write(b''.join(encode(20,s) for s in seqs))
        for seq in seqs:assert link.receive(20,seq)==b'\0PONG'
        link.request(9,struct.pack('<I',100))
        assert link.receive(0x84,0,timeout=3,kind=2)==bytes([4])
        state=link.request(3);assert state[1]==0 and state[2]>>4==2 and state[3:5]==b'\0\0'
        link.request(18)
        radio=link.request(22);assert struct.unpack_from('<I',radio,13)[0]>=1
        transport=link.request(23);assert transport[1:9]==b'\0'*8
        print('PHYSICAL REGRESSION PASSED: version, ping, capabilities, varied configuration/readbacks, CRC recovery, 32-frame burst, finite RX timeout, XOSC and zero transport drops/stalls')
    finally:
        try:link.request(10)
        finally:link.close()

if __name__=='__main__':main()
