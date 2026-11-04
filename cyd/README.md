# CYD2USB Meshtastic + DX-LR32 UART variant

This is a source overlay on the official Meshtastic firmware tag
`v2.7.26.54e0d8d`, commit `54e0d8d0ab2ff56b3a9ce967e53f79e49af560fb`.
It targets the user's ESP32-2432S028 with USB-C **and** micro-USB: ESP32-D0WD-V3,
4 MB Flash, ST7789 240x320 panel and XPT2046 resistive touch. The board is a
custom DIY variant, not an officially supported Meshtastic product.

The full upstream Meshtastic stack supplies channel configuration, encryption,
node database, USB PhoneAPI, Bluetooth and the built-in screen/menu UI. A custom
`RadioInterface` replaces direct radio SPI with our version-1 binary UART link
to DX-LR32 meshctrl v0.4.0 or v0.5.0. The updated source accepts both; the previously uploaded CYD image has not been replaced during the v0.5.0 CX32 work. It does not put a second Meshtastic stack on the CX32.
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
LVGL/device-ui package. The revised board UI reserves a bottom strip for Prev/Menu/Next/Chat and a TX
status line; Chat directly opens upstream's virtual keyboard. Native gestures
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

## Planned UART wiring

| CYD | DX-LR32 |
| --- | --- |
| GPIO27, UART2 TX | RXD / PD6 |
| GPIO22, UART2 RX | TXD / PD5 |
| GND | GND |

Use 3.3 V logic and the verified module supply arrangement. Do not connect two
UART transmitters to the same input: remove CH340 data leads when the CYD is
the host. The CYD's onboard USB CH340 stays on UART0; the external module uses
UART2, so logs/PhoneAPI do not share the radio data pins. The user has confirmed
the module is **not yet wired to the CYD**. Do not infer end-to-end radio success
from the CYD alone.

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
- Single packet transmit ownership, explicit one-shot MCU arming, sequence-matched
  TxDone, finite watchdog, and a conservative 31-second local cooldown. No UART
  timeout causes an automatic retransmission of an uncertain packet.
- TX remains gated by `config.lora.tx_enabled`; board defaults set it false.
  The build-time bootstrap forces TX off on each boot; any later enabling
  should be a deliberate test-session action. The MCU also limits power to -9 dBm and accepts only the initial LongFast TX
  profile and <=2 s airtime. The normal Meshtastic power slider therefore does
  not override the MCU's low-power experimental limit.
- No full CAD/CSMA implementation, deep sleep integration, multi-packet radio
  queue, complete receiver-loss accounting or sustained RF-switch qualification.
  Some routed/admin/PKI packets may be rejected by airtime/cooldown limits.
  This is an experimental adapter, not proven Meshtastic interoperability.

Host tests compile the **actual adapter source** against small hardware/core
stubs under ASan/UBSan, covering golden framing/handshake, integer frequency,
CRC rejection, RX metadata, packet ownership, arm/TX completion matching,
cooldown, failure/reconnect. They do not replace a wired-controller or peer test.
See `docs/cyd-bringup.md` for actual upload, API and display results.

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

The main content area is 320x184; the hardware screen remains 320x240. The bottom
bar has four 80x40 hit areas. Chat opens free-text composition on public channel
zero; typing alone does not transmit. Radio TX remains disabled for preparation.
Menu selects the native context/menu action; Prev/Next use native navigation.
The ordinary upstream page contents are still mostly monochrome, with colored
navigation controls. This is not a claim to have ported the separate modern
LVGL/device-ui application to this non-PSRAM ESP32.
