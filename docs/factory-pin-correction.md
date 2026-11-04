# Factory-code comparison resolves radio BUSY timeout — 2026-11-04

**Radio diagnostic v0.2.1 passes physical UART/SPI tests.** The original board
mapping reversed the two GPIOB signals, and our code inherited that mistake:

| Signal | Incorrect original mapping | Verified working mapping |
| --- | --- | --- |
| Radio NRESET (SX pin 15) | PB5 / MCU pin 8 | **PB4 / MCU pin 9** |
| Radio BUSY (SX pin 14) | PB4 / MCU pin 9 | **PB5 / MCU pin 8** |

NSS PA2, SCK PA1, MOSI PD3, MISO PD2 and DIO1 PD4 were unchanged. UART and SWD
pins were unchanged. This correction is established by factory-code analysis
and live radio responses; a new multimeter measurement was not available.

## What remained of the factory firmware

No complete factory backup exists. Earlier custom images erased only their
application sectors. A read-only 64 KB dump of current Flash revealed surviving
factory code from `0x1400` onward, including GPIO and radio routines. The dump
is a mixture of custom firmware and factory remnants, **not a restorable
factory image**. It stays in ignored local logs and was never executed.

The surviving code provides several independent pieces of evidence:

- Factory pin table at `0x7E6C` has three eight-byte entries: GPIOB/mask `0x10`
  (PB4), GPIOD/mask `0x10` (PD4), GPIOB/mask `0x20` (PB5).
- Factory GPIO setup at `0x3A62` onward configures PB5 as input and PB4 as
  output, then sets PB4 high.
- The radio reset routine at `0x68F4` writes table entry 0 low and high through
  the GPIO helper at `0x3B5C`. Its delay arguments are 10, 50 and 20; their time
  units were not established because the original delay implementation was
  overwritten. Our reset timing remains based on Semtech documentation.
- The BUSY wait at `0x6264` repeatedly reads table entry 2 through `0x3B34`
  until it is no longer high. This identifies **PB5 as BUSY**.
- Original SPI setup at `0x1B6A` onward uses AF3 for PA2 NSS, PD3 MOSI, PD2 MISO
  and PA1 SCK. Our PA2 GPIO chip select is valid and was physically verified;
  the other three pins retain their SPI AF3 selection.

The table location and helper call arguments matter: GPIO references alone
would not identify each signal's role. The initial startup, GPIO helper body
and some library code had been erased; the surviving call sites and constants
were cross-checked against live operation rather than treated as a full source
reconstruction. No factory binaries or disassembly are published.

## Live confirmation before programming

With the MCU halted through SWD, the test first released PB5 to input, then
configured PB4 as output and pulsed it low for 5 ms. After release and 100 ms
settling, GPIOB DIRCR was `0x10` and IDR was `0x10`: reset high, BUSY low.
A guarded read-only GetStatus transfer returned:

```text
MOSI C0 00
MISO A2 22
```

The second byte, `0x22`, indicates STBY_RC. The first byte is not the GetStatus
result. This explains the earlier measurements: we were driving the real BUSY
line on PB5 while reading the real reset line on PB4 as if it were BUSY.
The previous empty SPI traces and `0xFF` sentinels were therefore not radio
responses and did not establish a defective radio.

## Firmware correction and physical validation

`board.h` now defines RESET=4 and BUSY=5. `radio_io_init()` explicitly releases
BUSY before enabling the reset output. Version identifiers and the physical
UART test were updated to v0.2.1. The legacy experimental image shares the
corrected pin definitions; UART-only v0.1.1 remains unchanged as recovery.

Build and host validation:

```sh
make -C firmware radio-check uart-check check
arm-none-eabi-size -A firmware/build/radiodiag/controller.elf
```

All passed with the [previously documented tools and dependencies](BUILD.md).
The radio image uses **5,100 bytes Flash, 340 bytes static SRAM, and a reserved
1,024-byte stack**. Its SHA-256 is
`5fbece8f92d0ca1231fe96bc8a1a61c4f887d8aea6a071856cbba6a2a6982505`.
The default experimental image is now 1,596 bytes; full-driver linkage is
6,368 bytes. UART-only recovery is still 1,712 bytes.

