# RF-switch investigation

## v0.3.1 follow-up — 2026-11-04

The following measured connections supersede the unresolved-net statements in the
archived investigation below. The switch identity and its electrical load remain
unresolved. No factory firmware was executed during this analysis.

| Switch pin | User-measured connection |
| --- | --- |
| 1 | SX1262 RFO, pin 23 |
| 2 | GND |
| 3 | SX1262 RFI_N, pin 22 |
| 4 | CX32 PC4, package pin 11 |
| 5 | Antenna common path |
| 6 | CX32 PC3, package pin 10 |

The marking is approximately `S40C CFCI`; no reliable manufacturer marking match
has established its identity. DIO2 continuity to this component is **not** known.
An approximately 100-ohm reading between PC3/pin 6 and VDD is not proof of a
discrete pull-up. Do not drive PC3 LOW until the load is understood.

### Factory GPIO argument reconstruction

The archived bytes and decoded call arguments were compared with the vendor-authored
HAL preserved in the [pinned mirror header](https://github.com/Z1R343L-D77/CX32L003-HAL-SPL/blob/f8dabc3b529094127ab76cfd43589d8361b70cc5/CX32L003_HAL_Driver/Driver/Inc/cx32l003_hal_gpio.h)
and [implementation](https://github.com/Z1R343L-D77/CX32L003-HAL-SPL/blob/f8dabc3b529094127ab76cfd43589d8361b70cc5/CX32L003_HAL_Driver/Driver/Src/cx32l003_hal_gpio.c).
This is a reference HAL layout, not proof that the erased factory helper body
was identical. GPIO_InitTypeDef has 52 bytes: Pin +0, Mode +4, Pull +8,
OpenDrain +12, Debounce fields +16/+20/+24, SlewRate +28, DrvStrength +32,
Exti fields +36/+40/+44, Alternate +48.

At `0x39BC` the stack structure starts at SP+8; `0x39C0..0x39C4` calls the
52-byte zero-initializer (its helper at `0x0276` is erased). The surviving
GPIO setup establishes the following arguments:

| Address | Decoded argument/effect |
| --- | --- |
| `0x3A38` | Store mask `0x18` at SP+8: PC3 and PC4 |
| `0x3A3C` | Store Mode = 1 at SP+12: output |
| `0x3A40..0x3A44` | Zero Pull and OpenDrain at SP+16 and SP+20 |
| `0x3A48` | Load GPIOC base `0x40021800` from literal `0x3AEC` |
| `0x3A4A` | Call GPIO initialization helper `0x0E1A` (erased) |
| `0x3A54` | GPIO write: PC3 mask `0x08`, value 1 |
| `0x3A5E` | GPIO write: PC4 mask `0x10`, value 0 |

The surviving argument construction strongly supports **push-pull outputs, no
internal pulls**, with initial explicit PC3 HIGH / PC4 LOW. Under this HAL
layout, zero-initialized drive strength means HIGH, not LOW. Actual register
writes inside the erased initialization helper cannot be inspected. The
[CX32L003 reference material](https://www.zbitsemi.com/upload/file/20200903/20200903105452_99571.pdf)
and local reference manual GPIO section agree that OTYPER=0 is push-pull and
PUPDR=0 disables pulls. GPIO output configuration precedes the explicit initial
levels; the earlier output latch is unknown, so this is not a proven glitch-free
startup sequence.

The reference HAL temporarily clears DIRCR before configuring outputs. Because
the factory helper is erased, a matching temporary input phase cannot be proven
for the actual factory binary. No intentional input/dead-time phase is present
in the surviving pair-switch helpers.

### Exact transition order and radio context

| Helper | First operation | Second operation | Final PC3/PC4 | Intermediate state from opposite mode |
| --- | --- | --- | --- | --- |
| `0x3B18` | PC3 HIGH | PC4 LOW | 1/0, receive/non-TX | 1/1 |
| `0x3BA0` | PC3 LOW | PC4 HIGH | 0/1, TX | 0/0 |

No explicit delay or GPIO input reconfiguration appears between those writes.
The intermediate states are observations, **not validated safe states**. The
GPIO-write helper at `0x10B4` is erased; the reference HAL uses ODSET/ODCLR.

The send path `0x5750` calls the TX helper at `0x5796`. Its interface-table call
through entry `0x7844` (Thumb pointer `0x5D45`) reaches `0x5D44`, then `0x6E58`
and `0x6ECC`, constructing SetTx `0x83` (literal `0x6EFC`). A receive path calls
`0x3B18` at `0x4E92`, then uses table entry `0x7878` (pointer `0x5CFD`) at
`0x4EAC..0x4EB0`, reaching `0x5CFC`, `0x6CF0`, and SetRx construction at
`0x6DB4` (opcode `0x82`, literal `0x6DE4`). Other receive/non-TX helper references
include `0x4ACE`, `0x4F3E`, `0x511E`, `0x538E`, `0x594A`, and `0x7204`.

The initialization at `0x5AB4` executes, in order: wake, reset, retention setup,
`96 01` (DCDC regulator), **`9D 01`**, `80 00` (standby RC), **`80 01` (standby
XOSC)**, writes `05 05` at registers `0x0911/0x0912`, `93 30` (XOSC fallback),
then a read/modify/write OR `0x1E` at `0x08D8`. Relevant call sites are
`0x5AD2`, `0x5ADA`, `0x5AE2`, `0x5AEA`, `0x5AF4`, `0x5AFC`, `0x5B02`.
A separate helper at `0x602A` later writes `12 12` to `0x0911/0x0912` near
`0x6050`; `05 05` is not a universal factory trim setting. These values are
historical observations, not copied into the new diagnostic.

Thus factory DIO2 enable coexists with MCU-controlled PC3/PC4 switching; it
does not prove a shared physical net. No separate external PA/LNA enable pin
was established. The `0x95` constructor at `0x6C0C` (literal `0x6C40`) configures
the SX1262 internal PA and is not evidence of an external GPIO enable. Other
fan-out of PC3/PC4 remains possible. The bounded negative finding for `0x97`
below is unchanged; erased paths preclude an absolute claim.

### Electrical interpretation and remaining measurements

The [pSemi PE4259 datasheet](https://psemi.com/pdf/datasheets/pe4259ds.pdf),
DOC-03694-5.01, has matching RF/control pin functions. Pin 6 is VDD in
single-control operation, or the complementary control in dual-control
operation. With pin 6 held at supply, pin 4 selects RF1 HIGH / RF2 LOW;
complementary control selects RF1 with pin 6 LOW / pin 4 HIGH, and RF2 with
pin 6 HIGH / pin 4 LOW. This agrees with the factory logic **if** the actual
part supports that mode. It does not identify the marked component.

For the PE4259, recommended supply is 1.8–3.3 V; logic thresholds are
0.7 x VDD HIGH and 0.3 x VDD LOW. Absolute supply is -0.3–4 V and input
limits -0.3 to VDD+0.3 V. Supply current at 3 V is typically 9 microamps,
maximum 20 microamps. These limits apply to the documented part, not an
unidentified alternative. Complementary-mode supply behavior must be
interpreted using the manufacturer circuit, not by treating instantaneous
pin 6 LOW as an ordinary zero-volt supply.

[pSemi AN37](https://psemi.com/pdf/app_notes/an37.pdf), DOC-57537-1.01, explains
charge-pump supply switching and control-current transients. Its bias-resistor
example is not a recommended modification for this unknown board. If the
100-ohm reading were a linear connection to a stiff 3.3 V rail, a LOW output
could sink about 33 mA. That is only a conditional calculation; a semiconductor
or parallel path can produce a misleading resistance reading. Factory
push-pull intent does not validate this load.

Still needed before activation: establish the exact component, inspect/trace
the pin-6-to-VDD path with all power removed, compare resistance in both probe
polarities and diode mode, determine whether a discrete resistor actually
exists, and establish control voltage/current limits and any other connected
loads. Powered high-impedance voltage observations can help after confirming
a safe measurement setup. DIO2 continuity remains a separate question.

### v0.3.1 software preparation

`rf_switch.c` defines UNDRIVEN/RX/TX/UNKNOWN and validates GPIO direction, pulls
and alternate functions without modifying them. Every RX/TX request is denied
with UNVERIFIED_LOAD; there is no override or actuator backend. A pure host-tested
factory-plan function records the two-write ordering and intermediate states
for future review. Link garbage collection removes both that planner and the
denied-request routine from the physical image. The binary GET_RF_SWITCH
command reports masked input/configuration/latch registers, never writes them.
Incorrect configuration is reported, not repaired by driving a pin.

### Physical v0.3.1 follow-up

After SWD was restored, v0.3.1 was programmed and readback-verified. The physical
UART regression and one RC/XOSC/RC diagnostic passed. GET_RF_SWITCH before/after
returned `0000000000000000000000000800`: both pads remain inputs with no pulls
or alternate functions, latch zero, sampled PC3 HIGH / PC4 LOW. Input voltages
are external bias observations, not proof of a safe output load or RF path.
No PC3/PC4 drive, DIO2/DIO3 control, or RF operation was performed. See
[oscillator test results](oscillator-test.md) for commands and evidence.

## User-authorized GPIO pulse — 2026-11-04

After the v0.3.1 oscillator stage, the user explicitly authorized testing the
factory PC3/PC4 states. This superseded the earlier prohibition for this one
controlled experiment. The installed v0.3.1 firmware remains unchanged and
still has no active RF-switch command or actuator backend.

A 116-byte Thumb routine was temporarily placed in unused SRAM at 0x20000600,
with samples at 0x20000700. It disabled interrupts, preloaded the RX latch
before enabling push-pull outputs, then performed the factory order:

1. RX: PC3 HIGH, PC4 LOW.
2. TX logic: clear PC3, then set PC4.
3. RX logic: set PC3, then clear PC4.
4. Release both pins to inputs and restore the original latch.

Each stable state used a finite 255-iteration CPU settle loop. TX exposure
was limited by that loop and a short sample routine, not a host/USB delay.
Exact pulse width was not measured; nominal timing is of order tens of
microseconds at the configured 22.12 MHz clock. The helper reached its expected
breakpoint after releasing the pads. Saved helper SRAM and CPU registers were
restored. No Flash erase/programming, radio TX/RX/FS, PA, DIO2 or DIO3 control
was performed by the pulse test. GPIO pull/type/mux configuration was validated
before execution and not changed by the routine.

Actual masked samples (0x18 selects PC3/PC4):

| Phase | DIRCR | ODR | IDR | Interpretation |
| --- | --- | --- | --- | --- |
| Before | 0x00 | 0x00 | 0x08 | Inputs; PC3 reads HIGH |
| RX | 0x18 | 0x08 | 0x08 | PC3 HIGH / PC4 LOW |
| TX logic | 0x18 | 0x10 | 0x10 | PC3 LOW / PC4 HIGH |
| RX restored | 0x18 | 0x08 | 0x08 | PC3 HIGH / PC4 LOW |
| Inputs restored | 0x00 | 0x00 | 0x08 | Both released |

This establishes that the GPIO pad input readbacks followed the brief requested
states. It does **not** identify the RF-switch part, measure control current or
rail transients, establish safe continuous drive, or verify RF routing. In
particular, PC3 reaching logic LOW does not exclude an excessive sink current.
The approximately 100-ohm path therefore remains unresolved. No sustained
TX-state or RF test was attempted.

### Debugger side effect and recovery

The preflight used OpenOCD `verify_image` with work-area-backup disabled in the
existing programming configuration. Source inspection of
`src/target/armv7m.c:armv7m_checksum_memory` shows that it loads and executes a CRC
helper in the target work area, which begins at application SRAM 0x20000000.
Post-pulse UART exhibited stale responses and a configuration-generation jump
(from 6 before the pulse stage to 90 afterward), so preservation of application
runtime state cannot be claimed. This is a debugger-induced RAM disturbance;
the pin pulse itself is evidenced by the independent recorded samples.

The documented full-MCU reset was then performed to reinitialize the unchanged
v0.3.1 image. PING returned `00 50 4F 4E 47`; GET_STATUS returned standby RC
0x22, errors 0, active_valid=false, dirty=true, generation 1, as expected after
reset. GET_RF_SWITCH returned `0000000000000000000000000800` (inputs, no pulls
or alternate functions, activation disabled). Persistent firmware was not
modified. Future live identity/image checks must use a direct Flash dump and
host comparison, or a separately validated backed-up work area, rather than
this unbacked target checksum operation.

Local evidence is retained under `logs/rf-switch-test/`: `pulse.S`, `pulse.elf`,
`pulse.bin`, `pulse.log`, `snapshots.bin`, `preflight.log`, `before-radio.*`,
`after-{ping,radio,gpio}.*`, `post-test-reset.log`, and
`restarted-{ping,status,gpio}.*`. The private OpenOCD invocation was
`tools/openocd-cx32/bin/openocd -f configs/bringup-rf-switch-pulse.cfg`.
The pulse has not been repeated.

## Archived offline investigation — 2026-11-04

The remainder records the earlier read-only analysis and its then-current
uncertainties. Its statements about no serial/SWD use apply to that earlier
investigation only; the v0.3.1 stage attempted connection preflight separately.

## Findings and limits

**The recovered factory radio initialization issues `9D 01`
(SetDIO2AsRfSwitchCtrl enabled). This is executable command construction and a
call into the SPI transport, not an incidental byte match.** However, the same
firmware also drives **PC3 and PC4 as complementary direction outputs** around
TX and receive/non-TX paths. Therefore **DIO2 configuration does not establish
which net actually controls the PCB's RF switch**. PC3/PC4 must also be traced.

No executable `97 ...` TCXO-control transaction was found in the recovered
initialization or inspected surviving command routines. This is a bounded
negative finding, not proof that no erased or unrecognized path ever issued it.
The newly confirmed passive crystal is independent evidence to leave DIO3 TCXO
control disabled.

This investigation used saved files only. **No OpenOCD, serial-port access,
flashing, erasing, reset, TX/RX, DIO2 or DIO3 control operation was performed.**
No firmware source or working v0.3.0 image was changed. No archived factory code
was executed. Numeric command sequences below are decoded historical intent,
not commands run on the physical radio during this investigation.

## Evidence source and coverage

Input: `logs/factory-analysis/current-flash.bin`, 65,536 bytes, SHA-256:
`e18c9ee18865006d62cc8e19991a273c9a59410e874e71951e9d5563ccb20a9b`.
Its companion `current-flash.dis` and `after-flash.bin` were also retained from
the earlier bring-up. Addresses below are original zero-based Flash addresses,
identical to offsets in that binary; Thumb function pointers have bit 0 set.

This is the **archived mixed image captured before the v0.2.1 correction**:
`0x0000..0x13FF` already contained custom firmware/erased application sectors;
factory code/data survived from `0x1400` onward. It is not a complete factory
backup or the currently running v0.3.0 image. Some of this archived low-address
factory code was overwritten by the larger v0.3.0 image later. This analysis
uses the saved snapshot, not another target dump. Original reset/startup and
some GPIO/library helper bodies are unavailable.

Disassembly was checked against the bytes, literal pools, direct call targets,
function-pointer tables and argument flow. Blind `objdump -D` also disassembles
data as instructions; its apparent instructions at table/literal addresses
are not treated as executable evidence.

## 1. Proof of `9D 01` on the factory initialization path

### Reachability beyond an unused library function

1. The surviving main-like entry at `0x3518` calls board/application setup
   `0x34CC`. Setup calls `0x542C` at `0x34F8`, which configures board GPIOs and
   SPI, then calls radio/application initialization `0x4E78` at `0x350C`.
2. At `0x4E7A..0x4E80`, code loads the interface table at `0x7820`, reads its
   first function pointer and calls it with a callback pointer. The first word
   at `0x7820` is `0x00005AB5`, i.e. Thumb function **`0x5AB4`**.
3. Another configuration/reinitialization path calls the same table entry at
   `0x4A9A..0x4AA0`; `0x72E0` calls that path's function at `0x4A0C`.
4. Function `0x5AB4` contains the following uninterrupted argument setup/call:

```text
0x5AD6: movs r1, #1           ; enable = true
0x5AD8: movs r0, #0           ; context argument
0x5ADA: bl   0x6A90           ; RF-switch command constructor
```

The original reset entry was overwritten, so a complete reset-to-main proof
cannot be reconstructed. These surviving setup, interface and reinitialization
references nevertheless establish a normal radio-initialization call path;
`0x6A90` is not merely an unreferenced command stub. Its direct call at `0x5ADA`
is the only direct call to it identified in the surviving disassembly.

### Command construction and wire length

| Address | Evidence |
| --- | --- |
| `0x6A94` | Copies incoming `r1` (enable) to `r4` |
| `0x6A96..0x6A9A` | ADR/LDR loads the word at `0x6ABC`, then stores it at `SP+4` |
| `0x6ABC` | Literal word `0x0000009D`, bytes `9D 00 00 00` |
| `0x6A9C..0x6AA6` | Copies enable into the byte at `SP+5`; two XOR-by-1 instructions cancel, so 1 remains 1 |
| `0x6AAC` | Sets command length `r2 = 2` |
| `0x6AAE` | Sets command pointer `r1 = SP+4` |
| `0x6AA8..0x6AB2` | Sets additional data pointer/length to zero and forwards context |
| `0x6AB4` | Calls SPI write transport `0x6944` |

Thus the command buffer is exactly **`9D 01`**, length two, without an appended
payload. The odd-looking XOR sequence does **not** turn it into `9D 00`.

The transport is also identifiable as real SPI:

- `0x694E`: waits through `0x6264`, the previously identified PB5/BUSY reader.
- `0x6954`: selects NSS through `0x6218` with argument 0.
- `0x695C`: sends the command buffer through byte loop `0x627C`.
- `0x6964`: sends the separate data buffer; its length is zero for this command.
- `0x696A`: releases NSS through `0x6218` with argument 1.
- `0x6290`: the byte loop calls transmitter `0x1A70`; its `0x1AA8` store writes
  to peripheral offset `+0x0C`, SPI DATA. The handle's SPI base is initialized
  to `0x40000800` at `0x61EE..0x61F2`, from literal `0x6210`.
- `0x6218` calls `0x1C10`; `0x1C14` writes peripheral offset `+0x04`, SPI SSN.
  Factory SPI pin setup uses PA2 AF3 for NSS. NSS remains asserted across both
  transmitted bytes.

This proves the recovered code **requests a two-byte SPI transaction during
initialization when that path runs and hardware permits it**. It is static
code evidence, not a logic-analyzer capture of the original factory firmware.

### Surrounding initialization sequence

| Call site | Destination | Decoded action |
| --- | --- | --- |
| `0x5ABE` | `0x6F60 -> 0x692C` | NSS-based wake/BUSY synchronization; no opcode bytes in this helper |
| `0x5AC4` | `0x69E4 -> 0x68F4` | Hardware reset via table entry 0 = PB4 |
| `0x5ACA` | `0x6974` | Retention-list setup using the three register addresses at `0x7B88`: 08AC, 0889, 0736 |
| `0x5AD2` | `0x6C70` | `96 01`: regulator mode DC/DC |
| **`0x5ADA`** | **`0x6A90`** | **`9D 01`: enable automatic DIO2 RF-switch control** |
| `0x5AE2` | `0x6E10` | `80 00`: standby RC |
| `0x5AEA` | `0x6E10` | `80 01`: standby XOSC |
| `0x5AF4` | `0x6E38` | Write register 0911/0912 values 05/05 (XTA/XTB trim) |
| `0x5AFC` | `0x6D8C` | `93 30`: RX/TX fallback to standby XOSC |
| `0x5B02` | `0x6460` | Read/modify/write register 08D8, OR mask 1E |
| `0x5B08` | `0x5DC0` | Update software state only |

The separate standby helper at `0x602A` later writes XTA/XTB trim 12/12 at
`0x6050`; 05/05 is therefore not a universal, permanent factory setting. Reset
uses delay arguments 10/50/20; units are unresolved because its original delay
helper was overwritten. None of these settings is recommended for copying into
v0.3.0 as part of this analysis.

## 2. `0x97` search and crystal findings

Scanning `0x1400..0xFFFF` finds twelve bytes equal to `97`, at:
`1698, 2163, 218F, 377D, 3984, 5236, 555B, 57C8, 67FA, 7828, 7A01, 7B04`.
Their contexts do not establish a TCXO command:

- `1698` and `57C8`: immediate/offset bytes within Thumb literal-load instructions.
- `2163`, `218F`, `377D`, `555B`: parts of stack-store instructions.
- `3984`: part of a BL instruction targeting the GPIO write helper `0x10B4`.
- `5236`, `67FA`: parts of branch encodings.
- `7828`: low byte of function pointer `0x00005D97`; function `0x5D96` selects
  packet type through the `8A` wrapper at `0x6C48`.
- `7A01`, `7B04`: constant/data-region matches; no traced construction of an
  opcode/voltage/24-bit-delay SPI buffer from them.

There is no aligned literal word `0x00000097` in the surviving range, no
`MOVS Rn,#0x97` match there, and no identified five-byte TCXO-control constructor
or call in the inspected radio initialization. Absence of those patterns alone
would be insufficient; the complete recovered initialization and neighboring
SPI-command construction were inspected as well. **No execution of 0x97 is
established; a global historical “never executed” assertion is not possible
with a partial image and unresolved indirect/erased paths.**

The owner's newly confirmed wiring is a 32 MHz **passive 3225 crystal**:
crystal pad 1 -> SX XTB/pin 4; pad 3 -> XTA/pin 3; pads 2/4 -> GND. SX DIO3/pin 6
has a capacitor to ground. These findings supersede earlier oscillator
uncertainty. The XOSC/trim code is consistent with that crystal. DIO3 capacitor
presence does not justify enabling a TCXO regulator. **Do not enable 0x97.**

## 3. Dedicated MCU GPIO direction-control evidence

Factory GPIO setup `0x39BC` includes mask `0x18` on GPIOC (`0x40021800`) at
`0x3A38..0x3A4A`: PC3/PC4 output configuration. It requests PC3=1 at `0x3A54`
and PC4=0 at `0x3A5E`. Original GPIO helper bodies at `0x0E1A`/`0x10B4` were
lost; interpretation uses their consistent argument conventions, the surviving
call sites/literals, device documentation and the previously successful
PB4/PB5 reconstruction. Requested levels below are not fresh voltage measurements.

| Helper | Decoded GPIO write arguments | Requested PC3 | Requested PC4 |
| --- | --- | ---: | ---: |
| `0x3B18` | GPIOC/mask 08/value 1; GPIOC/mask 10/value 0 | 1 | 0 |
| `0x3BA0` | GPIOC/mask 08/value 0; GPIOC/mask 10/value 1 | 0 | 1 |

The writes are sequential. A receive-to-TX change requests an intermediate
PC3/PC4 state of 0/0; TX-to-receive requests 1/1 before reaching its final state.
The interval is not established, and neither break-before-make behavior nor
validity of these transitions for the unidentified switch is proven.

Direction correlation goes beyond coincidental GPIO references:

- High-level send routine `0x5750` calls `0x3BA0` at **`0x5796`**, before its
  packet-transmit operation through interface slot `+0x24`.
- Table slot `0x7844` contains `0x5D45`; this transmit routine at `0x5D44`
  writes the packet buffer, then calls `0x6E58` at `0x5D7A`.
  That reaches `0x6ECC`, whose command literal at `0x6EFC` is **83 (SetTx)**.
- Receive/rearm and non-TX return paths call `0x3B18`, including **`0x4E92`**,
  **`0x4ACE`**, **`0x4F3E`**, **`0x511E`**, **`0x538E`**, and **`0x594A`** in
  send recovery. For a concrete RX connection, `0x4E92` is followed by the
  receive interface call at `0x4EAC..0x4EB0`, slot `+0x58` (table `0x7878`,
  pointer `0x5CFD`). Function `0x5CFC` reaches `0x6DB4`, whose command literal
  `0x6DE4` is **82 (SetRx)**. Other helper calls occur before or after RX, or
  on branches selecting another non-TX state; not every call proves RX is
  already active. `0x7204` is another direct call to `0x3B18`.

These code paths were inspected only; **no TX/RX command was sent**. The pair
is strong evidence of deliberate external direction control. It could select
the switch, enable another front-end stage, or serve another direction-related
function. It is not yet a verified physical RF-switch net.

### Correlation with board pin mapping

| MCU port / QFN20 pin | Established role or candidate |
| --- | --- |
| **PC3 / pin 10** | Factory high in receive/non-TX paths, low for send; candidate complementary control |
| **PC4 / pin 11** | Factory low in receive/non-TX paths, high for send; candidate main control |
| PA3 / pin 7 | Measured external AUX; its high/low helpers are 39A8/3994, distinct from the PC3/PC4 pair |
| PC5 / pin 12, PC6 / pin 13 | Measured external M0/M1; input setup uses mask 60 at 3AC8..3AD8, not mask 18 |
| PB4 / pin 9 | Verified radio NRESET, factory table 7E6C entry 0 |
| PB5 / pin 8 | Verified radio BUSY, factory table 7E7C entry 2 |
| PD4 / pin 18 | Verified DIO1 input/IRQ, table entry 1 |
| PA2 / pin 3 | Verified NSS; SPI pin selection is separate from antenna switching |
| PD5/PD6 / pins 19/20 | Verified external UART TX/RX |
| PC7/PD1 / pins 14/15 | SWD, unrelated to the identified direction-control pair |

PC3/PC4 package positions follow the CX32L003 QFN20 pin table, not TSSOP20
numbering. Existing v0.3.0 source does not set up or drive this pair for RF
control; this investigation makes no such change.

## 4. PE4259 review and physical hypotheses

The marking approximately “S40C CFCI” does **not** positively identify a PE4259.
The reported six-pin layout is compatible with the PE4259's RF1/GND/RF2/CTRL/
RFC/pin-6 arrangement, but that does not identify the actual part.

For a **confirmed PE4259**, pSemi's [datasheet](https://psemi.com/pdf/datasheets/pe4259ds.pdf)
(DOC-03694-5.01, July 2026, tables 5–7) specifies:

| Mode | Pin 6 | Pin 4 | Selected path |
| --- | --- | --- | --- |
| Single-control | fixed VDD | high | RFC–RF1 |
| Single-control | fixed VDD | low | RFC–RF2 |
| Complementary | low | high | RFC–RF1 |
| Complementary | high | low | RFC–RF2 |

Single-control uses a separate supply on pin 6; complementary control uses
opposing logic levels on pins 4/6. Recommended VDD is 1.8–3.3 V, with high/low
thresholds 0.7/0.3 times VDD. These specifications are conditional on part identity.
[pSemi AN37](https://psemi.com/pdf/app_notes/an37.pdf) (DOC-57537-1.01, November
2025) also discusses transient current and source/sink requirements for the
complementary supply/control drive; complementary-looking firmware alone does
not establish an electrically valid implementation. No resistor or drive change
is proposed here.

Semtech's DIO2 automatic mode is high during TX and low otherwise, including RX;
it asserts before PA ramp-up and drops after ramp-down. This follows
[Semtech's command encoder](https://github.com/Lora-net/sx126x_driver/blob/a10c5dfdf89788c6ac805e9fe98889de44175aa2/src/sx126x.c)
and SX1261/2 Rev 2.2 sections 8.3.2/13.3.5, tables 13-32/33
([Semtech-authored PDF mirror](https://resource.heltec.cn/download/WiFi_LoRa_32_V4/datasheet/SX1261_2%20V2-2.pdf)).
The crystal/TCXO command distinction is in section 13.3.6, table 13-34.

With the owner's tentative RF1->RFO and RF2->receive-path assignment:

| Hypothesis | Proposed net mapping, **not established** | Why plausible |
| --- | --- | --- |
| A: automatic, single-control | switch pin 4 <- SX DIO2/pin 12; switch pin 6 <- fixed VDD | DIO2 polarity fits TX->RF1 / RX->RF2 |
| B: MCU complementary | switch pin 4 <- PC4/pin 11; switch pin 6 <- PC3/pin 10 | Factory requested TX=PC4 high/PC3 low; RX reverses it |
| C: split front-end controls | DIO2 and PC3/PC4 serve different devices/stages, or one output is unused | Both mechanisms can be configured even if only one reaches this switch |

**Factory code cannot choose between these hypotheses or establish the wiring.**
In particular, `9D 01` might enable an unconnected DIO2 while the MCU pair drives
the switch. Conversely the pair could drive something else. Do not tie control
sources together or enable both based solely on this analysis.

## 5. Measurements still needed

These are proposed future checks; none was performed in this task:

1. Confirm switch package orientation/pin 1 and obtain a reliable part marking
   or BOM identification. The PE4259 truth table must not be assumed for an
   unidentified pin-compatible device.
2. With power removed, trace **switch pin 4** to **SX DIO2/pin 12**,
   **MCU PC4/pin 11**, and **MCU PC3/pin 10**. Record any series resistor,
   inverter or transistor rather than treating no direct continuity as no net.
3. Trace **switch pin 6** to known VDD points and PC3/PC4/DIO2. Distinguish a
   hard supply connection from semiconductor/ESD conduction; check resistance
   in both meter polarities. “Possibly VDD” is not enough to select a mode.
4. Confirm pin 2 ground, pin 5 antenna common, and both RF branches through their
   matching/DC-block components. A receive branch touching RFI_N does not by
   itself describe the complete differential receive/matching network.
5. After the control nets/part are identified, measure idle supply/control
   voltages. Current firmware leaves RF switching disabled; an idle voltage
   alone cannot prove TX/RX polarity or RF-path selection. Dynamic control and
   RF-path tests require a later, explicitly scoped stage; none is authorized
   or attempted by this read-only investigation.

The most useful next check is **PC4 -> switch pin 4 and PC3 -> switch pin 6**, in
addition to the original DIO2 hypothesis. DIO3 TCXO control remains excluded by
the confirmed passive crystal. RF-switch activation remains blocked in v0.3.0.

## Reproduction and validation

All analysis commands operate on saved files, for example:

```sh
arm-none-eabi-objdump -D -b binary -m arm -M force-thumb \
  --start-address=0x5ab4 --stop-address=0x5b14 logs/factory-analysis/current-flash.bin
arm-none-eabi-objdump -D -b binary -m arm -M force-thumb \
  --start-address=0x6944 --stop-address=0x6ac0 logs/factory-analysis/current-flash.bin
arm-none-eabi-objdump -D -b binary -m arm -M force-thumb \
  --start-address=0x3a38 --stop-address=0x3bbc logs/factory-analysis/current-flash.bin
python3 logs/rf-switch-analysis/check_evidence.py
sha256sum firmware/build/uartctrl/controller.bin
```

The ignored evidence checker validates the archived image hash, Thumb BL targets,
init arguments, opcode literal/buffer stores, command length, SPI SSN/DATA stores
and complementary GPIO helper arguments. It passed. It is a byte-level static
cross-check, not an emulator, hardware test or proof of all possible paths.
No firmware build/tests were rerun because only documentation changed. The
local v0.3.0 binary remains SHA-256
`f99f0d03f340f1a922397e2b99574ace544d81745f093a512ed1b817304b49d1`.

Local audit output is under ignored `logs/rf-switch-analysis/`: extracted
`radio-routines.dis`, `gpio-calls.txt`, `check_evidence.py`, `evidence-check.log`
and `sources.json`. pSemi PDFs were reviewed through the web PDF reader;
direct-file download returned HTTP 403, so no downloaded PDF hash is claimed.
Raw factory images, full disassembly, identifiers and local logs are not pushed.
For QFN orientation use the [hardware pin-counting guide](hardware-photos-and-pin-counting.md);
for MCU pin functions use the [vendor CX32L003 datasheet](https://www.zbitsemi.com/upload/file/20200903/20200903105452_99571.pdf).


## Later user-authorized finite RX/TX experiment

The subsequent [RX/TX smoke test](radio-rxtx-smoke-test.md) drove the factory
GPIO states during one finite RX window and one low-power packet TX. RX mode,
RSSI, expected receive timeout and TxDone were observed with zero device errors;
both pads were released afterward. No current or RF-power measurement was made.
This supersedes the earlier stage’s no-RF-operation scope only for that explicit
experiment; it does not identify the switch or establish safe sustained drive.
The installed v0.3.1 firmware remains unchanged and its normal RF commands stay
disabled.

## Operational acceptance for v0.5.0

The user accepted the existing factory-style PC3/PC4 switching for continued
RX/TX testing. v0.5.0 uses that same control scheme, with stronger input-release
cleanup and GPIO validation. The public-channel peer exchange passed again.
This operational acceptance removes an implementation approval gate; it does
not identify the switch IC or establish measured current, temperature or RF
output. DIO2 and DIO3 control remain disabled.
