# CYD2USB initial Meshtastic upload — 2026-11-04

## Result

Custom Meshtastic firmware based on official tag `v2.7.26.54e0d8d` is running
on the connected ESP32-2432S028 with USB-C plus micro-USB. USB interrogation
identified ESP32-D0WD-V3 revision 3.1, 40 MHz crystal and 4 MB Flash. The two-USB
board configuration uses an ST7789 panel and XPT2046 resistive-touch controller.
The project identifies this unsupported custom board as PRIVATE_HW (255), not
as an official supported Meshtastic product.

The initial upload wrote the combined factory image and matching LittleFS
image. A subsequent application-only revision added calibrated touch, visible
navigation and Chat/virtual-keyboard access without erasing node configuration. Esptool reported **Hash of data verified** for both regions and reset
the ESP32. Both programming operations succeeded on their first attempt; there was no
blind retry after a programming failure. Earlier connection and
read-only backup attempts did not erase/program anything. The user explicitly
waived the old firmware backup before programming; no complete backup was
obtained or required. The previous application was replaced.

Boot logs verified:

- Meshtastic `2.7.26.54e0d8d`, environment `cyd-dxlr32`.
- TFT initialization, backlight/brightness and normal screen frames.
- Touch driver initialization.
- NimBLE Bluetooth startup.
- `DXLR UART prepared RX=22 TX=27; waiting for v0.4.0 controller`.

The user initially confirmed readable text but found the native gesture UI and
touch behavior difficult. After the UI revision and four-corner calibration,
the boot log reported `CYD touch calibration saved`, and the user confirmed
that navigation/Chat/keyboard interaction was “way better”. Calibration is
stored in NVS; a separate full power-cycle persistence test has not been done. The DX-LR32 is **not yet wired to the CYD**; neither UART-radio
connectivity nor over-the-air Meshtastic interoperability was physically tested
in this stage. The MCU/radio itself was not reflashed during CYD preparation.

An early log says `No radio instance available to provide entropy`: the upstream
optional RadioLib entropy mixer has no RadioLib instance in this UART variant.
Source inspection confirms ESP32 `esp_fill_random()` fills the buffer first;
the optional modem mix is not the sole random source. This was not a boot panic.

## Installed touch/UI revision

The installed application is 1,317,520 B, SHA-256
`de9cf1515f8c81e95fe06074305c72f7be76c73cbce80782bd4c15a14d5dc8ef`.
It reserves a 320x184 main content area, a TX status line, and a bottom row of
Prev/Menu/Next/Chat controls. Chat directly opens upstream's virtual keyboard
on public channel zero. Native tap/long-press gestures remain available.

First-use calibration draws four corner crosses, takes raw samples, checks
ADC bounds and adequate span, and saves eight values under `cydtouch/cal`.
The user completed calibration; the log confirms saved values. No full node
factory reset was used. Subsequent app-only updates preserve these values.
Underlying Meshtastic pages remain primarily monochrome; this is an improved
native TFT UI, not a separate modern LVGL/device-ui port.

The second upload used the same esptool command below, but wrote **only**
`firmware-cyd-dxlr32-2.7.26.54e0d8d.bin` at **0x10000**, with
`--before default_reset --after hard_reset`. Automatic connection worked;
no further button sequence was needed. Its Flash hash verified. Post-calibration
API checks again confirmed EU_433, LongFast, Bluetooth enabled and TX disabled.

## Official client/API verification

The WSL Meshtastic Python `SerialInterface` completed its real USB PhoneAPI
handshake and retrieved configuration from the newly running board:

```text
firmware: 2.7.26.54e0d8d
region: EU_433 (2)
modem preset: LONG_FAST (0)
radio TX enabled: false
Bluetooth enabled: true
hardware model: PRIVATE_HW (255)
role: CLIENT (0)
```

No RF test message was requested. The controller is absent, and both build
defaults and adapter checks prohibit transmitting while TX is disabled.
Bluetooth initialization/configuration was verified, but phone pairing was not.

