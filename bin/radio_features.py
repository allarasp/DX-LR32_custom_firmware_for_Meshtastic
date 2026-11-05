#!/usr/bin/env python3
"""DX-LR32 v0.7 raw LoRa/GFSK features. RF TX requires explicit --transmit."""
import argparse
from pathlib import Path
import struct
import time
from uart_radio import EventLink, configure

GFSK_FORMAT=struct.Struct('<IIIHHHH8s10B')

def gfsk_payload(bitrate=50000,deviation=25000,bandwidth=117300,preamble=32,crc_seed=0x1d0f,
                 crc_polynomial=0x1021,whitening_seed=0x1ff,sync=b'\x2d\xd4',shape=9,detector=5,
                 address_filter=0,header=1,length=255,crc=6,whitening=1,node=0,broadcast=255):
    if not isinstance(sync,bytes) or len(sync)>8:raise ValueError('GFSK sync must contain 0..8 bytes')
    return GFSK_FORMAT.pack(bitrate,deviation,bandwidth,preamble,crc_seed,crc_polynomial,whitening_seed,
                            sync.ljust(8,b'\0'),shape,detector,len(sync)*8,address_filter,header,length,crc,whitening,node,broadcast)

def configure_gfsk(link,frequency=433875000,power=-9,**params):
    wire=gfsk_payload(**params)
    link.request(4,struct.pack('<I',frequency));link.request(7,struct.pack('<b',power))
    link.request(34,wire)
    reply=link.request(8)
    assert struct.unpack_from('<HH',reply,1)==(0x1b,0x1b)
    state=link.request(35,b'\1');assert state[2:4]==b'\1\0' and state[4:]==wire
    return state

def wait_event(link,command,sequence=None,timeout=4):
    deadline=time.monotonic()+timeout
    while time.monotonic()<deadline:
        event=link.event(min(.1,max(0,deadline-time.monotonic())))
        if not event:continue
        link.log('RADIO_EVENT',command=event[2],sequence=event[3],payload=event[4].hex())
        if event[2]==0x83:raise RuntimeError(f'radio error: {event[4].hex()}')
        if event[2]==command and (sequence is None or event[3]==sequence):return event[4]
    raise TimeoutError(f'event {command:#x} missing')

def prepare(link,modem='lora',frequency=433875000,power=-9,**params):
    ext=link.request(36)
    if ext[2]==1:link.request(31)
    link.request(10)
    if modem=='gfsk':return configure_gfsk(link,frequency,power,**params)
    return configure(link,frequency=frequency,power=power,**params)

def regression(link):
    assert link.request(1)==bytes([0,0,7,0,1])
    assert struct.unpack_from('<I',link.request(2),2)[0]==0x7ffbff
    prepare(link)
    link.request(27);assert link.request(26)[2:]==b'\0'*6
    for gain in (0,1,0):
        assert link.request(28,bytes([gain]))==bytes([0,gain,0x96 if gain else 0x94])
        link.request(9,b'\0'*4)
        rssi=link.request(25);assert len(rssi)==8 and -510<=struct.unpack_from('<h',rssi,1)[0]<=0
        link.request(10)
    link.request(29,struct.pack('<BBBI',2,22,10,1000));seq=link.seq
    done=wait_event(link,0x85,seq);assert len(done)==10 and done[0]==0 and done[1] in (0,1)
    link.request(30)
    assert link.request(20)==b'\0PONG'
    sleeping=link.request(3);assert sleeping[1:3]==b'\1\xff' and sleeping[3:5]==b'\xff\xff'
    link.request(9,b'\0'*4,expected=11)
    link.request(31);assert link.request(3)[1]==0
    assert not link.request(14)[41]  # Cold sleep invalidates active configuration.
    prepare(link)
    # Querying state cannot wake the hardware or terminate its sniff loop.
    link.request(32,struct.pack('<III',100,100,600))
    for _ in range(3):
        ext=link.request(36);assert ext[2]==5
        state=link.request(3);assert state[1:3]==b'\5\xff'
        time.sleep(.04)
    wait_event(link,0x84,timeout=3);assert link.request(3)[1]==0
    # Both modulation families must recover on either supported band.
    for hz in (433875000,868000000):
        configure_gfsk(link,frequency=hz)
        link.request(29,struct.pack('<BBBI',2,22,10,1000),expected=9)  # CAD is LoRa-only.
        link.request(27);assert link.request(26)==b'\0\0'+b'\0'*6
        link.request(9,struct.pack('<I',150))
        state=link.request(3);assert state[1]==3 and state[2]>>4==5 and state[3:5]==b'\0\0'
        link.request(25);wait_event(link,0x84,timeout=3)
        link.request(32,struct.pack('<III',100,100,600))
        assert link.request(36)[2]==5
        wait_event(link,0x84,timeout=3)
        assert link.request(12)==b'\0\0\0'
    prepare(link)
    link.request(18)
    assert link.request(23)[1:9]==b'\0'*8
    assert link.request(12)==b'\0\0\0'
    print('FEATURE REGRESSION PASSED: RSSI, hardware stats/reset, normal/boosted gain, CAD event, sleep/wake/reapply, duty RX, GFSK configuration/readbacks/RX on both bands and LoRa recovery',flush=True)

