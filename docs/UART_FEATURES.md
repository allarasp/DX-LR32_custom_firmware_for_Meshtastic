# UART radio features — controller v0.8.0

Wire protocol remains version 1. Existing commands/events keep their formats.
Firmware version 0.8.0 advertises capability mask `0x01FFFBFF`: v0.6.0 features
plus bits 16 RSSI, 17 hardware statistics, 18 RX gain, 19 CAD, 20 sleep/wake,
21 duty RX, 22 GFSK, 23 host-managed transmit policy, 24 RX activity.
Bit 10 TCXO remains clear. All integers are little endian;
request/reply lengths below are payload lengths, excluding frame headers/CRC.

## Commands

Every reply starts with status:u8. Existing error codes apply. The new commands
never start automatic TX. Actual TX still requires ARM_TX (21) then SEND (11).

| ID | Command | Request | Successful reply |
| ---: | --- | --- | --- |
| 25 | GET_RSSI | empty | status, RSSI:i16 in quarter-dBm, packet_type:u8, frequency_hz:u32 (8 B) |
| 26 | GET_PHY_STATS | empty | status, packet_type:u8, received:u16, CRC_errors:u16, header_or_length_errors:u16 (8 B) |
| 27 | RESET_PHY_STATS | empty | status (1 B) |
| 28 | SET_RX_GAIN | boosted:u8 (0 or 1) | status, boosted:u8, register_value:u8 (3 B) |
| 29 | START_CAD | symbols_code:u8, peak:u8, minimum:u8, deadline_ms:u32 (7 B) | status; completion is event 0x85 |
| 30 | SLEEP | empty | status (1 B) |
| 31 | WAKE | empty | status (1 B) |
| 32 | START_DUTY_RX | rx_ms:u32, sleep_ms:u32, session_ms:u32 (12 B) | status; ends on STOP or session expiry |
| 33 | SET_PACKET_TYPE | type:u8: 0 GFSK, 1 LoRa | status; stages until APPLY |
| 34 | SET_GFSK_CONFIG | 38-byte record below | status; stages GFSK configuration and type 0 |
| 35 | GET_GFSK_CONFIG | selector:u8: 0 pending, 1 active | status, selector, common_active_valid, selected_snapshot_type, 38-byte record (42 B) |
| 36 | GET_EXT_STATUS | empty | 25-byte record below |

### RSSI, statistics and gain

GET_RSSI requires normal RX (mode 3), including GFSK RX. It does not start or
stop RX. The signed result preserves the radio's half-dB resolution in the
existing quarter-dB unit: divide by four for dBm. It measures current receive
energy, not SNR or successful packet delivery.

Hardware statistics are separate from GET_RADIO_STATS software counters.
LoRa's third hardware counter is header errors; GFSK's is length errors. These
16-bit counters wrap. RESET_PHY_STATS only clears the SX1262 counters; it does
not clear MCU software counters. Statistics require active config and idle or
normal RX; CAD/TX/sleep/duty-RX return NOT_READY.

SET_RX_GAIN requires idle mode. It writes and verifies RX gain register 0x08AC:
normal 0x94 or boosted 0x96. The desired setting is retained in MCU RAM and
verified/reapplied during RX/TX/CAD preparation. MCU reboot returns it to normal.
The setting is not persistent Flash configuration.

### CAD

CAD is LoRa-only, requires an active config and idle mode, and uses CAD_ONLY:
no automatic RX or TX follows. Symbols codes 0/1/2/3/4 mean 1/2/4/8/16 symbols.
Peak/minimum must be nonzero, minimum <= peak. Host deadline is 10..30000 ms;
it must also accommodate the configured symbol duration. The host chooses
appropriate detector parameters for its SF/BW. The tested default is code 2,
peak 22, minimum 10, deadline 1000 ms on SF11/BW250000.

Event 0x85 CAD_DONE: result:u8, detected:u8, elapsed_ms:u32, generation:u32
(10 B). Sequence matches START_CAD. STOP_RX cancels CAD with result DISABLED
and no spurious RADIO_ERROR. A genuine failure emits CAD_DONE plus RADIO_ERROR.
No RF energy or preamble detected is not a guarantee of a clear channel.

### Sleep and duty RX

SLEEP is deliberately **cold radio sleep**, accepted only while idle. It
cancels arming, releases switch GPIOs and invalidates active configuration;
pending settings remain in MCU RAM. The MCU/UART remain awake. WAKE verifies
standby RC and errors; APPLY is required before the next RX/TX. There is no
claim of transparent warm retention or whole-board low-power consumption.

