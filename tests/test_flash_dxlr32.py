"""Offline safety/flow checks: no programmer or USB is opened."""
import importlib.util
import json
from pathlib import Path
import struct
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import patch

spec = importlib.util.spec_from_file_location('flasher', Path(__file__).parents[1] / 'bin/flash_dxlr32.py')
f = importlib.util.module_from_spec(spec)
spec.loader.exec_module(f)


class FlashTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.data = {'cpuid': struct.pack('<I', 0x410CC601), 'uid': bytes(range(16)),
                     'usercfg': b'\xff' * 12, 'flash': b'\xff' * 65536}
        self.backup = self.root / 'backup'
        self.backup.mkdir()
        for name, data in self.data.items():
            (self.backup / (name + '.bin')).write_bytes(data)
        (self.backup / 'manifest.json').write_text(json.dumps(
            {'format': 1, 'sha256': {k: f.digest(v) for k, v in self.data.items()}}))
        self.image = struct.pack('<II', 0x20001000, 0xc1) + bytes(248)
        self.path = self.root / 'controller.bin'
        self.path.write_bytes(self.image)
        self.args = SimpleNamespace(image=self.path, backup_dir=self.backup)

    def test_invalid_vectors_and_sizes(self):
        for data in (b'', bytes(65537), struct.pack('<II', 0x20002000, 0xc1),
                     struct.pack('<II', 0x20001000, 0xc0) + bytes(248)):
            with self.assertRaises(ValueError):
                f.validate_image(data)
        f.validate_image(self.image)

    def test_backup_hash_and_protection(self):
        self.assertEqual(f.read_backup(self.backup), self.data)
        (self.backup / 'flash.bin').write_bytes(bytes(65536))
        with self.assertRaises(ValueError):
            f.read_backup(self.backup)
        for name, value in [('cpuid', bytes(4)), ('uid', b'\xff' * 16), ('usercfg', bytes(12))]:
            with self.assertRaises(ValueError):
                f.validate_identity(dict(self.data, **{name: value}))

    def test_changed_flash_refuses_before_program(self):
        changed = dict(self.data, flash=bytes(65536))
        with patch.object(f, 'capture', return_value=changed), patch.object(f, 'run_openocd') as run:
            with self.assertRaises(ValueError):
                f.flash(self.args)
            run.assert_not_called()

    def test_bad_readback_never_boots(self):
        calls = []
        def runner(args, directory, name, commands):
            calls.append(name)
            (directory / 'readback.bin').write_bytes(bytes(len(self.image)))
        with patch.object(f, 'capture', return_value=self.data), patch.object(f, 'run_openocd', runner):
            with self.assertRaises(ValueError):
                f.flash(self.args)
        self.assertEqual(calls, ['program'])

    def test_success_guards_before_write_and_boot(self):
        calls = []
        def runner(args, directory, name, commands):
            calls.append(name)
            self.assertIn('require_word 0x180000f0', commands)
            if name == 'program':
                self.assertLess(commands.index('require_word'), commands.index('flash write_image erase'))
                self.assertNotIn('mass_erase', commands)
                self.assertNotIn('verify_image', commands)
                (directory / 'readback.bin').write_bytes(self.image)
            else:
                self.assertIn('reset halt\nresume', commands)
        with patch.object(f, 'capture', return_value=self.data), patch.object(f, 'run_openocd', runner):
            f.flash(self.args)
        self.assertEqual(calls, ['program', 'boot'])

    def test_tcl_paths_are_literal(self):
        self.assertTrue(f.tcl_path(self.root / 'a $name [cmd]; file').startswith('{'))
        for bad in ('bad{name', 'bad}name', 'bad\\name', 'bad\nname'):
            with self.assertRaises(ValueError):
                f.tcl_path(self.root / bad)


if __name__ == '__main__':
    unittest.main()
