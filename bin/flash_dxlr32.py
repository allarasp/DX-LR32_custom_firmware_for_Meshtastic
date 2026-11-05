#!/usr/bin/env python3
"""Explicit CX32L003F8 backup/programming using the project's inspected OpenOCD.

No USB access on import/help. See docs/FLASHING_DX_LR32.md before use.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
FLASH_SIZE = 65536
REGIONS = {'cpuid': (0xE000ED00, 4), 'uid': (0x180000F0, 16),
           'usercfg': (0x08000000, 12), 'flash': (0, FLASH_SIZE)}


def digest(data):
    return hashlib.sha256(data).hexdigest()


def tcl_path(path):
    value = str(Path(path).resolve())
    if any(c in value for c in '{}\\\n\r'):
        raise ValueError('Use a project/backup path without braces, backslashes or newlines')
    return '{' + value + '}'


def validate_identity(data):
    if data['cpuid'] != struct.pack('<I', 0x410CC601):
        raise ValueError('Unexpected Cortex-M CPUID; stop')
    if len(data['uid']) != 16 or data['uid'] in (bytes(16), b'\xff' * 16):
        raise ValueError('Invalid/unreadable UID; stop')
    if data['usercfg'] != b'\xff' * 12:
        raise ValueError('Unsupported USERCFG/protection state; no unlock or erase attempted')


def validate_image(data):
    if not 8 <= len(data) <= FLASH_SIZE:
        raise ValueError('Image must be an 8..65536-byte raw CX32 binary')
    stack, reset = struct.unpack_from('<II', data)
    if stack != 0x20001000 or not reset & 1 or not 8 <= (reset & ~1) < len(data):
        raise ValueError('Unexpected vector table; use the validated meshctrl controller.bin')


def guard(data):
    lines = ['proc require_word {address expected} {',
             ' if {[lindex [read_memory $address 32 1] 0] != $expected} {',
             '  error "Target identity/protection mismatch; stop before erase"', ' }', '}']
    for name in ('cpuid', 'uid', 'usercfg'):
        address, _ = REGIONS[name]
        for i, (word,) in enumerate(struct.iter_unpack('<I', data[name])):
            lines.append(f'require_word 0x{address + i * 4:08x} 0x{word:08x}')
    return '\n'.join(lines) + '\n'


def run_openocd(args, directory, name, commands):
    script = directory / (name + '.cfg')
    script.write_text(f'source {tcl_path(ROOT / "configs/cx32l003-bringup.cfg")}\n'
                      + commands + '\nshutdown\n')
    command = [str(args.openocd.resolve()), '-s', str(args.scripts.resolve()),
               '-f', str(script.resolve())]
    try:
        result = subprocess.run(command, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                text=True, timeout=120, check=False)
    except subprocess.TimeoutExpired as exc:
        output = exc.stdout or ''
        if isinstance(output, bytes):
            output = output.decode(errors='replace')
        (directory / (name + '.log')).write_text(output + '\nOpenOCD timed out; no retry.\n')
        raise
    (directory / (name + '.log')).write_text(result.stdout)
    if result.returncode:
        raise RuntimeError(f'OpenOCD failed. Inspect {directory / (name + ".log")}; do not blindly retry')


def capture(args, directory, name, expected=None):
    commands = 'init\nhalt\n' + (guard(expected) if expected else '')
    for region, (address, length) in REGIONS.items():
        commands += f'dump_image {tcl_path(directory / (region + ".bin"))} 0x{address:x} {length}\n'
    run_openocd(args, directory, name, commands)
    data = {k: (directory / (k + '.bin')).read_bytes() for k in REGIONS}
    if any(len(data[k]) != length for k, (_, length) in REGIONS.items()):
        raise ValueError('Incomplete backup/readback; stop')
    validate_identity(data)
    return data


def read_backup(directory):
    manifest = json.loads((directory / 'manifest.json').read_text())
    if manifest.get('format') != 1:
        raise ValueError('Unsupported backup manifest')
    data = {k: (directory / (k + '.bin')).read_bytes() for k in REGIONS}
    for k, (_, length) in REGIONS.items():
        if len(data[k]) != length or digest(data[k]) != manifest['sha256'][k]:
            raise ValueError(f'Backup {k} length/hash mismatch; stop')
    validate_identity(data)
    return data


def backup(args):
    directory = args.output.resolve()
    directory.mkdir(parents=True, exist_ok=False)
    data = capture(args, directory, 'backup')
    manifest = {'format': 1, 'chip_confirmation': args.confirm_chip,
                'sha256': {k: digest(v) for k, v in data.items()},
                'note': 'Contents at backup time; not proof of original factory firmware.'}
    (directory / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
    run_openocd(args, directory, 'resume', 'init\nhalt\n' + guard(data) + 'resume')
    print(f'Full 64 KB backup and identity saved privately in {directory}')


def flash(args):
    data = args.image.read_bytes()
    validate_image(data)
    original = read_backup(args.backup_dir)
    # Stage an immutable copy and compare the CURRENT full image with the backup
    # before any erase. A changed target needs its own new backup.
    with tempfile.TemporaryDirectory(prefix='dxlr32-flash-') as temp:
        directory = Path(temp)
        (directory / 'controller.bin').write_bytes(data)
        try:
            current = capture(args, directory, 'preflight', original)
            if current != original:
                raise ValueError('Target changed since backup. Make a new backup before flashing')
            commands = ('init\nhalt\n' + guard(original)
                        + f'flash write_image erase {tcl_path(directory / "controller.bin")} 0 bin\n'
                        + f'dump_image {tcl_path(directory / "readback.bin")} 0 {len(data)}\n'
                        + guard(original))
            run_openocd(args, directory, 'program', commands)
            if (directory / 'readback.bin').read_bytes() != data:
                raise ValueError('Host readback mismatch; target left halted, do not resume')
            run_openocd(args, directory, 'boot',
                        'init\nhalt\n' + guard(original) + 'reset halt\nresume')
        finally:
            # Retain all evidence even when programming fails; backup is untouched.
            evidence = args.backup_dir / ('flash-' + digest(data)[:12])
            evidence.mkdir(exist_ok=True)
            for file in directory.iterdir():
                (evidence / file.name).write_bytes(file.read_bytes())
    print(f'Programmed and host-readback verified {len(data)} bytes, SHA-256 {digest(data)}')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--openocd', type=Path, default=ROOT / 'tools/openocd-cx32/bin/openocd')
    parser.add_argument('--scripts', type=Path, default=ROOT / 'tools/openocd-cx32/share/openocd/scripts')
    sub = parser.add_subparsers(dest='command', required=True)
    for name in ('backup', 'flash'):
        command = sub.add_parser(name)
        command.add_argument('--confirm-chip', required=True, choices=['CX32L003F8'],
                             help='Physically verify the exact MCU/board; CPUID cannot do this')
        if name == 'backup':
            command.add_argument('--output', type=Path, required=True)
        else:
            command.add_argument('--image', type=Path, required=True)
            command.add_argument('--backup-dir', type=Path, required=True)
            command.add_argument('--confirm-overwrite', action='store_true', required=True)
    args = parser.parse_args()
    try:
        (backup if args.command == 'backup' else flash)(args)
    except (OSError, ValueError, KeyError, RuntimeError, subprocess.TimeoutExpired) as exc:
        parser.exit(1, f'STOP: {exc}\nInspect logs and wiring. No automatic unlock, mass erase or retry.\n')


if __name__ == '__main__':
    main()
