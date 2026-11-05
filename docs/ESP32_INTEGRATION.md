# Using the v0.8.0 controller from an ESP32

The CX32 firmware is a raw LoRa/GFSK radio controller. ESP32 application code
owns region/channel selection, encryption, routing, acknowledgements, retries
and user configuration. No WSL process or ST-Link is required for normal radio
operation after programming. UART firmware update/OTA is not implemented;
future CX32 firmware changes still require SWD access.

## Wiring and CYD build

Power off while changing wiring. Use the established single module supply and
3.3 V UART logic. For the existing CYD variant:

| ESP32 CYD | DX-LR32 header |
| --- | --- |
| GPIO27 / UART2 TX | RXD / MCU PD6 |
| GPIO22 / UART2 RX | TXD / MCU PD5 |
| GND | GND |

Remove the external CH340 data leads when the ESP32 becomes the host: both
hosts must not drive the module RX line. AUX/M0/M1 are not required by this
protocol and remain inputs. SWD pins are not repurposed. Removing ST-Link does
not affect normal startup; keep the programming pads accessible for upgrades.

The current CYD adapter requires controller v0.8.0 and the host-mode/RX-activity
capabilities. The matching pair adds the upstream queue/delay behavior, CAD,
airtime accounting and saved TX configuration. The earlier v0.7.0 pair established
wired reception, UI transmission and broadcast delivery confirmation. See
[v0.8.0 results](v0.8.0-results.md) for the current build and physical evidence,
and [CYD build/wiring](../cyd/README.md). Application output:

`.local/meshtastic-cyd/.pio/build/cyd-dxlr32/firmware-cyd-dxlr32-2.7.26.54e0d8d.bin`

For the previously programmed board with the existing partition layout, this
is an application image at 0x10000. It is not a Flash-at-zero factory image.
The matching factory/partition/filesystem outputs also exist in that build
folder; do not mix layouts. Existing display touch calibration is in NVS.
The earlier v0.7.0 controller preparation did not update the ESP32; see
[cyd-bringup.md](cyd-bringup.md) for the subsequent wired updates.

New CYD configurations default to EU_433/public LongFast and TX disabled.
The current build preserves an explicit saved TX setting across reboot.
The adapter forwards upstream's configured/region-limited power, bounded by the
controller's -9..+22 dBm command range. Modem presets/custom LoRa settings and RX
gain also follow normal configuration within the implemented tuning windows.

## Host sequence

1. Configure UART 115200 8N1. Decode protocol-v1 delimiter/escape/CRC framing.
2. Accept BOOT event 0x80 and query GET_VERSION (1), capabilities (2), frequency
   ranges (24). Use capability bits rather than assuming every future command.
3. STOP_RX (10); a host implementing channel access/airtime policy may enable
   SET_HOST_MODE (37, byte 1, require echoed 1). Configure RX gain (28) if needed. Choose LoRa (33 with byte 1) or GFSK (33 with byte 0).
4. Stage frequency in Hz (4), power (7), and the corresponding modem/packet
   fields. LoRa uses 5/6; GFSK uses 34. APPLY (8) and require success, expected
   readback mask and active-valid state. Inspect radio errors (12).
5. START_RX (9) with u32 zero for continuous receive. Consume LoRa events 0x81
   or GFSK events 0x86, keeping their distinct metadata layouts.
6. For transmission, use the upstream priority queue and randomized/SNR-weighted
   delays. GET_RX_ACTIVITY (38) avoids interrupting a received frame. Once idle,
   STOP_RX, START_CAD (29) and await the matching 0x85 event. A busy scan returns
   to RX and reschedules; a clear scan permits ARM_TX (21, ASCII TX1!) and SEND
   (11). Check regional/channel airtime limits again and charge estimated airtime
   once. TX_DONE (0x82) confirms radio completion, not peer delivery. Resume RX.
   Hosts outside managed mode must still inspect the default test cooldown using
   GET_EXT_STATUS (36); their 2 s packet limit also remains.

7. On reset/BOOT, radio fault or cold sleep, reconfigure before RF operation.
   Do not blindly retry a TX with a new sequence after a lost reply; duplicate
   protection only remembers the most recent attempted sequence.

All common settings are in RAM. The host must reapply settings after MCU boot.
There is no automatic TX/RX at boot. Sleep command 30 keeps MCU UART awake,
invalidates active radio config and requires WAKE (31) followed by APPLY.
The complete extension formats are in [UART_FEATURES.md](UART_FEATURES.md).

## Meshtastic delivery status

The WSL test bridge receives/decrypts public packets and sends independent
hop-zero test messages. It does not implement a full Meshtastic relay/router.
Consequently a phone can display a delivery failure while this bridge receives
the text: the sender did not get the expected confirmation of that packet.
Another real node, such as Ly01, may supply an implicit broadcast acknowledgement
by rebroadcasting it. A separate reply with a different packet ID is not that
confirmation. See the official [broadcast algorithm](https://meshtastic.org/docs/overview/mesh-algo/).

The ESP32's actual Meshtastic stack owns ACK/rebroadcast behavior. This needs
an end-to-end wired test; it does not belong inside the generic CX32 firmware.
The current CYD uses a 16-packet upstream priority queue and host-managed TX
without the fixed test cooldown; normal airtime/channel limits still apply.

CAD, hardware counters, RSSI, gain, sleep, duty RX and GFSK are available to any
UART host. The CYD adapter integrates normal LoRa RX/TX and CAD. Sleep and GFSK are
controller capabilities, not integrated Meshtastic radio modes.
