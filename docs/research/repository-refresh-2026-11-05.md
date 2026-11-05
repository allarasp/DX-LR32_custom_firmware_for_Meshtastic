# Repository documentation refresh — 2026-11-05

The beginner entry point now describes the working two-USB ST7789 CYD + DX-LR32-A pair instead of presenting already-completed integration steps as future work. Controller/UI source changes already present before this refresh were retained. The GitHub name/URL has not been changed; the proposed clearer name is `Meshtastic-CYD-DX-LR32`.

## Preservation

- All existing research documents, measurement records, experiments and test sources remain. Earlier README/build/photo-index contents are archived beside this record, with relative links adjusted and historical context added.
- All four valid uploaded files were moved into `docs/images/` and checked byte-for-byte against their original Git blobs. The fifth photograph was recovered from the original DNG in Git history; its compressed pixel stream is unchanged and camera metadata omitted. Both invalid original placeholder files are retained. See [image provenance](image-provenance.md).
- Fourteen existing tracked firmware/adapter/test/helper diffs were compared against a pre-refresh snapshot and remained byte-for-byte unchanged. The CYD developer README was intentionally updated; the firmware source was not changed as part of this documentation refresh.
- Private logs, node identities, keys, ignored local bring-up records and original readbacks remain local, as before. No complete original manufacturer firmware backup exists in the project. A custom diagnostic recovery image is not a stock restore image.

## Checks performed

- `make -C firmware mesh-check`: passed, including source hashes, host sanitizer tests and image validation. Current controller remains 20,476 bytes with SHA-256 `6a1230183c8e96c47d7b91c00e2ddb09141ade0317e2249eda69576b0f875ff6`.
- `sh cyd/tests/check.sh`: adapter/queue, touch and GUI model checks passed with ASan/UBSan. The initial sandbox attempts failed because LeakSanitizer cannot run under tracing; the suites passed when rerun outside that sandbox.
- `python3 -m unittest discover -s tests -v`: six new offline flash-helper safety/flow tests passed, covering invalid vectors/protection/hash data, changed-target refusal before writing, bad-readback refusal before boot, success ordering and literal Tcl paths.
- `python3 bin/check_docs.py`: local Markdown links/headings and HTML image sources checked, no missing targets. This does not test availability of remote links or fully render GitHub Markdown.
- Python compilation and `git diff --check`: passed.
- Recovered JPEG visually inspected; moved uploads and unchanged compressed photograph pixels verified programmatically.

The public flash helper uses the project's already-tested SWD write/host-readback method, but the new wrapper itself was tested **offline with mocked OpenOCD**, not flashed onto a new board. No hardware was programmed during this refresh. No claim of new RF qualification or fresh physical GUI validation is made here; those results remain in their original evidence documents.

The new public routes are [DIY setup](../DIY_GUIDE.md), [module backup/flashing](../FLASHING_DX_LR32.md), [CYD flashing](../FLASHING_CYD.md), [troubleshooting](../TROUBLESHOOTING.md) and the [complete index](../README.md). Donation support appears in the root README and `.github/FUNDING.yml`.
