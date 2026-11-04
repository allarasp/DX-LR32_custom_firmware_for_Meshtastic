# v0.3.1 passive-crystal diagnostic

## Result: offline preparation complete; hardware test pending

On 2026-11-04 the v0.3.1 image built and all host tests passed. **It has not
been flashed and TEST_XOSC has not run on the physical SX1262.** The target
continues to report firmware 0.3.0 over CH340 UART. No erase, unlock, option-byte
change, programming or RF activation occurred during this stage.

ST-Link enumerates and reports approximately 3.31 V, but OpenOCD cannot enter
SWD: `init mode failed (unable to connect to the target)`. Native SWD at 100
and 50 kHz, USB reattachment, contact reseating, and HLA at 50 kHz did not
restore access. UART GET_VERSION returned payload `00 00 03 00 01` (OK,
firmware 0.3.0, protocol 1). The user reported that SWD contacts need soldering.
After the user requested another attempt, both USB adapters were reattached
to WSL. ST-Link again failed at 100 and 50 kHz (3.31 V); UART again returned
v0.3.0. Further programming was stopped. Target identity and
current Flash contents could not be reverified, so guarded programming did
not begin. USB enumeration/target voltage alone do not establish MCU identity.

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
No driver operation was reached because target connection failed.

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
before allowing programming. It failed at `init`, before its halt/guard/readback
steps. Exact invoked preflight commands:

```sh
tools/openocd-cx32/bin/openocd -f configs/bringup-v031-preflight.cfg
tools/openocd-cx32/bin/openocd -f configs/bringup-v031-preflight-slow.cfg
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

## Pending physical procedure

After soldering restores SWD, reverify target identity, Flash layout and the
v0.3.0 recovery image. Review the new image whitelist/vector layout again,
then perform one guarded program/verify/readback attempt using the previously
audited CX32L003 procedure. Compare readback with the binary before resetting.
Do not erase blindly or run the mixed factory archive.

After verified boot, run the existing UART regression test and binary PING,
then run the XOSC diagnostic once:

```sh
python3 bin/uart_radio.py --log logs/v0.3.1/physical-regression.json self-test
python3 bin/uart_radio.py --log logs/v0.3.1/physical-ping.json ping
python3 bin/uart_radio.py --log logs/v0.3.1/physical-rf-switch.json rf_switch
python3 bin/uart_radio.py --log logs/v0.3.1/physical-xosc.json xosc
```

These commands are **pending, not executed on v0.3.1**. Success requires real
reply status OK, flags 0x00FF, mode bits RC/XOSC/RC, zero errors at all three
steps, preserved configuration, and unchanged RF GPIO configuration/latches.
If it fails, preserve the first response and investigate without repeatedly
resetting or driving unknown RF-control pins. Even success would establish
oscillator mode entry only, not RF performance or Meshtastic interoperability.
