# DX-LR32 UART radio firmware

Firmware under development for using the **DX-LR32-A (433 MHz)** LoRa module as an external **UART-connected SX1262 radio controller**.

The CX32 firmware is a raw LoRa radio controller usable by other UART host applications, with Meshtastic as the first integration. It does not parse Meshtastic packets or manage encryption.

The goal of the Meshtastic integration is to build a standalone Meshtastic messaging device with an **ESP32-2432S028 CYD touchscreen**, without requiring a phone or Bluetooth. The ESP32 would run the Meshtastic network stack and user interface; the DX-LR32's existing **CX32L003F8** microcontroller would control its SX1262 over SPI.

> **Development status — a two-way public-channel text exchange with a phone-connected 433 MHz LilyGO TTGO is verified.** The WSL bridge and meshctrl v0.4.0 exchanged real EU_433 LongFast messages. This establishes the tested text/profile path, not complete Meshtastic routing or feature compatibility. The CYD radio adapter still needs a wired test. See [peer-test evidence](docs/meshtastic-peer-test.md).

**Current firmware: v0.6.0**, programmed and readback-verified. The same UART
frequency command selects **433–434 MHz or 863–870 MHz** directly, without a
separate mode/enable flag. TX uses UART-selected modem/packet settings and power
(-9..+22 dBm commanded); regional policy belongs to the host. Recovery,
duplicate-TX protection, packet/error counters and asynchronous UART output are
retained. Size: **12,920 B Flash, 2,392 B static RAM + 1 KB stack**. See
[v0.6.0 dual-band results and usage](docs/v0.6.0-results.md) and the
[UART contract](docs/UART_LIVE.md). Earlier v0.5.x peer exchanges passed; those
results do not establish measured 868 MHz RF performance or complete Meshtastic
router compatibility.

**The preserved v0.3.1 recovery image was programmed, readback-verified and physically tested.** Its bounded
crystal diagnostic reported standby RC → XOSC → RC (`0x22 → 0x32 → 0x22`),
zero errors at every step, and 18 ms elapsed. UART regression tests and binary
PING/PONG pass. Build size is 7,664 B Flash and 992 B static SRAM plus 1 KB stack.
See [measured oscillator results](docs/oscillator-test.md) and
[RF-switch investigation](docs/rf-switch-investigation.md). PC3/PC4 remained
undriven inputs during that stage. The load remains unresolved. RX/TX,
DIO2 control and DIO3 TCXO control remain disabled in that firmware.
The oscillator diagnostic itself performed no RF operation.

A later, explicitly authorized [RAM-only RX/TX experiment](docs/radio-rxtx-smoke-test.md)
verified RX mode/RSSI and one -9 dBm-commanded four-byte transmission with TxDone
at 433.500 MHz. No peer was used for that early smoke test; the later
LongFast exchange is documented separately below. Flash v0.3.1 was unchanged; its normal RX/TX commands remain disabled.
The RF-switch load remains unresolved, and safe sustained operation is not established.

The separate experimental **meshctrl v0.4.0** image and WSL bridge passed a
real public-channel round trip: the peer sent `Alr1 ready`, our one test packet
arrived in the phone app, and the bridge received the peer's reply `Recived`.
The radio was then returned to standby RC with zero errors and switch GPIOs
released. v0.4.0 remains 11,784 B Flash / 1,304 B static SRAM + 1 KB stack;
v0.3.1 remains a buildable recovery target. See [measured results](docs/meshtastic-peer-test.md),
[WSL test guide](docs/MESHTASTIC_TEST.md) and [live UART additions](docs/UART_LIVE.md).

A custom Meshtastic **2.7.26** build has also been uploaded to the two-USB
ESP32-2432S028 CYD. Flash hashes, boot and the official serial API were verified. The revised UI
adds calibrated touch, Prev/Menu/Next/Chat buttons and an on-screen keyboard;
the user confirmed improved usability.
EU_433/LongFast and Bluetooth are enabled, while RF TX is disabled. The CYD is
not yet wired to the DX-LR32, so the adapter's end-to-end radio path is untested.
See [CYD build/wiring](cyd/README.md) and [actual bring-up results](docs/cyd-bringup.md).

## Architecture

