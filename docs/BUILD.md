# Build and test

Run these commands from the repository root in Ubuntu/WSL. GCC Arm Embedded
13.2.1, Arm binutils 2.42, host GCC 13.3.0, Make 4.3, Python 3.12.3 and Git
2.43.0 were used for the original builds. No Python packages, RTOS, Meshtastic
SDK, Keil, IAR, OpenOCD or connected hardware are required to build firmware.

```sh
sudo apt-get install --no-install-recommends \
  gcc-arm-none-eabi binutils-arm-none-eabi build-essential git python3
python3 firmware/tools/prepare_deps.py
make -C firmware -j2 protocol-check radio-check uart-check check
```

Dependency preparation needs network access on a fresh checkout. Subsequent
builds/tests are offline. The script fetches exact commits into ignored
`vendor/` directories, uses sparse checkouts for the required upstream files,
and verifies dependency hashes. It rejects a different revision, modified
checkout, or changed extracted device file rather than replacing local work.
The ignored dependency checkouts are not submodules of this repository.

## Images

| Target | Output | Status |
| --- | --- | --- |
| `make -C firmware uartctrl` | `firmware/build/uartctrl/controller.{elf,bin,hex,map}` | Binary UART controller v0.3.1; offline tests pass; physical deployment/XOSC pending (v0.3.0 remains installed) |
| `make -C firmware radiodiag` | `firmware/build/radiodiag/controller.{elf,bin,hex,map}` | v0.2.1, flashed/readback verified; UART and non-RF SPI diagnostics physically pass |
| `make -C firmware uartdiag` | `firmware/build/uartdiag/controller.{elf,bin,hex,map}` | UART diagnostic v0.1.1, physically tested at 115200 8N1 |
| `make -C firmware all` | `firmware/build/controller.{elf,bin,hex,map}` | Experimental UART/SX1262 foundation; UART 9600 8N1, SPI mode 0 at 500 kHz; not flashed |
| `make -C firmware check` | Also `firmware/build/driver-check.elf` | Retains all baseline `sx126x.c` functions to validate linkage and size; not a deployment image |

The default Makefile target builds the experimental foundation. Use `uartctrl`
explicitly for the binary controller; `radiodiag` and `uartdiag` preserve the
physically tested text recovery images. No build target programs,
erases, resets, or otherwise accesses a device.

Observed sizes with the toolchain above:

| Image | Flash | Static SRAM | Reserved stack |
| --- | ---: | ---: | ---: |
| Binary UART controller v0.3.1 | 7,664 B | 992 B | 1,024 B |
| Preserved v0.3.0 recovery | 6,888 B | 988 B | 1,024 B |
| Radio diagnostic | 5,100 B | 340 B | 1,024 B |
| UART diagnostic | 1,712 B | 60 B | 1,024 B |
| Experimental foundation | 1,596 B | 8 B | 1,024 B |
| Full baseline driver link check | 6,368 B | 176 B | 1,024 B |

The linker's RAM-span report shows 100% because the stack is placed at the top
of RAM. Static usage is `.data + .bss`, excluding the 1 KB `.stack` section.
There is no heap. The memory map is 64 KB Flash at `0x00000000` and 4 KB SRAM
at `0x20000000`. Link assertions prevent static-data/stack overlap and Flash
overflow. No option-byte, calibration, UID or ISP region is emitted.

## Tests

`protocol-check` validates the binary controller, runs C framing/configuration
and failure/recovery tests with the real pinned Semtech command encoder and a
mock radio platform, then runs six Python codec/cross-language/cleanup tests.
No USB access occurs. The separate physical utility `bin/uart_radio.py` opens
the CH340 and performs real non-RF tests; see the [protocol specification](UART_PROTOCOL.md)
and [build/physical results](uart-protocol-results.md). The binary controller
uses an RX interrupt/ring; preserved text recovery images retain their original
polling UART implementation.

`radio-check` validates the new image and tests the real pinned Semtech command
encoder through a mocked platform: golden frames, packet-type readback and
restoration, invalid responses, BUSY/tick deadlines, SPI failure cleanup and
RF-command rejection. Physical results and the corrected RESET/BUSY mapping are in [factory-code comparison](factory-pin-correction.md).

`uart-check` verifies source hashes and the ELF/binary layout, then runs the
host UART command-parser tests. `check` verifies the experimental image,
links the full baseline radio driver and runs mock HAL tests for framing,
dummy bytes, transfer failures, BUSY timeout/wraparound and NSS cleanup.
Warnings are errors for firmware compilation. Image validation checks vectors,
Thumb entry, initial stack, load ranges, data placement and unresolved symbols.

To rebuild unchanged objects, add `-B` to Make. `CROSS_COMPILE` can override
the default `arm-none-eabi-` executable prefix, and `HOSTCC` the host compiler.
Stack estimates are generated as ignored `*.su` files. These tests do not
establish real radio operation or worst-case stack usage.

## Pinned dependencies and provenance

The exact revisions and required-file SHA-256 hashes are in
[source-lock.json](source-lock.json).