## Build, tests and images

Maintained source, build commands and planned wiring are in
[cyd/README.md](../cyd/README.md). The official firmware is pinned to commit
`54e0d8d0ab2ff56b3a9ce967e53f79e49af560fb`. Project-specific patches and adapter
source are retained under `cyd/`; the full build checkout is ignored under
`.local/meshtastic-cyd/`.

```sh
python3 cyd/prepare.py
sh cyd/tests/check.sh
.venv/bin/pio run -d .local/meshtastic-cyd -e cyd-dxlr32 -j 4
```

The actual adapter source passed ASan/UBSan host tests for handshake/framing,
CRC rejection, integer frequency, receive metadata, packet ownership, one-shot
arming, sequence-matched completion, cooldown, rejected commands and reconnect.
The full ESP32 build passed. Upstream LovyanGFX/LTO warnings remain; runtime
visual/touch checks and a wired-radio test are still required.

Final build resource report: **1,310,801 B program usage of 2,424,832 B application
partition; 95,224 B static RAM of 327,680 B**. Boot logs reported about 95 KB
free heap after initial setup, before later Bluetooth activity; this is not a
long-duration heap/stability measurement.

Output prefix:
`.local/meshtastic-cyd/.pio/build/cyd-dxlr32/`

Initial-upload hashes (the build paths now hold the newer UI revision):

| Initial artifact | Size | SHA-256 |
| --- | ---: | --- |
| `firmware-cyd-dxlr32-2.7.26.54e0d8d.bin` | 1,307,312 B | `90992acc8f7af3979908d43fbdba6cbe424d55eb03f773bcbe984937ee1e961a` |
| `firmware-cyd-dxlr32-2.7.26.54e0d8d.factory.bin` | 1,372,848 B | `9de554da20f288c6a4f2d094da6fbcdfa27f67654eb7b0c33806abf1f39d1188` |
| `littlefs-cyd-dxlr32-2.7.26.54e0d8d.bin` | 1,048,576 B | `eb3416678fe58537247635c615c8f79f306414446d7dff593621dfeca5b5d922` |

## Upload procedure and serial findings

Only the connected CYD CH340 was targeted. VID:PID 1a86:7523 alone is not an
ESP32 identifier: esptool established the actual chip first. Initial Windows
COM descriptions changed after USB reattachment; no unrelated device was used.
The eventual successful route was WSL `/dev/ttyUSB0` with PlatformIO-bundled
esptool 4.11.0 at 115200 baud. Initial esptool 5.6.0/read attempts and native
Windows attempts failed before programming. Long Flash reads had truncated
packets through USB/serial; firmware writes used compressed transfers and
verified both resulting Flash hashes.

The user manually held BOOT, tapped/released RST, and the captured ROM log
confirmed `boot:0x3 (DOWNLOAD_BOOT...)` / `waiting for download`. BOOT could then
be released. Normal boot instead showed `boot:0x13`, explaining earlier failed
synchronization attempts. The successful upload did not reset out of download
mode before connecting:

```sh
.venv/bin/python /home/allar/.platformio/packages/tool-esptoolpy/esptool.py \
  --chip esp32 --port /dev/ttyUSB0 --baud 115200 \
  --before no_reset --after hard_reset write_flash -z \
  --flash_mode dio --flash_freq 40m --flash_size 4MB \
  0 .local/meshtastic-cyd/.pio/build/cyd-dxlr32/firmware-cyd-dxlr32-2.7.26.54e0d8d.factory.bin \
  0x300000 .local/meshtastic-cyd/.pio/build/cyd-dxlr32/littlefs-cyd-dxlr32-2.7.26.54e0d8d.bin
```

The factory binary is a merged image **for address zero**, containing its
matching bootloader at 0x1000, partition table at 0x8000, OTA selection at
0xE000 and application at 0x10000. Do not substitute the application-only binary
at address zero. The 4 MB partition map reserves app 0x10000/0x250000, secondary
flash-app 0x260000/0xA0000 and filesystem 0x300000/0x100000.