```text
ESP32-2432S028 CYD
  Meshtastic + touchscreen UI
           |
       UART 115200
           |
DX-LR32-A (CX32L003F8 MCU)
  custom radio-controller firmware
           |
           SPI
           |
     SX1262 (433 MHz)
           |
        LoRa mesh
```

## Hardware

- **DX-LR32-A:** 433 MHz LoRa module with Semtech SX1262.
- **CX32L003F8 (ZB003F8):** Cortex-M0+, 64 KB Flash, 4 KB RAM.
- **ST-Link V2:** SWD programming/debugging.
- **CH340 USB–UART:** serial diagnostics, 115200 baud, 8N1.
- **ESP32-2432S028 CYD:** planned standalone touchscreen host.

### SX1262 ↔ CX32L003F8

The SPI/DIO1 connections were traced using a multimeter. The original RESET/BUSY assignment was reversed; the corrected mapping below was established from surviving factory code and verified with real SPI responses. See [the pin correction and physical tests](docs/factory-pin-correction.md). IC pin numbers are package pin numbers.

| SX1262 signal (pin) | CX32L003F8 port (pin) |
| --- | --- |
| NSS (19) | PA2 (3) |
| SCK (18) | PA1 (2) |
| MOSI (17) | PD3 (17) |
| MISO (16) | PD2 (16) |
| NRESET (15) | PB4 (9) |
| BUSY (14) | PB5 (8) |
| DIO1 (13) | PD4 (18) |

### External DX-LR32 connections

| Module signal | CX32L003F8 port (pin) |
| --- | --- |
| AUX | PA3 (7) |
| TXD | PD5 / UART0_TX (19) |
| RXD | PD6 / UART0_RX (20) |
| M1 | PC6 (13) |
| M0 | PC5 (12) |

Each of these five external signal lines has a measured **100 Ω series resistor**. Connect UART TX to the host's RX and UART RX to the host's TX. Use compatible logic levels and a common ground.

### SWD pads on the module

| Pad | Connection |
| --- | --- |
| 1 | NRST (CX32 pin 1) |
| 2 | SWDCLK / PD1 (CX32 pin 15) |
| 3 | SWDIO / PC7 (CX32 pin 14) |
| 4 | GND |
| 5 | VCC |

Ensure a single safe power arrangement before connecting a programmer or USB–UART adapter.

For QFN pin-1 orientation and counterclockwise physical pin numbering, SWD pad numbering, and photos taken after removal of the RF shield, see the [hardware photographs and pin-counting guide](docs/hardware-photos-and-pin-counting.md).

## Verified progress

- [x] Mapped MCU↔SX1262, external UART, and SWD connections.
- [x] Established ST-Link V2 SWD connection at 100 kHz.
- [x] Identified the Cortex-M0+ and checked the target Flash.
- [x] Built and flashed custom diagnostic firmware **v0.1.1**.
- [x] Verified boot, firmware readback, and physical UART communication.
- [x] Verified `PING` / `PONG`, `VERSION`, and `STATUS` over UART at 115200 8N1.
- [x] Verify v0.1.1 boot and UART after a full power cycle.
- [x] Build, program and readback-verify radio diagnostic **v0.2.1**; UART remains working.
- [x] Resolve BUSY timeout by correcting RESET=PB4 / BUSY=PB5.
- [x] Initialize SX1262 in standby RC, read status/errors, and verify reversible GFSK/LoRa packet-type changes over SPI.
- [x] Build/flash v0.3.0 and physically test binary UART framing, dynamic configuration, readbacks, validation and recovery.
- [ ] Validate receive/transmit of LoRa packets.
- [ ] Implement the ESP32-side Meshtastic UART radio adapter.
- [ ] Implement and test the standalone CYD touchscreen interface.
- [ ] Demonstrate interoperability with a real Meshtastic node.

## Source code and setup

The firmware sources, GNU startup/linker code, tests, and generic OpenOCD configurations are included. On Ubuntu/WSL, install the build prerequisites and fetch the pinned source dependencies:

```sh
sudo apt-get install --no-install-recommends gcc-arm-none-eabi binutils-arm-none-eabi build-essential git python3
python3 firmware/tools/prepare_deps.py
make -C firmware -j2 protocol-check radio-check uart-check check
```

