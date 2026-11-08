# ESPHome Bambu Spool Reader

An ESPHome-based NFC spool reader for Bambu Lab filaments, running on a **Waveshare ESP32-S3 with a 2.8" touch screen**. It reads the NFC tag embedded in Bambu Lab spools and displays the filament information on the screen. It also integrates with [BambuBuddy](https://github.com/piitaya/bambuddy) to track spool inventory and remaining filament weight.

> Special thanks to [@piitaya](https://github.com/piitaya) for his foundational work on the NFC tag decoding and spool matching logic that this project builds upon.

---

## Features

- Reads Bambu Lab NFC spool tags via a PN532 NFC reader (SPI)
- Displays filament type, color, subtype, temperatures, and weight on the touch screen
- LVGL-powered UI with idle, filament info, and detail pages
- Syncs with BambuBuddy: looks up spools by tag UID and shows remaining weight
- Can add unknown spools to BambuBuddy directly from the device
- Exposes all filament sensors to Home Assistant via the ESPHome API
- Backlight control via Home Assistant
- All the screens are in French, sorry

---

## Hardware

| Component | Details |
|-----------|---------|
| Microcontroller | Waveshare ESP32-S3 1.69" / 2.8" Touch (ST7789V display, CST328 touch) |
| Display | 240×320 ST7789V via SPI |
| Touch | CST328 via I2C |
| NFC Reader | PN532 module (SPI) |

### Pin Mapping

#### Display SPI
| Signal | GPIO |
|--------|------|
| CLK | GPIO40 |
| MOSI | GPIO45 |
| CS | GPIO42 |
| DC | GPIO41 |
| RST | GPIO39 |
| Backlight | GPIO5 |

#### Touch I2C
| Signal | GPIO |
|--------|------|
| SDA | GPIO1 |
| SCL | GPIO3 |
| INT | GPIO4 |
| RST | GPIO2 |

#### NFC SPI
| Signal | GPIO |
|--------|------|
| CLK | GPIO18 |
| MOSI | GPIO15 |
| MISO | GPIO44 |
| CS | GPIO43 |

### PN532 NFC Reader — Jumper Configuration

The PN532 module must be set to **SPI mode** using its two DIP switches:

| Switch | Position |
|--------|----------|
| 1 | **ON** |
| 2 | **KE** (OFF) |

This selects the SPI interface on the PN532 module. Make sure to set these before powering the board.

---

## Software Setup

### ESP32-C3 with SSD1306 and two buttons

[`esphome.yaml`](esphome.yaml) is the configuration for an ESP32-C3, a
128x64 SSD1306 I2C display, and the existing SPI NFC reader. It uses the local
`components/bambu_nfc` component and does not require LVGL or the CST328 touch
component. The entire OLED workflow is in `esphome.yaml`; no YAML packages
are required. Keep only the `components` folder and your local `secrets.yaml`
next to the YAML.
The local `bambuddy_oled` component performs HTTP/HTTPS requests on a separate
FreeRTOS task; display, NFC and button processing remain on ESPHome's main loop.
The original touchscreen packages remain unchanged.

Connect button 1 to **GPIO0 and GND**, and button 2 to **GPIO1 and GND**.
Both inputs use internal pull-ups and 20 ms debouncing. Change
`button_1_pin` / `button_2_pin` in the substitutions if needed.

Add these entries to your local `secrets.yaml`:

```yaml
wifi_ssid: "YourWiFiSSID"
wifi_password: "YourWiFiPassword"
spool_reader_api_encryption_key: "your_32_byte_base64_key"
bambuddy_url: "https://your-bambuddy-instance"
```

For authenticated Bambuddy installations, also add `bambuddy_api_key` to
the secrets and change the `bambuddy_api_key` substitution from `""` to
`!secret bambuddy_api_key`. The key needs inventory read and update permissions.
HTTPS certificate validation stays enabled.
The existing `api.encryption.key: ""` placeholder is left unchanged. To
validate/flash with current ESPHome, supply a valid 32-byte Base64 key there,
for example via `!secret spool_reader_api_encryption_key`. Automated validation
uses a temporary random key only in the container's copy, never in your YAML.

This workflow targets **Bambuddy v1.3.0's built-in inventory**, not an external
Spoolman inventory:

| Screen | Button 1 (short) | Button 1 (at least 800 ms) |
|--------|------------------|--------------------------|
| Home | No action | No action |
| NFC scan | Look up spool in Bambuddy | No action |
| Not Found, create? | Create the scanned spool | No action |
| Bambuddy info | Load storage locations | No action |
| Storage locations | Select next location (wraps around) | Confirm selected location |
| Location confirmation | Save location and show returned spool info | No action |

Button 2 returns home and discards the scan, spool details, and location
selection on every screen. A tag still on the reader stays dismissed until it
is removed and presented again. A new successful scan opens the scan screen;
it does **not** automatically query Bambuddy.

Home shows `Spool-Reader`, the device IP, the last Bambuddy connection result,
and the total spool count (including archived spools). It refreshes when WiFi
connects, after 5 seconds, and every 60 seconds while home is open. Scan keeps the previous
UID/type/color/tray/date layout. Spool info shows the server's type, color,
remaining grams (`max(0, label_weight - weight_used)`), and storage location.
New spools use the NFC label weight rather than a fixed 1000 g; core weight
uses Bambuddy's default because NFC does not provide the empty core weight.
Archived spools can also be looked up, preventing accidental duplicate creation.

The location list begins with `(kein Lagerort)` (an empty value), initially
selects the current location, and scrolls through all entries in groups of
three. Inversion highlights the selection; `*` marks the saved location.
Long names scroll horizontally. The confirmation shows type, color, old
location, and new location. Selecting the empty entry sends
`{"location_id": null}` to clear the location.

HTTP/authentication/JSON/NFC errors are logged and shown on the display.
Only a lookup returning HTTP 404 offers creation. After an uncertain create
result (e.g. timeout), button 1 performs another lookup rather than blindly
repeating the POST. Inventory and location arrays are read one object at a
time, so the spool count is not limited by a full-response buffer. An
individual response object larger than 8 KiB produces an explicit error.
Button 2 works while a request runs: returning home or scanning a new tag
invalidates the previous request's result. A late response cannot reopen a
dismissed screen or update a different spool's details. Only one network
request runs at a time; starting another while the dismissed request is still
finishing shows an explicit message and can be retried. Network reads time out
after 10 seconds without received data. Button 2 cannot undo a write that the
server has already accepted; the home count is refreshed after a dismissed
request completes.

Docker validation (Windows CLI, WSL engine):

```powershell
docker run --rm --mount "type=bind,source=/mnt/d/src/esphome-bambuddy-reader,target=/config" ghcr.io/esphome/esphome:latest config esphome.yaml
docker run --rm --mount "type=bind,source=/mnt/d/src/esphome-bambuddy-reader,target=/config" ghcr.io/esphome/esphome:latest compile esphome.yaml
docker run --rm --mount "type=bind,source=/mnt/d/src/esphome-bambuddy-reader,target=/src,readonly" gcc:14 sh -c "g++ -std=c++17 -Wall -Wextra -Werror /src/tests/oled_state_test.cpp -o /tmp/oled-tests && /tmp/oled-tests"
docker run --rm --mount "type=bind,source=/mnt/d/src/esphome-bambuddy-reader,target=/config,readonly" --entrypoint python ghcr.io/esphome/esphome:latest /config/tests/run_bambuddy_client_tests.py
```

Replace the mount source with your checkout's WSL path. These commands
require local secrets; never commit `secrets.yaml`.
The client tests compile the actual component against mock ESP-IDF/FreeRTOS
interfaces and the ArduinoJson dependency from the preceding firmware build.
They cover non-blocking request start, dismissed/stale replies, buffered
responses, remaining weight, large inventories, error handling and JSON
GET/POST/PATCH behavior. Mock writes never contact a real Bambuddy instance.

### Prerequisites

- [ESPHome](https://esphome.io/) installed (CLI or Home Assistant add-on)
- A running [BambuBuddy](https://github.com/piitaya/bambuddy) instance (optional — the reader works standalone without it)

### Secrets

Create a `secrets.yaml` file alongside `spool-reader.yaml` with the following keys:

```yaml
wifi_ssid: "YourWiFiSSID"
wifi_password: "YourWiFiPassword"

spool_reader_api_encryption_key: "your_32_byte_base64_key"
spool_reader_ota_password: "your_ota_password"
spool_reader_ap_password: "your_ap_password"

# Optional — BambuBuddy integration
bambuddy_url: "http://your-bambuddy-instance:port"
bambuddy_api_key: "your_bambuddy_api_key"
```

If BambuBuddy is not configured, set `bambuddy_url` to any non-HTTP value (e.g. `disabled`) and the sync icon will remain grey.

### Configuration

Copy `spool-reader.yaml` as your starting point — it already includes all the package references and settings for the Waveshare ESP32-S3:

```yaml
substitutions:
  bambuddy_url: !secret bambuddy_url
  bambuddy_api_key: !secret bambuddy_api_key

esphome:
  name: spool-reader
  friendly_name: spool-reader

esp32:
  board: esp32-s3-devkitc-1
  flash_size: 16MB
  framework:
    type: esp-idf

psram:
  mode: octal
  speed: 80MHz

external_components:
  - source:
      type: git
      url: https://github.com/BluetriX/esphome-CST328-Touch
    components: [cst328]

logger:

api:
  encryption:
    key: !secret spool_reader_api_encryption_key

ota:
  - platform: esphome
    password: !secret spool_reader_ota_password

wifi:
  ssid: !secret wifi_ssid
  password: !secret wifi_password
  ap:
    ssid: "Spool-Reader Fallback Hotspot"
    password: !secret spool_reader_ap_password

captive_portal:

packages:
  display: !include spool-reader/hw-display.yaml
  fonts: !include spool-reader/ui-fonts.yaml
  nfc: !include spool-reader/hw-nfc.yaml
  bambuddy: !include spool-reader/api-bambuddy.yaml
  styles: !include spool-reader/ui-styles.yaml
  page_idle: !include spool-reader/page-idle.yaml
  page_filament: !include spool-reader/page-filament.yaml
  page_details: !include spool-reader/page-details.yaml
  triggers: !include spool-reader/pages-triggers.yaml
```

### Flashing

```bash
esphome run spool-reader.yaml
```

---

## Custom Component: `bambu_nfc`

The `components/bambu_nfc/` directory contains the custom ESPHome component that handles:

- SPI communication with the PN532
- Deriving the Bambu Lab NFC authentication keys from the tag UID
- Decoding the proprietary Bambu Lab tag data format
- Resolving color hex codes to human-readable color names
- Looking up material density by filament type

### ESPHome 2026 API Compatibility

The component uses `std::span` for writes and `pn532::MifareReadData` for
Mifare block buffers. SPI reads use the fixed-capacity `pn532::PN532Frame`
buffer, preserving the leading status byte and response checksum validation.

The custom loop no longer dispatches the inherited PN532 `on_tag`,
`on_tag_removed`, or NFC binary sensor callbacks. The Bambu-specific
success and error triggers remain unchanged.

### Keeping the Bambu Database Up to Date

Color names and densities are sourced from [SpoolmanDB](https://github.com/Donkie/SpoolmanDB/blob/main/filaments/bambulab.json). If a scan shows a raw hex code instead of a color name, regenerate the lookup tables:

```bash
cd /homeassistant/esphome/components/bambu_nfc
python3 generate_bambu_db.py
```

This regenerates `bambu_colors.h` and `bambu_densities.h`. Then recompile and reflash the firmware.

---

## Project Structure

```
esphome.yaml              # ESP32-C3 / SSD1306 / two-button configuration
spool-reader.yaml          # Main ESPHome config
spool-reader/
  hw-display.yaml          # Display & touch hardware config
  hw-nfc.yaml              # NFC hardware & sensor config
  api-bambuddy.yaml        # BambuBuddy HTTP integration
  ui-fonts.yaml            # LVGL font declarations
  ui-styles.yaml           # LVGL styles
  page-idle.yaml           # Idle screen (waiting for tag)
  page-filament.yaml       # Filament info screen
  page-details.yaml        # Detailed filament data screen
  pages-triggers.yaml      # Page navigation triggers
components/
  bambu_nfc/               # Custom ESPHome NFC component
  bambuddy_oled/           # ESP-IDF HTTP worker and OLED state
tests/                    # Navigation, streaming and mock client tests
```

---

## Credits

- [@piitaya](https://github.com/piitaya) — foundational work on Bambu Lab NFC tag decoding and spool matching, base for the UI (nearly everything)
- [BluetriX/esphome-CST328-Touch](https://github.com/BluetriX/esphome-CST328-Touch) — CST328 touchscreen ESPHome component
- [SpoolmanDB](https://github.com/Donkie/SpoolmanDB) — Bambu Lab filament database

---

## License

MIT
