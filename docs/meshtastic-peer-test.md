# Verified public-channel round trip — 2026-11-04

## Outcome

**Basic two-way Meshtastic public-channel text interoperability passed.**
The path under test was:

```text
WSL Python bridge ↔ CH340 ↔ CX32 meshctrl v0.4.0 ↔ SX1262
    ↔ user's 433 MHz LilyGO TTGO ↔ Meshtastic phone app
```

The CYD was not part of this test. ST-Link was not used, and no firmware was
flashed, erased or reset during the peer session. The successful exchange is
limited evidence for this channel/profile and short text packets, not proof
of a complete mesh router, PKI DMs, relaying, range, RF output calibration or
all Meshtastic versions/features.

## Actual exchange

The user identified the peer as Alr1, EU_433 / LongFast, public channel zero.
The bridge matched the received full sender ID against the locally configured
suffix; raw identifiers and the host sender identity remain in ignored local
files and are not published here.

1. The phone sent `Alr1 ready`. The bridge received and decoded it from the
   matching peer: **RSSI -40 dBm, SNR 6.5 dB**, TEXT_MESSAGE_APP port 1,
   public-channel broadcast. This establishes packet reception/decryption,
   not merely a noise reading or SPI completion.
2. The bridge made one explicitly armed SEND_PACKET request, broadcasting:
   `Alr1: DX-LR32 test A7C2 received you. Reply A7C2 on this channel.`
   The MCU accepted it and the host received a successful TxDone event.
   The user then confirmed that this exact message appeared in the phone app.
3. The bridge received the phone's reply **`Recived`** from the same peer:
   **RSSI -44 dBm, SNR 6.75 dB**. The reply did not repeat token A7C2, so the
   result is supported by the same-peer reply and the user's explicit delivery
   confirmation, not a claim of an exact token echo. Another `Alr1 ready`
   message was also received after our TX, at -42 dBm / 7 dB, demonstrating
   that reception resumed.

There was one bridge-originated packet, no automatic retries, and no automatic
NodeInfo announcement. Radio power was **commanded -9 dBm**; actual radiated
power was not measured. No second receiver/instrument captured the waveform.
The peer firmware version and exact TTGO submodel were not read from hardware.

## Configuration

- 433875000 Hz, default EU_433 LongFast slot 4.
- SF11, BW250000 Hz, coding rate 4/5, LDRO off.
- Preamble 16, explicit header, CRC enabled, normal IQ, SX1262 sync 0x24B4.
- Public LongFast channel, default public PSK alias AQ==, channel hash 0x08.
- Full host-built 16-byte Meshtastic radio header plus encrypted protobuf Data;
  our outgoing packet was a hop-zero broadcast without an ACK request.
- CX32 meshctrl v0.4.0, wire protocol 1, CH340 115200 8N1.

The PHY settings and public-channel crypto are implemented by the pinned source
and tests documented in [MESHTASTIC_TEST.md](MESHTASTIC_TEST.md). The bridge
received real RX_PACKET events and decoded real peer traffic. The public PSK
and channel hash provide no authentication; this is a cooperative user test.

## Commands and retained evidence

From the existing project root, after attaching the known CH340 to WSL and
restoring serial permissions:

```sh
python3 bin/uart_radio.py --log logs/peer-test-version.json version
python3 bin/uart_radio.py --log logs/peer-test-status.json status
.venv/bin/python bin/meshtastic_bridge.py listen --seconds 180 \
  --log logs/peer-test/discovery.json
.venv/bin/python bin/meshtastic_bridge.py send --transmit \
  --text 'Alr1: DX-LR32 test A7C2 received you. Reply A7C2 on this channel.' \
  --seconds 180 --log logs/peer-test/roundtrip.json
```

Each listener was stopped with SIGINT after its purpose was fulfilled; the
utility's `finally` block sent STOP_RX and closed the port. Thus exit code 130
and KeyboardInterrupt in those local logs reflect deliberate cleanup, not a
radio failure. No second process accessed the serial port concurrently.

Final checks:

```sh
python3 bin/uart_radio.py --log logs/peer-test/final-status.json status
python3 bin/uart_radio.py --log logs/peer-test/final-gpio.json rf_switch
```

Final status: **mode 0 / standby RC, raw 0x22, device errors 0**, active config
valid, dirty=false, applied/readback masks 0x0F, generation 3. GPIO report:
state UNDRIVEN, direction/pull/mux/latch masks zero, input mask 0x08. PC3 and PC4
are released to inputs. `activation_enabled=true` reports that v0.4.0 contains
the backend, not that the pads remain driven.

Evidence retained locally: `logs/peer-test/{discovery,roundtrip}.{json,log}`,
`logs/peer-test/final-{status,gpio}.{json,log}`, and initial version/status logs.
Incoming message details and sender IDs are excluded from GitHub. Source and
firmware were unchanged, so no rebuild was needed for this hardware-only test.

## Remaining work

- Wire and test the CYD UART adapter; that path is not covered by this result.
- Verify larger messages, congestion/loss handling, CAD/CSMA, acknowledgments,
  routing/relaying, reconnects and sustained operation independently.
- Resolve the RF-switch component and the approximately 100-ohm PC3/VDD path.
  Successful short packet operation did not measure control current, rail
  transients or temperature and does not establish electrical qualification.
- Measure RF power/sensitivity and range before making RF-performance claims.


## Additional passive discovery — same evening

After the user powered on a second node and could not send a manual message
from it, another receive-only LongFast scan captured a real NODEINFO_APP
announcement with short name **Ly01**. It was a distinct sender from the earlier
Alr1 peer. Reception measured **-40 dBm RSSI / 6 dB SNR**. The full node number
is retained only in ignored local logs. This establishes discovery of Ly01
through its automatic announcement; no packet was transmitted by our bridge
during this scan. It does not establish a two-way exchange with Ly01 or a
complete inventory of nearby nodes.

Two distinct peers have therefore been heard across the evening sessions:
Alr1 (text round trip verified) and Ly01 (NodeInfo received). Only Ly01 produced
a decoded packet in this additional scan. The receiver was stopped afterward
and status again confirmed standby RC 0x22, zero errors. Evidence:
`logs/peer-test/more-devices.{json,log}` and
`more-devices-final-status.{json,log}`.
