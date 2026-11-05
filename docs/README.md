# Documentation and research index

Begin with the [project overview](../README.md), then follow the [DIY guide](DIY_GUIDE.md). Research records below are retained in place. A result from an early diagnostic firmware describes that firmware and date, not a limitation silently carried forward to today's matched pair.

## Current builder documentation

| Document | Purpose |
| --- | --- |
| [DIY guide](DIY_GUIDE.md) | Equipment, PC setup, UART/power, configuration and first radio test |
| [DX-LR32 flashing](FLASHING_DX_LR32.md) | Back-pad SWD, original-firmware warning, private backup, source-built programmer, current image |
| [CYD flashing](FLASHING_CYD.md) | Exact board, pinned build, factory/filesystem install and later app updates |
| [Troubleshooting](TROUBLESHOOTING.md) | Connectivity, flashing, touch, UART and RF diagnosis |
| [Build/test reference](BUILD.md) | Current targets, offline checks, dependency provenance/licenses |
| [Hardware photographs and pin counting](hardware-photos-and-pin-counting.md) | Five organized images, corrected pin map, SWD photo and continuity measurements |
| [CYD GUI](cyd-gui.md) | Menu, chat/DM, manual clock, device/LoRa settings and display sleep |
| [CYD developer overlay](../cyd/README.md) | Architecture, pinned source overlay, adapter behavior/limits |
| [ESP32 integration](ESP32_INTEGRATION.md) | Radio-host integration and delivery interpretation |
| [Live UART protocol](UART_LIVE.md) | Live RX/TX controller protocol additions |
| [Typed UART features](UART_FEATURES.md) | Current command/capability contract |
| [Source lock](source-lock.json) | Exact dependency revisions and file hashes |

## Measurements and development history

These records preserve the work leading to the current system. Some early statements (for example “RF not yet tested”) intentionally remain as the historical state at that step.

| Record | What it preserves |
| --- | --- |
| [Initial bring-up summary](bring-up.md) | Hardware mapping, early MCU/UART milestones and original limitations |
| [Radio bring-up](radio-bring-up.md) | Early SPI diagnostics and failures |
| [Factory pin correction](factory-pin-correction.md) | Surviving-code analysis, corrected RESET/BUSY and real command readbacks |
| [RF-switch investigation](rf-switch-investigation.md) | PC3/PC4 measurements, factory switching sequence, unresolved component/load questions |
| [Oscillator test](oscillator-test.md) | Passive crystal identification and v0.3.1 RC→XOSC→RC measurements |
| [UART protocol results](uart-protocol-results.md) | Early binary controller physical/regression evidence |
| [Original diagnostic UART protocol](UART_PROTOCOL.md) | Non-RF recovery-controller contract; not the current deployment target |
| [RAM-only RF smoke test](radio-rxtx-smoke-test.md) | First bounded RX/TX experiment without replacing recovery Flash |
| [Smoke-test source](../experiments/radio-smoke/README.md) | Preserved experiment and its offline checks |
| [Meshtastic peer exchange](meshtastic-peer-test.md) | First real public-channel round trip through the PC bridge |
| [PC bridge test guide](MESHTASTIC_TEST.md) | Historical WSL-host workflow; no PC bridge needed with CYD |
| [Firmware readiness review](firmware-readiness-review.md) | Earlier review, tests and then-open qualifications |
| [v0.5.0](v0.5.0-results.md) | Live controller and regression evidence |
| [v0.5.1](v0.5.1-results.md) | Commanded TX power range changes |
| [v0.6.0](v0.6.0-results.md) | Frequency windows/image calibration |
| [v0.7.0](v0.7.0-results.md) | Typed features, CAD/gain/sleep/GFSK and physical tests |
| [v0.8.0](v0.8.0-results.md) | Normal Meshtastic host-managed queue/CAD/airtime and standalone peer tests |
| [CYD bring-up](cyd-bringup.md) | Actual flashing, hashes, touch and integration history |
| [GUI evidence](cyd-gui.md) | Physical controls, channel/PKI messages and display sleep validation |

## Archived entry points and image provenance

- [2026-11-05 repository refresh verification](research/repository-refresh-2026-11-05.md): preservation checks, documentation/helper tests and their physical-test limits.

- [README before the beginner-guide rewrite](research/repository-notes-before-diy-guide.md): retains the earlier overview, tables, checklists and findings.
- [Build notes through v0.7.0](research/build-notes-through-v0.7.0.md): retains every earlier target/size table and programming-tool discussion.
- [Earlier photo index](research/photo-index-before-image-recovery.md): preserves the original planned-image list.
- [Image provenance/recovery](research/image-provenance.md): moved uploads, recovered color photograph, original placeholders and source-DNG Git history.

No original research document or test source was deleted during this reorganization. Uploaded image paths changed; the original uploads remain in Git history. Private local bring-up records, hardware identifiers, original readbacks and raw serial logs remain excluded from Git as before. The public findings and test results are retained here; excluding those private artifacts is not a claim that a stock firmware backup exists. **There is no complete original manufacturer backup to download.**

To support further hardware/testing work: [Buy Me a Coffee — eightteamtu](https://buymeacoffee.com/eightteamtu).
