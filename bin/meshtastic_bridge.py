#!/usr/bin/env python3
"""Experimental public-channel endpoint; not a mesh router or PhoneAPI device."""
import argparse
from collections import deque
import json
import os
from pathlib import Path
import secrets
import select
import struct
import time
from cryptography.hazmat.primitives.ciphers import Cipher, algorithms, modes
from google.protobuf.message import DecodeError
from meshtastic import util
from meshtastic.protobuf import mesh_pb2, portnums_pb2
from uart_radio import Link, configure

BROADCAST=0xffffffff
HEADER=struct.Struct('<IIIBBBB')
PUBLIC_KEY=util.DEFAULT_KEY
CHANNEL_HASH=util.generate_channel_hash('LongFast',PUBLIC_KEY)

def frequency(slot=0):
    """Firmware DJB2/channel-slot selection for EU433 LongFast only."""
    if not slot:
        h=5381
        for b in b'LongFast': h=(h*33+b)&0xffffffff
        slot=h%4+1
    if slot not in range(1,5): raise ValueError('EU433 LongFast slot must be 1..4')
    return 433000000+125000+(slot-1)*250000

def crypt(data,sender,packet_id):
    nonce=struct.pack('<QII',packet_id,sender,0)
    cipher=Cipher(algorithms.AES(PUBLIC_KEY),modes.CTR(nonce)).encryptor()
    return cipher.update(data)+cipher.finalize()

def encode(sender,packet_id,data):
    if sender<4 or sender>=BROADCAST or not 0<packet_id<=BROADCAST:
        raise ValueError('invalid source/packet ID')
    payload=data.SerializeToString()
    if len(payload)>239: raise ValueError('radio payload exceeds 255 bytes')
    # Direct, hop-zero channel broadcast; no ACK, route, PKI or relay behavior.
    return HEADER.pack(BROADCAST,sender,packet_id,0,CHANNEL_HASH,0,0)+crypt(payload,sender,packet_id)

def decode(packet,own_node=None):
    if not 16<len(packet)<=255: raise ValueError('bad radio packet length')
    dest,sender,pid,flags,ch,next_hop,relay=HEADER.unpack_from(packet)
    if sender<4 or sender==BROADCAST or not pid: raise ValueError('bad mesh header')
    if ch!=CHANNEL_HASH: raise ValueError('not the public LongFast channel')
    if dest!=BROADCAST and dest!=own_node: raise ValueError('unsupported addressed/PKI packet')
    # Public-channel CTR has no authentication. Protobuf checks are not a MAC.
    data=mesh_pb2.Data()
    try: data.ParseFromString(crypt(packet[16:],sender,pid))
    except DecodeError as e: raise ValueError('payload is not supported channel protobuf') from e
    if not data.portnum or len(data.payload)>233: raise ValueError('invalid application envelope')
    result=dict(sender=f'!{sender:08x}',destination=f'!{dest:08x}',packet_id=pid,port=data.portnum,hop_limit=flags&7)
    if data.portnum==portnums_pb2.TEXT_MESSAGE_APP:
        try: result['text']=data.payload.decode('utf-8')
        except UnicodeDecodeError as e: raise ValueError('invalid UTF-8 text') from e
    elif data.portnum==portnums_pb2.NODEINFO_APP:
        user=mesh_pb2.User()
        try:user.ParseFromString(data.payload)
        except DecodeError as e:raise ValueError('invalid NodeInfo') from e
        result.update(long_name=user.long_name,short_name=user.short_name)
    return result

