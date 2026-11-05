> Historical/research document: this records an earlier stage or diagnostic workflow. For the current standalone CYD + DX-LR32 installation use the [DIY guide](DIY_GUIDE.md) and [current build reference](BUILD.md). Later measurements are indexed in [the research index](README.md).

# WSL two-way public-channel test preparation

## Verified round trip

The later evening test passed: a real 433 MHz TTGO/phone peer sent `Alr1 ready`,
received our public-channel test message, and returned `Recived`, decoded by the
WSL bridge. See [actual evidence and limits](meshtastic-peer-test.md). The CYD
was not part of that test; the instructions below reproduce the WSL path.

## Preparation record — v0.4.0 experimental meshctrl

The existing USB hardware is sufficient for an experimental two-way test:

```text
WSL Python bridge (Meshtastic channel packet encoding/decoding)
  ↔ CH340 / 115200 8N1
  ↔ CX32 meshctrl v0.4.0 (raw radio RX/TX)
  ↔ SX1262 / 433 MHz antenna
  ↔ second EU_433 Meshtastic node ↔ phone app
```

ST-Link is used for programming/recovery, not for moving chat messages. No
Bluetooth connection between WSL and the phone/node is required. The official
Meshtastic Python CLI 2.7.11 is installed locally in `.venv`; **its normal
PhoneAPI serial connection cannot talk directly to our custom UART protocol**.
Use `bin/meshtastic_bridge.py` for this experiment. This is a small public-channel
endpoint, not the full Meshtastic stack or a compatible PhoneAPI device.

v0.4.0 was programmed with the known-target guard and its entire 11,784-byte
Flash image was compared byte-for-byte with readback. Static SRAM is 1,304 B,
reserved stack 1,024 B, leaving 1,768 B between allocations and stack. The final
binary SHA-256 is
`b225d9db13a54583cfe08be70652f90ce4c25d8933731366520151d1a106a2bd`.
The proven `uartctrl` v0.3.1 build remains unchanged at 7,664 B, SHA-256
`115756f460b1b09e180de7ae98c445d6d0810304287677155eaa96a2bf0911f1`,
and all recovery outputs are preserved locally in
`logs/mesh-prepare/recovery-v031/`.

Physical v0.4.0 checks passed: version/capabilities, PING/PONG, normal configuration
and register readbacks, finite 300 ms RX (status 0x52), expected return to RC
0x22, GPIO release, and rejection of unarmed SEND_PACKET. A six-second invocation
of the actual bridge confirmed RX/zero errors and clean STOP_RX. The final
firmware is idle, with PC3/PC4 inputs and TX unarmed. No packet was transmitted
during this preparation. The asynchronous v0.4.0 TX/event path subsequently passed the physical peer
exchange; before that test it had host coverage only. Earlier RAM-only RX/TX experiments
are separately recorded in [radio-rxtx-smoke-test.md](radio-rxtx-smoke-test.md).
The later peer test verified discovery, text TX and a peer reply over the air.
These initial preparation checks themselves did not establish that result.

## Peer settings

The user confirmed a 433 MHz peer with EU_433, LongFast and the public channel.
Its identifying suffix/name is stored only in ignored `.local/mesh-test-profile.json`.
Confirm the peer's firmware version and actual channel/frequency settings when
starting the test; region/preset alone do not rule out a manual frequency slot
or renamed channel. No private channel key is needed for this public test.

Default resolved settings:

| Item | Value |
| --- | --- |
| Center frequency | 433875000 Hz (433.875 MHz) |
| Frequency slot | 4, using the default LongFast channel name |
| SF / bandwidth / CR | SF11 / 250000 Hz / 4/5 |
| Preamble / header / CRC | 16 symbols / explicit / enabled |
| IQ / raw SX1262 sync | normal / 0x24B4 (logical 0x2B) |
| Channel name / hash | LongFast / 0x08 |
| Key | Public default PSK, alias AQ== |
| Experimental transmit power | Fixed commanded -9 dBm; output not measured |

The WSL host computes the slot; the MCU receives integer Hz and does not
calculate Meshtastic slots. `--slot 1..4` allows a confirmed manual EU_433
LongFast slot. Do not change bands to try to find an 868/915 MHz node.

