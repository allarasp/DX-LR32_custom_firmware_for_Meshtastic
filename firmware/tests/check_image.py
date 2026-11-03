"""Validate the offline artifact, not the attached hardware. Standard library only."""
from pathlib import Path
import struct
import subprocess
import os
import sys

stem = Path(sys.argv[1] if len(sys.argv) > 1 else "build/controller")
elf_path = str(stem.with_suffix(".elf"))
elf = Path(elf_path).read_bytes()
binary = stem.with_suffix(".bin").read_bytes()
assert elf[:7] == b"\x7fELF\x01\x01\x01", "ELF32 little endian required"
header = struct.unpack_from("<16sHHIIIIIHHHHHH", elf)
assert header[2] == 40, "ARM ELF required"
entry, phoff, phsize, phcount = header[4], header[5], header[9], header[10]
stack, reset = struct.unpack_from("<II", binary)
assert stack == 0x20001000 and stack % 8 == 0
assert reset & 1 and reset == entry and reset < 65536
assert 192 <= len(binary) <= 65536
flash_end = 0
for i in range(phcount):
    kind, offset, va, pa, filesz, memsz, flags, align = struct.unpack_from("<8I", elf, phoff + i * phsize)
    if kind != 1:
        continue
    assert (va < 65536 and va + memsz <= 65536) or (0x20000000 <= va <= va + memsz <= 0x20001000)
    if filesz:
        assert pa + filesz <= 65536, "Load image outside application flash"
        flash_end = max(flash_end, pa + filesz)
        assert binary[pa:pa + filesz] == elf[offset:offset + filesz]
assert flash_end == len(binary), "Binary load span mismatch"
prefix = os.environ.get("CROSS_COMPILE", "arm-none-eabi-")
assert not subprocess.check_output([prefix + "nm", "-u", elf_path]).strip(), "Unresolved symbols"
symbols = subprocess.check_output([prefix + "nm", elf_path], text=True)
addresses = {fields[2]: int(fields[0], 16) for line in symbols.splitlines() if len(fields := line.split()) == 3}
assert addresses["_ebss"] <= addresses["_stack_limit"] == 0x20000c00
assert struct.unpack_from("<I", binary, 15 * 4)[0] == addresses["SysTick_Handler"] | 1
print(f"Image checks passed: {len(binary)} flash bytes, MSP=0x{stack:08x}, reset=0x{reset:08x}, no unresolved symbols")
