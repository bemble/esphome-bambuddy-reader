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
- Optional audio feedback: a chime on a successful scan and a buzz on a failed read, via the board's onboard PCM5101 I²S DAC
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

#### Audio I²S (PCM5101, optional)
The Waveshare 2.8" board has an onboard PCM5101 I²S DAC + amplifier feeding a small speaker connector. Used only if you enable the optional audio package (see below).

| Signal | GPIO |
|--------|------|
| BCLK | GPIO48 |
| LRCLK (WS) | GPIO38 |
| DOUT | GPIO47 |

### PN532 NFC Reader — Jumper Configuration

The PN532 module must be set to **SPI mode** using its two DIP switches:

| Switch | Position |
|--------|----------|
| 1 | **ON** |
| 2 | **KE** (OFF) |

This selects the SPI interface on the PN532 module. Make sure to set these before powering the board.

---

## Software Setup

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

### Audio (optional)

The Waveshare 2.8" board includes a PCM5101 I²S DAC, an amplifier, and a small speaker connector. To get a chime on a successful scan and a buzz on a failed read, add one line to the `packages:` block above — anywhere **before** `triggers:`:

```yaml
packages:
  # ...
  audio: !include spool-reader/hw-audio.yaml
  triggers: !include spool-reader/pages-triggers.yaml
```

That's the only change required. `hw-audio.yaml` is fully self-contained: it sets up the I²S DAC, speaker, and an [`rtttl`](https://esphome.io/components/rtttl.html) tone player, and it hooks its own actions onto the existing `on_bambu_success` / `on_bambu_error` triggers (ESPHome merges these onto the core ones — including the package before `triggers:` makes the tones play first). If you don't add the line, the firmware builds and behaves exactly as before.

Retune the two ringtones by overriding their substitutions in your root config:

```yaml
substitutions:
  sound_good: "good:d=16,o=6,b=170:c,e,g"   # rising arpeggio
  sound_bad: "bad:d=8,o=6,b=140:f,d,f,d"    # descending buzz
```

> Tip: keep tones in octave 6+ — small speakers can't reproduce low frequencies, so an octave-4/5 tone will sound very quiet.

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
spool-reader.yaml          # Main ESPHome config
spool-reader/
  hw-display.yaml          # Display & touch hardware config
  hw-nfc.yaml              # NFC hardware & sensor config
  hw-audio.yaml            # Optional PCM5101 I2S audio + scan tones
  api-bambuddy.yaml        # BambuBuddy HTTP integration
  ui-fonts.yaml            # LVGL font declarations
  ui-styles.yaml           # LVGL styles
  page-idle.yaml           # Idle screen (waiting for tag)
  page-filament.yaml       # Filament info screen
  page-details.yaml        # Detailed filament data screen
  pages-triggers.yaml      # Page navigation triggers
components/
  bambu_nfc/               # Custom ESPHome NFC component
```

---

## Credits

- [@piitaya](https://github.com/piitaya) — foundational work on Bambu Lab NFC tag decoding and spool matching, base for the UI (nearly everything)
- [BluetriX/esphome-CST328-Touch](https://github.com/BluetriX/esphome-CST328-Touch) — CST328 touchscreen ESPHome component
- [SpoolmanDB](https://github.com/Donkie/SpoolmanDB) — Bambu Lab filament database

---

## License

MIT
