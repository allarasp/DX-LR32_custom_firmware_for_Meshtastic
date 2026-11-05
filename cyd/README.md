# CYD2USB Meshtastic + DX-LR32 UART variant

This is a source overlay on the official Meshtastic firmware tag
`v2.7.26.54e0d8d`, commit `54e0d8d0ab2ff56b3a9ce967e53f79e49af560fb`.
It targets the user's ESP32-2432S028 with USB-C **and** micro-USB: ESP32-D0WD-V3,
4 MB Flash, ST7789 240x320 panel and XPT2046 resistive touch. The board is a
custom DIY variant, not an officially supported Meshtastic product.

The full upstream Meshtastic stack supplies channel configuration, encryption,
node database, USB PhoneAPI, Bluetooth and the built-in screen/menu UI. A custom
`RadioInterface` replaces direct radio SPI with our version-1 binary UART link
to DX-LR32 meshctrl v0.8.0. This matched pair negotiates host-managed channel
access; older controller images must be upgraded before using the current CYD
adapter. Meshtastic itself runs on the ESP32, with the CX32 as a raw radio.
The WSL bridge is not required while the CYD is the radio host.

**Default is EU_433, LongFast, public channel and radio TX disabled.** There is
no RF radio inside an unmodified CYD. Without the external DX-LR32, the UI/API
can boot and configure, but there is no mesh reception/transmission. The adapter
waits for the controller and retries discovery; it does not pretend to have
found a controller merely because the ESP32 serial port opened.

## Build

From the main project root, with the project Python environment containing
PlatformIO 6.2.0:

```sh
git clone --depth 1 --branch v2.7.26.54e0d8d \
  https://github.com/meshtastic/firmware.git .local/meshtastic-cyd
git -C .local/meshtastic-cyd submodule update --init --depth 1 protobufs
python3 cyd/prepare.py
sh cyd/tests/check.sh
.venv/bin/pio run -d .local/meshtastic-cyd -e cyd-dxlr32 -j 4
```

`prepare.py` requires the pinned commit and reconstructs only the files it owns
from that revision. Keep independent edits out of the ignored build checkout.
Maintained source is in `cyd/`. The script copies the variant/radio adapter,
adds the UART radio factory, configures ST7789/touch in the existing LovyanGFX
path, and avoids initializing the unrelated default LoRa SPI bus. Small guards
fix upstream disabled-I2C keyboard and disabled-WiFi OTA compilation paths.
Required upstream ringtone/time-zone defaults are preserved while board defaults
are overridden. Do not publish generated node keys or Flash/NVS readbacks.

Toolchain: PlatformIO Espressif32 6.13.0, Arduino-ESP32 2.0.17,
Xtensa GCC 8.4.0+2021r2-patch5, LovyanGFX 1.2.21, NimBLE-Arduino 1.4.3,
rweather Crypto 0.4.0. WiFi/MQTT, GPS, I2C and unused sensors are excluded for
this board. Bluetooth, serial API, text messaging, administration and touch
input remain enabled. This uses upstream's ordinary TFT UI, not the separate
LVGL/device-ui package. The CYD UI uses a 320x200 logical content area and four bottom touch controls.
Menu becomes green OK in menus/editors. TX enable/activity and heartbeat-backed
UART status share the native battery/time header. Chat opens recent conversations,
configured channels and known-node DMs; Compose opens the upstream touch keyboard. Native gestures
remain available. First boot without valid `cydtouch/cal` NVS data prompts four
corner-cross presses and stores eight calibration values. Later application-only
updates preserve them. Calibration and visual orientation require physical
confirmation; compiling a touch driver is not a physical touch test.

Build outputs (ignored):
`.local/meshtastic-cyd/.pio/build/cyd-dxlr32/`, including
`firmware-cyd-dxlr32-2.7.26.54e0d8d.{bin,elf,factory.bin}`, `bootloader.bin`,
`partitions.bin` and `littlefs-cyd-dxlr32-2.7.26.54e0d8d.bin`.
The matching partition map is upstream's 4 MB `partition-table.csv`; do not
flash an app binary at address zero or mix bootloaders/partitions from another
board build. Hardware programming uses the actual discovered serial port.

## UART wiring

| CYD | DX-LR32 |
| --- | --- |
| GPIO27, UART2 TX | RXD / PD6 |
| GPIO22, UART2 RX | TXD / PD5 |
| GND | GND |

Use 3.3 V logic and the verified module supply arrangement. Do not connect two
UART transmitters to the same input: remove CH340 data leads when the CYD is
the host. The CYD's onboard USB CH340 stays on UART0; the external module uses
UART2, so logs/PhoneAPI do not share the radio data pins. The module is now wired to the CYD; controller handshake and a real peer
receive test passed on 2026-11-05.

Other CYD pins: display SCK14/MOSI13/MISO12/CS15/DC2/backlight21; touch
SCK25/MOSI32/MISO39/CS33/IRQ36. The default unrelated LoRa SPI initialization
is bypassed. I2C is disabled so it cannot claim the external UART RX pin.

## Adapter behavior and limits

- Bounded CRC/framing parser, UART2 RX buffer 2048 B, 281-byte decoded frame
  limit; checked protocol/controller version, capabilities and APPLY masks.
- Ordered nonblocking handshake/staging/APPLY/RX with integer-Hz frequency
  derived from upstream's normal region/preset/slot calculation. A missing
  controller does not block the screen or USB/Bluetooth API.
