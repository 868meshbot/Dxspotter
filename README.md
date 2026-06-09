# DXSpotter

Standalone ham-radio information firmware for the **LilyGo T-Deck Plus** (ESP32-S3).
DXSpotter turns the T-Deck into a pocket DX dashboard — live cluster spots, solar and
propagation conditions, HamAlert alerts, a band-activity heatmap, and current
DXpeditions — all over WiFi. No PC, no app, no LoRa.

> Status: early (v0.1.0). WiFi-only build for the T-Deck Plus.

## Features

- **DX Spots** — live spots from Holy Cluster (WebSocket), DXwatch, DXScape EU/WW,
  DXLite, or a custom URL. Frequencies are colour-coded by band; mode is taken from
  the feed or inferred from frequency via an IARU R1 band-plan table. Filter by band
  and mode; tap a row for full spot detail.
- **Solar / Propagation** — SFI, A/K indices and HF band conditions from hamqsl.com,
  colour-graded good/fair/poor.
- **HamAlert** — your triggered spots, streamed live over HamAlert's telnet feed.
  Manage your alert **triggers** right on the device: **Add** a callsign or **Delete**
  an existing trigger via HamAlert's web API.
- **Band Map** — two-page activity heatmap (HF 160–10 m; VHF/UHF 6 m–23 cm), fed by
  the live cluster stream.
- **DXpeditions** — two pages: Holy Cluster's schedule and a DX-World scrape (with
  entity/country names). Tap a DX-World entry to add that callsign to HamAlert with the
  operation's date range pre-filled as the alert comment.
- **Settings** — callsign, WiFi (with network scan), feed source, HamAlert
  credentials, brightness, keyboard backlight (with auto-night), volume, theme, and
  timezone. Persisted to flash (NVS).

## Hardware

- **LilyGo T-Deck Plus** — ESP32-S3, 320×240 ST7789 IPS display, BBQ10 keyboard,
  trackball, GT911 touch, 16 MB flash, 8 MB PSRAM.

## Build & flash

Requires [PlatformIO](https://platformio.org/).

```bash
pio run -e t-deck                 # build
pio run -e t-deck -t upload       # flash over USB
pio device monitor                # serial monitor @115200
```

Each build also writes named artifacts to `firmware/`:

- `dxspotter-t-deck-plus-<version>.bin` — app only, flash at `0x10000` (OTA / launcher).
- `dxspotter-t-deck-plus-<version>-merged.bin` — bootloader + table + app, flash at
  `0x0` (first-time flash / full recovery).

GitHub Actions builds on every push and uploads these as artifacts; pushing a `v*`
tag publishes a Release.

## First-run setup

1. Flash the firmware and power on.
2. Open **Settings** → set your **WiFi** (scan and pick a network, or enter manually).
3. Set your **Callsign** and choose a **DX Feed**.
4. For HamAlert:
   - **HamAlert User** — your hamalert.org username.
   - **HamAlert Telnet** — the *telnet* password you set in HamAlert (for the live feed).
   - **HamAlert Password** — your hamalert.org *account* password (needed only for
     Add/Delete trigger management).

## Project layout

```
src/hardware/   board / pins / peripherals
src/net/        WiFi + data feeds (each on its own task)
src/ui/         LVGL screens + UI core
src/utils/      config (NVS) + logging
platformio.ini  build config (env: t-deck)
post_build.py   emits named firmware/ binaries
```

See [CLAUDE.md](CLAUDE.md) for architecture details and contributor conventions.

## License

GPL-3.0-or-later. © 2026 DXSpotter Contributors.

DXSpotter is not affiliated with HamAlert, DX-World, Holy Cluster, DXwatch, DXScape,
or hamqsl.com; it consumes their public feeds. Respect each service's terms of use.
