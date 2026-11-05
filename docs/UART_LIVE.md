# Live raw-LoRa UART controller v0.7.0

The `meshctrl` build name is retained for compatibility, but the CX32 handles
raw LoRa packets and has no Meshtastic packet/encryption dependency. Existing
protocol-v1 frame encoding, command IDs and payload layouts remain unchanged.
Legacy `uartctrl` is still a separate non-RF v0.3.1 recovery target.

BOOT and GET_VERSION identify 0.7.0. Capabilities mask is 0x007FFBFF: existing
0–9/11 features, bit 12 configurable TX, bit 13 radio/transport statistics,
bit 14 queued UART output, bit 15 multiple frequency ranges (command 24). Bit 10 (TCXO) remains clear. Bits 16–22 add RSSI, PHY statistics, RX gain, CAD, sleep/wake, duty RX and GFSK; see [feature formats](UART_FEATURES.md).

## Configuration and transmission

Use SET_FREQUENCY, SET_LORA_PARAMS, SET_PACKET_PARAMS and SET_TX_POWER followed
by APPLY_CONFIG while idle. RX and TX use the applied snapshot; pending edits
never silently alter an active session. The host supplies integer Hz and all
PHY parameters; the controller does not calculate Meshtastic channels/slots.

TX now uses the configured SF, bandwidth, coding rate, LDRO, preamble, header,
CRC, IQ, sync word and requested power. Power is a **commanded** -9..+22 dBm,
not measured antenna output. The nominal full-range SX1262 PA tuple is
04/07/00/01 with a 200 us ramp (Semtech datasheet table 13-21). The host
implements regional TX-power limits, antenna/EIRP accounting and application
policy. Capabilities offsets 26 and 27 both report 22: silicon maximum and
controller ceiling respectively. Offset 27 retains its historical wire position
and legacy name but is not an EU power limit in this live image. The default
requested power and test bridge remain -9 dBm. No value outside the hardware
command range is accepted. SET_FREQUENCY accepts 433–434 MHz or 863–870 MHz directly,
including the configured bandwidth edges. The same integer-Hz command selects
either band: no separate mode switch, enable flag or recompilation. The radio
uses image calibration 6B/6F or D7/DB according to the applied frequency.
Capabilities' original module-band fields retain the 433–532 MHz manufacturer
declaration and primary-window fields retain 433–434 MHz for old clients. New
hosts use GET_FREQUENCY_RANGES to discover all controller tuning windows. The
ranges describe accepted settings, not measured RF performance.


Implicit-header TX requires packet length equal to configured payload length.
Explicit-header TX accepts 1..255 bytes. Airtime is calculated with the actual
applied modem/packet parameters; values over 2000 ms are rejected. Slow profiles
can therefore require shorter packets. Configuration validation still enforces
LDRO, SF/preamble and band-edge combinations.

- START_RX (9): timeout_ms:u32, zero continuous; otherwise 1..262143. Finite
  timeout is a per-receive interval restarted on rearming, not an absolute
  session lifetime. No host-disconnect lease is implemented.
- STOP_RX (10), SET_MODE(0): return to standby RC and release switch GPIOs.
  Active TX returns NOT_READY and completes through its bounded watchdog.
- SEND_PACKET (11): raw bytes. Acceptance is not delivery confirmation.
- ARM_TX (21): ASCII `TX1!`. One attempt within 30 seconds; not authentication.

Each attempted TX reserves a 30-second cooldown, including a failed attempt.
Invalid length/airtime/combination or busy rejection does not consume the arm.
There is no automatic retry. Radio timeout is airtime+100 ms; software watchdog
adds 200 ms. Success resumes RX if it was active. Failure stops/releases and
requires explicit recovery. Cooldown/arming are volatile per boot and are not a
complete regulatory duty-cycle or channel-access implementation.

The most recent attempted SEND sequence is remembered until another attempt or
MCU reboot. Reusing that sequence with the same length/CRC16 returns DUPLICATE
(status 15) without another RF attempt; different data with the same sequence
returns COMBINATION. This is bounded replay protection, not an unlimited cache
or proof that the previous packet arrived. Hosts must not blindly retry TX with
a fresh sequence after a lost response. A duplicate rejection does not consume
an existing arm. There is no persistence across reboot.

