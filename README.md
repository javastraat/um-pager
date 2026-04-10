# um-pager

Firmware for the **LilyGo T-Lora Pager** — a handheld ESP32 device with LoRa radio, rotary encoder, and physical keyboard. Runs the [UniversalMesh](https://github.com/johestephan/UniversalMesh) networking stack with an LVGL-based touchscreen UI.

---

## Features

- **Boot splash** — logo + node name, auto-advances to the main menu after 3 s
- **Menu** — horizontal tile carousel navigable by rotary encoder or touch
  - **ESP-Now / Mesh** — scans channels 1–13 to find a UniversalMesh coordinator
  - **LoRa** — long-range radio screen *(in development)*
  - **Messages** — inbox & compose *(in development)*
  - **Settings** — device configuration *(in development)*
  - **Help** — controls reference & firmware version
- **OTA updates** — WiFi-based firmware upload via ArduinoOTA (no USB required)
- **SDL2 desktop simulator** — run and iterate on the UI without flashing hardware
- **Sleep / wake** — power button in the top bar; wakes on boot button or rotary press

---

## Hardware

| Component | Details |
|---|---|
| Board | LilyGo T-Lora Pager |
| MCU | ESP32 (with PSRAM) |
| Radio | LR1121 + SX1262 (dual-band LoRa) |
| Flash | 16 MB (QIO mode) |
| Display | Colour LCD driven by LilyGoLib + LVGL |
| Input | Rotary encoder, physical keyboard (TCA8418 key matrix) |
| GNSS | On-board GPS (TinyGPSPlus) |
| NFC | ST25R3916 (ST25R3916-fork / NFC-RFAL-fork) |
| BLE | NimBLE-Arduino |
| Power | XPowersLib PMU |

---

## Project structure

```
um-pager/
├── src/
│   ├── um_main.cpp          # Arduino setup() / loop(), OTA gate
│   ├── um_nav.h             # Screen enum + navigator (go / back)
│   ├── um_shared.h          # Shared volatile flags (otaRequested)
│   ├── um_welcome.cpp       # Boot splash (3 s timer → menu)
│   ├── um_menu.cpp          # Tile-carousel main menu
│   ├── um_mesh.cpp          # ESP-Now / UniversalMesh screen
│   ├── um_lora.cpp          # LoRa screen stub
│   ├── um_messages.cpp      # Messages screen stub
│   ├── um_settings.cpp      # Settings screen stub
│   ├── um_help.cpp          # Help & about screen
│   ├── um_logo.h            # Embedded PNG logo (512×512)
│   ├── ota_update.h         # ArduinoOTA helper (WiFi connect + flash)
│   ├── lilygo_compat.h      # Board compatibility shims
│   └── sim/                 # SDL2 desktop simulator stubs
│       ├── main_sim.cpp
│       └── um_mesh_stub.cpp
├── boards/                  # Custom PlatformIO board definition
│   └── lilygo-t-lora-pager.json
├── variants/                # Arduino variant files
│   └── lilygo_tlora_pager/
├── partitions/              # Custom partition table
│   └── t_lora_pager.csv
├── platformio.ini           # Build environments
└── firmware.bin             # Pre-built binary (latest release)
```

---

## Getting started

### Prerequisites

- [PlatformIO](https://platformio.org/) (CLI or VS Code extension)
- For the simulator only: `brew install sdl2` (macOS)

### Build & flash

```bash
# Flash to a connected T-Lora Pager
pio run -e tlora_pager --target upload

# Monitor serial output
pio device monitor
```

### Desktop simulator (no hardware needed)

```bash
# Build
pio run -e tlora_pager_sim

# Run
.pio/build/tlora_pager_sim/program
```

The simulator compiles the LVGL UI screens natively using SDL2. Close the window or press `Ctrl-C` to quit.

---

## OTA updates

The firmware supports wireless updates via ArduinoOTA. Configure your WiFi credentials in `platformio.ini` under `[node_ota]`:

```ini
[node_ota]
build_flags =
    -D OTA_SSID=\"YourNetwork\"
    -D OTA_PASSWORD=\"YourPassword\"
```

Trigger an OTA session from the Mesh screen using the `cmd:update:` command. The device:
1. Stops ESP-NOW, disconnects from the mesh
2. Connects to WiFi and starts an ArduinoOTA server
3. Waits up to 2 minutes for a PlatformIO OTA upload
4. Reboots automatically when flashing completes (or on timeout)

Flash OTA from PlatformIO:

```bash
pio run -e tlora_pager --target upload --upload-port <device-ip>
```

---

## Navigation

| Control | Action |
|---|---|
| Rotary — turn | Move focus left / right through tiles |
| Rotary — press | Select focused tile / button |
| Keyboard — Esc / Backspace | Go back to the main menu |
| Power icon (top-right) | Enter deep sleep |
| Boot button or rotary press | Wake from sleep |

---

## Dependencies

| Library | Purpose |
|---|---|
| [LilyGoLib](https://github.com/Xinyuan-LilyGO/LilyGoLib) | Board HAL, display, power |
| [lvgl](https://lvgl.io/) v9.4 | GUI framework |
| [UniversalMesh](https://github.com/johestephan/UniversalMesh) | ESP-NOW mesh networking |
| [RadioLib](https://github.com/jgromes/RadioLib) v7.4 | LoRa / LR1121 / SX1262 driver |
| [XPowersLib](https://github.com/lewisxhe/XPowersLib) | PMU control |
| [SensorLib](https://github.com/lewisxhe/SensorLib) | IMU / environmental sensors |
| [TinyGPSPlus](https://github.com/mikalhart/TinyGPSPlus) | GNSS parsing |
| [NimBLE-Arduino](https://github.com/h2zero/NimBLE-Arduino) | BLE |
| ST25R3916-fork / NFC-RFAL-fork | NFC |
| ArduinoJson | JSON serialisation |
| PubSubClient | MQTT |
| Adafruit TCA8418 | Keyboard matrix controller |

---

## Node name

The node identifier is set at compile time via the `NODE_NAME` build flag (default: `um-pager`). Override it in `platformio.ini`:

```ini
build_flags =
    ...
    -D NODE_NAME=\"my-node\"
```

The name appears in the boot splash, the menu top bar, the Help screen, and is used as the ArduinoOTA hostname.

---

## License

See [LICENSE](LICENSE) if present, or contact the project maintainer.
