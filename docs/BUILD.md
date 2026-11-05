# Current build and test reference

For a complete first installation, start with the [DIY guide](DIY_GUIDE.md). This page is the developer reference for the **v0.8.0 controller + CYD overlay on Meshtastic 2.7.26.54e0d8d**. Builds/tests below do not access hardware. Only explicitly invoked programming or serial tools do.

## Build the module controller

Install Ubuntu prerequisites from [PC setup](DIY_GUIDE.md#1-prepare-the-pc), then:

```sh
python3 firmware/tools/prepare_deps.py
make -C firmware mesh-check
```

Output: `firmware/build/meshctrl/controller.{bin,elf,hex,map}`. The recorded GCC Arm 13.2.1 build is 20,476 B Flash, 2,504 B static SRAM plus a reserved 1,024 B stack, leaving 568 B outside those allocations. The map's RAM-span percentage includes the stack's placement at the top of RAM; it is not static allocation alone. There is no heap. Memory is 64 KB Flash at `0x0` and 4 KB SRAM at `0x20000000`. Image validation checks vectors, sections and unresolved symbols. These are not proofs of worst-case stack use.

`mesh-check` runs the real protocol/radio code against host mocks, sanitizer tests, Python feature tests, source-hash validation and ELF/binary validation. It never programs the module. See [v0.8.0 results](v0.8.0-results.md).

## Build the CYD

Follow [the pinned checkout, overlay and build commands](FLASHING_CYD.md#1-fetch-the-pinned-upstream-firmware-and-apply-the-overlay). Build tools are pinned in [requirements-build.txt](../requirements-build.txt): [PlatformIO 6.2.0](https://pypi.org/project/platformio/6.2.0/) and [esptool 4.11.0](https://pypi.org/project/esptool/4.11.0/). The first build downloads dependencies; subsequent builds reuse them.

```sh
python3 cyd/prepare.py
sh cyd/tests/check.sh
.venv/bin/pio run -d .local/meshtastic-cyd -e cyd-dxlr32 -j 4
```

The current app-only physical test image was 1,349,008 bytes, SHA-256 `ea5f7145b086c938dabddda0a82dd94fb2b08730d789478f20131c8e99231a85`. Different sources/toolchains may change output. See [CYD evidence](cyd-bringup.md) and [GUI validation](cyd-gui.md); successful compilation alone does not verify touch or radio operation.

## Broader offline regression

```sh
make -C firmware -j2 check radio-check uart-check protocol-check mesh-check
.venv/bin/python firmware/tests/test_meshtastic_bridge.py
sh cyd/tests/check.sh
sh experiments/radio-smoke/check.sh
python3 -m unittest discover -s tests -v
python3 bin/check_docs.py
```

The bridge tests require [requirements-meshtastic.lock](../requirements-meshtastic.lock), installed in the project virtual environment. The CYD tests compile the actual adapter, pinned upstream queue, touch/UI model with host stubs and ASan/UBSan. Flash-helper tests mock OpenOCD and do not open USB.

## Programming is separate

- [DX-LR32 SWD backup/programming](FLASHING_DX_LR32.md) includes building the pinned CX32-aware OpenOCD and its source-built RAM loader.
- [CYD USB programming](FLASHING_CYD.md) distinguishes first-install factory/filesystem writes from app-only updates.
- Optional raw UART tools are documented in [UART feature contract](UART_FEATURES.md). They are not required for normal CYD operation. Do not connect a second host while CYD UART is attached.

Do not use an old generic/default target as the final controller. Do not copy device-specific ignored `configs/bringup-*` scripts from an author's machine; the public helper derives identity from your own backup.

## Historical targets retained for research

| Make target | Output directory | Purpose |
| --- | --- | --- |
| `meshctrl` / `mesh-check` | `firmware/build/meshctrl/` | **Current v0.8.0 live controller** |
| `uartctrl` / `protocol-check` | `firmware/build/uartctrl/` | Earlier v0.3.1 non-RF binary diagnostics |
| `radiodiag` / `radio-check` | `firmware/build/radiodiag/` | Earlier SPI/radio text diagnostics |
| `uartdiag` / `uart-check` | `firmware/build/uartdiag/` | Earlier text UART diagnostics |
| `all` (default) / `check` | `firmware/build/` | Experimental foundation / linkage tests, **not the deployed pair** |

Original size tables, tool versions, test details and historical OpenOCD discussion remain in the [archived build notes](research/build-notes-through-v0.7.0.md). Shared-source changes can alter rebuilt diagnostic sizes; an old measured image hash is not automatically the hash of a newly rebuilt target. None of these images is manufacturer firmware.

## Sources and licenses

Exact source revisions and required-file SHA-256 hashes are in [source-lock.json](source-lock.json). `prepare_deps.py` fetches sparse, pinned dependencies into ignored `vendor/` and rejects modified/mismatched sources instead of replacing local edits.

| Dependency | Pinned revision | Provenance/license |
| --- | --- | --- |
| CX32 device pack mirror | `f8dabc3b529094127ab76cfd43589d8361b70cc5` | [CX32L003-HAL-SPL](https://github.com/Z1R343L-D77/CX32L003-HAL-SPL); original official SDK was unavailable; third-party mirror, no explicit pack license inferred |
| ARM CMSIS 5.9.0 | `2b7495b8535bdcb306dac29b9ded4cfb679d7e5c` | [ARM CMSIS](https://github.com/ARM-software/CMSIS_5); Apache-2.0 |
| Semtech SX126x driver 2.5.0 | `a10c5dfdf89788c6ac805e9fe98889de44175aa2` | [Semtech driver](https://github.com/Lora-net/sx126x_driver); BSD-3-Clause-Clear |
| Meshtastic firmware | `54e0d8d0ab2ff56b3a9ce967e53f79e49af560fb` | [Upstream firmware](https://github.com/meshtastic/firmware); GPL-3.0 derivative obligations apply to the CYD build |
| CX32-aware OpenOCD fork | `2431daa7992436f61ea49d2d54308b61f08fbbee` | [Source fork](https://source.haeckserei.de/nica/openocd-code); GPL-2.0-or-later loader notice; Jim Tcl submodule `f160866171457474f7c4d6ccda70f9b77524407e` |

Only required source/metadata is extracted from the vendor pack; its opaque FLM algorithms are not installed or invoked. Third-party sources remain external, with upstream notices/licenses. No project-wide license is inferred from dependency licenses. Generated firmware, private keys, NVS dumps and device identifiers are not source artifacts to publish.