Local evidence is excluded from GitHub under `logs/cyd/`: `build.log`,
`upload.log`, `new-boot.{log,bin}`, `api-check.log`, `image-info.log`,
`artifacts.json`, `build-touch.log`, `upload-touch.log`,
`touch-boot.{log,bin}`, `api-touch-check.log`, and read/connection diagnostics. These logs may contain local
node identifiers or newly generated configuration; they must not be published.

## Next connection

With power removed, prepare UART2 signals as documented: CYD GPIO27 TX to module
RXD/PD6; CYD GPIO22 RX from module TXD/PD5; common ground and the verified module
supply. Do not leave the separate CH340 TX connected in parallel. Once wired,
verify version/capabilities, apply settings, RX status and a real peer message
before enabling TX for the evening experiment. TX is forced off at bootstrap
by this preparation build; enabling it later is an explicit test-session action.

## Wired update — 2026-11-05

The user reported the CYD connected to the DX-LR32 over UART. WSL exposed the
CYD CH340 at `/dev/ttyUSB0`; esptool confirmed ESP32-D0WD-V3 revision 3.1.
Automatic bootloader entry worked. The v0.7.0-compatible application
(1,317,552 B, SHA-256 `42a9dc11131b0a9f07230e8e8b2bf39380c855d4e00910de2ac6981c80a082b9`)
was uploaded at 0x10000 with compressed transfer and verified Flash hash.
NVS and filesystem were not erased. The full build and adapter/touch sanitizer
tests passed; the latter required execution outside sandbox tracing.

After reset the screen initialized and USB PhoneAPI connected, reporting
EU_433, LongFast, Bluetooth enabled and TX disabled. A 35-second boot capture
showed UART initialization but **no connected-and-receiving confirmation**.
The user could not confirm the exact wiring/power arrangement; physical
inspection is the next step. No peer reception or RF transmission is verified.
Local evidence: `logs/cyd/build-wired.log`, `upload-wired.log`,
`wired-boot.{log,bin}`, and `api-wired.log`.

A subsequent source-only diagnostic logs pending UART command/stage on timeout;
this diagnostic is not part of the application uploaded above.

After the user corrected crossed wiring to GPIO27 -> RXD/PD6 and
GPIO22 <- TXD/PD5, reattached USB to WSL and restored serial access, a fresh
boot capture confirmed at uptime 1 second:
`DXLR radio connected and receiving; TX=disabled`.
This establishes the real UART discovery/configuration/START_RX handshake.
The USB API again reported EU_433/LongFast and TX disabled. Peer packet
reception remains a separate test.

The subsequent real peer test succeeded: USB PhoneAPI delivered
`TEXT_MESSAGE_APP`, text `CYD test 1`, packet ID 2123299481, RSSI -45 dBm,
SNR 5 dB. This verifies external RF reception through the DX-LR32 UART adapter
and upstream Meshtastic decoding. RF transmit/ACK behavior is still untested;
TX remained disabled throughout this receive test.

## ESP32 TX enable/persistence and RX/TX sequence — 2026-11-05

The user attempted ESP32 text transmission while TX was disabled and observed
Alr1 reporting a delivery failure despite successful reception at the CYD.
Receive-only operation cannot provide the broadcast rebroadcast confirmation.

A real PhoneAPI LoRa TX-enable write was accepted, but upstream's configuration
reboot then reapplied the preparation build's unconditional TX-off bootstrap.
The overlay now defaults new configurations to TX off while preserving an
explicit saved setting. The next application-only upload verified successfully;
post-reboot API readback confirmed TX true, EU_433 and LongFast. Startup traffic
produced `DXLR TX complete result=0 elapsed=1102`.

The first text test overlapped startup cooldown; another controller SEND returned
status 11 (`RP_NOT_READY`). Source review found SEND can reject pending RX IRQs.
The adapter now acknowledges STOP_RX before arming/SEND and restarts continuous
RX after matching TX_DONE. A failed/abandoned attempt reconnects without blindly
retransmitting the packet. Sanitizer tests cover STOP_RX ordering and RX restart,
including rejection and ownership checks; full build passed. The 31-second local
and 30-second MCU cooldowns remain and can reject acknowledgements/control traffic.