- `firmware/build/uartctrl/controller.elf`: **v0.3.1 binary UART controller**, programmed/readback-verified; physical UART regression and RC/XOSC/RC tests pass. 7,664 bytes Flash, 992 bytes static SRAM plus 1 KB stack. Its v0.3.0 predecessor was programmed/readback-verified and physically tested after power restoration. Dynamic center frequency is sent as integer Hz; capability responses distinguish the declared 433–532 MHz module band from the current 433–434 MHz EU_433 policy. 868 MHz and RF activation are rejected.
- `firmware/build/radiodiag/controller.elf`: **v0.2.1 radio diagnostics**, flashed and UART-tested; 5,100 bytes Flash, 340 bytes static SRAM plus 1 KB stack. `RADIO_RESET`, `RADIO_STATUS`, `RADIO_ERRORS`, and `RADIO_TEST` are implemented with timeouts. Physical reset, initialization, status/error queries and reversible packet-type checks pass: status `0x22`, errors `0x0000`. No RF transmissions or DIO2/DIO3 control.
- `firmware/build/uartdiag/controller.elf`: the **v0.1.1 UART diagnostic firmware tested on hardware**, using 115200 8N1. Its binary is 1,712 bytes; static SRAM is 60 bytes, plus a reserved 1 KB stack.
- `firmware/build/controller.elf`: the **experimental SX1262 foundation**, built and host-tested but not flashed or radio-verified. It currently uses 9600 baud and a 4 MHz clock; it is a separate development image.
- `firmware/src/`, `firmware/include/`: firmware implementations and the SX126x platform hooks.
- `firmware/tests/`: image, binary/text parser, configuration, Python codec, and mocked radio HAL tests.
- `configs/`, `bin/`: generic debug configuration and optional diagnostic/tool-build helpers. Builds and tests do not access hardware.

See the [binary UART protocol](docs/UART_PROTOCOL.md), [v0.3.0 build/physical results and Meshtastic architecture review](docs/uart-protocol-results.md), [verified radio results and factory-code pin correction](docs/factory-pin-correction.md), [earlier bring-up history](docs/radio-bring-up.md), [build instructions and dependency sources](docs/BUILD.md) and the [hardware bring-up summary](docs/bring-up.md). Generated firmware, downloaded dependencies, private logs, and device-specific identification records are intentionally excluded. Dependencies are fetched by exact commit and verified against [source hashes](docs/source-lock.json); they are not bundled in Git.

**Do not treat this README alone as a flashable firmware release.** Only flash firmware built from reviewed and verified sources for the exact target MCU.

This project is experimental and is **not affiliated with or officially supported by Meshtastic**. Meshtastic is a separate open-source project: https://meshtastic.org/.

## Binary UART controller

After building/programming the reviewed `uartctrl` image, use the standard-library Python utility:

```sh
python3 bin/uart_radio.py caps
python3 bin/uart_radio.py status
python3 bin/uart_radio.py configure --frequency 433500123 --sf 9 --bandwidth 125000 --cr 8
python3 bin/uart_radio.py readback
python3 bin/uart_radio.py self-test
```

For the current live image use `make -C firmware mesh-check` and
`python3 bin/test_live_controller.py` for the physical non-transmitting regression
(which briefly enters RX). The `self-test` above is only for the non-RF
`uartctrl` recovery target.

The utility discovers a single CH340 by-id port or accepts `--port`. SET operations stage RAM; APPLY performs the supported non-RF changes. The self-test restores a known baseline and never starts RX/TX. Persistent Meshtastic configuration stays on the ESP32. The binary image uses version/status/config/readback/link-stat commands and an additive binary PING/PONG in v0.3.1; `radiodiag` preserves the original text diagnostics as a separate recovery image.

## Radio notes

The latest [RF-switch investigation](docs/rf-switch-investigation.md) confirms factory `9D 01` initialization **and** complementary PC3/PC4 direction outputs; the user measured the switch controls to PC3/PC4 and accepted factory-style switching for operation. The component identity and load measurements remain unqualified. The oscillator is now confirmed as a passive 32 MHz crystal, so DIO3 TCXO control must remain disabled. This analysis did not change v0.3.0 or activate RF.

This is the **433 MHz module variant**, not an 868 MHz radio. Frequency selection, output power, duty cycle, and regional configuration must comply with applicable rules. Never transmit without a suitable antenna and verified RF configuration.
