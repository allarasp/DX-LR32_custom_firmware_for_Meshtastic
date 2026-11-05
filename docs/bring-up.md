> Historical/research document: this records an earlier stage or diagnostic workflow. For the current standalone CYD + DX-LR32 installation use the [DIY guide](DIY_GUIDE.md) and [current build reference](BUILD.md). Later measurements are indexed in [the research index](README.md).

# Hardware bring-up status

This is an experimental UART-to-SX1262 controller, not a Meshtastic firmware
release. The ESP32 CYD network/UI side and radio integration remain future work.
The measured package pin mapping is in the repository README.

## Physically tested

The previous hardware stage tested a user-identified ZB003F8/CX32L003F8 QFN20
on a DX-LR32-A with ST-Link V2 at 100 kHz SWD. The UID was checked locally to
bind operations to that device; unique identifiers and raw logs are not public.
CPUID/DPIDR alone are not proof of the exact F8/package variant.

- Generic Cortex-M0+ examination and memory access succeeded.
- Read protection was unset on that board; application reads succeeded.
- UART diagnostic v0.1.1 was written and independently read back byte-for-byte.
- Only the four application sectors covering the image were erased. No mass
  erase, read-protection removal, option-byte change or factory backup was used.
- Reset_Handler was observed at `0xC0`, with initial MSP `0x20001000`.
- Actual `.data`/`.bss` checks passed; ticks advanced and the main loop ran.
- Full MCU reset booted the application; a physical power-cycle is untested.
- A 3.3 V CH340 link at 115200 8N1 captured the startup banner, VERSION,
  STATUS and six PING/PONG exchanges. Final counters were 53 RX bytes,
  135 TX bytes, eight commands and zero firmware errors.
- SWD remained available. AUX/M0/M1 stayed inputs; radio code was excluded.

These are results from that stage, not hardware tests performed on every build
or every module. Local raw logs and build products are intentionally excluded
from the repository.

## UART diagnostic behavior

The tested image is `firmware/build/uartdiag/controller.elf`, built with
`make -C firmware uart-check`. It sends `DXLR32 CUSTOM FW BOOT` plus CRLF
at startup. Commands are uppercase, terminated by CR or LF:

| Command | Response |
| --- | --- |
| `PING` | `PONG` |
| `VERSION` | `DXLR32 UARTDIAG 0.1.1` |
| `STATUS` | `STATUS CLK=22120000 BAUD=115200 RADIO=UNINITIALIZED` |

Replies include CRLF. Empty lines are ignored; unknown and overlong commands
return errors. There is no binary packet protocol, bootloader command or RF
operation in this parser. The STATUS clock is the configured nominal frequency,
not a frequency-counter measurement.

The image explicitly selects HIRC and uses the packaged-part factory 22.12 MHz
trim. With DBAUD=1 and BRG=11, nominal UART baud is 115208.3 (+0.0072% divider
error before oscillator tolerance). It feeds an already-running IWDG and leaves
a stopped one stopped. Factory firmware had enabled IWDG, while the diagnostic
application after a full MCU reset observed it stopped. No watchdog option
word was changed. Flash read timing was checked against the vendor manual;
no STM32 register layout or Flash latency register was assumed.

The SRAM `diagnostic` record tracks startup, nominal clock, reset flags, ticks,
UART progress and errors. Its boot magic is a software marker, not a hardware
unique identifier. The linker only emits the main application memory regions,
with no NVR/ISP/option/calibration output sections.

## Issues resolved during that stage

1. The first image programmed and booted but timed out while polling UART.
   Vendor HAL source explains that RIEN/TIEN must unmask status flags even
   when polling. The diagnostic image and experimental board code include
   this correction, with the UART0 NVIC interrupt disabled.
2. The adapter leads were reversed in practice. With both MCU pads as inputs,
   host transmissions initially drove PD5 instead of PD6. Swapping the data
   leads put host TX on module RXD/PD6. The same input scan verified the fix.
3. An initial WSL USB-forwarded capture duplicated bytes despite MCU counters
   showing one transmission. Reattaching only the CH340 cleared it; the final
   exact-response test passed. The deeper transport cause is unknown. A native
   Windows-port fallback did not pass and is not claimed as tested support.
4. A hardware breakpoint did not survive full MCU reset. Cortex `reset halt`
   caught the reset entry; a separate vendor MCU reset confirmed normal boot.

There were two deliberate firmware writes: the initial image and the diagnosed
polling-flag correction. Both writes and readbacks passed; there was no blind
retry of a failed Flash operation. Neither ST-Link nor ESP32 firmware was changed.

## Development code that remains unverified on hardware

`firmware/src/main.c`, `board.c` and `sx126x_hal.c` form a separate experimental
image. It compiles the pinned Semtech baseline driver and provides four HAL
hooks with BUSY timeouts, command/data framing and NSS cleanup. Its GPIO map
matches the measured board; it currently uses HIRC 4 MHz, UART 9600 8N1 and
SPI mode 0 at 500 kHz. Single-byte `V`/`S` diagnostics differ from the tested
line-command UART image. No actual SX1262 status/RX/TX exchange has been tested.

Mock tests validate software sequencing, not radio wiring or RF behavior.
TCXO/crystal details, DIO2 RF switching, PA configuration, DIO1 handling,
packet queues and the custom binary UART protocol still need investigation.
No LoRa transmit command was issued during bring-up.

## Optional manual test helpers

`bin/uart-bringup-test.py --port <identified-serial-port>` captures a fresh boot
and checks exact UART responses. It actively sends diagnostic commands to the
chosen port and saves output only under ignored `logs/`. It is not run by Make.
The Windows PowerShell helper likewise requires an explicit identified port;
it remains an experimental fallback. `examine.sh` is a separate manual SWD
helper with a power/wiring confirmation gate; it is not part of build/tests.

The public generic configs contain no device UID and no automatic flashing
operation. Original board-specific guards and session scripts stay private.

## References

- [Vendor reference manual](https://www.zbitsemi.com/en/upload/file/20200903/20200903105731_41280.pdf): clocks, GPIO, UART, Flash and option bytes.
- [Vendor product/download listing](https://www.czlongyang.com/display/id/139.html).
- [Pinned device-pack mirror](https://github.com/Z1R343L-D77/CX32L003-HAL-SPL): headers, vendor HAL and readable manuals.
- [Semtech SX126x driver](https://github.com/Lora-net/sx126x_driver).
- [CX32L003 OpenOCD fork](https://source.haeckserei.de/nica/openocd-code).

See [BUILD.md](BUILD.md) for exact dependency revisions, source integrity checks
and the distinction between the two images.
