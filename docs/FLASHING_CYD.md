# Build and flash the two-USB CYD

This variant targets **ESP32-2432S028 with USB-C and micro-USB, 4 MB Flash, ST7789 display and XPT2046 touch**. A generic CYD Meshtastic image or an ILI9341 board configuration is not equivalent. The external module must run the matching **meshctrl v0.8.0** controller.

[Install PC prerequisites and the Python environment](DIY_GUIDE.md#1-prepare-the-pc) first. All commands run from this repository root. The maintained overlay is in `cyd/`; its generated upstream checkout stays ignored under `.local/`.

## 1. Fetch the pinned upstream firmware and apply the overlay

```sh
git clone --depth 1 --branch v2.7.26.54e0d8d \
  https://github.com/meshtastic/firmware.git .local/meshtastic-cyd
git -C .local/meshtastic-cyd submodule update --init --depth 1 protobufs
python3 cyd/prepare.py
sh cyd/tests/check.sh
.venv/bin/pio run -d .local/meshtastic-cyd -e cyd-dxlr32 -j 4
```

The pinned upstream commit is `54e0d8d0ab2ff56b3a9ce967e53f79e49af560fb`. `prepare.py` refuses another revision. Rerun it after changes to this project's overlay; keep independent edits out of the generated checkout. Dependencies download on the first build. Tests/builds do not access USB hardware.

The output directory is `.local/meshtastic-cyd/.pio/build/cyd-dxlr32/`. The factory, app, partition table and filesystem must all come from this same build.

## 2. Find the CYD serial port

Connect a USB **data** cable to the CYD and attach it to WSL if applicable. In Linux:

```sh
ls -l /dev/ttyUSB* /dev/ttyACM*
```

The examples below use `/dev/ttyUSB0`; replace it with your actual CYD port. Do not choose a separate DX-LR32 USB–UART adapter by mistake. Close serial monitors and Meshtastic CLI sessions first.

Usually USB auto-reset enters the ESP32 bootloader. You do **not** need to hold BOOT in advance. If esptool remains at “Connecting”, hold BOOT, press/release RESET, and release BOOT once the connection starts. If your board has no accessible buttons, use its documented download-mode procedure rather than guessing GPIO connections.

## 3. First installation: factory image and filesystem

This replaces the CYD's previous application/partition layout. Back up any existing CYD configuration you need before changing firmware. Use the complete factory image at **`0x0`**, then the matching LittleFS image at **`0x300000`**:

```sh
.venv/bin/esptool.py --chip esp32 --port /dev/ttyUSB0 --baud 115200 \
  --before default_reset --after hard_reset write_flash -z \
  --flash_mode dio --flash_freq 40m --flash_size 4MB \
  0x0 .local/meshtastic-cyd/.pio/build/cyd-dxlr32/firmware-cyd-dxlr32-2.7.26.54e0d8d.factory.bin \
  0x300000 .local/meshtastic-cyd/.pio/build/cyd-dxlr32/littlefs-cyd-dxlr32-2.7.26.54e0d8d.bin
```

Wait for successful writing/hash verification and reset. The factory image includes the matching bootloader/partition/application components; do not substitute the shorter app binary at address zero. Do not routinely run `erase_flash`: it deletes NVS configuration and touch calibration. Installing over unrelated old firmware may require separately resolving incompatible old NVS; do not erase it merely to fix a wiring problem.

First boot asks you to calibrate resistive touch. Hold each corner cross until it disappears, release, then press the next. The library briefly uses portrait orientation during calibration, then returns to landscape.

## 4. Later updates: app only

For a CYD already using **this same partition layout**, preserve NVS, calibration and filesystem by writing only the app at **`0x10000`**:

```sh
.venv/bin/esptool.py --chip esp32 --port /dev/ttyUSB0 --baud 115200 \
  --before default_reset --after hard_reset write_flash -z \
  --flash_mode dio --flash_freq 40m --flash_size 4MB \
  0x10000 .local/meshtastic-cyd/.pio/build/cyd-dxlr32/firmware-cyd-dxlr32-2.7.26.54e0d8d.bin
```

If a future update changes the filesystem or partition map, follow that update's instructions instead of assuming app-only is sufficient. The current 4 MB map is app `0x10000` / size `0x250000`, flash-app `0x260000` / size `0xA0000`, LittleFS `0x300000` / size `0x100000`. Developer details and output provenance are in [cyd/README.md](../cyd/README.md) and [bring-up evidence](cyd-bringup.md).

## 5. Configure and test

Power off, [connect UART and power correctly](DIY_GUIDE.md#3-install-the-cyd-firmware-and-connect-uart), then follow the [first-start test](DIY_GUIDE.md#4-first-start-and-radio-test). A freshly configured CYD defaults to **EU_433, LongFast and TX disabled**. Enable TX deliberately through the phone app or USB API, and match the other node's channel settings. `UART ON` verifies the controller link; it does not by itself prove over-the-air delivery.
