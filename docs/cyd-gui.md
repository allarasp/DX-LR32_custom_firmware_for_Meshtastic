# CYD GUI update — 2026-11-05

This extends the existing pinned native Meshtastic TFT UI. It does not introduce
LVGL or replace the tested mesh controller. The hardware remains 320x240;
logical content grows from 320x184 to 320x200 by removing the bottom TX strip.

## Controls and status

- Menu is blue on ordinary pages. In an open menu/editor it turns green and
  reads OK. It selects the highlighted item. Numeric editors show minus/plus
  on the neighboring controls; OK advances through digits and accepts the field.
- TX ON/OFF moves beside the native battery/time header; yellow indicates a
  transmit pulse held long enough to be visible at the idle redraw rate.
- UART ON requires the adapter handshake to complete. Idle PING/PONG runs every
  2 s, with a 3 s reply timeout; a disconnected idle controller becomes OFF
  within about 5 s. Opening a UART port alone never means ON.
- Status badges are hidden while modal menus/keyboards need the drawing area.
- The first complete touch gesture after screen-off wakes the screen and is
  consumed through release. It does not activate a control underneath.

## Clock

Clock Menu opens date/time, timezone, 12/24-hour format and the native clock-face
picker. Date/time is edited as year/month/day/hour/minute, with a preview and
Save/Cancel. Calendar bounds and local-time/DST conversion are validated before
calling the upstream RTC setter. A date rejected by the upstream build-epoch
check is reported rather than silently accepted. The menu identifies unset,
manual and synchronized time. Manual time is volatile without a battery-backed
RTC; timezone and format persist in the ordinary configuration.

## System settings

The existing Notifications, Display Options, Bluetooth and Reboot/Shutdown
remain. New controls are:

| Setting | Values |
| --- | --- |
| Rebroadcast Mode | All, skip decoding, local channels, known nodes, none, core ports |
| Node Info interval | 15/30 min, 1/3/6/12/24 h, or custom 1–10080 min |
| Screen on for | 1/2/3 min or Never |
| LoRa region | EU_433, EU_868 (the implemented controller bands) |
| Preset | The pinned enum's ordinary, Lite, Narrow and labeled legacy presets |
| Hop limit | Default (3), or 1–7 |
| RX boosted gain | Off/On |
| TX power | Common values up to the upstream regional ceiling and +22 dBm hardware bound |

New settings editors show the current value and require Save; Cancel does not
change configuration. The same fields are used by the phone API, with the usual
client refresh/reconnect needed for cached phone settings. Radio changes reload
the adapter; other settings save without gratuitous radio reinitialization.
Node Info interval changes (including phone-side edits) reschedule the periodic
check immediately, retaining upstream anti-spam/airtime checks.

Display timeout turns off only the screen. Radio and Bluetooth remain active.
Existing saved timeout values, including the previously configured 600 s, are
preserved until changed. Zero means Never on this CYD. A touch or an ordinary
upstream screen wake starts a fresh display interval.

## Chat

Chat opens Recent conversations, Channels, and Direct messages/New DM. Channel
choices come from configured enabled channels; DM destinations come from known
nodes with pagination. It does not silently redirect a chosen DM to broadcast.
A conversation displays its actual channel/peer; Compose opens the existing
CannedMessageModule touch keyboard with that destination. Its native delivery
status tracking remains in use. Back returns from composition to conversation.

History reuses the existing ten-message shared store and persistence behavior;
it is not ten messages per peer and is not an unlimited phone-style archive.
Unread counts are isolated by channel/peer, bounded to 24 tracked conversations
and 99 unread per conversation, and reset on reboot. Viewing a conversation
marks only its count read; an obscuring menu does not count as reading it.
Incoming text cannot steal the selected conversation or an open editor/draft.
Long names/previews are clipped at UTF-8 boundaries to respect the native
selection renderer's fixed highlighted-line buffer.

## Validation

The full ESP32 build and ASan/UBSan adapter/touch/GUI-model tests passed.
Coverage includes real upstream radio queue regressions, heartbeat success/loss,
calendar/leap-day rejection, numeric bounds, unread isolation/saturation/eviction,
UTF-8 menu-label bounds and full wake-gesture suppression. These tests do not
prove the physical appearance or touch usability; device observations follow.

## Installed image and device observations

The complete application is 1,349,008 B, SHA-256
`ea5f7145b086c938dabddda0a82dd94fb2b08730d789478f20131c8e99231a85`.
Program use is 1,342,285 B of 2,424,832 B; static RAM is 95,976 B of 327,680 B.
Application-only programming at 0x10000 completed with verified Flash hash.
The DX-LR32 v0.8.0 controller was not reflashed for this UI update.

Post-update PhoneAPI confirmed all recorded fields matched the pre-update
snapshot: EU_433, LongFast, TX enabled, 10 dBm, hop limit 3, RX gain on,
rebroadcast ALL, Node Info interval 10800 s, screen timeout 600 s, Tallinn's
POSIX timezone and Bluetooth enabled. That comparison precedes user-directed
UI edits. Calibration, messages and configuration storage were not erased.

Raw serial monitoring (without a live PhoneAPI session) captured actual touch
selections in Clock, hour/minute editing, Cancel, then Save confirmation. The
upstream RTC accepted the manual time and Clock changed from Not set to Manual.
The controller then completed Node Info transmission normally. Further physical
checks are recorded as they complete rather than inferred from the host tests.

Evidence stays local under ignored `logs/cyd/gui/`: app artifact and manifest,
verified upload, pre/post configuration snapshots/comparison, raw boot and UI
monitoring logs. Host tests/build reports are `logs/cyd/test-gui-complete.log`
and `logs/cyd/build-gui-complete.log`.

The user confirmed Menu/Clock/System controls worked and the channel message
was sent. Live logs captured channel 0 selection, Compose to broadcast,
packet 961214142 transmission and its implicit ACK. The user then selected
Alr1 (`06f369e4`) as a DM destination; Compose retained that destination,
upstream logged `Use PKI!`, packet 1416626880 completed TX, and its matching
ACK arrived. Recent conversations displayed the channel and DM threads.
This verifies the new picker/compose path for both broadcast and one real
PKI direct-message peer. It is not a general multi-peer encryption qualification.

The user also confirmed the one-minute screen timeout and wake behavior with
messaging/Bluetooth still working. The raw log independently records display
timeout at uptime 366 s, first wake touch swallowed at 369 s, and display timeout
again at 429 s—exactly one minute after that touch. The serial listener was
then deliberately stopped to release the USB port; it was a raw log reader,
not an ongoing PhoneAPI session. No further configuration was changed by the
assistant after the user's physical tests.

The completed validation summary is in local `gui-validation.json`. Screen
appearance and controls were user-confirmed; host tests alone were not used as
proof of touch behavior. A broad long-run UI/memory test and testing every
alternate LoRa preset remain separate from this successful short bring-up.