class Identity:
    def __init__(self,path):
        self.path=Path(path)
        self.path.parent.mkdir(parents=True,exist_ok=True)
        if self.path.exists():self.value=json.loads(self.path.read_text())
        else:self.value=dict(node=secrets.randbelow(0xfffffffb)+4,counter=secrets.randbelow(0x7fffffff)+1)
        if not 4<=self.value['node']<BROADCAST or not 0<self.value['counter']<BROADCAST:
            raise ValueError('invalid/exhausted local identity; create a new identity deliberately')
    def next(self):
        pid=self.value['counter']
        if pid>=BROADCAST:raise ValueError('packet IDs exhausted; create a new identity')
        self.value['counter']+=1
        temp=self.path.with_suffix('.tmp')
        fd=os.open(temp,os.O_WRONLY|os.O_CREAT|os.O_TRUNC,0o600)
        with os.fdopen(fd,'w') as f:json.dump(self.value,f);f.flush();os.fsync(f.fileno())
        os.replace(temp,self.path)
        return self.value['node'],pid

class RadioLink(Link):
    def __init__(self,*args,**kwargs):
        super().__init__(*args,**kwargs);self.radio_events=deque();self.dropped_events=0
    def pump(self,timeout):
        if not select.select([self.fd],[],[],timeout)[0]:return
        data=os.read(self.fd,4096)
        if not data:raise OSError('serial device disconnected')
        for frame in self.decoder.feed(data):
            if frame[0]!=1:continue
            if frame[1]==2:
                if len(self.radio_events)>=64:self.radio_events.popleft();self.dropped_events+=1
                self.radio_events.append(frame)
            else:self.pending.append(frame)
    def receive(self,command,seq,timeout=2,kind=1):
        deadline=time.monotonic()+timeout
        while time.monotonic()<deadline:
            for _ in range(len(self.pending)):
                frame=self.pending.pop(0)
                if frame[:4]==(1,kind,command,seq):return frame[4]
                self.log('UNMATCHED',kind=frame[1],command=frame[2],seq=frame[3])
            self.pump(min(.1,max(0,deadline-time.monotonic())))
        raise TimeoutError(f'no reply command={command} sequence={seq}')
    def event(self,timeout=.5):
        if not self.radio_events:self.pump(timeout)
        return self.radio_events.popleft() if self.radio_events else None

def event_packet(frame,own_node,target_suffix=None):
    _,kind,event,seq,p=frame
    if kind!=2 or event!=0x81:return None
    if len(p)<15 or len(p)!=15+p[14] or (p[13]&15)!=15:raise ValueError('invalid RX metadata')
    msg=decode(p[15:],own_node)
    msg.update(rssi_dbm=struct.unpack_from('<h',p,8)[0]/4,snr_db=struct.unpack_from('<b',p,10)[0]/4)
    if target_suffix:msg['target_candidate']=msg['sender'].endswith(target_suffix.lower().lstrip('!'))
    return msg

