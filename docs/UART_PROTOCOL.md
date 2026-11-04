# DX-LR32 binary radio protocol v1

Implemented by the separate **uartctrl v0.3.0** image. The v0.2.1 text-diagnostic
image remains a buildable recovery target. This protocol is experimental and
is **not Meshtastic's client/protobuf serial protocol**. No interoperability
with Meshtastic nodes has been demonstrated.

The ESP32 owns Meshtastic configuration, encryption, channel/preset selection,
routing and persistent settings. It sends resolved physical parameters and,
when enabled in a future release, opaque raw LoRa packets. The CX32 does not
calculate a Meshtastic frequency slot or parse encrypted mesh contents.

## Transport and framing

115200 baud, 8 data bits, no parity, 1 stop bit; crossed TX/RX and common GND.
One request may be outstanding. Wait for its response before sending another.
No RTS/CTS is required. All multibyte integers are **little endian** unless
explicitly described as a raw register value.

```text
7E | escaped(version kind command sequence:u16 length:u16 payload CRC:u16) | 7E
```

| Decoded offset | Bytes | Field |
| --- | ---: | --- |
| 0 | 1 | Version = 1 |
| 1 | 1 | Kind: 0 request, 1 response, 2 event |
| 2 | 1 | Command/event ID |
| 3 | 2 | Sequence number |
| 5 | 2 | Payload length, 0–272 |
| 7 | length | Payload |
| 7 + length | 2 | CRC-16/CCITT-FALSE, little endian |

CRC covers the unescaped seven-byte header and payload; polynomial `0x1021`,
initial `0xFFFF`, no reflection, no final XOR. Check value for `123456789` is
`0x29B1`. GET_VERSION sequence 1, empty payload, is:
`7e010001010000004a957e`.

Inside a frame, `7E` encodes as `7D 5E`, and `7D` as `7D 5D`. Other escape
sequences are invalid. Delimiters are never escaped. A closing delimiter may
also open the next frame; repeated delimiters are harmless. Noise before an
opening delimiter is ignored. Maximum decoded body is 281 bytes; maximum
escaped frame is 564 bytes, about 49 ms on this UART.

The parser abandons a nonempty partial frame after 100 ms without another
consumed byte, or 1,000 ms total. Both deadlines handle millisecond-counter
wraparound. Overlong/invalid frames are discarded until the next delimiter.
UART framing errors or receive-ring overflow abort the parser and increment a
loss counter. CRC, length, version and framing validation precede dispatch;
invalid frames cannot stage or apply settings. Response/event input is ignored,
preventing response loops.

If at least a seven-byte request header exists, parsing errors return a
one-byte status with the header's command/sequence. Those fields are untrusted
on a bad-CRC request, so clients must match their outstanding transaction.
Short/no-header noise only increments counters. Responses echo command and
sequence and use protocol version 1, including an unsupported-version error.
No automatic retransmission or sequence replay cache is implemented. Sequences
correlate responses; reusing one does not suppress execution. Getters and SET
operations can be retried after resynchronization; after an APPLY/RESET timeout,
query status/configuration and reconcile before retrying. The host utility uses
2-second response deadlines, 10 seconds for APPLY's bounded sequence of SPI
operations. Normal measured APPLY time is approximately 50 ms.

## Responses and errors

Every response payload starts with one status byte; following offsets below
are relative to the **response payload, including that byte**. A nonzero status
may still include command-specific diagnostics; never interpret it as success.
Most errors contain only that byte.

| Status | Meaning |
| ---: | --- |
| 0 | OK |
| 1 | Unsupported wire version |
| 2 | Invalid frame/escape |
| 3 | CRC mismatch |
| 4 | Partial-frame timeout |
| 5 | Unknown command |
| 6 | Invalid payload/body length |
| 7 | Parameter/policy range error |
| 8 | Invalid combination/occupied bandwidth |
| 9 | Unsupported hardware frequency |
| 10 | Radio transfer, command/status or device-error failure |
| 11 | Not ready (reserved for future radio operations) |
| 12 | Hardware readback mismatch |
| 13 | Operation disabled |
| 14 | Decoded frame overflow |