- Raw RX headers/payloads, RSSI and SNR feed the upstream router for decoding.
  PKI/channel encryption remains upstream; the adapter does not decrypt itself.
- The pinned upstream 16-packet priority queue, queue status, cancellation,
  duplicate suppression, upgraded-hop replacement and late rebroadcast windows
  are connected to the router. Normal upstream randomized/SNR-weighted delays
  precede each attempt. An active RX is allowed to finish; UART CAD checks the
  channel before arming/SEND. Busy CAD returns to RX and defers the queued packet.
- TX/RX airtime feeds upstream AirTime statistics. Regional TX duty budget and
  channel utilization are checked again at dispatch, including queued traffic.
  Estimated TX airtime is charged once before SEND so a lost completion cannot
  undercount airtime. The fixed 30/31-second test cooldown is absent in the CYD's
  negotiated host mode. Generic raw-controller tools keep their conservative
  default test policy until explicitly opting into host mode.
- TX follows saved `config.lora.tx_enabled`; new configurations default off.
  Upstream modem, frequency slot, power (bounded by the controller's -9..+22 dBm
  range and upstream regional ceiling), and RX gain settings are forwarded.
  The implemented tuning windows remain 433–434 and 863–870 MHz; RF performance
  outside the original module band requires separate qualification.
- Explicit one-shot MCU arming, sequence-matched CAD/TX completion and finite
  airtime-derived watchdogs remain. Up to 60 s packet airtime is supported in
  host mode. No UART timeout automatically replays an uncertain packet.
- This remains a custom board/adapter. Deep sleep, GFSK through Meshtastic,
  full failed-RX/noise-floor/counter reporting, and long-run RF/stability
  qualification remain outside the verified scope. Normal Meshtastic routing,
  encryption and acknowledgements are supplied by the actual upstream stack.

Host tests compile the actual adapter and unchanged pinned upstream queue against
hardware/core stubs with ASan/UBSan. Coverage includes framing/version/capability
negotiation, CRC/RX metadata, ownership, priority/cancellation, active RX, CAD
clear/busy/stale sequence, dispatch duty/channel limits, rapid consecutive TX,
late windows and uncertain-TX recovery. These are complemented by full builds
and physical tests recorded in [v0.8.0 results](../docs/v0.8.0-results.md).

## Sources

- [Official Meshtastic firmware](https://github.com/meshtastic/firmware/tree/54e0d8d0ab2ff56b3a9ce967e53f79e49af560fb)
- [CYD2USB display configuration](https://github.com/witnessmenow/ESP32-Cheap-Yellow-Display/blob/main/DisplayConfig/CYD2USB/User_Setup.h)
- [Espressif esptool documentation](https://docs.espressif.com/projects/esptool/en/latest/esp32/)

The built firmware is a derivative of the upstream GPL-3.0 project and remains
subject to its license. The overlay is provided as source, not as an unrelated
prebuilt image whose hardware configuration is unknown.


## Touch use and calibration

The panel is resistive: a stylus or deliberate fingertip pressure works better
than very light capacitive-phone gestures. At first calibration, hold the colored
cross until it disappears, release, and press the next cross (four corners).
The library briefly uses portrait orientation during calibration, then restores
landscape. Invalid/degenerate calibration is not saved and will be requested
after reset. Recalibration currently requires clearing only the `cydtouch`
namespace in a controlled diagnostic; do not factory-reset the whole node merely
to recalibrate touch.

The logical content area is 320x200 including the native header; the hardware
screen remains 320x240. The bottom
bar has four 80x40 hit areas. Chat opens a bounded conversation browser. A
channel or known-node DM can be selected before composition, and the existing
MessageRenderer supplies history and delivery state. The native message store
retains its limit of ten messages total; per-conversation unread counters are
RAM-only. Back exits the keyboard to the selected conversation. Menu opens its
Compose/Choose another chat actions; in a menu it turns green and reads OK.

Clock settings include numeric date/time editing, timezone and 12/24-hour format.
System exposes rebroadcast mode, Node Info interval (presets/custom minutes),
display-only timeout, and region/preset/hop/gain/power controls. New editors use
current-value displays and Save/Cancel. Configurations use the same fields as
PhoneAPI/Bluetooth, and saved TX remains enabled across ordinary updates.
The first touch after display sleep is consumed until release; radio/BLE are
not put to sleep. See [GUI controls and validation](../docs/cyd-gui.md).

## Historical controller v0.7.0 preparation

The earlier adapter accepted v0.7.0 and was built successfully. That earlier app was
1,317,552 B, SHA-256
`42a9dc11131b0a9f07230e8e8b2bf39380c855d4e00910de2ac6981c80a082b9`.
That compatible image was subsequently uploaded and the wired receive test
passed. The current app also preserves explicit TX settings across reboot; see
[bring-up results](../docs/cyd-bringup.md). MCU CAD/sleep/GFSK support does not automatically integrate
those features into this LoRa Meshtastic adapter.
See [ESP32 steps and delivery-status limits](../docs/ESP32_INTEGRATION.md).

## First-time builders

Use the [complete DIY guide](../docs/DIY_GUIDE.md), [CYD flashing procedure](../docs/FLASHING_CYD.md) and [troubleshooting](../docs/TROUBLESHOOTING.md). This file is the developer overlay reference. New configurations require explicit TX enablement through the phone app or USB API.
