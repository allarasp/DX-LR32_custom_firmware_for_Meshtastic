# v0.3.1 passive-crystal diagnostic

## Result: physical RC / XOSC / RC test passed

On 2026-11-04 SWD communication was restored at 100 kHz after the earlier
connection failures. The known target passed the private identity guard and
reported the expected Cortex-M0+ and 64 KB CX32L003 Flash configuration. A full
64 KB pre-programming readback was saved locally; its first 6,888 bytes exactly
matched the preserved working v0.3.0 image.

One guarded programming attempt installed v0.3.1 using the audited `cx32l003`
driver. OpenOCD verification and host byte-for-byte comparison of the 7,664-byte
readback both passed. Only the required application sectors were erased; no
read-protection removal, mass erase, unlock, or option-byte change was needed.
The existing USERCFG values remained unchanged. This does not add an independent
read-protection decoder; successful readback/programming establishes access for
this device state.

The documented volatile full-MCU reset started the image. CH340 captured the
binary BOOT event for v0.3.1 with initialization OK, and GET_VERSION confirmed
0.3.1 / protocol 1. The complete physical UART regression suite passed, including
PING/PONG, dynamic configuration/register readbacks, unsupported-frequency and
RF-operation rejection, CRC/framing errors, partial-frame timeout and recovery.

The **single** TEST_XOSC invocation returned:

```text
payload: 00ff00223222000000000000000012000000
status: OK; flags: 0x00FF; elapsed: 18 ms
initial RC status: 0x22; device errors: 0x0000
XOSC status:       0x32; device errors: 0x0000
final RC status:   0x22; device errors: 0x0000
first failure phase: 0; HAL error: 0
```

GET_CONFIG was identical before/after: active configuration remained valid,
dirty=false, applied/readback masks 0x0F, generation 5. GET_RF_SWITCH returned
`0000000000000000000000000800` both times: PC3/PC4 direction, pulls, alternate
functions and latch masks were zero; input mask was 0x08 (PC3 HIGH / PC4 LOW).
**Those are undriven input levels, not MCU-driven RF-switch states.** Final
GET_STATUS again returned standby RC 0x22 with zero errors.

No TX/RX/SetFs/PA, DIO2 RF-switch-control, DIO3 TCXO-control or PC3/PC4 drive
operation was issued. This verifies crystal mode entry/exit and radio responsiveness,
not crystal frequency accuracy, RF performance, or Meshtastic interoperability.
The RF-switch electrical load remains unresolved and active control stays disabled.

### Earlier connection failures

Before restoration, ST-Link repeatedly enumerated and measured about 3.31 V but
failed `init mode failed (unable to connect to the target)` at 100/50 kHz through
native SWD and HLA. v0.3.0 remained responsive over UART throughout those checks.
No erase or programming occurred until the identity/image checks above succeeded.
The failed-attempt logs were retained alongside the successful preflight.

The working v0.3.0 build outputs were preserved locally at
`logs/v0.3.1/recovery-v0.3.0/controller.{bin,elf,hex,map}` before rebuilding.
Its binary is 6,888 bytes with SHA-256
`f99f0d03f340f1a922397e2b99574ace544d81745f093a512ed1b817304b49d1`.
This is the preserved build artifact, not a new physical readback. The v0.2.1
text-diagnostic recovery source and local image also remain available.

## Physical evidence and scope

User-measured passive 3225 quartz wiring: pad 1 to SX1262 XTB (pin 4), pad 3
to XTA (pin 3), pads 2/4 to ground. SX1262 DIO3 (pin 6) connects to a capacitor
to ground. These observations support trying the external crystal without
SetDIO3AsTCXOCtrl. They do not establish oscillator amplitude, startup time,
frequency accuracy, capacitor loading, or RF performance.

