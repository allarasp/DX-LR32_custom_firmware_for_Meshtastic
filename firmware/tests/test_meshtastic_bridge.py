import os
from pathlib import Path
import struct
import sys
import tempfile
import unittest
sys.path.insert(0,str(Path(__file__).resolve().parents[2]/'bin'))
from meshtastic_bridge import *
class TestMesh(unittest.TestCase):
 def test_profile(self):
  self.assertEqual(frequency(),433875000);self.assertEqual(CHANNEL_HASH,8)
  self.assertEqual([frequency(i) for i in range(1,5)],[433125000,433375000,433625000,433875000])
  for x in (-1,5):
   with self.assertRaises(ValueError):frequency(x)
 def test_packet_and_cipher_vector(self):
  data=mesh_pb2.Data(portnum=1,payload=b'DXLR32 test')
  p=encode(0x12345678,0x11223344,data)
  self.assertEqual(p.hex(),'ffffffff7856341244332211000800004c4b30ee27f26ccf8683a9345e6fc2')
  self.assertEqual(decode(p)['text'],'DXLR32 test')
  self.assertEqual(crypt(crypt(b'\0'*48,4,1),4,1),b'\0'*48)
  self.assertNotEqual(crypt(b'\0'*48,4,1),crypt(b'\0'*48,4,2))
 def test_reject(self):
  for p in (b'',b'0'*15,b'0'*256):
   with self.assertRaises(ValueError):decode(p)
  p=bytearray(encode(4,1,mesh_pb2.Data(portnum=1,payload=b'hello')));p[13]=99
  with self.assertRaises(ValueError):decode(bytes(p))
  p=bytearray(encode(4,1,mesh_pb2.Data(portnum=1,payload=b'hello')));p[:4]=struct.pack('<I',5)
  with self.assertRaises(ValueError):decode(bytes(p),6)
  with self.assertRaises(ValueError):encode(0,1,mesh_pb2.Data())
  with self.assertRaises(ValueError):encode(4,1,mesh_pb2.Data(portnum=1,payload=b'x'*240))
 def test_nodeinfo(self):
  user=mesh_pb2.User(id='!12345678',long_name='Test node',short_name='TEST')
  p=encode(0x12345678,1,mesh_pb2.Data(portnum=portnums_pb2.NODEINFO_APP,payload=user.SerializeToString()))
  self.assertEqual(decode(p)['short_name'],'TEST')
  event=bytearray(15);struct.pack_into('<h',event,8,-380);event[10]=0xf9;event[13]=15;event[14]=len(p)
  msg=event_packet((1,2,0x81,1,bytes(event)+p),5,'5678')
  self.assertTrue(msg['target_candidate']);self.assertEqual(msg['snr_db'],-1.75);self.assertEqual(msg['rssi_dbm'],-95)
  with self.assertRaises(ValueError):event_packet((1,2,0x81,1,bytes(event)),5)
 def test_identity_persists(self):
  with tempfile.TemporaryDirectory() as tmp:
   path=Path(tmp)/'id.json';ident=Identity(path);a=ident.next();b=Identity(path).next()
   self.assertEqual(a[0],b[0]);self.assertEqual(a[1]+1,b[1]);self.assertEqual(path.stat().st_mode&0o777,0o600)
 def test_queue_preserves_events(self):
  # A PTY exercises actual framed UART decoding, not a mock of the codec.
  import pty,threading
  from uart_radio import encode as wire
  master,slave=pty.openpty();link=RadioLink(os.ttyname(slave))
  try:
   def send():
    time.sleep(.02);os.write(master,wire(0x81,1,b'event',kind=2)+wire(1,7,b'\0\0\4\0\1',kind=1))
   thread=threading.Thread(target=send);thread.start()
   self.assertEqual(link.receive(1,7),b'\0\0\4\0\1');self.assertEqual(link.event()[4],b'event');thread.join()
  finally:link.close();os.close(master);os.close(slave)
if __name__=='__main__':unittest.main()
