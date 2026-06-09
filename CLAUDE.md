# CLAUDE.md

Guidance for AI assistants (and humans) working in this repository.

## What this is

**DXSpotter** is standalone firmware for the **LilyGo T-Deck Plus** (ESP32-S3) that
displays ham-radio DX spots, solar/propagation data, HamAlert alerts, a band-activity
heatmap, and active DXpeditions — all over **WiFi only** (no BT/LoRa). Built with
Arduino + PlatformIO, rendered with LVGL 8.3 on a 320×240 ST7789 IPS panel.

- License: **GPL-3.0-or-later** (every source file carries an SPDX header).
- C++ namespace: **`dxs`** (UI lives in `dxs::ui`, config in `dxs::config`).

## Build / flash

```bash
pio run -e t-deck                 # build
pio run -e t-deck -t upload       # flash (USB CDC, 921600 baud)
pio device monitor                # serial @115200
```

> **Do NOT run `pio run` (or upload) automatically.** The maintainer builds and
> flashes on their own hardware. Make code changes and let them verify on-device.
> When you change code, say so plainly; don't claim it's "tested/working" — it's
> unbuilt until they flash it.

A `post:post_build.py` script copies the build output to
`firmware/dxspotter-t-deck-plus-<version>[-merged].bin`. The `firmware/` dir and all
`*.bin` are git-ignored. CI (`.github/workflows/platformio.yaml`) builds on push and
publishes those artifacts; tagging `v*` cuts a GitHub Release.

## Layout

```
src/
  main.cpp              setup(): Board → Config → UI → WiFi → feed tasks
  version.h             DXS_VERSION ("0.1.0")
  hardware/Board.*      T-Deck Plus pins, power rail, I2C, trackball, audio, backlight
  utils/Config.*        NVS (Preferences) persistence, namespace "dxspotter"
  utils/Log.h           DXS_LOG(tag, fmt, ...)
  net/                  data feeds (each runs its own FreeRTOS task)
  ui/                   one Screen* per screen + UIScreen (LVGL/TFT init + tick)
```

### Feeds (`src/net/`)
- `DXFeed` — HTTP DX spots: DXwatch (JSON), DXScape EU/WW & DXLite (HTML `<pre>`),
  Custom URL. Picks `WiFiClient` vs `WiFiClientSecure` by URL scheme; sends a browser
  User-Agent (the bot UA is blocked).
- `HolyClusterFeed` — `wss://holycluster.iarc.org/spots_ws` WebSocket; provides the
  display spot buffer **and** the band-map heatmap ring buffer.
- `DXpeditionFeed` — `https://holycluster.iarc.org/dxpeditions` (callsign + dates).
- `DXWorldFeed` — scrapes DX-World "Featured DXpeditions" (entity names + dates).
- `SolarFeed` — hamqsl.com XML, 10-min periodic.
- `HamAlertFeed` — persistent **telnet** client (hamalert.org:7300); streams the
  user's triggered "DX de …" lines. Auth: username + **telnet** password.
- `HamAlertApi` — HTTPS client that **manages HamAlert triggers** (list/add/delete)
  via the site's `/ajax/*` endpoints. Auth: username + **account/web** password.
  Async worker task; UI fires `requestList/Add/Delete` and polls `status()`.

### Screens (`src/ui/`) — 3×2 launcher grid
DX Spots · Solar · HamAlert · Band Map · Settings · DXpeditions.

## Config (NVS)

`dxs::Config` (in `utils/Config.*`) persists via `Preferences`, namespace `"dxspotter"`
(fully separate from any OpenMeshOS namespaces — no collision). Two distinct HamAlert
secrets:
- `hamAlertKey` (NVS `haKey`) — **telnet** password, used by `HamAlertFeed`.
- `hamAlertPass` (NVS `haPass`) — **account/web** password, used by `HamAlertApi`.

Every config change calls `config::save()`. Add a field by editing the struct,
`_setDefaults`, `init()` (load), `save()` (store), and a `setX()` setter.

## Conventions & gotchas

- **SPDX header** on every new source file (copyright + GPL-3.0-or-later), matching
  the existing files.
- **LVGL deletes:** when closing/replacing an object from inside its own event
  callback, use `lv_obj_del_async` (immediate `lv_obj_del` causes use-after-free /
  LoadProhibited). Defer page rebuilds from gesture callbacks via `lv_async_call`.
- **Stack pressure:** large per-row arrays in `_rebuildList()` are declared `static`
  — the LVGL tick runs on the Arduino loop task and the spot/alert buffers blow an
  8 KB stack. (`platformio.ini` already bumps the loop stack to 24 KB.)
- **HTTPS:** `WiFiClientSecure ssl; ssl.setInsecure(); http.begin(ssl, url);` and send
  a browser User-Agent. Network I/O runs on background tasks, never the LVGL tick —
  UI fires an async request and polls a status flag (see `HamAlertApi`/`_haPoll`).
- **No screensaver** — removed permanently (it caused lockups); display stays at the
  configured brightness. Beep/popup alerts replace it.
- **Threading:** each feed owns a FreeRTOS task and a mutex; the UI copies snapshots
  out under the mutex (`getAlerts`, `getDXpeditions`, `getTriggers`).