## Commands

| ID | Name | Request | Successful response after status |
| ---: | --- | --- | --- |
| 1 | GET_VERSION | empty | firmware major/minor/patch:u8 each, wire version:u8; currently 0/3/0/1 |
| 2 | GET_CAPABILITIES | empty | capability record below |
| 3 | GET_RADIO_STATUS | empty | status record below |
| 4 | SET_FREQUENCY | frequency_hz:u32 | empty; stages only |
| 5 | SET_LORA_PARAMS | SF:u8, bandwidth_hz:u32, CR denominator:u8, LDRO:u8 | empty; stages only |
| 6 | SET_PACKET_PARAMS | preamble:u16, raw sync word:u16, header:u8, CRC:u8, invert_IQ:u8, payload_length:u8 | empty; stages only |
| 7 | SET_TX_POWER | requested dBm:i8 | empty; validates/stages only; PA programming disabled |
| 8 | APPLY_CONFIG | empty | applied mask:u16, readback mask:u16, generation:u32 |
| 9 | START_RX | reserved below | DISABLED in this release |
| 10 | STOP_RX | empty | empty; requests standby RC |
| 11 | SEND_PACKET | reserved opaque bytes below | DISABLED in this release |
| 12 | GET_DEVICE_ERRORS | empty | device error mask:u16; errors are not cleared |
| 13 | RESET_RADIO | empty | empty; reset + standby RC; invalidates active configuration |
| 14 | GET_CONFIG | empty | configuration record below |
| 15 | SET_MODE | mode:u8 | empty; only 0 (standby RC) accepted |
| 16 | GET_LINK_STATS | empty | accepted frames:u32, parser errors:u32, parser timeouts:u32, UART losses:u32 |
| 17 | GET_READBACK | empty | packet type:u8, raw sync word:u16, IQ register:u8, TX-modulation register:u8 |

Unknown commands fail explicitly. Lengths are exact. START_RX/SEND_PACKET always
return DISABLED for any otherwise well-framed payload; their operational length
validation is deferred until the capabilities advertise RX/TX support. Reserved
modes are 1 sleep, 2 standby XOSC, 3 RX, 4 TX; all currently return DISABLED.
There is no arbitrary-SPI or debug override command.

### Capability response (76 bytes including status)

| Offset | Type | Meaning/current value |
| ---: | --- | --- |
| 1 | u8 | Schema = 1 |
| 2 | u32 | Feature mask = `0x1F` |
| 6 | u16 | Future maximum raw packet = 255 bytes |
| 8 | u16 | Maximum wire payload = 272 bytes |
| 10, 14 | u32 each | Declared module band: 433000000–532000000 Hz |
| 18, 22 | u32 each | Allowed firmware policy: 433000000–434000000 Hz |
| 26 | u8 | Declared silicon/module TX ceiling: 22 dBm; not measured |
| 27 | u8 | Policy maximum requested power: 10 dBm |
| 28 | u8 | EU_433 duty-cycle metadata: 10 percent |
| 29 | u8 | Number of accepted nominal bandwidths = 10 |
| 30 | 10 x u32 | 7800, 10400, 15600, 20800, 31250, 41700, 62500, 125000, 250000, 500000 Hz |
| 70, 71 | u8 each | Minimum/maximum SF = 5/12 |
| 72, 73 | u8 each | Minimum/maximum CR denominator = 5/8 |
| 74 | u16 | Supported hardware-apply mask = `0x0F` |

Feature bits: 0 binary diagnostics, 1 staging, 2 non-RF apply, 3 partial register
readback, 4 requested-power staging; bits 8 RX, 9 TX, 10 TCXO control, 11 RF-switch
control are **clear**. Unlisted bits are reserved and sent as zero. Schema 1
represents one module interval and one policy interval. Future module variants
can report different intervals; disjoint/multiple bands require a new capability
schema. Clients must check schema, feature bits, both band intervals and apply
mask rather than infer capability from the SX1262 silicon's tuning range.

