# Using the v0.7.0 controller from an ESP32

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

The maintained CYD adapter source now accepts controller v0.7.0. The CYD image
installed during the earlier display stage only accepted the older controller;
**upload the newly built compatible CYD application before testing this link**.
The updated application has been compiled, not uploaded/tested on the CYD in
this stage. See [CYD build/wiring](../cyd/README.md) and the local build outputs:

`.local/meshtastic-cyd/.pio/build/cyd-dxlr32/firmware-cyd-dxlr32-2.7.26.54e0d8d.bin`

For the previously programmed board with the existing partition layout, this
is an application image at 0x10000. It is not a Flash-at-zero factory image.
The matching factory/partition/filesystem outputs also exist in that build
folder; do not mix layouts. Existing display touch calibration is in NVS.
No ESP32 erase/upload or configuration write was performed in the v0.7.0 stage.

The CYD build still defaults to EU_433/public LongFast and TX disabled at boot.
Its adapter deliberately requests -9 dBm for the initial wired test, although
the CX32 accepts -9..+22 dBm. Later host changes can use the normal regional
power choice; the MCU no longer applies an EU power ceiling. Enabling TX on the
CYD is an explicit test-session action, separate from wiring and RX validation.

## Host sequence

1. Configure UART 115200 8N1. Decode protocol-v1 delimiter/escape/CRC framing.
2. Accept BOOT event 0x80 and query GET_VERSION (1), capabilities (2), frequency
   ranges (24). Use capability bits rather than assuming every future command.
3. STOP_RX (10); choose LoRa (33 with byte 1) or GFSK (33 with byte 0).
4. Stage frequency in Hz (4), power (7), and the corresponding modem/packet
   fields. LoRa uses 5/6; GFSK uses 34. APPLY (8) and require success, expected
   readback mask and active-valid state. Inspect radio errors (12).
5. START_RX (9) with u32 zero for continuous receive. Consume LoRa events 0x81
   or GFSK events 0x86, keeping their distinct metadata layouts.
6. For transmission, inspect cooldown through GET_EXT_STATUS (36). ARM_TX (21,
   ASCII TX1!) grants one attempt; SEND_PACKET (11) carries 1..255 raw bytes.
   The command reply accepts the attempt; only TX_DONE 0x82 confirms radio
   completion. Neither proves peer/application delivery. Match the sequence.
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
The current CYD adapter retains a one-packet queue and conservative 31-second
transmit spacing; the CX32 has a 30-second attempt cooldown and <=2 s airtime
guard. Those constraints can delay or reject mesh control traffic, so complete
routing/ACK interoperability is not established by the WSL text test.

CAD, hardware counters, RSSI, gain, sleep, duty RX and GFSK are available to any
UART host. The existing CYD adapter uses normal LoRa RX/TX; its full integration
with CAD/sleep/GFSK is not implemented merely because the MCU supports them.
