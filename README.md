# OBD2TTGO (LILYGO T-Display-S3 + Generic ELM327 BLE)

A simple Arduino sketch that turns a **LILYGO T-Display-S3** into a BLE OBD-II RPM display.

It scans for a generic ELM327-style BLE adapter, connects over GATT, sends the OBD-II RPM PID (`010C`), and shows live RPM on the built-in screen.

## Hardware

- **LILYGO T-Display-S3** (ESP32-S3)
- **Generic ELM327 BLE adapter** (the type that exposes `FFF0/FFF1/FFF2` service/chars)
- Car with OBD-II port

## What this project does

- Scans BLE advertisements for service UUID: `0000FFF0-0000-1000-8000-00805F9B34FB`
- Connects if the device is connectable
- Writes command `010C\r` every second to `FFF2` (RX/write)
- Subscribes to notifications on `FFF1` (TX/notify)
- Parses `410C A B` response and computes RPM as `((A << 8) | B) / 4`
- Displays RPM on the T-Display-S3

> Note: Not all “ELM327 BLE” adapters use the same BLE service/characteristic UUIDs. This sketch expects `FFF0/FFF1/FFF2`.

## Repository structure

- `CarRPMNimBLE/CarRPMNimBLE.ino` – main sketch (BLE scan/connect, PID request, parse, display)
- `README.md` – setup and usage guide

## Arduino IDE setup

1. Install **Arduino IDE 2.x**.
2. Install **ESP32 boards by Espressif Systems** in Board Manager.
3. Select board: **LILYGO T-Display-S3** (or equivalent ESP32-S3 board profile if needed).
4. Install libraries:
   - `NimBLE-Arduino`
   - `TFT_eSPI`
5. Open `CarRPMNimBLE/CarRPMNimBLE.ino`.

## TFT_eSPI configuration (important)

`TFT_eSPI` usually needs a correct board/display configuration.

Depending on your install, configure one of these:

- `User_Setup_Select.h` to include the right setup file, or
- A custom setup matching LILYGO T-Display-S3 pins and driver.

If the screen stays blank or corrupted, this is the first thing to verify.

## Flashing

1. Connect the T-Display-S3 with USB.
2. Pick the right **Port** in Arduino IDE.
3. Click **Upload**.
4. Open Serial Monitor at `115200` baud (optional, for debug logs).

## Running it in the car

1. Plug the ELM327 BLE adapter into the OBD-II port.
2. Turn ignition to ACC/ON (adapter powered).
3. Power the T-Display-S3.
4. The screen should show scan/connection status, then RPM updates.

## Troubleshooting

### No device found

- Confirm your adapter is **BLE**, not classic Bluetooth-only.
- Some adapters stop advertising while already connected to a phone app.
- Power-cycle the adapter and rescan.

### Connects but no RPM

- Adapter may use different UUIDs than `FFF0/FFF1/FFF2`.
- Some adapters require ELM initialization commands (`ATZ`, `ATE0`, etc.) before PID queries.
- Check raw replies in Serial Monitor.

### Screen issues

- Re-check `TFT_eSPI` configuration for T-Display-S3.

## Next improvements

- Add other PIDs (speed `010D`, coolant temp `0105`, etc.)
- Add reconnect/backoff logic and better status UI
- Add adapter profile options for alternate UUID layouts
- Add startup ELM initialization sequence for broader adapter compatibility

## License

MIT. See `LICENSE`.