The 433–532 MHz band comes from the stated DX-LR32-A hardware specification and
has not been RF-qualified here. The narrower policy follows the inspected
Meshtastic EU_433 region table. 868 MHz is rejected, not silently remapped.
Frequencies within the module band but outside the policy return RANGE;
frequencies outside the module band return UNSUPPORTED. There is also an
independent PLL-word band guard at the SPI HAL boundary.

### Configuration records and validation

A configuration is exactly 20 bytes, serialized explicitly rather than copying
C structs:

| Offset | Type | Field |
| ---: | --- | --- |
| 0 | u32 | RF center frequency in Hz |
| 4 | u32 | Nominal bandwidth in Hz |
| 8 | u16 | Preamble symbols, 8–65535 |
| 10 | u16 | Full SX126x LoRa sync register pair, MSB/LSB register value |
| 12–18 | u8 each | SF, CR denominator, LDRO, header, CRC, inverted IQ, payload length |
| 19 | i8 | Requested TX power in dBm |

Wire byte order stays little endian: raw sync `0x24B4` is sent `B4 24`, then
written to SX registers `0740=24`, `0741=B4`. Meshtastic's logical `0x2B` sync word
maps to `0x24B4` in this representation. The host must perform that translation;
no sync-word constant is imposed by the controller.

SF is 5–12; CR denominator 5–8 means 4/5–4/8. Header 0 is explicit, 1 implicit.
CRC/IQ/LDRO are 0 or 1. Payload length is 1–255; it is the fixed packet length in
implicit mode and the configured maximum in explicit mode. SF5/SF6 require at
least 12 preamble symbols and LDRO off. The conservative validation requires
LDRO when symbol time is at least 16 ms. LDRO is sent explicitly, not silently
chosen. The full occupied nominal bandwidth must fit within the policy band.
Requested power is limited to -9..10 dBm. Hardware PA configuration, antenna
gain and actual radiated power remain unverified; accepting this value does
not program the PA or establish regulatory compliance.

SET commands validate their own field ranges and change **pending RAM only**.
Cross-field combinations are checked by APPLY, permitting ordered multi-field
updates. Invalid SET leaves both records unchanged. Invalid combinations fail
before SPI and leave the old active record valid. A validated APPLY first
invalidates active state, writes standby RC, LoRa packet type, integer-frequency
PLL value, modem/packet parameters and sync registers, and checks status/errors.
It includes the Semtech IQ and 500 kHz modulation workarounds. Only after all
checks/readbacks pass is the active snapshot committed. Hardware changes are
not transactional: a transfer failure can leave partial hardware settings,
which is why active-valid and masks are cleared; retry a full apply or reset.

Applied-mask bits: 0 frequency command, 1 modem command/workaround, 2 packet
command/IQ workaround, 3 sync registers, 4 PA power (**clear**). Readback-mask
bits: 0 packet type, 1 sync registers, 2 IQ workaround bit, 3 TX-modulation
workaround bit. These masks have different meanings. The active record's power
is requested metadata, **not an assertion of a programmed PA setting**.
Frequency, SF/BW/CR, preamble/header/CRC have no independent documented getter
used here. Status acceptance is not direct frequency/modem readback or RF
validation. GET_READBACK exposes only the documented reads listed above.
Frequency conversion uses Semtech's integer helper and a nominal 32 MHz
reference; hardware PLL quantization is approximately 0.954 Hz per step.

GET_CONFIG response: pending record at offset 1; active snapshot at 21;
active-valid:u8 at 41; dirty:u8 at 42; applied-mask:u16 at 43;
readback-mask:u16 at 45; generation:u32 at 47. Generation changes on radio reset
and on an apply that reaches hardware; validation-only rejection does not
increment it. An invalid active snapshot must never authorize future RX/TX.

GET_RADIO_STATUS offsets: mode:u8 at 1 (`0` RC, `255` unknown), last raw status:u8
at 2, errors:u16 at 3, active-valid:u8 at 5, dirty:u8 at 6, applied mask:u16 at 7,
readback mask:u16 at 9, generation:u32 at 11, uptime_ms:u32 at 15,
BUSY timeouts:u32 at 19, SPI failures:u32 at 23, last HAL error:u8 at 27.
A failed query may retain the last observed raw status; response status must be
checked. HAL errors 0–5 are OK, BUSY timeout, SPI timeout/fault, stalled tick,
bad argument, blocked command. Link frame count excludes the current stats
request until its handler completes. Counters/generation wrap modulo 2^32.

