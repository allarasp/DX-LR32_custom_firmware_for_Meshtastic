# Troubleshooting

Work from power and connectivity toward RF. Do not erase firmware to diagnose a reversed wire.

| Symptom | Check |
| --- | --- |
| ST-Link missing in Ubuntu/WSL | Check `lsusb`, USB data cable, and usbipd attachment of the **ST-Link**, not just the CYD. |
| ST-Link sees voltage but cannot connect | Power off; check SWDIO → pad 3 / PC7, SWCLK → pad 2 / PD1, and common GND. Check power/shorts with a meter. The tested adapter speed is 100 kHz. Voltage alone does not prove correct SWD wiring. |
| Unsupported identity/protection or backup error | Stop. Verify the physical part and read the logs. Never unlock or mass-erase merely to make the command succeed. No stock restore image is available. |
| Flash helper says target changed since backup | Make a new backup of this module's present contents. Do not edit the manifest or disable the guard. |
| No CYD serial device | Use a USB data cable, check Linux permissions and WSL attachment. Reconnecting can change BUSID/serial port. |
| esptool stays at Connecting | Close the serial monitor; try BOOT held while tapping RESET, then release BOOT after connection. Check the actual CYD serial port. |
| CYD blank/incorrect colors or unusable touch | Verify the **two-USB ST7789** board, exact overlay and factory/partition pairing. An ILI9341 one-USB build is different. Finish four-point resistive calibration with firm, steady presses. |
| UART OFF | Module powered? GPIO27 → **RXD/PD6**, GPIO22 ← **TXD/PD5**, common GND? Remove competing USB–UART data leads. Install matching controller v0.8.0; opening USB serial does not prove radio handshake. |
| Receives but cannot send | Check TX ON, saved `lora.tx_enabled`, suitable antenna and matching region/preset/channel. Explicitly enable TX in the phone app or USB API; changing power alone does not enable it. |
| Peer receives but sender reports failed delivery | Delivery confirmation needs an ACK/rebroadcast heard by the sender. Test both directions and report exact text/status. One-way reception is not proof of the return RF path. |
| New node absent from direct-message list | Receive its Node Info first; the DM browser uses known nodes. Check public-channel communication before PKI DMs. |
| First touch after sleep does nothing else | Expected: the entire first touch wakes the screen; touch again to select. Radio/Bluetooth remain active. |
| Clock wrong after complete power loss | Manual time is volatile without a backed RTC; set it again or synchronize it. |
| Traffic waits before sending | The current pair uses normal Meshtastic queue/channel/airtime checks. Older mismatched controller/test modes can retain a fixed cooldown. Do not remove regional limits to force traffic. |

Do not attach both CYD UART and an external USB–UART transmitter to the module. To run the optional raw diagnostics, first power off and disconnect CYD TX/RX, then connect a 3.3 V logic adapter with common GND and a single supply. Test commands can change the radio configuration; return to normal operation by reconnecting CYD and rebooting the pair.

For a useful issue report, include board photographs/markings, source revision, which build target was used, exact commands and sanitized error logs. Do not publish node private keys, full NVS dumps, backup images or UID records. Historical RF limitations and test results are indexed in [docs/README.md](README.md).
