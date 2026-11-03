# SX1262 non-transmitting bring-up — 2026-11-03

## Actual hardware result

The previous UART-only v0.1.1 survived a user-confirmed full removal of target
power (VDD checked at 0 V), followed by restoration of the single supply.
The CH340 captured the complete boot banner, VERSION, STATUS and six PING/PONG
exchanges at 115200 8N1. The power transition also produced partial/noisy bytes;
these were not treated as the banner or a successful command exchange.

Radio diagnostic v0.2.0 was then programmed in **one deliberate attempt**.
The previously reviewed CX32L003 OpenOCD driver erased only application sectors
0–9 (512 bytes each), wrote 5,100 bytes at address 0, and verified the image.
A separate complete 5,100-byte Flash dump matched the binary byte-for-byte:

```text
SHA-256 987b8d0ecc8842a5e472dbec43e0e4fc2dd002eecd4f6ed62af0fedf44f3f342
```

No protection removal, mass erase, option-byte change or boot configuration
change was needed. CPUID, private identity guard and unchanged USERCFG words
were checked before and after programming. The CPUID identifies the CPU class,
not the exact part number; the CX32L003F8 identification also relies on the
previously inspected package and board evidence.

After a full MCU reset through its documented volatile RCC reset registers,
the new firmware emitted the boot banner. **Radio initialization failed.**
The following are reassembled physical CH340 responses, not mock results:

```text
DXLR32 CUSTOM FW BOOT
RADIO_INIT ERROR reason=BUSY_TIMEOUT raw=0xFF mode=UNVERIFIED errors=0xFFFF busy=1 dio1=0 tx= rx= spi_sr=0x00 reset_busy_seen=1
PING -> PONG
VERSION -> DXLR32 RADIODIAG 0.2.0
STATUS -> STATUS CLK=22120000 BAUD=115200 RADIO=ERROR
RADIO_STATUS -> RADIO_STATUS ERROR reason=BUSY_TIMEOUT raw=0xFF mode=UNVERIFIED errors=0xFFFF busy=1 dio1=0 tx= rx= spi_sr=0x00
RADIO_ERRORS -> RADIO_ERRORS ERROR reason=BUSY_TIMEOUT raw=0xFF mode=UNVERIFIED errors=0xFFFF busy=1 dio1=0 tx= rx= spi_sr=0x00
RADIO_TEST -> RADIO_TEST ERROR reason=BUSY_TIMEOUT raw=0xFF mode=UNVERIFIED errors=0xFFFF busy=1 dio1=0 tx= rx= spi_sr=0x00 gfsk=0xFF lora=0xFF restored=0xFF
RADIO_RESET -> RADIO_RESET ERROR reason=BUSY_TIMEOUT raw=0xFF mode=UNVERIFIED errors=0xFFFF busy=1 dio1=0 tx= rx= spi_sr=0x00 reset_busy_seen=1
PING -> PONG
```

`0xFF` status and `0xFFFF` errors here are **unread sentinel values**, not SX1262
responses. Empty TX/RX fields mean the BUSY guard prevented any SPI clocks.
UART remains responsive after failures. The physical radio test exits nonzero.
No LoRa transmissions, RX/FS requests, DIO2 RF-switch control or DIO3 TCXO
control were issued. Radio communication has **not** been established.

Live SWD inspection found GPIOB DIRCR=`0x20`, OTYPER=`0`, ODR=`0x20`,
IDR=`0x10`, AFR=`0`: PB5 is configured as a push-pull GPIO output with a high
latch, but reads low; PB4/BUSY reads high. A direct, bounded low/high reset probe
changed PB5's latch as intended without changing this input observation.
RCC LXTCR=`0x42F` has LXTPORT and LXTEN clear, so the MCU's 32 kHz oscillator
is not selecting those GPIOs. GPIO/peripheral clocks and mux registers were
also inspected. Physical NRESET, BUSY and radio supply voltage measurements
are needed to distinguish an electrical/reset issue from further MCU setup
issues. Do not bypass BUSY or infer that a longer timeout establishes SPI.

## Implementation and review

`radiodiag` reuses the verified clock, UART, startup and linker code. The
unchanged `uartdiag` v0.1.1 target remains the recovery image. Its binary still
hashes to `a2aa71f27ed74a54ab7dcc69846135a9bfd975df4fc2802a4a594ad27847e8fc`.
The original experimental foundation and full-driver linkage target remain.

- PA1/PD3/PD2 use AF3 for SPI clock/MOSI/MISO; PA2 is GPIO NSS.
- PB5 reset is preloaded inactive before output enable. PB4 BUSY and PD4 DIO1
  are inputs. SWD PC7/PD1 and UART PD5/PD6 fields are preserved.
- CX32 manual SPI control table 24-2 selects mode 0, MSB first, master,
  Fsys/64: nominal 345,625 Hz with the verified 22.12 MHz HIRC.
- NSS stays low across the entire command, dummy and response sequence.
  The MCU's internal SSN register follows the GPIO NSS level.
- Reset is low for two millisecond tick transitions, released even on timer
  failure, followed by 10 ms settling and a 100 ms BUSY deadline.
- Each SPI byte has a 10 ms deadline. Waits also have finite spin backstops
  and service an already-running watchdog. NSS is released on transfer error.
- A two-tick post-command delay precedes BUSY polling, covering Semtech's
  documented maximum 600 ns delay before BUSY assertion after NSS rises.