## Events and future packet operations

Current events: `0x80 BOOT`, payload firmware major/minor/patch and init status
(4 bytes), sequence 0; `0x83 RADIO_ERROR`, payload originating command and error
status, emitted after a RADIO_ERROR/READBACK response with its request sequence.
No error is automatically cleared. Query status/errors and decide whether to
reset/reapply. No background radio IRQ processing is enabled yet.

Reserved v1 future operations, unavailable until capability bits are set:

- START_RX: timeout_ms:u32; 0 continuous, otherwise a finite timeout no greater
  than 262143 ms. Command acknowledgment only means accepted, not a packet.
- SEND_PACKET: 1–255 opaque bytes, including the complete host-built radio
  header and encrypted payload. No Meshtastic protobuf or UART fragmentation
  is placed inside the radio packet. Completion is asynchronous.
- `0x81 RX_PACKET`: config generation:u32, local timestamp_ms:u32,
  RSSI in quarter-dBm:i16, SNR in quarter-dB:i8, IRQ flags:u16, validity flags:u8,
  packet length:u8, raw packet bytes. Maximum payload 270 bytes. Validity bits
  0 CRC OK, 1 RSSI valid, 2 SNR valid, 3 timestamp valid; others reserved.
  The frame sequence is a wrapping RX event counter, not a request sequence.
- `0x82 TX_DONE`: result:u8, elapsed_ms:u32, config generation:u32; frame sequence
  correlates to SEND_PACKET. An eventual error also generates RADIO_ERROR.

Future implementations must define queue-full behavior and overflow reporting,
use bounded DIO1 IRQ service and keep SPI out of the ISR. One RX and one TX
255-byte buffer would consume 510 additional bytes before metadata; they are
not currently allocated or advertised as operational. At 115200 baud a maximum
escaped RX event can occupy roughly 49 ms, so a future adapter must account for
backpressure, dropped events, half-duplex radio timing and scheduling latency.
Local millisecond timestamps are not synchronized with ESP32 time.

Meshtastic's CAD/activity detection, receive rearming, preamble/IRQ handling,
RX gain and TX-completion ownership require additional adapter work. Reserved
RX/TX commands alone do not implement those contracts. Duty-cycle metadata is
not an airtime scheduler: TX is disabled. A future transmitting release must
add enforcement, verify RF-switch/oscillator/PA wiring and test RF performance.

## Persistence, reconnect and diagnostics

Everything is volatile. Boot stages 433875000 Hz, BW250000, SF11, CR4/5,
LDRO0, preamble16, sync24B4, explicit header, CRC on, normal IQ, length255,
requested10dBm; **boot never applies these settings or starts RX/TX**. Only
radio reset + standby RC is performed. Radio RESET preserves pending values
but invalidates active state; MCU reset reinstates defaults. Generation is not
a unique boot/session identifier.

On every new ESP32 connection: send a delimiter; GET_VERSION/CAPABILITIES;
verify schema/features/band/policy; GET_STATUS; stage the host's full resolved
configuration; APPLY; check masks and GET_READBACK. Reconfigure after any MCU
reset, missed BOOT event or uncertain timeout. Persistent user configuration
belongs on the ESP32. Closing/reopening UART by itself does not reset the MCU
or discard its current RAM settings; a partial frame expires or is discarded
at the next delimiter. There are no writes to option bytes, calibration storage
or target configuration Flash.

GET_VERSION, GET_STATUS, GET_DEVICE_ERRORS, GET_CONFIG, GET_READBACK and
GET_LINK_STATS provide the binary equivalent of the old text diagnostics.
The uartctrl image emits no text banner. Do not send ASCII PING to it; build
`radiodiag` for the preserved v0.2.1 ASCII interface. See the
[build and physical test record](uart-protocol-results.md).