Duty RX uses the SX1262's hardware RX/sleep loop. Both rx_ms and sleep_ms are
10..10000; session_ms is at least their sum and at most 3600000. It requires
idle/active config. DIO1 signals packet completion; firmware does not poll SPI
through sleeping intervals. The retention list preserves RX gain and existing
modulation/IQ workaround registers. Successful packets are extracted and duty
RX is rearmed without restarting the overall session deadline. Session expiry
emits existing RX_TIMEOUT (0x84) and returns to standby. STOP_RX wakes the radio
if necessary and ends the session. Appropriate peer preamble/window timing is
required; duty RX can miss packets that continuous reception would receive.

GET_STATUS during sleep or duty RX returns logical state but does not touch
SPI: raw chip status is FF and device_errors FFFF, meaning unobserved. Those
sentinels are not a measured radio fault. GET_ERRORS/GET_READBACK/PHY_STATS are
rejected in those modes so they cannot accidentally wake/terminate sniffing.
GET_EXT_STATUS/PING/config snapshots remain available without changing RF state.

## GFSK configuration

Common frequency and power still use existing commands 4/7. SET_GFSK_CONFIG
stages the following record; APPLY (8) validates and commits it. SET_LORA_PARAMS
(5) selects pending LoRa for backward-compatible hosts. Explicit command 33 is
also available. Active and pending modulation settings are independent.

| Offset | Type | Field |
| ---: | --- | --- |
| 0 | u32 | Bitrate, bits/s, 600..300000 |
| 4 | u32 | Frequency deviation Hz, 1..200000 |
| 8 | u32 | RX bandwidth Hz: 4800,5800,7300,9700,11700,14600,19500,23400,29300,39000,46900,58600,78200,93800,117300,156200,187200,234300,312000,373600,467000 |
| 12 | u16 | Preamble length in bits, >=8 |
| 14 | u16 | CRC initial seed |
| 16 | u16 | CRC polynomial, nonzero |
| 18 | u16 | Whitening seed, 0..511 |
| 20 | 8 bytes | Sync word, padded to eight bytes |
| 28 | u8 | Pulse shape: 0 off; 8 BT0.3, 9 BT0.5, 10 BT0.7, 11 BT1.0 |
| 29 | u8 | Preamble detector: 0 off; 4/5/6/7 = 8/16/24/32 bits |
| 30 | u8 | Sync length in bits: 0..64, byte-aligned |
| 31 | u8 | Address filter: 0 disabled, 1 node, 2 node+broadcast |
| 32 | u8 | Header: 0 fixed length, 1 variable length |
| 33 | u8 | Payload length / variable RX maximum: 1..255 |
| 34 | u8 | CRC: 1 off; 0 one byte, 2 two bytes, 4 one byte inverted, 6 two bytes inverted |
| 35 | u8 | Whitening: 0 disabled, 1 enabled |
| 36 | u8 | Node address |
| 37 | u8 | Broadcast address |

APPLY requires bitrate + 2*deviation <= RX bandwidth and bandwidth edges within
one of the existing tuning windows. The preamble must cover its detector size.
Workarounds for 600/1200 bit/s are applied and reset when leaving those settings.
Fixed-length SEND must match configured payload length. Address-filter users
must supply the appropriate address byte in their raw packet; this feature has
not had a peer interoperability test. GFSK airtime uses GFSK parameters and
retains the 2000 ms packet guard and TX cooldown in default test mode;
explicit host-managed mode has the limits described below.

GFSK APPLY/readback mask is 0x001B: packet type, sync, TX-modulation workaround,
and GFSK seed/address checks. It does not claim full frequency/modem/packet
parameter readback. Legacy GET_CONFIG still carries the common frequency/power
and the retained LoRa fields; use GET_GFSK_CONFIG for GFSK snapshots. Legacy
GET_READBACK returns UNSUPPORTED while GFSK is active.

Event 0x86 GFSK_RX_PACKET uses 15 metadata bytes followed by raw payload:

| Offset | Type | Meaning |
| ---: | --- | --- |
| 0 | u32 | Configuration generation |
| 4 | u32 | Timestamp ms |
| 8 | i16 | Average RSSI, quarter-dBm |
| 10 | i16 | Sync RSSI, quarter-dBm; no GFSK SNR field |
| 12 | u8 | Raw SX1262 GFSK RX status |
| 13 | u8 | Flags: bit0 CRC configured; bits1/2/3 average RSSI/sync RSSI/status present |
| 14 | u8 | Payload length |
| 15 | bytes | Raw payload, up to 255 B |