The [Semtech SX1261/2 datasheet](https://resource.heltec.cn/download/WiFi_LoRa_32_V4/datasheet/SX1261_2%20V2-2.pdf)
(Semtech-authored PDF mirror) specifies SetStandby `80 00` for RC and `80 01`
for XOSC. GetStatus chip-mode bits 6:4 distinguish RC=2 from XOSC=3. GetDeviceErrors
reports oscillator-related failures; an SPI transaction completing is insufficient
proof. The implementation uses the existing pinned Semtech driver 2.5.0,
commit `a10c5dfdf89788c6ac805e9fe98889de44175aa2`.

Factory analysis independently found an initialization call to standby XOSC at
`0x5AEA`, after standby RC at `0x5AE2`; it is historical evidence only. Factory
regulator, RF-switch, fallback and trim settings were **not** copied into this
diagnostic. See [RF-switch investigation](rf-switch-investigation.md).

## Implemented test and safeguards

Binary command 18, TEST_XOSC, first validates that PC3/PC4 remain undriven,
requests standby RC, checks status and zero device errors, requests standby
XOSC, waits for BUSY, and verifies XOSC mode and errors. It then attempts a
bounded return to RC and verifies final status/errors, including after a
failure. Each BUSY wait is bounded to 100 ms plus a finite clock-stall guard;
each SPI-byte wait is bounded to 10 ms plus a finite iteration guard.

The reply preserves phase-specific raw status/error values and the first
failure even if cleanup fails. Successful tests preserve configuration and
generation; failures invalidate active configuration for explicit host recovery.
There is no automatic oscillator retry, reset, error clearing, or trim update.
There are no TX/RX, SetFs, PA, DIO2-control or DIO3-control operations. PC3/PC4
inspection performs reads only. The transport whitelist continues to reject
RF activation; SET_MODE still accepts only standby RC. Existing protocol IDs
and formats remain intact; see [UART_PROTOCOL.md](UART_PROTOCOL.md).

## Build and verification

Tools: GNU Arm GCC 13.2.1, Arm binutils 2.42, host GCC 13.3.0, Make 4.3,
Python 3.12.3. Audited local OpenOCD reports
`0.12.0+dev-g2431daa-dirty (2026-11-03-18:08)`; ST-Link reports V2J34S7.
The configured Flash driver is `cx32l003`, 64 KB at zero, 128 sectors of 512 B.
The restored connection allowed the guarded write, verification and readback.

Run from the project root:

```sh
python3 firmware/tools/prepare_deps.py
make -C firmware -j2 protocol-check radio-check uart-check check
arm-none-eabi-size -A firmware/build/uartctrl/controller.elf
arm-none-eabi-nm firmware/build/uartctrl/controller.elf
sha256sum firmware/build/uartctrl/controller.bin
```

Result: **7,664 B Flash; 992 B static SRAM (4 data + 988 BSS); 1,024 B stack**.
An additional 2,080 B separates static allocations and reserved stack. Vector
validation passes: initial SP `0x20001000`, reset vector `0x000000C1`.
Binary SHA-256:
`115756f460b1b09e180de7ae98c445d6d0810304287677155eaa96a2bf0911f1`.
No heap is used. Largest individual stack frames are 128 B; physical stack
watermark/high-water usage has not been measured.

Additional analysis commands (local outputs are ignored):

```sh
for unit in rf_switch radio_control radio_protocol radio_io uart_control radio_transport uart_system; do
  arm-none-eabi-gcc -mcpu=cortex-m0plus -mthumb -mfloat-abi=soft \
    -std=c11 -Os -Wall -Wextra -Werror -ffreestanding -fno-builtin \
    -DUART_RADIO_PROTOCOL -fanalyzer -Ifirmware/include \
    -Ivendor/cx32l003-dfp/Device/Include -Ivendor/CMSIS_5/CMSIS/Core/Include \
    -Ivendor/sx126x_driver/src -c "firmware/src/$unit.c" \
    -o "logs/v0.3.1/analyze-$unit.o"
done
cc -std=c11 -O1 -g -Wall -Wextra -Werror \
  -fsanitize=address,undefined -fno-omit-frame-pointer -DUART_RADIO_PROTOCOL \
  -Ifirmware/include -Ivendor/sx126x_driver/src \
  firmware/tests/test_radio_protocol.c firmware/src/radio_protocol.c \
  firmware/src/radio_control.c firmware/src/rf_switch.c \
  firmware/src/radio_transport.c vendor/sx126x_driver/src/sx126x.c \
  -o logs/v0.3.1/test_protocol_sanitized
logs/v0.3.1/test_protocol_sanitized
```

All passed. Tests cover successful RC/XOSC/RC, wrong mode, nonzero errors,
BUSY timeout, first-failure retention, cleanup, bad GPIO, unchanged successful
configuration, denied RF switching, and prior protocol/CRC/framing regressions.
These are mocked radio tests and do **not** demonstrate a working crystal.

## Connection attempts and retained local evidence

The private preflight verifies the known target identity and old firmware
before allowing programming. Earlier attempts failed at `init`; the restored
attempt completed the guard, image verification and full Flash readback.
Commands used:

```sh
tools/openocd-cx32/bin/openocd -f configs/bringup-v031-preflight.cfg
tools/openocd-cx32/bin/openocd -f configs/bringup-v031-preflight-slow.cfg
tools/openocd-cx32/bin/openocd -f configs/bringup-v031-program.cfg
tools/openocd-cx32/bin/openocd -f configs/bringup-full-reset.cfg
python3 bin/uart_radio.py --log logs/v0.3.1/still-v030 version
```

The private configuration files are intentionally not published because they
reference the hardware identity guard and local recovery artifacts. The
tracked starting configuration is `configs/cx32l003-bringup.cfg`; it is not a
standalone authorization to program an arbitrary connected device.

Local evidence, deliberately excluded from GitHub:

- `logs/v0.3.1/final-checks.log`, `static-analysis.log`, `sanitized.log`.
- `logs/v0.3.1/preflight{,-slow,-reattach,-ready,-retry,-hla,-soldered,-soldered-slow}.log`.
- `logs/v0.3.1/before-version.*`, `before-status.*`, `still-v030`, `after-solder-version.json`.
- `logs/factory-analysis/` archived binary/disassembly; never published/executed.

## Executed physical procedure

After SWD restoration, the target identity, Flash layout, preserved v0.3.0
image and new image were verified. The guarded script verified the full old
Flash readback, executed `flash write_image erase firmware/build/uartctrl/controller.bin 0 bin`,
verified the new binary, dumped exactly 7,664 bytes, and reran the identity guard
while halted. Host comparison matched before the documented full-MCU reset.
The archived factory firmware was not executed.

The regression utility was started with `--wait-boot --timeout 40` before
reset to capture BOOT, then ran the regression suite. The following physical
commands completed successfully (the first line includes the boot-capture flags):

```sh
python3 bin/uart_radio.py --wait-boot --timeout 40 --log logs/v0.3.1/physical-regression.json self-test
python3 bin/uart_radio.py --log logs/v0.3.1/physical-ping.json ping
python3 bin/uart_radio.py --log logs/v0.3.1/physical-rf-switch.json rf_switch
python3 bin/uart_radio.py --log logs/v0.3.1/physical-xosc.json xosc
```

All commands above were executed on v0.3.1. The XOSC utility issued exactly one
TEST_XOSC request and validated all reply fields plus before/after snapshots.
No retry was necessary. Physical logs are retained locally as
`logs/v0.3.1/physical-{regression,ping,rf-switch,xosc}.{json,log}`;
`preflight-restored.log`, `program.log`, `program-readback.bin`,
`before-full-flash.bin` and `first-boot-reset.log` preserve programming evidence.
They remain excluded from GitHub. No physical stack watermark or RF test was
performed; the measured mode transition does not establish RF performance.