def setup(link,hz):
    version=link.request(1)
    if tuple(version[1:4]) not in ((0,4,0),(0,5,0),(0,5,1),(0,6,0)):
        raise RuntimeError('requires live controller v0.4.0, v0.5.x or v0.6.0; installed v0.3.1 cannot receive packets over UART')
    caps=link.request(2)
    if struct.unpack_from('<I',caps,2)[0]&0x300!=0x300:raise RuntimeError('RX/TX capability missing')
    link.request(10)
    configure(link,frequency=hz,sf=11,bandwidth=250000,cr=5,ldro=0,preamble=16,sync=0x24b4,
              header=0,crc=1,iq=0,length=255,power=-9)
    link.request(9,struct.pack('<I',0))
    state=link.request(3)
    if state[1]!=3 or state[2]>>4!=5 or state[3:5]!=b'\0\0':raise RuntimeError('radio did not confirm RX mode with zero errors')

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('command',choices=('plan','listen','send','announce'))
    parser.add_argument('--port');parser.add_argument('--seconds',type=float,default=180)
    parser.add_argument('--slot',type=int,default=0);parser.add_argument('--target-suffix')
    parser.add_argument('--profile',type=Path,default=Path('.local/mesh-test-profile.json'))
    parser.add_argument('--identity',type=Path,default=Path('.local/mesh-identity.json'))
    parser.add_argument('--text',default='DX-LR32 test. Please reply on this public channel.')
    parser.add_argument('--transmit',action='store_true',help='explicitly authorize one outgoing packet')
    parser.add_argument('--log',type=Path,default=Path('logs/meshtastic-bridge.json'))
    args=parser.parse_args()
    profile=json.loads(args.profile.read_text()) if args.profile.exists() else {}
    target=args.target_suffix or profile.get('target_suffix')
    hz=frequency(args.slot)
    print(json.dumps(dict(region='EU_433',preset='LongFast',frequency_hz=hz,channel='LongFast',
                          channel_hash=CHANNEL_HASH,public=True,power_dbm=-9,tx_enabled=args.transmit)))
    if args.command=='plan':return
    if args.command in ('send','announce') and not args.transmit:parser.error('send/announce requires --transmit; no packet sent')
    if args.command=='send' and not 1<=len(args.text.encode('utf-8'))<=80:parser.error('text must be 1..80 UTF-8 bytes')
    ident=Identity(args.identity)
    ports=list(Path('/dev/serial/by-id').glob('usb-1a86_*'))
    if not args.port and len(ports)!=1:parser.error('specify --port; exactly one CH340 was not found')
    link=RadioLink(args.port or str(ports[0]),args.log)
    seen=set();tx_sequence=None;tx_deadline=None;tx_complete=False
    try:
        link.write(b'\x7e');setup(link,hz)
        if args.command in ('send','announce'):
            # Listen before sending; firmware also rejects preamble/header activity.
            time.sleep(.3+secrets.randbelow(300)/1000)
            sender,pid=ident.next()
            if args.command=='send':data=mesh_pb2.Data(portnum=portnums_pb2.TEXT_MESSAGE_APP,payload=args.text.encode())
            else:
                user=mesh_pb2.User(id=f'!{sender:08x}',long_name='DX-LR32 WSL test',short_name='DXLR')
                data=mesh_pb2.Data(portnum=portnums_pb2.NODEINFO_APP,payload=user.SerializeToString())
            link.request(21,b'TX1!');link.request(11,encode(sender,pid,data));tx_sequence=link.seq;tx_deadline=time.monotonic()+4
            print('ONE PACKET ACCEPTED; waiting for TxDone. This is not a delivery confirmation.',flush=True)
        deadline=time.monotonic()+args.seconds;health_at=time.monotonic()+5
        while time.monotonic()<deadline:
            if tx_deadline and not tx_complete and time.monotonic()>tx_deadline:raise TimeoutError("TX completion missing; no retry")
            if time.monotonic()>=health_at and (tx_sequence is None or tx_complete):
                health=link.request(3)
                if health[1]!=3 or health[2]>>4!=5 or health[3:5]!=b'\0\0':raise RuntimeError('RX stopped or radio reports errors')
                health_at=time.monotonic()+5
            frame=link.event()
            if not frame:continue
            if frame[2]==0x82:
                if frame[3]!=tx_sequence:continue
                if len(frame[4])!=9 or frame[4][0]:raise RuntimeError('TX failed; no retry')
                tx_complete=True
                print('TX_DONE; listening for the peer reply.',flush=True);continue
            if frame[2]==0x83:raise RuntimeError(f'radio error: {frame[4].hex()}')
            try:msg=event_packet(frame,ident.value['node'],target)
            except ValueError as e:print(json.dumps(dict(ignored=str(e))));continue
            if msg:
                key=(msg['sender'],msg['packet_id'])
                if key in seen:continue
                if len(seen)>=1024:seen.clear()
                seen.add(key);link.log('MESH_RX',decoded=msg)
        if args.command in ('send','announce') and tx_sequence is not None:
            print('Listening window ended. Only a received reply can confirm the two-way link.')
    finally:
        try:link.request(10)
        except Exception as e:print(f'Cleanup status: {e}')
        if link.dropped_events:print(f"Host event queue dropped {link.dropped_events} events")
        link.close()

if __name__=='__main__':main()