The final RX/TX sequence app is 1,317,616 B,
SHA-256 `4191c12968d3c9861d088cd540a7a392d3133e573b89702a1dd8bd85a24f02fb`.
Application Flash verification passed and API readback retained TX enabled.
During the live test the user sent `tt` from the ESP32, and confirmed Alr1
received it. The controller reported `TX complete result=0 elapsed=484` at
uptime 62 seconds for this transmission. This verifies the actual CYD UI ->
Meshtastic -> UART -> DX-LR32 RF -> peer text path. The scheduled API test text
arrived within 31 seconds of the user's send and had no TX completion; it is
not evidence of an additional RF transmission. Logs: `build-tx-rx-sequence.log`,
`upload-tx-rx-sequence.log`, and `verify-tx4{,-debug}.log` under `logs/cyd/`.

After the confirmed ESP32 `tt` transmission, the return text `Test` arrived
from Alr1 through USB PhoneAPI with RSSI -50 dBm, SNR 5 dB, packet ID
1103757575. This verifies RX resumed after the new STOP_RX/TX sequence.
The test session leaves saved TX enabled; new configurations still default off.

### Broadcast delivery confirmation verified

The earlier return `Test` still showed a phone delivery failure. A subsequent
idle test with routing capture verified the complete confirmation path:
`ACK test 1`, packet ID 1706946266 (`0x65bdeeda`), arrived from Alr1
`!06f369e4` at uptime 346 s with RSSI -47 dBm, SNR 6.5 dB, hop limit/start 3.
Upstream logged `Rebroadcast received message coming from e4` for that same
packet; the adapter then reported `TX complete result=0 elapsed=484` at uptime
347 s. The user confirmed Alr1's phone now showed **delivered to mesh**.
This is a verified implicit broadcast acknowledgement, distinct from the
independent ESP32 `tt` message. Evidence: `logs/cyd/ack-{listen,debug}.log`.
Closely spaced traffic can still fail confirmation because the controller
cooldown is retained; this is not a sustained routing/stability qualification.

## v0.8.0 matched update — 2026-11-05

The previous cooldown limitations above describe the bring-up images. The
current controller/CYD pair replaces them with upstream priority queuing,
randomized/SNR-weighted waiting, active-RX deferral, CAD and packet-airtime
accounting. Normal configured power and modem settings are forwarded. Both
images were programmed and verified; saved TX and touch configuration remain.
Three five-second-spaced messages and a return broadcast passed; after an RX
accounting correction the exact final app passed two four-second-spaced messages,
both delivery acknowledgements and the user's `Got it` reply/rebroadcast.
See [v0.8.0 results and limitations](v0.8.0-results.md) for hashes, tests, update
checks and exact final-image airtime verification. No commit/push was performed.

## Native GUI revision — 2026-11-05

The CYD-only GUI revision adds green Menu/OK state, compact TX/UART header
badges with an idle UART heartbeat, manual clock editing, System/LoRa editors,
display-only timeout and a channel/DM conversation browser with bounded unread
state. Logical content is now 320x200, retaining 40 px touch controls. The
application Flash hash verified; all recorded pre/post API settings matched.
Physical clock edit/cancel/save and subsequent radio activity were captured.
See [GUI controls, exact image and validation](cyd-gui.md). Controller v0.8.0
is unchanged. User touch/appearance checks complement the passing model,
radio and full-build checks.

User-confirmed GUI tests subsequently passed Menu/Clock/System controls,
channel composition, a PKI DM to Alr1 with ACK, and display-only sleep/wake.
Raw logs independently show sleep at uptime 366 s, swallowed wake touch at
369 s, and the next one-minute timeout at 429 s. The USB listener was closed
after confirmation; no live PhoneAPI session was left to interfere with BLE.
