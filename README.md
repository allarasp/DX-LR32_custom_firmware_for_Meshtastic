# DX-LR32 custom firmware for Meshtastic

Experimental firmware for using the **DX-LR32-A (433 MHz)** LoRa module as an external **UART-connected SX1262 radio controller**.

The goal is to build a standalone Meshtastic messaging device with an **ESP32-2432S028 CYD touchscreen**, without requiring a phone or Bluetooth. The ESP32 would run the Meshtastic network stack and user interface; the DX-LR32's existing **CX32L003F8** microcontroller would control its SX1262 over SPI.

> **Work in progress — not yet Meshtastic-compatible.** The custom MCU firmware boots and UART diagnostics work on real hardware. SX1262 reset, standby-RC initialization, status/error queries and packet-type readback now work on real hardware. LoRa packet communication and Meshtastic integration are **not yet verified**.

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
- [ ] Validate receive/transmit of LoRa packets.
- [ ] Implement the ESP32-side Meshtastic UART radio adapter.
- [ ] Implement and test the standalone CYD touchscreen interface.
- [ ] Demonstrate interoperability with a real Meshtastic node.

## Source code and setup

The firmware sources, GNU startup/linker code, tests, and generic OpenOCD configurations are included. On Ubuntu/WSL, install the build prerequisites and fetch the pinned source dependencies:

```sh
sudo apt-get install --no-install-recommends gcc-arm-none-eabi binutils-arm-none-eabi build-essential git python3
python3 firmware/tools/prepare_deps.py
make -C firmware -j2 radio-check uart-check check
```

- `firmware/build/radiodiag/controller.elf`: **v0.2.1 radio diagnostics**, flashed and UART-tested; 5,100 bytes Flash, 340 bytes static SRAM plus 1 KB stack. `RADIO_RESET`, `RADIO_STATUS`, `RADIO_ERRORS`, and `RADIO_TEST` are implemented with timeouts. Physical reset, initialization, status/error queries and reversible packet-type checks pass: status `0x22`, errors `0x0000`. No RF transmissions or DIO2/DIO3 control.
- `firmware/build/uartdiag/controller.elf`: the **v0.1.1 UART diagnostic firmware tested on hardware**, using 115200 8N1. Its binary is 1,712 bytes; static SRAM is 60 bytes, plus a reserved 1 KB stack.
- `firmware/build/controller.elf`: the **experimental SX1262 foundation**, built and host-tested but not flashed or radio-verified. It currently uses 9600 baud and a 4 MHz clock; it is a separate development image.
- `firmware/src/`, `firmware/include/`: both firmware implementations and the SX126x platform hooks.
- `firmware/tests/`: image, UART command-parser, and mocked radio HAL tests.
- `configs/`, `bin/`: generic debug configuration and optional diagnostic/tool-build helpers. Builds and tests do not access hardware.

See [verified radio results and factory-code pin correction](docs/factory-pin-correction.md), [earlier bring-up history](docs/radio-bring-up.md), [build instructions and dependency sources](docs/BUILD.md) and the [hardware bring-up summary](docs/bring-up.md). Generated firmware, downloaded dependencies, private logs, and device-specific identification records are intentionally excluded. Dependencies are fetched by exact commit and verified against [source hashes](docs/source-lock.json); they are not bundled in Git.

**Do not treat this README alone as a flashable firmware release.** Only flash firmware built from reviewed and verified sources for the exact target MCU.

This project is experimental and is **not affiliated with or officially supported by Meshtastic**. Meshtastic is a separate open-source project: https://meshtastic.org/.

## Radio notes

This is the **433 MHz module variant**, not an 868 MHz radio. Frequency selection, output power, duty cycle, and regional configuration must comply with applicable rules. Never transmit without a suitable antenna and verified RF configuration.
