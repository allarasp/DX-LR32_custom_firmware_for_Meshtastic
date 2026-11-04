# One-shot RAM-only RX/TX experiment

This is a privileged hardware bring-up experiment, not an extension of the
production UART protocol. It was run once in RX and once in TX on 2026-11-04,
with explicit user authorization and a user-confirmed 433 MHz antenna.
The installed v0.3.1 Flash image was not changed. See
[measured results](../../docs/radio-rxtx-smoke-test.md).

Build and host-check from the repository root:

```sh
python3 firmware/tools/prepare_deps.py
sh experiments/radio-smoke/check.sh
sh experiments/radio-smoke/build.sh
```

These commands do not access hardware. They produce ignored files in
`logs/radio-smoke/`. GNU Arm GCC/binutils and host GCC use the same versions
as [the firmware build](../../docs/BUILD.md). Host checks use ASan/UBSan and the
real pinned Semtech command encoder, with a mock HAL that checks command
framing, timeout encoding, PA/power/payload, GPIO ordering prerequisites,
completion, stalled IRQ deadlines and cleanup/reset fallback. They do not
simulate electrical load or RF propagation.

## Preconditions for manual execution

The RAM test relies on the verified v0.3.1 platform clock, SPI mode 0 and
corrected RESET=PB4/BUSY=PB5 wiring. Before execution the host must configure
and verify the following through the existing UART protocol: 433500000 Hz,
SF7, BW125000 Hz, CR4/5, LDRO off, preamble 12, private sync word 0x1424,
explicit header, CRC on, normal IQ, payload length 4, requested power -9 dBm.
The helper itself does not set/check center frequency or modem settings.
It must not be run against an unknown radio configuration or another board.

SWD must pass the private known-target identity guard, direct Flash readback
must match v0.3.1, and PC3/PC4 must be inputs with push-pull type, no pulls and
GPIO mux selected. The first experiment demonstrated brief drive only; the
unknown switch identity and approximately 100-ohm path remain unresolved.
There is no general approval for sustained GPIO drive or higher-power RF.

**The helper overwrites application SRAM. Do not resume the old application
context afterward.** After collecting results, the known full-MCU reset must
restart the unchanged Flash firmware and reset/reinitialize the SX1262.
No public automatic flashing or hardware-run script is provided. Actual
invocations use private, identity-guarded OpenOCD configurations under ignored
`configs/bringup-*` paths. Do not use `verify_image` with an unbacked application
RAM work area for live preflight; use a direct Flash dump and host comparison.

## Memory and behavior

Entry 0x20000000; completion breakpoint 0x2000000A. RX code/constants occupy
1750 bytes; TX occupies 1990 bytes. Linker assertion limits code/data to
0x20000000–0x20000AFF. The 100-byte result structure is at 0x20000B00; stack
starts at 0x20001000 with at least 768 bytes reserved above 0x20000D00. No heap
or initialized/BSS globals are used. Result fields are explicitly initialized.
The largest individual linked HAL frame is 56 bytes; stack watermark was not
physically measured. Interrupts are disabled; polling SysTick supplies nominal
millisecond timing with finite iteration guards as a second limit. Watchdog
feeding is retained. SWD pin configuration is not modified.

Common setup uses image calibration 0x6B/0x6F for 430–440 MHz, standby-RC
fallback, buffer bases 0/0, IRQ mask 0x03FF with all DIO IRQ routes zero, and
clears old IRQs. DIO2 RF-switch and DIO3 TCXO control are never enabled.
Factory PC3/PC4 states are driven through GPIO: RX HIGH/LOW; TX LOW/HIGH.
The host has already applied LoRa and frequency settings before execution.

RX uses SetRx(500 ms), verifies RX chip mode, samples up to 16 instantaneous
RSSI readings and polls IRQs with a 650 ms software deadline. TX uses PA
parameters 02/02/00/01, the documented TX-clamp workaround, commanded -9 dBm,
200 us ramp, and payload `DXT1`; it calls SetTx once with a 100 ms radio timeout
and a 150 ms software deadline. No continuous carrier, infinite preamble,
continuous RX or retransmission is implemented. Cleanup requests standby RC,
returns the switch to RX then releases both GPIOs; failed SPI cleanup asserts
physical radio reset. The host-run script also has bounded wait/failure cleanup.

The last-active-pad field is overwritten by the cleanup RX state; it therefore
reports 0x08 for both tests. TX entry nevertheless requires `pads(2)` to read
0x10 or it aborts before SetTx. Do not interpret that final 0x08 as a TX-state
snapshot. RSSI uses the pinned Semtech integer-dBm decoder.
