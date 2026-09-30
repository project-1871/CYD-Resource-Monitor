# 📊 CYD Resource Monitor

**English** · [ภาษาไทย](README.th.md)

> **This is a fork of [moomdate/CYD-Resource-Monitor](https://github.com/moomdate/CYD-Resource-Monitor).** It adds a **Linux agent**, a **quick-launch button page** (tap the CYD to open apps on your PC), a **hello / goodbye screen** when the PC starts and shuts down, and **four extra themes** including the CRT-scanline `REDROOT` and `MONOROOT`. Everything from the original still works on Windows and macOS. See [What this fork adds](#what-this-fork-adds).

A PC/Mac hardware monitor on a **$6 ESP32 board with a 2.8" touch screen** (CYD "Cheap Yellow Display" family). One USB cable carries both power and data — no WiFi setup, no extra wiring, no drivers to install.

![PlatformIO](https://img.shields.io/badge/PlatformIO-ESP32-orange)
![Agent](https://img.shields.io/badge/agent-Windows%20%7C%20macOS%20%7C%20Linux-blue)
![License](https://img.shields.io/badge/license-MIT-green)

<!-- Screenshots: drop photos into images/ with these names -->
![Dashboard](images/dashboard.jpg)

| | |
|---|---|
| ![Detail](images/detail.jpg) | ![Settings](images/settings.jpg) |
| Tap any tile → 60-second history graph with MIN / AVG / MAX | Pick tiles, theme, and RGB bar — saved to flash |

## Features

- **Dashboard** — CPU / GPU / RAM / TEMP / DISK / NET tiles with donut gauges, area-filled sparklines, and a layout that auto-adjusts to however many tiles you enable (1–6)
- **8 themes**, switchable on-device: `CYBER` (neon on black), `SYNTHWAVE` (purple/pink/orange), `MATRIX` (all green), `LIGHT`, plus this fork's `MONO`, `JAPGLITCH`, `REDROOT` and `MONOROOT`
- **Quick-launch page** — swipe left for six buttons that open apps, sites or folders on your PC (fork addition)
- **Animated RGB bar** under the header, motherboard-style — and yes, you can turn it off in Settings
- Tiles pulse a **red warning border** past 85% load
- **Detail pages** — tap a tile for a 60-second graph, MIN/AVG/MAX, GPU name, dGPU/iGPU flag, VRAM
- **Discrete GPU detection** — the agent reports every GPU it finds, discrete cards first
- All settings persist in NVS flash across power cycles

## What this fork adds

### Linux agent

`agent/monitor_agent.py` now runs on Linux too, with no extra packages beyond `requirements.txt`:

| Data | Source |
|---|---|
| CPU temp | `k10temp` (AMD, `Tctl`) or `coretemp` (Intel) via psutil |
| AMD GPU load / temp / VRAM / name | amdgpu sysfs (`gpu_busy_percent`, hwmon, `mem_info_vram_*`); name from `lspci` |
| NVIDIA GPU | `pip install pynvml`, same as on Windows |
| Network rate | physical NICs only; loopback, Docker, veth and VPN tunnels are skipped |

Your user needs permission to open the serial port. On most distros that means being in the `uucp` (Arch) or `dialout` (Debian/Ubuntu) group; log out and back in after adding it.

### Quick-launch page

Swipe **left** on the dashboard (or tap the page button in the header) to get a 3×2 grid of buttons. Tapping one sends `L <n>` back over the same USB cable, and the agent launches whatever you configured. Swipe right to go back.

The buttons live in `~/.config/cyd-monitor/keys.json` on the PC (`%USERPROFILE%\.config\cyd-monitor\keys.json` on Windows). The agent creates it with defaults on first run and **re-reads it on every press**, so edits apply instantly: no reflash, no agent restart.

```json
[
  { "label": "STEAM",   "icon": "steam",   "url": "steam://open/main" },
  { "label": "YOUTUBE", "icon": "youtube", "cmd": ["firefox", "--new-tab", "https://www.youtube.com"] },
  { "label": "FILES",   "icon": "folder",  "url": "~" }
]
```

| Field | Meaning |
|---|---|
| `label` | text under the button (up to 13 characters) |
| `icon` | one of the built-in icons below; an unknown name shows the label's first letter |
| `url` | a link, protocol URL or folder, opened with the system's default handler (`xdg-open` / `open` / Windows shell) |
| `cmd` | a command instead of a `url`: a list of arguments (recommended) or a shell string |

Up to six entries are used. A fuller example is in [`agent/keys.example.json`](agent/keys.example.json). On Linux desktops that use `uwsm` (e.g. Hyprland setups), launched apps are wrapped in `uwsm-app` so they keep running when the agent restarts.

**Built-in icons:** `steam` `web` `firefox` `chrome` `email` `discord` `youtube` `folder` `terminal` `music` `gamepad` `spotify` `settings` `chat` `camera` `power` `star` `twitch` `whatsapp` `robot`

To add an icon, add a `(name, codepoint)` pair to `ICONS` in [`tools/make_icons.py`](tools/make_icons.py) (codepoints are from [JetBrainsMono Nerd Font](https://www.nerdfonts.com/cheat-sheet)), run it to regenerate `src/icons.h`, and reflash. The script needs Pillow and the font installed; edit `FONT` at the top if yours lives elsewhere.

### Hello / goodbye screen

When the agent starts (e.g. at login) the CYD types out **HELLO** full-screen in the current theme with "<hostname> is online", then returns to the dashboard. When the agent is stopped (shutdown, logout, `systemctl --user stop`, Ctrl+C) it sends a **GOODBYE** that stays up until data comes back. If your board cuts USB power at shutdown, the goodbye only shows briefly.

### Page button

Every page has one big **next page** button in its header (the dots inside show where you are). Each tap moves on: **dashboard → quick launch → boombox → radio → dashboard**. Swiping left/right still works too.

### Boombox page (Linux)

A boombox for whatever is playing on the PC: two speakers that pump while music plays, an LCD with title, artist, equalizer and progress, big **VOL− ⏮ ⏯ ⏭ VOL+** buttons, and the player's track list underneath (tap a song to jump to it). The agent reads any [MPRIS](https://specifications.freedesktop.org/mpris-spec/latest/) player over D-Bus (it prefers the one that's playing, then VLC), so it needs `pip install jeepney`. Volume buttons change the system volume (`wpctl`). The track list needs a player with the MPRIS TrackList interface, such as VLC (enable **Tools → Preferences → Interface → Main interfaces → D-Bus**, or `dbus=1` in `vlcrc`). If nothing is playing, ⏯ opens `~/Music/All My Music.m3u` in VLC on shuffle.

### Radio page (Linux + Omarchy)

Drives the [Radio Atlas](https://github.com/AksharP5/omarchy-radio-atlas) Omarchy plugin: a tuner dial whose needle tracks your place in the station list, the station and song on air, **⏮ ⏯ ⏭**, a die for a random station, radio volume, genre buttons (**ROCK JAZZ HIPHOP LOFI NEWS RECENT**) that load 20 stations each, and a station list you swipe up/down and tap to play. The agent calls the plugin's own `radio-player` / `radio-fetch` scripts; the page shows "Radio Atlas not found" without it.

### Talking face (optional)

If something sends JSON datagrams to UDP `127.0.0.1:47811` (`{"talk":1}`, `{"m":0.0-1.0}` mouth openness, `{"w":"word"}` captions, `{"talk":0}`), the agent relays them and the CYD shows a full-screen talking face lip-synced to it, with the words captioned underneath. The author feeds it from a local text-to-speech voice. The face art isn't included: put a black-and-white line drawing at `tools/art/catgirl.png` and run `tools/make_catgirl.py` (edit the mouth/eye coordinates at the top for your drawing). Without it the mouth animates on a blank page. `tools/face_preview.cpp` renders frames on a PC.

### Extra themes

Pick them on the device: **gear → THEME arrows → SAVE**.

| Theme | Look |
|---|---|
| `MONO` | pure black and white, grey gauge tracks |
| `JAPGLITCH` | dark purple with pink/cyan/orange accents and random row-tearing glitch bursts |
| `REDROOT` | retro hacker terminal: red and white on black, **CRT scanlines** with a rolling bright band, and red/white glitch tears |
| `MONOROOT` | `REDROOT` in black and white: same scanlines, roll band and glitch tears, greyscale palette |

Themes are one line each in `src/ui.h`. Besides the base colors, a theme can set three optional extras: a gauge `track` color, its own 6-color `strip` for the animated bar, and the `glitch` and `scan` effect flags. Colors are RGB565, but the frame buffer is 8-bit (`RRRGGGBB`), so stick to values that survive that; near-greys in particular drift toward blue or green.

## Architecture

```
┌─────────────┐  JSON lines @ 2 Hz   ┌──────────────┐
│   PC / Mac   │ ──── USB serial ────▶│  CYD display  │
│ monitor_agent│      115200 baud     │   firmware    │
└─────────────┘                      └──────────────┘
```

The link is two-way: quick-launch taps travel back from the CYD as `L <n>` lines. The protocol is transport-agnostic line-delimited JSON — the same lines could ride a WebSocket later without changing either side. No custom drivers: the board's CH340 USB-serial chip is supported out of the box on macOS (Big Sur+) and Windows 10/11.

## The board

This runs on a 2.8" ESP32 display board of the **CYD family** — the pinout matches the widely documented `ESP32-2432S028R`, which is what the config in `platformio.ini` is built around.

**👉 The exact board used here: [Shopee listing](https://s.shopee.co.th/30mqperiSK)** (USB-C, LCD panel marked `TPM408-2.8`)

| Part | Detail |
|---|---|
| MCU | ESP32-WROOM-32, dual-core 240 MHz, 4 MB flash, no PSRAM |
| Display | 2.8" ILI9341, 320×240, SPI @ 65 MHz |
| Touch | XPT2046 resistive (bit-banged: CLK 25, DIN 32, DOUT 39, CS 33) |
| USB-serial | CH340 — built-in OS support, no driver install |

> ⚠️ CYD units vary. If colors look inverted, remove `-D TFT_INVERSION_ON=1` from `platformio.ini`. If touches land in the wrong place, update the `touch.setCal(...)` values in `src/main.cpp`.

## Getting started

### 1. Flash the firmware

```bash
git clone https://github.com/project-1871/CYD-Resource-Monitor.git
cd CYD-Resource-Monitor
pio run -t upload
```

### 2. Run the agent on the computer you want to monitor

Grab a prebuilt Windows or macOS binary from [Releases](https://github.com/project-1871/CYD-Resource-Monitor/releases) (`cyd-monitor-agent-windows.exe` or `cyd-monitor-agent-macos`), or run from source (the only option on Linux):

```bash
cd agent
python -m venv .venv && . .venv/bin/activate    # Windows: .venv\Scripts\activate
pip install -r requirements.txt
python monitor_agent.py          # auto-detects the board's port
```

> macOS note: the release binary is unsigned — right-click → Open the first time.

| Option | Use when |
|---|---|
| `--list` | show available serial ports |
| `--port COM5` | pick a port manually |
| `--print` | dry run: print JSON to stdout, no board needed |
| `--interval 1` | slow down updates (default 0.5 s) |

### 3. Unlock more sensors (optional)

| Data | What to do |
|---|---|
| NVIDIA GPU load / temp / VRAM | `pip install pynvml` |
| Windows: CPU temp + AMD/Intel GPU | run [LibreHardwareMonitor](https://github.com/LibreHardwareMonitor/LibreHardwareMonitor) with Options → Remote Web Server on (agent auto-connects to `localhost:8085`) |
| macOS: CPU temp | `brew install smctemp` |
| Linux: CPU temp + AMD GPU | nothing: read straight from sysfs |

The agent degrades gracefully — anything unavailable shows as `n/a` on the display.

## Daily use & auto-start

### Windows

The `.exe` is **portable — no installer**. Put it anywhere, plug in the board, double-click, done. A console window opens and shows `connected to COM5`; the display starts updating immediately.

- **First run only**: SmartScreen will warn because the binary is unsigned — click *More info → Run anyway*.
- **No configuration needed**: the agent finds the board's COM port (CH340) by itself, and NVIDIA support is bundled in. Only CPU temp / AMD / Intel GPU stats need LibreHardwareMonitor running alongside (see the table above).
- **After a reboot the agent does NOT start by itself** — the display shows *WAITING FOR PC* until you run it again. To make it automatic:

| Method | Steps | Result |
|---|---|---|
| **Startup folder** (easiest) | `Win + R` → type `shell:startup` → Enter → right-drag the `.exe` in → *Create shortcut here* | runs at every login, console window stays open (minimize it) |
| **Task Scheduler** (cleaner) | create a task, trigger *At log on*, action = the `.exe`, tick *Hidden* | runs silently in the background, no window |

If you use LibreHardwareMonitor for temperatures, enable its own *Run On Windows Startup* option too.

### macOS

Run `./cyd-monitor-agent-macos` (first time: right-click → Open, because it's unsigned). To start it at login: *System Settings → General → Login Items → +* and pick the binary.

### Linux

Run it as a systemd user service so it starts at login and restarts if it ever crashes. A ready-made unit is in [`agent/cyd-monitor.service`](agent/cyd-monitor.service):

```bash
cp agent/cyd-monitor.service ~/.config/systemd/user/
# edit ExecStart if your clone or venv is somewhere other than ~/CYD-Resource-Monitor/agent/.venv
systemctl --user daemon-reload
systemctl --user enable --now cyd-monitor
journalctl --user -u cyd-monitor -f      # should say "connected to /dev/ttyUSB0"
```

Stop the service before flashing (`systemctl --user stop cyd-monitor`), because it holds the serial port.

### Good to know

The agent has a built-in reconnect loop — unplugging the board, replugging it, or rebooting the display never requires restarting the agent. It just reconnects.

> ⚠️ **macOS limitation**: Apple locks GPU-load counters behind sudo-only APIs on Apple Silicon, so GPU load shows `n/a` on Macs. Windows gets the full picture with LibreHardwareMonitor running.

## Releases (CI)

Pushing a `v*` tag builds and attaches everything automatically:

```bash
git tag v1.0.0 && git push origin v1.0.0
```

| Artifact | Built on |
|---|---|
| `cyd-monitor-agent-windows.exe` | windows-latest (PyInstaller, no Python needed) |
| `cyd-monitor-agent-macos` | macos-latest |
| `cyd-resource-monitor-firmware.bin` | ubuntu-latest (PlatformIO) |

## Project structure

```
agent/
├── monitor_agent.py   # Python agent (Windows + macOS + Linux)
├── keys.example.json  # sample quick-launch buttons
└── cyd-monitor.service # Linux systemd user unit
tools/
├── make_icons.py      # renders Nerd Font glyphs into src/icons.h
├── make_catgirl.py    # bakes your line drawing into src/catgirl_art.h (not in the repo)
└── face_preview.cpp   # renders talking-face frames on a PC
src/
├── config.h           # pins
├── ui.h               # sprite renderer, themes, touch, widgets
├── data.h             # JSON parse, history rings, settings NVS
├── dash.h             # dashboard grid
├── detail.h           # per-metric graph page
├── settings.h         # theme / RGB / tile picker
├── keys.h             # quick-launch page
├── boom.h             # boombox page (MPRIS media player)
├── radio.h            # radio page (Radio Atlas)
├── mouth.h            # talking-face mode: state, captions
├── face.h             # talking face renderer (cat girl line art + live mouth)
├── face_popart.h      # older pop-art lips face (not built; kept for reference)
├── icons.h            # 48×48 1-bit button icons (generated)
└── main.cpp           # loop + screen switching
```

## License

[MIT](LICENSE) — do whatever you like; a link back is appreciated.

Original project by [moomdate](https://github.com/moomdate/CYD-Resource-Monitor). Linux support, quick-launch page and the `MONO` / `JAPGLITCH` / `REDROOT` / `MONOROOT` themes by [project-1871](https://github.com/project-1871). Button icons are rendered from [Nerd Fonts](https://www.nerdfonts.com/) glyphs (MIT / OFL).