## Diagnostics and recovery

GET_STATUS remains 28 bytes: mode 0 idle, 1 sleep, 2 CAD, 3 RX, 4 TX, 5 duty RX, 255 GPIO cleanup fault. Sleep/duty status queries do not perform SPI and use FF/FFFF sentinels for unobserved chip status/errors.
Actual chip status and device errors remain separate fields. GET_RF_SWITCH
keeps its layout, activation_enabled=1, and checks direction/type/pull/mux plus
input/latch levels when driven. PC3/PC4 use the user-accepted factory switching
logic. A stop always attempts input release even if GPIO configuration is
corrupted. A radio reset or cleanup failure invalidates the active config.

New commands, empty request payloads:

- GET_RADIO_STATS (22), 37-byte reply: status:u8 followed by nine u32 counters:
  RX extracted packets, CRC/header rejects, RX read/IRQ failures, RX timeouts,
  TX attempts, TX completions, TX errors, cleanup failures, duplicate-sequence
  requests. Counters wrap and reset on MCU reboot. Extracted packets can still
  be dropped at UART output; consult transport statistics too.
- GET_FREQUENCY_RANGES (24), 19-byte reply: status:u8, schema:u8=1, count:u8=2,
  followed by (minimum_hz:u32, maximum_hz:u32) for each range: 433000000–434000000
  and 863000000–870000000. No separate band selector is required.
- GET_TRANSPORT_STATS (23), 11-byte reply: status:u8, dropped_output_frames:u32,
  tx_stalls:u32, tx_queue_free:u16. Drop counts cover responses and events.

UART output uses a 1024-byte interrupt-driven ring (1023 usable), not blocking
per-byte waits in the radio loop. Frames are admitted only if their worst-case
escaped size fits, so congestion drops a whole frame instead of truncating it.
Events reserve an additional 180 bytes for a command reply. No unbounded queue
or lossless delivery guarantee exists. A 100 ms stalled transmitter flushes its
queue, stops RF, cancels arming and invalidates configuration; the host must
reconfigure. Mere cable disconnection is not necessarily a UART stall: an
unconnected UART TX pin can still shift bytes normally.

SPI stays in main context. IRQs are polled at bounded intervals. RX extraction
pauses reception and rearms before emission. The asynchronous UART reduces
blind time, but back-to-back RF packet loss is still possible. CAD, GFSK, sleep and duty RX are now exposed through typed commands. There is
no automatic CSMA/retransmission engine, arbitrary SPI access, DIO2 or DIO3 control.

## Events

- 0x80 BOOT: existing version/init-result layout.
- 0x81 RX_PACKET: generation:u32, timestamp_ms:u32, RSSI quarter-dBm:i16,
  SNR quarter-dB:i8, IRQ:u16, validity:u8, length:u8, bytes. Wrapping RX sequence.
  Validity bits 1/2/3 mark metadata present; bit 0 means configured CRC enabled.
- 0x82 TX_DONE: result:u8, elapsed_ms:u32, generation:u32; SEND sequence.
- 0x83 RADIO_ERROR: origin:u8, status:u8; async RX errors use sequence zero.
  RX read failures are reported instead of silently discarded.
- 0x84 RX_TIMEOUT: status:u8=TIMEOUT (4), sequence zero; finite hardware RX
  timeout has stopped RX and released GPIOs. Software watchdog failures instead
  produce RADIO_ERROR and increment the timeout counter.

Existing v0.4.0 physical results remain historical evidence in
[peer-test results](meshtastic-peer-test.md). Current release verification is
recorded in [v0.5.0 results](v0.5.0-results.md); the power-range follow-up is
[v0.5.1](v0.5.1-results.md).

Current dual-band configuration and physical RX checks are recorded in
[v0.6.0 results](v0.6.0-results.md).

Version 0.7.0 extension events are CAD_DONE 0x85 and GFSK_RX_PACKET 0x86;
LoRa event 0x81 is unchanged. Complete layouts and sleep/duty state rules are
in [UART_FEATURES.md](UART_FEATURES.md).
