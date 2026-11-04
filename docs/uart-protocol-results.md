# UART controller v0.3.0 — build and physical results, 2026-11-04

**The physical CH340 binary-protocol test passed on CX32L003F8 and the onboard
radio.** Dynamic non-transmitting configuration, partial register readbacks,
explicit rejection and parser recovery were exercised. Actual RF frequency,
RF performance, RX/TX and Meshtastic interoperability remain untested.

The [wire specification](UART_PROTOCOL.md) is the authoritative command layout.
The working v0.2.1 radio diagnostic image remains unchanged/buildable as recovery
(SHA-256 `5fbece8f92d0ca1231fe96bc8a1a61c4f887d8aea6a071856cbba6a2a6982505`).
Local recovery artifacts were saved before development under
`logs/protocol-stage/recovery-v0.2.1/`.

## Build and memory

Use the same pinned dependencies and tools as [BUILD.md](BUILD.md): GCC Arm
13.2.1, binutils 2.42, host GCC 13.3.0, GNU Make 4.3, Python 3.12.3. No additional
Python package or firmware dependency was introduced.

```sh
python3 firmware/tools/prepare_deps.py
make -C firmware protocol-check radio-check uart-check check
arm-none-eabi-size -A firmware/build/uartctrl/controller.elf
python3 -m py_compile bin/uart_radio.py
```

Output: `firmware/build/uartctrl/controller.{elf,bin,hex,map}`.

| Resource | Usage |
| --- | ---: |
| Flash | 6,888 bytes / 65,536 |
| Static SRAM (.data + .bss) | 988 bytes / 4,096 |
| Reserved stack | 1,024 bytes |
| Unallocated SRAM below stack | 2,084 bytes |
| Heap | none |

The receive ISR uses a 512-byte ring (511 usable); the bounded parser holds at
most 281 decoded bytes. Request handlers use a maximum 80-byte response area.
No radio packet queues/buffers are currently allocated. All storage is fixed;
no recursion or variable-length stack arrays are used. GCC stack-usage output
shows `rp_feed` 128 bytes, `rc_handle` 104, main 40, transport 48 and SPI I/O 32;
these are individual function frames, not a whole-call-stack measurement.
Inspection of the linked call paths, interrupt frames and small libgcc helpers
leaves margin within the reserved 1 KB; linker assertions protect static-data
separation. A runtime stack-watermark attempt could not connect through SWD,
so no measured runtime high-water mark is claimed. Future IRQ/RX/TX additions
need a renewed stack/buffering audit.

Image validation passed: vectors at 0, initial MSP `0x20001000`, Thumb reset
entry `0x000000C1`, no unresolved symbols and no load sections outside
64 KB Flash / 4 KB SRAM. New binary SHA-256:
`f99f0d03f340f1a922397e2b99574ace544d81745f093a512ed1b817304b49d1`.

Automated coverage includes golden CRC/framing, every byte value, fragmented
input, invalid version/length/CRC/escapes, oversize frames, incomplete-frame and
total-frame deadlines, timer wraparound, bounded random noise and recovery,
response-loop prevention, staging versus active state, invalid frequencies and
parameter combinations, readback mismatches, SPI/BUSY/timer failures and reset
cleanup. Forbidden PA/RF/TCXO/switch commands and an 868 MHz PLL word are rejected
at the HAL boundary. Python's independent codec also drives the actual C parser,
configuration service and pinned Semtech encoder through a mock platform.
Six Python tests pass. The C tests also passed AddressSanitizer/UndefinedBehaviorSanitizer;
these are host memory checks, not hardware timing proof.

## Programming and startup

The existing reviewed `cx32l003` OpenOCD procedure was used at 100 kHz:
`0.12.0+dev-g2431daa-dirty`, native ST-Link V2J34S7. Before programming, the private
identity/USERCFG guard and the complete previous 64 KB image were checked.
One deliberate programming attempt erased only application sectors 0–13
(512 bytes each), wrote 6,888 bytes at Flash 0, verified the image and read back
all firmware bytes. `cmp` and SHA-256 matched. No unlock, mass erase,
option-byte change or target configuration persistence was used.

