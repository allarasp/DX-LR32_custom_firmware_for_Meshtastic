"""Rebuild the inspected fork's RAM loader without newlib or opaque binary input."""
from pathlib import Path
import hashlib
import json
import subprocess

root = Path(__file__).resolve().parents[1]
repo = root / 'src/openocd-code'
source = repo / 'contrib/loaders/flash/cx32l003'
build = root / 'logs/bringup/loader-build'
build.mkdir(parents=True, exist_ok=True)
(build / 'include').mkdir(exist_ok=True)
# The sole libc use is memset; provide its standard declaration and the project's
# freestanding implementation rather than requiring a newlib package.
(build / 'include/string.h').write_text('#include <stddef.h>\nvoid *memset(void *, int, size_t);\n')
entry = (source / 'entry_cx32l003.S').read_text()
entry = entry.replace('.section .entry', '.section .entry,"ax",%progbits')
entry = entry.replace('entry:\n', '.thumb_func\nentry:\n')
entry = entry.replace('=_estack - 4', '=_estack')
(build / 'entry.S').write_text(entry)
commands = []
def run(args, **kwargs):
    commands.append([str(a) for a in args])
    return subprocess.check_output(args, text=True, **kwargs)
script = run(['arm-none-eabi-gcc', '-E', '-P', '-x', 'c', '-I'+str(source), str(source/'cx32l003_flash.lds')])
script = script.replace('_stack = .;', '. = ALIGN(8); _stack = .;')
script += '\nASSERT((_estack & 7) == 0, "Loader stack alignment")\nASSERT(_estack <= 0x20001000, "Loader exceeds SRAM")\nASSERT(_ebuffers <= 0x20000210, "Loader buffer overlap")\n'
(build / 'loader.ld').write_text(script)
flags = ['-mcpu=cortex-m0plus','-mthumb','-Os','-g3','-Wall','-Wextra','-Werror','-ffreestanding','-fno-builtin','-ffunction-sections','-fdata-sections','-fpack-struct','-I'+str(build/'include'),'-I'+str(source)]
for filename, output in [(source/'cx32l003_flash.c','loader.o'),(build/'entry.S','entry.o'),(root/'firmware/src/memory.c','memory.o')]:
    run(['arm-none-eabi-gcc',*flags,'-c',str(filename),'-o',str(build/output)])
run(['arm-none-eabi-gcc','-mcpu=cortex-m0plus','-mthumb','-nostdlib','-T'+str(build/'loader.ld'),'-Wl,-e,entry,-z,noexecstack,--gc-sections,-Map='+str(build/'loader.map'),str(build/'entry.o'),str(build/'loader.o'),str(build/'memory.o'),'-lgcc','-o',str(build/'loader.elf')])
run(['arm-none-eabi-objcopy','-O','binary',str(build/'loader.elf'),str(build/'loader.bin')])
symbols = run(['arm-none-eabi-nm','-n',str(build/'loader.elf')])
(build / 'loader.symbols').write_text(symbols)
addresses = {p[2]: int(p[0],16) for line in symbols.splitlines() if len(p:=line.split()) == 3}
assert addresses['g_cfg'] == 0x20000000 and addresses['g_buf'] == 0x20000010
assert addresses['_estack'] % 8 == 0 and addresses['_estack'] <= 0x20001000
assert addresses['entry'] == 0x20000210
assert not run(['arm-none-eabi-nm','-u',str(build/'loader.elf')]).strip()
blob = (build/'loader.bin').read_bytes()
assert 0 < len(blob) < 3568 - 512
inc = '/* Rebuilt from fork 2431daa; see bin/rebuild-cx32-loader.py. GPL-2.0-or-later. */\n'
inc += '\n'.join(','.join(f'0x{x:02x}' for x in blob[i:i+16])+',' for i in range(0,len(blob),16))+'\n'
(source/'cx32l003_algo.inc').write_text(inc)
(build/'loader.dis').write_text(run(['arm-none-eabi-objdump','-d',str(build/'loader.elf')]))
(build/'commands.json').write_text(json.dumps(commands,indent=2)+'\n')
print(f'Rebuilt loader: {len(blob)} bytes, stack=0x{addresses["_estack"]:08x}, sha256={hashlib.sha256(blob).hexdigest()}')
