"""Independent Python codec and Python->C protocol integration tests (no USB)."""
import importlib.util
from pathlib import Path
import subprocess
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location('uart_radio', ROOT/'bin/uart_radio.py')
radio = importlib.util.module_from_spec(spec)
spec.loader.exec_module(radio)

class CodecTests(unittest.TestCase):
    def test_golden_crc_and_frame(self):
        # Independently fixed CRC-16/CCITT-FALSE check and GET_VERSION request.
        self.assertEqual(radio.binascii.crc_hqx(b'123456789', 0xffff), 0x29b1)
        self.assertEqual(radio.encode(1, 1).hex(), '7e010001010000004a957e')

    def test_all_octets_and_fragmentation(self):
        payload = bytes(range(256))
        encoded = radio.encode(11, 0x7e7d, payload)
        decoder = radio.Decoder()
        frames = []
        for b in encoded:
            frames.extend(decoder.feed(bytes([b])))
        self.assertEqual(frames, [(1,0,11,0x7e7d,payload)])

    def test_bad_crc_truncated_noise_and_recovery(self):
        d = radio.Decoder()
        self.assertEqual(d.feed(b'noise'+radio.encode(1,1,bad_crc=True)), [])
        self.assertEqual(d.feed(radio.encode(1,2)[:-2]), [])
        result = d.feed(b'\x7e\x7d\x00\x7e'+b'\x7e'+b'x'*1024+b'\x7e'+radio.encode(1,3))
        self.assertEqual(result, [(1,0,1,3,b'')])
        self.assertLessEqual(len(d.body), radio.MAX_PAYLOAD+9)

    def test_disconnect_cleanup_closes_fd(self):
        link = radio.Link.__new__(radio.Link)
        link.fd, link.original = 123, []
        with patch.object(radio.termios, 'tcsetattr', side_effect=radio.termios.error(5,'disconnected')), patch.object(radio.os, 'close') as close:
            link.close()
            close.assert_called_once_with(123)

    def test_oversize_is_explicit(self):
        with self.assertRaises(ValueError):
            radio.encode(11,1,b'x'*273)

    def test_python_client_against_real_c_parser_and_service(self):
        packets = [radio.encode(1,0x7e7d), radio.encode(4,2,(868000000).to_bytes(4,'little')),
                   radio.encode(1,3,bad_crc=True), radio.encode(1,4,version=2),
                   radio.encode(1,5,b'x'), radio.encode(11,6,b'\x7d\x7e'*136),
                   radio.encode(8,7), radio.encode(17,8)]
        process = subprocess.run([str(ROOT/'firmware/build/uartctrl/test_protocol'),'--bridge'],
                                 input=b''.join(packets),capture_output=True,check=True)
        frames = radio.Decoder().feed(process.stdout)
        responses = {f[3]:f for f in frames if f[1]==1}
        self.assertEqual(responses[0x7e7d][4], bytes([0,0,3,0,1]))
        for seq,status in [(2,9),(3,3),(4,1),(5,6),(6,13),(7,0),(8,0)]:
            self.assertEqual(responses[seq][4][0],status)
        self.assertEqual(responses[8][4][1:4], bytes([1,0xb4,0x24]))

if __name__ == '__main__':
    unittest.main()