| Dependency | Source | Revision |
| --- | --- | --- |
| Vendor device pack mirror | [CX32L003-HAL-SPL](https://github.com/Z1R343L-D77/CX32L003-HAL-SPL) | `f8dabc3b529094127ab76cfd43589d8361b70cc5` |
| ARM CMSIS 5.9.0 | [ARM-software/CMSIS_5](https://github.com/ARM-software/CMSIS_5) | `2b7495b8535bdcb306dac29b9ded4cfb679d7e5c` |
| Semtech SX126x driver 2.5.0 | [Lora-net/sx126x_driver](https://github.com/Lora-net/sx126x_driver) | `a10c5dfdf89788c6ac805e9fe98889de44175aa2` |

The device headers and legacy system source are extracted from
`XMC.CX32L003_DFP.1.0.7.pack`. Only source/metadata files are extracted; FLM
programming algorithms are not installed or invoked. The official SDK download
was unavailable during preparation, so this is a pinned third-party mirror,
not a claim that a complete official SDK was installed/authenticated.
The diagnostic image uses the project clock/watchdog implementation in
`uart_system.c`; the experimental foundation uses the vendor system source.
Both reuse the project's GNU startup, since the pack's GCC startup is only
an unrelated placeholder.

CMSIS is Apache-2.0 and the Semtech driver is BSD-3-Clause-Clear; their upstream
license files are fetched and hash-checked. The vendor pack has source notices
but no explicit license file in its metadata. It is fetched externally rather
than redistributed here. No project-wide license is inferred from these
dependency licenses.

## Optional OpenOCD build (offline tool preparation only)

This is not needed for firmware compilation. Standard OpenOCD 0.12 can examine
the core with `configs/generic-examine.cfg`, but its installed driver set did
not include CX32L003 Flash support. The previous hardware stage used the fork
below, explicitly configured as 64 KB / 128 sectors of 512 bytes at 100 kHz.
It was tested on CX32L003F8, not every variant in the family.

Install host-tool prerequisites separately if this optional tool is needed:

```sh
sudo apt-get install --no-install-recommends \
  autoconf automake libtool libtool-bin pkg-config libusb-1.0-0-dev texinfo
git clone https://source.haeckserei.de/nica/openocd-code.git src/openocd-code
git -C src/openocd-code checkout --detach 2431daa7992436f61ea49d2d54308b61f08fbbee
git -C src/openocd-code submodule update --init jimtcl
openocd_prefix="$PWD/tools/openocd-cx32"
(cd src/openocd-code && ./bootstrap nosubmodule && \
  ./configure --prefix="$openocd_prefix" --enable-internal-jimtcl \
    --enable-stlink --disable-jlink --disable-werror \
    --disable-doxygen-html --disable-doxygen-pdf)
python3 bin/rebuild-cx32-loader.py
make -C src/openocd-code -j4
make -C src/openocd-code install
```

The rebuild helper compiles the inspected RAM loader without newlib, aligns its
stack to eight bytes and regenerates the fork's loader include. It runs only
compilers and file operations, not OpenOCD or USB access. Generated audit output
stays under ignored `logs/bringup/loader-build/`. The resulting tool reports a
dirty suffix because of that regenerated include. The upstream Jim Tcl revision
is `f160866171457474f7c4d6ccda70f9b77524407e`.

`configs/cx32l003-bringup.cfg` configures that optional native ST-Link driver;
it does not automatically initialize the adapter or write Flash. The upstream
target's examination hook sets volatile debug controls. Device-specific guards,
erase/write recipes and local historical scripts are intentionally not published
as a generic automatic flasher.

The fork has no protection-check callback: its sector labels are not a real
read-protection measurement. It also does not establish the exact part number
through silicon identification. Future programming requires checking the actual
target, current protection state, driver behavior and image independently.


## v0.3.1 validation

`make -C firmware -j2 protocol-check radio-check uart-check check` passes.
The protocol tests cover XOSC success, wrong-mode readback, device errors, BUSY
timeout, cleanup/first-error retention, unchanged successful configuration,
invalid GPIO, and all prior framing/CRC/configuration regressions. A separate
RF-switch unit test validates denied activation, input configuration checks,
and factory ordering/intermediate states without hardware access.

ELF validation confirms MSP `0x20001000`, Thumb Reset_Handler `0xC1`, Flash
within 0x00000000–0x0000FFFF and SRAM within 0x20000000–0x20000FFF. Static SRAM
is 992 B, reserved stack 1024 B, leaving 2080 B between static data and stack.
GCC `.su` reports show largest individual frames of 128 B in rc_handle and
rp_feed; no recursion, VLA or heap allocation is used. A physical stack
watermark has not been measured.

GCC `-fanalyzer -c` checks of the seven changed/control source units passed;
the host C protocol suite also passed AddressSanitizer/UndefinedBehaviorSanitizer.
The deployed-image symbol inspection contains RF-switch inspection/init only;
the planner and denied actuator request are discarded. Physical flashing and
oscillator results are tracked separately in [oscillator-test.md](oscillator-test.md).