One guarded programming attempt checked the previous Flash image, wrote and
verified v0.2.1, and read back all 5,100 firmware bytes for host comparison.
A full 64 KB readback also confirmed bytes `0x1400..0xFFFF` were unchanged.
No mass erase, protection removal or persistent configuration change occurred.
A full MCU reset then produced this physical CH340 transcript (reassembled):

```text
DXLR32 CUSTOM FW BOOT
RADIO_INIT OK raw=0x22 mode=STBY_RC errors=0x0000 busy=0 dio1=0 tx=17000000 rx=A2A20000 reset_busy_seen=1
PING -> PONG
VERSION -> DXLR32 RADIODIAG 0.2.1
STATUS -> STATUS CLK=22120000 BAUD=115200 RADIO=READY_RC
RADIO_STATUS -> RADIO_STATUS OK raw=0x22 mode=STBY_RC errors=0x0000 busy=0 dio1=0 tx=C000 rx=A222
RADIO_ERRORS -> RADIO_ERRORS OK raw=0x22 mode=STBY_RC errors=0x0000 busy=0 dio1=0 tx=17000000 rx=A2A20000
RADIO_TEST -> RADIO_TEST OK raw=0x22 mode=STBY_RC errors=0x0000 busy=0 dio1=0 tx=17000000 rx=A2A20000 gfsk=0x00 lora=0x01 restored=0x00
RADIO_RESET -> RADIO_RESET OK raw=0x22 mode=STBY_RC errors=0x0000 busy=0 dio1=0 tx=17000000 rx=A2A20000 reset_busy_seen=1
RADIO_TEST -> RADIO_TEST OK raw=0x22 mode=STBY_RC errors=0x0000 busy=0 dio1=0 tx=17000000 rx=A2A20000 gfsk=0x00 lora=0x01 restored=0x00
PING -> PONG
```

Packet-type changes/readbacks prove more than a constant SPI status byte.
They occur entirely in RC standby; **no RF transmission, RX/FS request, DIO2
RF-switch control or DIO3 TCXO control was issued**. Crystal versus TCXO and
RF-switch wiring remain unknown. These command responses establish functional
communication with the onboard SX126x-compatible radio; they are not a unique
silicon part-identification mechanism. LoRa packet operation and Meshtastic
integration remain untested. v0.2.1 boot after software reset was captured;
the earlier full removal-of-power test was on v0.1.1.

## Local commands and evidence

```sh
tools/openocd-cx32/bin/openocd -f configs/bringup-factory-remainder.cfg
arm-none-eabi-objdump -D -b binary -m arm -M force-thumb logs/factory-analysis/current-flash.bin
tools/openocd-cx32/bin/openocd -f configs/bringup-factory-pin-test.cfg
tools/openocd-cx32/bin/openocd -f configs/bringup-factory-pin-program.cfg
cmp firmware/build/radiodiag/controller.bin logs/factory-analysis/corrected-readback.bin
tools/openocd-cx32/bin/openocd -f configs/bringup-full-reset.cfg
python3 bin/radio-bringup-test.py --port /dev/serial/by-id/usb-1a86_USB2.0-Ser_-if00-port0 --wait-boot --timeout 180 --log-dir logs/factory-analysis/uart
```

The ignored programming script uses the private target guard, checks the full
previous image, then runs `flash write_image erase ... 0 bin`, `verify_image`,
and `dump_image` before leaving the CPU halted for host comparisons. Only the
first ten 512-byte sectors are erased. The OpenOCD version/driver and unchanged
protection state are as documented in [radio bring-up](radio-bring-up.md).

Evidence is retained under `logs/factory-analysis/`: `read.log`,
`current-flash.bin`, `current-flash.dis`, `factory-pin-test.log`,
`build-checks.log`, `program.log`, `corrected-readback.bin`, `after-flash.bin`,
`full-reset.log`, `uart.log`, and `uart/transcript.json`. Device-specific guard
scripts are under ignored `configs/bringup-*`; raw logs, unique IDs and factory
remnants are excluded from GitHub.