def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('command',choices=('test','configure','rssi','stats','reset-stats','gain','cad','sleep','wake','duty','rx','tx','status'))
    ap.add_argument('--port');ap.add_argument('--log',type=Path,default=Path('logs/radio-features/session.json'))
    ap.add_argument('--modem',choices=('lora','gfsk'),default='lora')
    ap.add_argument('--frequency',type=int,default=433875000);ap.add_argument('--power',type=int,default=-9)
    ap.add_argument('--gain',type=int,choices=(0,1),default=0)
    ap.add_argument('--seconds',type=float,default=10);ap.add_argument('--samples',type=int,default=10)
    ap.add_argument('--interval-ms',type=int,default=100)
    ap.add_argument('--rx-ms',type=int,default=100);ap.add_argument('--sleep-ms',type=int,default=100)
    ap.add_argument('--bitrate',type=int,default=50000);ap.add_argument('--deviation',type=int,default=25000)
    ap.add_argument('--bandwidth',type=int,help='Hz; default 250000 for LoRa, 117300 for GFSK');ap.add_argument('--sync',default='2dd4')
    ap.add_argument('--shape',type=int,default=9);ap.add_argument('--preamble',type=int)
    ap.add_argument('--sf',type=int,choices=range(5,13),default=11);ap.add_argument('--cr',type=int,choices=range(5,9),default=5)
    ap.add_argument('--ldro',type=int,choices=(0,1));ap.add_argument('--sync-word',type=lambda x:int(x,0),default=0x24b4)
    ap.add_argument('--implicit',action='store_true');ap.add_argument('--length',type=int,default=255)
    ap.add_argument('--invert-iq',action='store_true')
    ap.add_argument('--crc',type=int,choices=(0,1,2,4,6));ap.add_argument('--whitening',type=int,choices=(0,1),default=1)
    ap.add_argument('--fixed-length',type=int);ap.add_argument('--hex',dest='payload')
    ap.add_argument('--transmit',action='store_true',help='authorize exactly one raw packet; no automatic retry')
    args=ap.parse_args()
    if args.command=='tx' and (not args.transmit or not args.payload):ap.error('TX requires --transmit and --hex with 1..255 bytes')
    if not .01<=args.seconds<=3600 or not 1<=args.samples<=1000 or not 10<=args.interval_ms<=10000:ap.error('invalid duration/sample interval')
    ports=list(Path('/dev/serial/by-id').glob('usb-1a86_*'))
    if not args.port and len(ports)!=1:ap.error('specify --port; exactly one CH340 is required')
    link=EventLink(args.port or str(ports[0]),args.log)
    cleanup=args.command in ('test','rssi','cad','duty','rx','tx')
    try:
        link.write(b'\x7e')
        if args.command=='test':regression(link);return
        if args.command=='status':link.request(36);return
        if args.command in ('stats','reset-stats'):link.request(26 if args.command=='stats' else 27);return
        if args.command=='sleep':link.request(10);link.request(30);return
        if args.command=='wake':link.request(31);return
        if args.command=='gain':link.request(10);link.request(28,bytes([args.gain]));return
        params={}
        if args.modem=='gfsk':
            params=dict(bitrate=args.bitrate,deviation=args.deviation,bandwidth=args.bandwidth or 117300,sync=bytes.fromhex(args.sync),
                        shape=args.shape,preamble=args.preamble or 32,crc=6 if args.crc is None else args.crc,whitening=args.whitening,
                        header=0 if args.fixed_length is not None else 1,length=args.fixed_length if args.fixed_length is not None else 255)
        else:
            bw=args.bandwidth or 250000
            if args.crc not in (None,0,1):raise ValueError('LoRa CRC must be 0 or 1')
            params=dict(sf=args.sf,bandwidth=bw,cr=args.cr,ldro=args.ldro if args.ldro is not None else int((1<<args.sf)*1000>=16*bw),
                        preamble=args.preamble or 16,sync=args.sync_word,header=int(args.implicit),length=args.length,
                        crc=1 if args.crc is None else args.crc,iq=int(args.invert_iq))
        prepare(link,args.modem,args.frequency,args.power,**params)
        if args.command=='configure':return
        if args.command=='cad':
            link.request(29,struct.pack('<BBBI',2,22,10,1000));wait_event(link,0x85,link.seq);return
        if args.command=='rssi':
            link.request(9,b'\0'*4)
            for _ in range(args.samples):link.request(25);time.sleep(args.interval_ms/1000)
            return
        if args.command=='tx':
            packet=bytes.fromhex(args.payload)
            if not 1<=len(packet)<=255:raise ValueError('packet must be 1..255 bytes')
            ext=link.request(36);remaining=struct.unpack_from('<I',ext,17)[0]
            if remaining:raise RuntimeError(f'TX cooldown has {remaining} ms remaining; no attempt made')
            link.request(21,b'TX1!');link.request(11,packet)
            done=wait_event(link,0x82,link.seq)
            if done[0]:raise RuntimeError('TX failed; no retry')
            print('TX_DONE; peer receipt has not been established',flush=True);return
        if args.command=='duty':link.request(32,struct.pack('<III',args.rx_ms,args.sleep_ms,int(args.seconds*1000)))
        else:link.request(9,b'\0'*4)
        deadline=time.monotonic()+args.seconds+.3
        while time.monotonic()<deadline:
            frame=link.event(.1)
            if frame:
                link.log('RADIO_EVENT',command=frame[2],sequence=frame[3],payload=frame[4].hex())
                if frame[2]==0x83:raise RuntimeError('radio error event')
                if frame[2]==0x84:break
    finally:
        if cleanup:
            try:link.request(10)
            except Exception as e:print(f'Cleanup result: {e}')
        if link.dropped_events:print(f'Host dropped events: {link.dropped_events}')
        link.close()
if __name__=='__main__':main()
