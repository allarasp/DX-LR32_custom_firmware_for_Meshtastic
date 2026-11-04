# First finite RX and one-packet TX — 2026-11-04

## Verified outcome and limits

After explicit user authorization to drive the factory PC3/PC4 states and
perform RX then TX, both one-shot RAM experiments completed. The user confirmed
that a suitable 433 MHz antenna was attached. No external peer or RF instrument
was available. These tests verify internal radio operation, IRQs, and RSSI;
they do **not** verify an over-the-air link, calibrated RF output, sensitivity,
switch insertion loss, or Meshtastic interoperability.

| Observation | RX | TX |
| --- | --- | --- |
| Operating status | 0x52: RX | 0x62: TX |
| Completion IRQ | 0x0200: expected receive timeout | 0x0001: TxDone |
| Device errors | 0 | 0 |
| Final status | 0x22: standby RC | 0x22: standby RC |
| Entire helper duration | 530 ms | 76 ms |
| Radio operation timeout | 500 ms | 100 ms |
| SPI/BUSY HAL error | 0 | 0 |
| Start requests | One SetRx | One SetTx; no retry |

RX produced 16 instantaneous RSSI values in dBm:

```text
-92 -94 -86 -85 -88 -93 -94 -87 -88 -91 -92 -86 -89 -92 -88 -88
```

Range -94 to -85 dBm. No RxDone IRQ was recorded. These are receiver power
estimates, potentially including ambient interference and board noise; they
are not decoded packets or audible noise, and do not alone prove the antenna
switch selected the correct RF path.

TX loaded exactly four bytes, ASCII `DXT1` (44 58 54 31), with a private LoRa
sync word. The SX1262 entered TX and signaled TxDone, but no receiver confirmed
the packet. Commanded power was **-9 dBm**, not a measured radiated value.
The 76 ms total includes setup, image calibration, polling and cleanup; it is
not measured airtime. No second transmission or continuous carrier was used.

## Exact physical settings

433500000 Hz, SF7, BW125000 Hz, CR4/5, LDRO off, preamble 12 symbols, sync
0x1424, explicit header, CRC enabled, normal IQ, four-byte payload. Normal
v0.3.1 UART configuration applied frequency/modem/packet settings first.
The RAM TX helper then configured the SX1262 PA as 02/02/00/01, applied the
TX-clamp workaround, and set -9 dBm with a 200 us ramp.

The radio used the measured passive 32 MHz crystal. DIO3 TCXO and DIO2 RF-switch
control remained disabled. GPIO-controlled factory RX state was PC3 HIGH / PC4
LOW; TX was PC3 LOW / PC4 HIGH. Both tests returned through RX and released
PC3/PC4 to inputs. `pads(2)` required the input readback 0x10 before SetTx, but
cleanup overwrites the last-active-pad result with RX 0x08; the final recorded
pad field is not a capture of the TX state.

These sequences follow the [Semtech SX1261/2 datasheet, Rev. 1.2](https://cdn.sparkfun.com/assets/6/b/5/1/4/SX1262_datasheet.pdf)
(Semtech-authored mirror), sections 9.2.1, 13 and 14, and the pinned Semtech
SX126x driver 2.5.0 used by this project. No factory register-layout assumptions
from STM32 were used.

## Execution and recovery

Flash v0.3.1 was retained throughout. RX/TX helpers were loaded into SRAM and
executed through ST-Link/OpenOCD at 100 kHz, after the known-target guard.
Unlike the earlier GPIO pulse preflight, Flash verification used **direct
readback plus host comparison**, avoiding the target CRC helper and its SRAM
side effect. Because these experiments intentionally replace application SRAM,
the normal application context was not resumed. Full-MCU reset restarted the
unchanged v0.3.1 firmware after each experiment.

RAM source, linker, build commands and host tests are in
[experiments/radio-smoke](../experiments/radio-smoke/README.md). RX and TX binary
sizes are 1750 and 1990 bytes; normal firmware remains 7664 bytes Flash and
992 bytes static SRAM plus 1 KB reserved stack. Before each helper, the exact
UART command was:

```sh
python3 bin/uart_radio.py --frequency 433500000 --sf 7 --bandwidth 125000 \
  --cr 5 --ldro 0 --preamble 12 --sync 0x1424 --header 0 --crc 1 --iq 0 \
  --length 4 --power -9 --log logs/radio-smoke/configure-rx.json configure
```

For TX the log path was `configure-tx.json`; the settings were identical.
Private guarded invocations (not portable run instructions):

```sh
tools/openocd-cx32/bin/openocd -f configs/bringup-radio-smoke-preflight.cfg
# Host compares flash-before.bin with firmware/build/uartctrl/controller.bin.
tools/openocd-cx32/bin/openocd -c 'set TESTMODE 0' -f configs/bringup-radio-smoke-run.cfg
tools/openocd-cx32/bin/openocd -f configs/bringup-full-reset.cfg
# Reconfigure/verify through UART, then repeat preflight before TX.
tools/openocd-cx32/bin/openocd -c 'set TESTMODE 1' -f configs/bringup-radio-smoke-run.cfg
tools/openocd-cx32/bin/openocd -f configs/bringup-radio-smoke-final-read.cfg
# Host compares flash-after.bin with the unchanged v0.3.1 binary.
tools/openocd-cx32/bin/openocd -f configs/bringup-full-reset.cfg
```

Final Flash readback matched v0.3.1 byte-for-byte. After the final reset:
GET_VERSION = 0.3.1 / protocol 1; PING response `00 50 4F 4E 47`;
GET_STATUS = standby RC 0x22, errors 0, active_valid=false, dirty=true,
generation 1; GET_RF_SWITCH = `0000000000000000000000000800` (both inputs,
no pulls or alternate functions, activation disabled).
The ESP32/host must reapply configuration after this reset.

Local raw evidence is excluded from GitHub under `logs/radio-smoke/`:
`configure-{rx,tx}.{json,log}`, `preflight-{rx,tx}.log`, `{rx,tx}.log`,
`result-{0,1}.bin`, `results.json`, `flash-before.bin`, `flash-after.bin`,
`reset-after-{rx,tx}.log`, and `final-{version,ping,status,gpio}.{json,log}`.
No device identifiers or raw factory dump are included in the new source/docs.

## Still unresolved

The RF-switch identity and approximately 100-ohm PC3-to-VDD path have not been
established. This experiment did not measure GPIO current, supply droop or
component temperature. Successful short operation does not establish safe
sustained operation. Normal v0.3.1 firmware still rejects RX/TX and cannot drive
the RF switch; the UART protocol has not silently acquired these capabilities.
The next useful RF verification requires a second configured receiver or an RF
measurement instrument, followed by deliberate protocol integration and current
measurements. No Meshtastic compatibility is claimed.