## Commands for the later test

Run from WSL:

```sh
cd ~/dx-lr32-meshtastic
.venv/bin/meshtastic --version
.venv/bin/python bin/meshtastic_bridge.py plan
```

If restoring the host environment on another checkout:

```sh
python3 -m venv .venv
.venv/bin/python -m pip install -r requirements-meshtastic.lock
```

Both ST-Link and CH340 must be attached to WSL. Discover current Windows BUSIDs
using `usbipd.exe list`, then attach the appropriate device, not a guessed BUSID.
The bridge discovers a single CH340 by-id path; `--port /dev/serial/by-id/...`
overrides it. The WSL user needs serial-device permissions. No command in the
bridge installs firmware or changes Windows USB attachment.

### 1. Find the peer without transmitting

```sh
.venv/bin/python bin/meshtastic_bridge.py listen --seconds 180
```

On the phone, open the **public LongFast channel chat** and send a short message
such as `DXLR peer ready`. Discovery is passive: a quiet node cannot be located
merely from its name. A public text message immediately supplies the full sender
node number; NodeInfo packets can additionally supply long/short names. Messages
from the configured suffix are marked `target_candidate`. This is a public,
unauthenticated channel hint, not cryptographic identity verification.

The bridge logs sender, text/NodeInfo, RSSI and SNR, and deduplicates packets.
It does not forward them. Hearing the expected text from the expected full
node ID is the first over-the-air acceptance check. Do not claim discovery
from noise/RSSI alone.

### 2. Send one test message and listen for the reply

After discovery, run one deliberately authorized transmission:

```sh
.venv/bin/python bin/meshtastic_bridge.py send --transmit \
  --text 'DX-LR32 test A7C2: please reply A7C2 on this channel' --seconds 180
```

This sends one **channel broadcast**, with hop limit zero and no ACK request.
It will appear in the public channel chat, not as an encrypted direct message.
`TX_DONE` only confirms internal radio completion. On the phone, answer on the
same **public channel**, e.g. `A7C2 received`. A received reply from the peer's
full node ID confirms the two-way link. Do not use a private/DM conversation
for this first test. There is no automatic resend or automatic response to
received packets.

An optional, separate manual announcement can give the test endpoint a name:

```sh
.venv/bin/python bin/meshtastic_bridge.py announce --transmit --seconds 40
```

It broadcasts NodeInfo with the name `DX-LR32 WSL test` / `DXLR`. Wait at least
30 seconds between outgoing attempts. The MCU enforces a conservative 30-second
interval and a fresh one-shot TX token. No announcement is sent automatically.
The logical sender ID and next packet ID persist locally in
`.local/mesh-identity.json`; do not copy that identity to another active endpoint
or delete it casually, because packet ID uniqueness also protects CTR nonce
uniqueness. The file and peer profile are excluded from GitHub.

The host restores RX after successful TxDone, checks radio health every five
seconds while listening, and requests standby/released pads on normal exit.
After a failed TX or UART disconnect, inspect the log/status and explicitly
recover/reapply; do not blindly retry. MCU TX timeout/watchdog cleanup is bounded.

## Implemented scope and limits

- Public-channel AES-CTR and official protobuf Data/User encoding; text and
  NodeInfo decoding. No PKI direct messages, AEAD channels, custom channel keys,
  remote admin, mesh routing, relaying, auto ACKs, node database or phone API.
- Incoming packets use the 16-byte Meshtastic radio header, not a serialized
  MeshPacket or PhoneAPI frame. Channel CTR has no authentication; successful
  protobuf parsing is not proof of authenticity.
- One UART request at a time; RX/TX events are preserved while waiting for
  responses. Host event queue is bounded to 64; software-queue drops are reported.
- One 270-byte MCU event buffer; raw packets up to 255 B. RX is polled outside
  interrupts, pauses briefly for packet extraction, and UART emission can take
  about 49 ms for a maximally escaped frame. Packet loss/overwrites are possible;
  complete receiver-drop counting is not implemented.