ST-Link then failed to reconnect for software reset, even after USB reattachment.
The user confirmed a target power cycle. This also disconnected the CH340,
so the initial BOOT-event capture ended with a USB disconnect; it is not a
successful boot-banner capture. Both adapters were reattached to WSL, serial
permissions were restored, and the running v0.3.0 answered the complete binary
self-test after power restoration. Thus startup and command handling after the
user-confirmed power cycle are verified, but its initial BOOT event was missed.
No additional flashing was needed. The host's USB-disconnect cleanup was fixed
and unit-tested so a cleanup error cannot hide the original disconnect.

Commands used (private target-specific configs/logs remain ignored):

```sh
tools/openocd-cx32/bin/openocd -f configs/bringup-uartctrl-program.cfg
cmp firmware/build/uartctrl/controller.bin logs/protocol-stage/flash-readback.bin
sha256sum firmware/build/uartctrl/controller.bin logs/protocol-stage/flash-readback.bin
tools/openocd-cx32/bin/openocd -f configs/bringup-full-reset.cfg
# After user power cycle, reattach only the known CH340/ST-Link Windows BUSIDs.
python3 bin/uart_radio.py --log logs/protocol-stage/physical-after-power.json self-test
python3 bin/uart_radio.py --log logs/protocol-stage/reconnect.json config
```

The programming config calls `verify_image` against the previous saved image,
`flash write_image erase firmware/build/uartctrl/controller.bin 0 bin`,
`verify_image` against that binary, then
`dump_image logs/protocol-stage/flash-readback.bin 0 6888`. It remains halted
until host comparison. A generic unguarded automatic flasher is not published.

## Actual physical results

All records below come from decoded CH340 responses with valid wire CRC and
matching request sequence, not from the host mocks.

| Check | Result |
| --- | --- |
| GET_VERSION | firmware 0.3.0, protocol 1 |
| GET_CAPABILITIES | module 433000000–532000000 Hz; policy 433000000–434000000 Hz; RX/TX/TCXO/switch bits clear |
| Baseline APPLY | 433875000 Hz, BW250000, SF11, CR4/5, LDRO0, preamble16, sync24B4, explicit, CRC on, normal IQ, length255 |
| Baseline GET_READBACK | type=1 (LoRa), sync=24B4, IQ register=0D, TX-modulation register=04 |
| Changed APPLY | **433500123 Hz**, BW125000, SF9, CR4/8, LDRO0, preamble24, sync1424, implicit, CRC off, inverted IQ, length42 |
| Changed GET_READBACK | type=1, sync=1424, IQ register=09, TX-modulation register=04 |
| GET_CONFIG | pending/active snapshots match each applied candidate; valid=1, dirty=0, masks=000F/000F |
| 868 MHz / below module / above module | UNSUPPORTED; no active-state change |
| 500 MHz within declared module but outside EU_433 policy | RANGE |
| SF4, requested +11 dBm | RANGE |
| SF12/BW125000 without LDRO | COMBINATION on apply; previous active record/generation preserved |
| START_RX, SEND_PACKET, modes sleep/XOSC/RX/TX | DISABLED |
| 272-byte SEND containing NUL/7D/7E | DISABLED, correctly framed and received |
| Bad CRC / version / command payload length / unknown command | CRC / VERSION / LENGTH / UNKNOWN respectively |
| 273-byte decoded payload | OVERFLOW; subsequent commands work |
| Truncated request / invalid escape | TIMEOUT / FRAME; subsequent requests work |
| Escaped sequence 0x7E7D | response matches sequence |
| First-run link counters before stats request completes | 40 accepted frames, 5 deliberate parser errors, 1 timeout, **0 UART losses** |
| Final status after restoring baseline | STBY_RC, raw=22, errors=0000, active valid |
| Close/reopen host utility | GET_CONFIG succeeds; RAM configuration retained |

Representative response payloads, including leading status:

```text
GET_VERSION:              00 00 03 00 01
Baseline GET_READBACK:    00 01 B4 24 0D 04
Changed GET_READBACK:     00 01 24 14 09 04
Successful APPLY (gen 4): 00 0F 00 0F 00 04 00 00 00
Unsupported frequency:   09
Disabled RX/TX:          0D
CRC failure:             03
Partial-frame timeout:   04
```

The non-RF APPLY path checks command status/errors and genuine register reads.
There is no independent documented frequency/modem getter used: the exact
integer-Hz value is retained in the active software snapshot and converted by
Semtech's integer PLL helper. This proves configuration-command acceptance,
not that an RF oscillator/carrier was measured at that frequency. TX power is
validated/staged metadata only; the applied mask explicitly excludes PA power.
No SetTx, SetRx, SetFs, image calibration, PA setup, DIO2 RF-switch control or
DIO3 TCXO control was issued. Crystal/TCXO and switch wiring remain unknown.

## Meshtastic architecture review

Reviewed `meshtastic/firmware` develop revision
`51ac6ea7b7d7cb430acee10fcff41fbca94a20ba` on 2026-11-04, not an unspecified moving
branch. This source is inspected documentation only, not a new build dependency:

- [RadioInterface.cpp](https://github.com/meshtastic/firmware/blob/51ac6ea7b7d7cb430acee10fcff41fbca94a20ba/src/mesh/RadioInterface.cpp):
  EU_433 table, preset/custom settings, bandwidth, default-name hashing,
  frequency slot count/selection and frequency override. Its EU_433 record is
  433.0–434.0 MHz, duty 10, power 10. The ESP32 must preserve that behavior and
  pass the final frequency; the CX32 adds no independent slot calculation.
- [RadioInterface.h](https://github.com/meshtastic/firmware/blob/51ac6ea7b7d7cb430acee10fcff41fbca94a20ba/src/mesh/RadioInterface.h):
  255-byte maximum raw LoRa packet and 16-byte PacketHeader. A future SEND must
  carry the whole raw frame; the mesh header consumes part of the 255-byte
  budget. The CX32 does not interpret it.
- [RadioLibInterface.h](https://github.com/meshtastic/firmware/blob/51ac6ea7b7d7cb430acee10fcff41fbca94a20ba/src/mesh/RadioLibInterface.h):
  logical sync word 0x2B and asynchronous radio/task contracts.
- [SX126xInterface.cpp](https://github.com/meshtastic/firmware/blob/51ac6ea7b7d7cb430acee10fcff41fbca94a20ba/src/mesh/SX126xInterface.cpp):
  frequency/modem/sync/preamble configuration, power limiting, RX/CAD/activity
  logic, IRQ/preamble handling and raw packet staging. This cannot be replaced
  by merely tunneling Meshtastic's external protobuf serial API.
- [Pinned Semtech encoder](https://github.com/Lora-net/sx126x_driver/blob/a10c5dfdf89788c6ac805e9fe98889de44175aa2/src/sx126x.c)
  and [Semtech datasheet provenance](radio-bring-up.md#sources): integer PLL
  conversion, documented registers and required IQ/500 kHz workarounds.

An ESP32 UART RadioInterface adapter, packet IRQ/timing ownership, CAD/activity,
RX rearming, RSSI/SNR/CRC metadata, TX completion, duty-cycle enforcement and RF
qualification remain future work. The protocol reserves data/event formats
without claiming these operations work. **No Meshtastic compatibility claim is
made.**

## Retained local evidence

Ignored `logs/protocol-stage/` contains recovery images, `meshtastic/` source
snapshots/revision, `build.log`, `all-checks.log`, `protocol-final.log`,
`sanitized.log`, `stack-usage.txt`, `stack-mark.log` (failed SWD attempt),
`program.log`, `flash-readback.bin`, `reset.log` (failed reconnects),
`physical.log` (USB disconnect), `physical-after-power.{log,json}`,
`physical-final.{log,json}` (updated host utility), `rebuild-checks.log`,
`recovery-checks.log`, and `reconnect.{log,json}`. Device-specific guard scripts and raw identifiers stay
out of GitHub. Build outputs and downloaded dependencies remain ignored.