- GetStatus frame: MOSI `C0 00`, status from the second MISO byte.
  GetDeviceErrors: MOSI `17 00 00 00`, error MSB/LSB from MISO bytes 3/4.
- Responses must indicate STBY_RC with no command error or reserved error bits.
  Completing a shift alone never establishes communication.
- RADIO_TEST changes packet type GFSK → LoRa and checks both readbacks, then
  restores the original packet type. This runs only in standby RC. It makes
  no frequency, PA, RX or TX request. Restoration is attempted after a failed
  state-changing test; failure remains visible.
- The new transport explicitly allows only standby RC, packet-type changes,
  and a small set of reads; RF, DIO2 and DIO3 commands are rejected.
  Device errors are reported without automatically clearing them.

The user reports a **32 MHz** oscillator marking. Passive crystal versus active
TCXO, DIO3-to-supply wiring, and DIO2-to-RF-switch wiring are still unknown.
A frequency marking cannot resolve these. RC standby tests do not need guessed
settings; neither `SetDIO3AsTCXOCtrl` nor `SetDIO2AsRfSwitchCtrl` is enabled.
The hardware must be traced before future RF operation.

## Sources

- [Semtech SX1262 product and documentation](https://www.semtech.com/products/wireless-rf/lora-connect/sx1262).
- Semtech-authored SX1261/2 datasheet, revision 2.2, December 2024:
  [PDF hosted by Heltec](https://resource.heltec.cn/download/WiFi_LoRa_32_V4/datasheet/SX1261_2%20V2-2.pdf),
  sections 8.1–8.3 and command reference. The vendor download endpoint was not
  directly retrievable, so this is explicitly a document mirror.
  PDF SHA-256: `6d783125dbef567954ce53095ad068d0950b2a8de52f95bb2cfde4146c4306c2`.
- [Pinned Semtech driver source](https://github.com/Lora-net/sx126x_driver/blob/a10c5dfdf89788c6ac805e9fe98889de44175aa2/src/sx126x.c)
  supplies the actual command encoding; dependency pins are unchanged.
- CX32 GPIO AF table, manual sections 9 and 24, and reviewed flash-driver
  provenance are linked in [the earlier bring-up record](bring-up.md) and
  [build documentation](BUILD.md). STM32 peripheral layouts were not assumed.

## Reproduce the build and tests

```sh
python3 firmware/tools/prepare_deps.py
make -C firmware radio-check uart-check check
arm-none-eabi-size -A firmware/build/radiodiag/controller.elf
```

The same versions as [BUILD.md](BUILD.md) were used. New firmware: 5,100 bytes
Flash, 340 bytes static SRAM, 1,024-byte reserved stack, no heap. Initial MSP
is `0x20001000`, Thumb reset entry is `0x000000C1`, vectors start at Flash 0.
Host radio tests cover golden SPI frames, reversible packet readbacks, invalid
responses, stuck BUSY/tick, SPI failure cleanup, reset release and RF command
rejection. These mocked tests passed; the physical test failed as stated above.

Commands used for this target-specific programming/debug session:

```sh
lsusb
python3 bin/uart-bringup-test.py --port /dev/serial/by-id/usb-1a86_USB2.0-Ser_-if00-port0 --timeout 600 --log-dir logs/radio-bringup/power-cycle
# Local ignored scripts include the previously recorded private target guard.
tools/openocd-cx32/bin/openocd -f configs/bringup-radio-program.cfg
cmp firmware/build/radiodiag/controller.bin logs/radio-bringup/flash-readback.bin
sha256sum firmware/build/radiodiag/controller.bin logs/radio-bringup/flash-readback.bin
tools/openocd-cx32/bin/openocd -f configs/bringup-full-reset.cfg
tools/openocd-cx32/bin/openocd -f configs/bringup-radio-inspect.cfg
tools/openocd-cx32/bin/openocd -f configs/bringup-radio-pins.cfg
tools/openocd-cx32/bin/openocd -f configs/bringup-radio-reset-probe.cfg
python3 bin/radio-bringup-test.py --port /dev/serial/by-id/usb-1a86_USB2.0-Ser_-if00-port0 --wait-boot --timeout 180
python3 bin/radio-bringup-test.py --port /dev/serial/by-id/usb-1a86_USB2.0-Ser_-if00-port0 --diagnose --log-dir logs/radio-bringup/uart-diagnosis
```

The programming script checked the old recovery binary, called
`flash write_image erase firmware/build/radiodiag/controller.bin 0x0 bin`,
`verify_image firmware/build/radiodiag/controller.bin 0x0 bin`, and
`dump_image logs/radio-bringup/flash-readback.bin 0x0 5100`, then stayed halted
until the host comparison passed. OpenOCD was
`0.12.0+dev-g2431daa-dirty`, using its reviewed `cx32l003` driver, native ST-Link
V2J34S7 at 100 kHz; observed target voltage was 3.303 V. No ST-Link firmware
was changed. Private scripts are retained locally, not distributed as an
unguarded automatic flasher.

Local evidence under ignored `logs/radio-bringup/`: `power-cycle-capture.log`,
`power-cycle/uart-transcript.json`, `offline-checks.log`, `program.log`,
`flash-readback.bin`, `full-reset.log`, `register-inspect.log`, `pins.log`,
`reset-probe.log`, `uart-capture.log`, `uart/transcript.json`,
`uart-diagnosis.log`, and `uart-diagnosis/transcript.json`.
The recovery binaries remain in `recovery-v0.1.1/`. Raw logs and hardware
unique identifiers are intentionally excluded from GitHub.
