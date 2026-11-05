"""Cross-language GFSK configuration and actual PTY asynchronous transport tests."""
from pathlib import Path
import os
import pty
import struct
import subprocess
import sys
import threading
import time
import unittest
ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'bin'))
from radio_features import gfsk_payload
from uart_radio import Decoder,EventLink,encode

class TestFeatures(unittest.TestCase):
    def test_gfsk_real_firmware_config_and_recovery(self):
        wire=gfsk_payload()
        bad=bytearray(wire);bad[:4]=b'\0'*4
        commands=[(1,b''),(34,wire),(8,b''),(35,b'\1'),(34,bytes(bad)),(8,b''),(35,b'\1'),
                  (5,struct.pack('<BIBB',11,250000,5,0)),(8,b'')]
        process=subprocess.run([str(ROOT/'firmware/build/meshctrl/test_config'),'--bridge'],
                               input=b''.join(encode(cmd,i+1,p) for i,(cmd,p) in enumerate(commands)),
                               capture_output=True,check=True)
        replies={f[3]:f[4] for f in Decoder().feed(process.stdout) if f[1]==1}
        self.assertEqual(replies[1],bytes([0,0,7,0,1]))
        self.assertEqual(replies[3][:5],b'\0\x1b\0\x1b\0')
        self.assertEqual(replies[4],bytes([0,1,1,0])+wire)
        self.assertEqual(replies[5],bytes([7]))
        self.assertEqual(replies[7],replies[4])
        self.assertEqual(replies[9][:5],b'\0\x0f\0\x0f\0')
    def test_sync_bound_rejected_before_uart(self):
        with self.assertRaises(ValueError):gfsk_payload(sync=b'x'*9)
    def test_events_survive_command_response_and_boot_wait(self):
        master,slave=pty.openpty();link=EventLink(os.ttyname(slave))
        try:
            def sender():
                time.sleep(.02)
                os.write(master,encode(0x81,1,b'packet',kind=2)+encode(0x80,0,b'\0\7\0\0',kind=2)+encode(20,7,b'\0PONG',kind=1))
            thread=threading.Thread(target=sender);thread.start()
            self.assertEqual(link.receive(20,7),b'\0PONG')
            self.assertEqual(link.receive(0x80,0,kind=2),b'\0\7\0\0')
            self.assertEqual(link.event()[4],b'packet');thread.join()
        finally:link.close();os.close(master);os.close(slave)
    def test_bounded_event_queue_records_overflow(self):
        master,slave=pty.openpty();link=EventLink(os.ttyname(slave))
        try:
            os.write(master,b''.join(encode(0x81,i,b'x',kind=2) for i in range(70)))
            for _ in range(10):
                link.pump(.01)
                if len(link.radio_events)==64:break
            self.assertEqual(len(link.radio_events),64)
            self.assertEqual(link.dropped_events,6)
            self.assertEqual(link.event()[3],6)
        finally:link.close();os.close(master);os.close(slave)
if __name__=='__main__':unittest.main()