LoRa remains event 0x81 with its original RSSI/SNR/IRQ layout. GFSK abort,
length, CRC and address-error flags are rejected rather than delivered as good
packets. A single fixed event buffer is shared; this is not a lossless queue.

## Extended status

Reply offsets: 0 status; 1 schema=1; 2 mode; 3 pending packet type; 4 last
active type; 5 active_valid; 6 desired boosted gain; 7 BUSY GPIO; 8 DIO1 GPIO;
9 generation:u32; 13 remaining duty session ms:u32; 17 remaining TX cooldown
ms:u32; 21 remaining arm lifetime ms:u32. Modes: 0 idle, 1 sleep, 2 CAD,
3 normal RX, 4 TX, 5 duty RX, 255 cleanup fault. An active type is not proof of
valid hardware config unless active_valid is true.

Timed LoRa RX cleanup now stops RTC/clears its event through Semtech's provided
helper, including implicit-header receive-to-stop transitions. There are still
no arbitrary register writes, DIO2/DIO3 control commands, continuous carrier or
infinite-preamble commands.

## Host-managed Meshtastic channel access (v0.8.0)

| ID | Command | Request | Successful reply |
| ---: | --- | --- | --- |
| 37 | SET_HOST_MODE | Empty to query; otherwise mode:u8 (0 test, 1 host-managed), set only while idle | status:u8, mode:u8 |
| 38 | GET_RX_ACTIVITY | Empty; normal RX with valid configuration required | status:u8, active:u8 (0/1), raw_irq:u16 |

Mode defaults to 0 after MCU boot and is reset to 0 by a UART transport fault.
Mode 1 explicitly assigns channel-access and airtime policy to the UART host.
It removes the fixed 30-second TX pause and raises the per-packet airtime bound
from 2 s to 60 s. It does not transmit anything or grant an ARM. One-shot ARM,
duplicate-sequence protection, hardware timeout, power/frequency/PHY validation
and cleanup behavior remain. GET_EXT_STATUS reports zero TX cooldown in mode 1;
its existing schema/length is unchanged. Query command 37 for the current mode.
Switching back to mode 0 retains any unexpired conservative attempt cooldown.

Activity queries leave RX/IRQ state intact. Preamble/sync/header IRQs defer the
host; a latched noise indication expires after maximum configured packet airtime
plus 1 s. Completed/error/timeout packets are not classified as active RX.
After an idle result, the CYD stops RX, runs CAD with the current PHY and awaits
the matching event. Busy CAD resumes RX and backs off. A clear scan permits
ARM/SEND after the upstream queue delay and airtime/channel-budget checks.
The CYD uses upstream Meshtastic's 16-packet priority queue, cancellation and
randomized/SNR-weighted delays, and logs estimated TX airtime once even if a
completion becomes uncertain. Actual radio completion still does not prove
peer delivery; normal Meshtastic acknowledgements provide that separately.

## Host tool

`bin/radio_features.py` uses only the Python standard library and preserves
asynchronous events while waiting for replies. Examples:

```sh
python3 bin/radio_features.py rssi --samples 10
python3 bin/radio_features.py stats
python3 bin/radio_features.py gain --gain 1
python3 bin/radio_features.py cad
python3 bin/radio_features.py duty --rx-ms 300 --sleep-ms 100 --seconds 10
python3 bin/radio_features.py sleep
python3 bin/radio_features.py wake
python3 bin/radio_features.py configure --modem gfsk --bitrate 50000 --deviation 25000 --bandwidth 117300
python3 bin/radio_features.py rx --modem gfsk --seconds 10
# Exactly one explicit raw packet; not a Meshtastic message:
python3 bin/radio_features.py tx --modem gfsk --hex 44584c523037 --power -9 --transmit
python3 bin/radio_features.py test
```

Sleep leaves the radio asleep; wake leaves config invalid until APPLY. Session
commands stop/release on exit. No TX retry is automatic. LoRa tool options
include --sf, --bandwidth, --cr, --ldro, --preamble, --sync-word, --implicit,
--length, --crc and --invert-iq. GFSK sync is supplied as hex through --sync.
See [physical results](v0.7.0-results.md) and [ESP32 integration](ESP32_INTEGRATION.md).