- RSSI uses signed quarter-dBm and SNR signed quarter-dB in the UART event;
  host displays dBm/dB. Timestamps are local MCU milliseconds, not synchronized.
- TX requires a fresh one-shot token valid for 30 seconds. Actual PA request is
  fixed -9 dBm regardless of staged power. Only the initial LongFast/CRC/explicit
  header/normal-IQ/sync profile is accepted; computed packet airtime must be
  <=2 seconds. Every attempt charges a 30-second per-boot cooldown. Reboot clears
  that volatile budget; this is not persistent duty accounting.
- TX is rejected while a preamble/header IRQ is pending. This is a limited busy
  check, not Meshtastic's full CAD/CSMA behavior. No collision-free operation or
  interoperability is promised. Keep the first test local and deliberate.
- PC3/PC4 drive follows observed factory states, but the switch identity and
  approximately 100-ohm path remain unresolved. No current, rail-transient,
  temperature, output-power or sustained-drive measurement was made. Longer
  LongFast packets are not equivalent to the earlier short pulse experiment.
  Software readiness does not establish electrical/RF qualification.

## Reproduction, evidence and source references

```sh
make -C firmware -j2 check radio-check uart-check protocol-check mesh-check
.venv/bin/python firmware/tests/test_meshtastic_bridge.py
sh experiments/radio-smoke/check.sh
```

The separate `meshctrl` target uses the existing startup, linker, UART and
pinned Semtech driver. The existing `uartctrl` image and regression tests still
pass. Host tests cover maximum RX payload/metadata, CRC discard, single-arm TX,
30-second cooldown, expiry, radio-busy rejection, missing-IRQ watchdog, reset
fallback and recovery. Python tests cover slot/hash, protobuf packets, an
independently OpenSSL-checked AES-CTR vector, malformed packets, NodeInfo, identity
persistence and actual PTY framing with asynchronous events preceding responses.
These mocked/codec tests do not replace the pending peer test.

Local evidence: `logs/mesh-prepare/physical-uart.{json,log}`,
`final-{program,reset,listen}.log`, `final-flash-readback.bin`, `final-listen.json`,
`final-checks.log`, `final-mesh-build.log`, plus `logs/meshtastic-install.log`.
Identifiers, message logs, raw Flash readbacks and keys remain excluded from GitHub.

Reference implementation inspected at Meshtastic firmware commit
`51ac6ea7b7d7cb430acee10fcff41fbca94a20ba`:
[RadioInterface.cpp](https://github.com/meshtastic/firmware/blob/51ac6ea7b7d7cb430acee10fcff41fbca94a20ba/src/mesh/RadioInterface.cpp)
(EU_433 table, DJB2 and frequency slots),
[RadioInterface.h](https://github.com/meshtastic/firmware/blob/51ac6ea7b7d7cb430acee10fcff41fbca94a20ba/src/mesh/RadioInterface.h)
(16-byte header),
[Channels.cpp](https://github.com/meshtastic/firmware/blob/51ac6ea7b7d7cb430acee10fcff41fbca94a20ba/src/mesh/Channels.cpp)
(key expansion/hash),
[CryptoEngine.cpp](https://github.com/meshtastic/firmware/blob/51ac6ea7b7d7cb430acee10fcff41fbca94a20ba/src/mesh/CryptoEngine.cpp)
(nonce/CTR), and installed official
[Meshtastic Python package](https://github.com/meshtastic/python) 2.7.11.
See also the official [mesh packet overview](https://meshtastic.org/docs/overview/mesh-algo/)
and [encryption description](https://meshtastic.org/docs/overview/encryption/).

## v0.5.0 update

The preparation/limitations above record v0.4.0. Current `meshctrl` v0.5.0
removes its fixed-LongFast TX restriction and honors staged power, using actual
modem parameters for airtime. The host bridge still deliberately selects public
LongFast at -9 dBm for repeatable peer testing. It accepts v0.4.0 and v0.5.0.
The same peer exchange passed on v0.5.0; see [results](v0.5.0-results.md) and
[updated protocol](UART_LIVE.md). No additional bands or TCXO/DIO2 controls were
enabled. CYD source accepts both controller versions; the CYD was not reflashed
or connected to the radio during this stage.
