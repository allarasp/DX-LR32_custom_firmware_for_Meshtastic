> Historical/research document: this records an earlier stage or diagnostic workflow. For the current standalone CYD + DX-LR32 installation use the [DIY guide](DIY_GUIDE.md) and [current build reference](BUILD.md). Later measurements are indexed in [the research index](README.md).

# CX32 controller readiness review

Historical v0.4.0 review; several issues below are addressed by the subsequent
[v0.5.0 work](v0.5.0-results.md).

Review date: 2026-11-04. Scope: current source, offline checks and previously
recorded hardware results. No hardware commands, flashing or RF tests were
performed for this review.

## Conclusion

The v0.4.0 `meshctrl` image is a working experimental radio controller for the
tested EU_433 LongFast profile. It is not yet a finished general-purpose radio
firmware. Its architecture is suitable: the CX32 exchanges raw radio packets,
while the host implements Meshtastic framing, encryption and routing. Other
applications should use the same binary radio protocol without a Meshtastic
dependency on the CX32.

Previously recorded physical tests demonstrate public-channel text exchange
with a Meshtastic peer and reception of another node's announcement. They do
not establish full Meshtastic interoperability, sustained traffic performance,
RF electrical qualification or CYD-to-radio operation. See
[peer test results](meshtastic-peer-test.md).

## What already works

- Versioned binary UART framing, sequence numbers, CRC, bounded parser deadlines
  and recovery after malformed input.
- Staged frequency and LoRa configuration, explicit apply, configuration status
  and selected readbacks. Readbacks do not prove every physical radio setting.
- SPI/BUSY timeouts, reset, standby RC and crystal diagnostics.
- Raw RX payloads up to 255 bytes, RSSI/SNR metadata, TX completion/error events,
  one-shot TX arming and conservative attempt spacing.
- No automatic transmission at boot; SWD remains available.

## Changes needed before a general-purpose release

### 1. Make capabilities and actual behavior agree

`firmware/src/radio_live.c:126` restricts TX to SF11, BW250000, CR4/5,
explicit header, CRC on, normal IQ and sync word 0x24b4. Its airtime calculation
uses that same fixed modem. At line 137, TX always requests -9 dBm, regardless
of the power staged in `rc_active`. The configuration API accepts a broader
range. A successful configuration is therefore not a promise that arbitrary
modem settings can transmit, or that staged power will be used.

Capabilities must expose effective TX restrictions and commanded power. A
future implementation should use validated active parameters for airtime and
TX, with board-qualified PA limits. Commanded power is not measured radiated
power.

The current 433–434 MHz policy is separate from the declared 433–532 MHz module
range. Keep that distinction and retain the 868 MHz rejection. Broadening the
policy requires appropriate hardware, calibration and regulatory decisions;
it must not follow merely from the SX1262 silicon's tuning range. Image
calibration is currently fixed to 0x6b/0x6f in `radio_live.c:26`.

### 2. Strengthen failure handling and observability

- `radio_live_io.c` checks GPIO type/pull/mux before releasing the switch pins
  to inputs. If that check fails, even `live_pads(0)` returns without releasing
  outputs. `stop()` nevertheless sets logical mode to idle. Design and test a
  board-safe fault state that reports actual GPIO state and cleanup failure.
- The live host test replaces `live_pads()` with an always-successful stub.
  Existing switch tests cover the older disabled abstraction, not this active
  GPIO backend. Add backend fault-injection tests before changing switching.
- RX buffer/status read failures can silently discard a packet when rearming
  succeeds (`radio_live.c:55–68`). Add counters and meaningful error reporting
  for SPI/read failures, CRC/header rejects, RX drops and radio timeouts.
- RX timeout IRQ stops reception without an explicit completion event. RX
  rearming restarts its timer. Specify whether timeout means an overall
  session deadline or a per-receive interval, then test that contract.
- There is no host-disconnect lease. Continuous RX continues if the host
  disappears. Document that behavior or offer a configurable session timeout.
- Sequence numbers correlate replies but do not provide duplicate-request
  suppression. Define retry/idempotency rules, particularly for TX when a
  response is lost.

### 3. Qualify traffic handling

IRQ service is polled, with SPI transactions and UART emission sharing the main
loop. RX uses one event buffer; there is no packet queue or radio drop counter.
Rearming before UART output is useful, but does not establish lossless burst
reception. Test back-to-back packets, maximum escaped UART frames, simultaneous
commands/events, UART overruns and sustained RX. Add a small bounded queue or
backpressure only where measurements justify the RAM cost.

The 30-second TX attempt spacing is volatile across reboot and is not a general
regulatory duty-cycle engine. It is also not CAD/channel-access arbitration.
The host needs an explicit responsibility contract for channel access and
airtime policy.

### 4. Separate board support from application profiles

Keep a generic radio-control core and binary protocol. Put CX32 GPIO/SPI/UART,
crystal, RF switch, PA configuration and physical band limits in a board layer.
Keep region, channel, encryption and application packet interpretation on the
host. Meshtastic LongFast should be a documented host example/profile, not the
only permitted modem hardcoded in the general controller.

Unify the disabled `rf_switch.c` abstraction and the active `radio_live_io.c`
backend so diagnostics and actuation share one state model. Consolidate command
IDs, capability bits and version constants. In particular, `radio_protocol.h` defines 0.3.1 while the live build manually
overrides BOOT and GET_VERSION to 0.4.0. Those live results agree, but duplicated
version constants are a maintenance risk. Consolidate them.

“General-purpose” means other host applications can use this supported board.
It does not mean one binary safely supports arbitrary SX1262 modules or bands.
GFSK, CAD, sleep and additional boards can be later optional extensions rather
than requirements for the first stable LoRa controller release.

## Hardware qualification still required

The switch identity and approximately 100-ohm PC3-to-VDD path remain unresolved.
Factory-style switching and successful messages are evidence of functionality,
not proof of safe current, temperature or long-term electrical operation.
Measure these before broader power testing or a general release. Do not enable
DIO3 TCXO control or assume that DIO2 controls this switch. Preserve the proven
image and recovery procedure before future hardware changes.

## Offline verification and budget

Passed during this review:

```sh
make -C firmware -j2 check radio-check uart-check protocol-check mesh-check
arm-none-eabi-size firmware/build/meshctrl/controller.elf
```

Checks include source hashes, ELF/vector validation, HAL/framing/configuration
tests, diagnostic tests, disabled-switch tests and live-radio host tests with
AddressSanitizer/UndefinedBehaviorSanitizer. They do not replace hardware or
active-switch electrical tests.

The live image occupies 11,784 bytes Flash. Static RAM is 1,304 bytes; the
linker also reserves 1,024 bytes for stack, leaving 1,768 bytes outside those
allocations. `size` reports 2,324 BSS bytes because that total includes the
reserved stack. This is a static budget, not a measured worst-case stack
high-water mark. Flash has substantial headroom; RAM remains the main limit
for queues and added features.

Recommended order: truthful capabilities/versioning and fault handling first;
generic validated PHY/TX configuration second; burst/recovery hardware tests
and electrical qualification before declaring a stable general-purpose release.
No implementation changes were made as part of this review.
