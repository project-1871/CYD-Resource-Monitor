#!/usr/bin/env python3
"""CYD Resource Monitor agent.

Reads system stats and streams them as JSON lines over USB serial
to the ESP32 display. Works on Windows, macOS and Linux.

  python monitor_agent.py               # auto-detect port, 2 Hz
  python monitor_agent.py --list        # list serial ports
  python monitor_agent.py --port COM5   # explicit port
  python monitor_agent.py --print       # dry run: print JSON, no serial

Optional richer data:
  - NVIDIA GPUs:            pip install pynvml
  - Windows temps/AMD/Intel: run LibreHardwareMonitor with
    Options > Remote Web Server enabled (default http://localhost:8085)
  - macOS CPU temp:          install `smctemp` (brew install smctemp)
  - Linux: CPU temp (k10temp/coretemp) and AMD GPUs (amdgpu sysfs) work out of the box
"""

import argparse
import json
import os
import platform
import re
import shutil
import signal
import select
import socket
import subprocess
import sys
import functools
print = functools.partial(print, flush=True)
import time

import psutil

IS_WIN = platform.system() == "Windows"
IS_MAC = platform.system() == "Darwin"
IS_LINUX = platform.system() == "Linux"

# ── NVIDIA via pynvml (optional) ───────────────────────────────
try:
    import pynvml
    pynvml.nvmlInit()
    NVML = True
except Exception:
    NVML = False


def nvidia_gpus():
    gpus = []
    if not NVML:
        return gpus
    try:
        for i in range(pynvml.nvmlDeviceGetCount()):
            h = pynvml.nvmlDeviceGetHandleByIndex(i)
            name = pynvml.nvmlDeviceGetName(h)
            if isinstance(name, bytes):
                name = name.decode()
            util = pynvml.nvmlDeviceGetUtilizationRates(h)
            mem = pynvml.nvmlDeviceGetMemoryInfo(h)
            temp = pynvml.nvmlDeviceGetTemperature(h, pynvml.NVML_TEMPERATURE_GPU)
            gpus.append({
                "name": name[:24],
                "load": float(util.gpu),
                "temp": float(temp),
                "vram_used": round(mem.used / 2**30, 1),
                "vram_total": round(mem.total / 2**30, 1),
                "discrete": True,
            })
    except Exception:
        pass
    return gpus


# ── LibreHardwareMonitor web server (optional, Windows) ────────
def lhm_fetch(url):
    """Return flat list of (path, value, unit) from LHM's data.json tree."""
    import urllib.request
    try:
        with urllib.request.urlopen(url, timeout=1) as r:
            tree = json.load(r)
    except Exception:
        return None
    flat = []

    def walk(node, path):
        text = node.get("Text", "")
        val = node.get("Value", "")
        p = path + [text]
        if val:
            m = re.match(r"([\d.,]+)\s*(.*)", str(val))
            if m:
                try:
                    flat.append(("/".join(p), float(m.group(1).replace(",", ".")), m.group(2)))
                except ValueError:
                    pass
        for ch in node.get("Children", []):
            walk(ch, p)

    walk(tree, [])
    return flat


def lhm_extract(flat):
    """Pull CPU temp and non-NVIDIA GPU stats out of the LHM sensor list."""
    out = {"cpu_temp": None, "gpus": []}
    if not flat:
        return out
    # CPU package/core temperature
    for path, v, unit in flat:
        low = path.lower()
        if unit == "°C" and "cpu" in low and ("package" in low or "core (tctl" in low or "core average" in low):
            out["cpu_temp"] = v
            break
    if out["cpu_temp"] is None:
        cands = [v for p, v, u in flat if u == "°C" and "cpu" in p.lower()]
        if cands:
            out["cpu_temp"] = max(cands)
    # AMD / Intel GPU nodes (NVIDIA already handled by pynvml)
    gpu_names = set()
    for path, v, unit in flat:
        parts = path.split("/")
        for part in parts:
            low = part.lower()
            if ("gpu" in low or "radeon" in low or "graphics" in low) and len(part) > 6 and "nvidia" not in low:
                gpu_names.add(part)
    for name in gpu_names:
        g = {"name": name[:24], "discrete": not ("intel" in name.lower() and "uhd" in name.lower())}
        for path, v, unit in flat:
            if name not in path:
                continue
            low = path.lower()
            if unit == "%" and ("gpu core" in low or "d3d 3d" in low) and "load" not in g:
                g["load"] = v
            if unit == "°C" and "temp" not in g:
                g["temp"] = v
        out["gpus"].append(g)
    return out


# ── Linux helpers ──────────────────────────────────────────────
def _read(path):
    try:
        with open(path) as f:
            return f.read().strip()
    except OSError:
        return None


def linux_amd_cards():
    """amdgpu cards as (sysfs device dir, marketing name); boot VGA / most VRAM first."""
    import glob
    import os
    cards = []
    for dev in sorted(glob.glob("/sys/class/drm/card[0-9]*/device")):
        if _read(dev + "/gpu_busy_percent") is None or dev in [c[0] for c in cards]:
            continue
        slot = os.path.basename(os.path.realpath(dev))
        name = "AMD GPU"
        try:
            r = subprocess.run(["lspci", "-vmm", "-s", slot], capture_output=True, text=True, timeout=2)
            f = dict(l.split(":\t", 1) for l in r.stdout.splitlines() if ":\t" in l)
            name = f.get("SDevice") or re.sub(r".*\[(.+)\]", r"\1", f.get("Device", name))
        except Exception:
            pass
        vram = int(_read(dev + "/mem_info_vram_total") or 0)
        cards.append((dev, name, vram))
    cards.sort(key=lambda c: (_read(c[0] + "/boot_vga") != "1", -c[2]))
    return [(dev, name, vram > 1 << 30) for dev, name, vram in cards]


def linux_gpus(cards):
    import glob
    gpus = []
    for dev, name, discrete in cards:
        g = {"name": name[:24], "discrete": discrete}
        busy = _read(dev + "/gpu_busy_percent")
        if busy is not None:
            g["load"] = float(busy)
        for h in glob.glob(dev + "/hwmon/hwmon*/temp1_input"):  # temp1 = edge
            g["temp"] = int(_read(h)) / 1000
            break
        used, total = _read(dev + "/mem_info_vram_used"), _read(dev + "/mem_info_vram_total")
        if used and total:
            g["vram_used"] = round(int(used) / 2**30, 1)
            g["vram_total"] = round(int(total) / 2**30, 1)
        gpus.append(g)
    return gpus


def linux_cpu_temp():
    try:
        temps = psutil.sensors_temperatures()
    except Exception:
        return None
    for chip, pref in (("k10temp", ("Tctl", "Tdie")), ("coretemp", ("Package id 0",)), ("zenpower", ("Tdie", "Tctl"))):
        for s in temps.get(chip, []):
            if s.label in pref:
                return s.current
        if temps.get(chip):
            return temps[chip][0].current
    return None


NET_SKIP = ("lo", "docker", "veth", "br-", "virbr", "waydroid", "tun", "tailscale")


def net_counters():
    """Bytes (recv, sent) over physical NICs; on Linux skip loopback/container/VPN interfaces."""
    if not IS_LINUX:
        n = psutil.net_io_counters()
        return n.bytes_recv, n.bytes_sent
    per = psutil.net_io_counters(pernic=True)
    nics = [v for k, v in per.items() if not k.startswith(NET_SKIP)]
    return sum(v.bytes_recv for v in nics), sum(v.bytes_sent for v in nics)


# ── macOS helpers ──────────────────────────────────────────────
def mac_gpus():
    gpus = []
    try:
        r = subprocess.run(
            ["system_profiler", "-json", "SPDisplaysDataType"],
            capture_output=True, text=True, timeout=10)
        data = json.loads(r.stdout).get("SPDisplaysDataType", [])
        for d in data:
            name = d.get("sppci_model", "GPU")
            bus = str(d.get("sppci_bus", ""))
            gpus.append({
                "name": name[:24],
                "discrete": "pcie" in bus.lower() or "spdisplays_pcie" in bus.lower(),
            })
    except Exception:
        pass
    return gpus


def mac_cpu_temp():
    if not shutil.which("smctemp"):
        return None
    try:
        r = subprocess.run(["smctemp", "-c"], capture_output=True, text=True, timeout=2)
        return float(r.stdout.strip())
    except Exception:
        return None


def host_name():
    name = socket.gethostname().split(".")[0]
    if name and not name.isdigit():
        return name[:20]
    if IS_MAC:  # hostname can be an IP on some networks; ask macOS directly
        try:
            r = subprocess.run(["scutil", "--get", "ComputerName"],
                               capture_output=True, text=True, timeout=2)
            if r.stdout.strip():
                return r.stdout.strip()[:20]
        except Exception:
            pass
    return "Mac" if IS_MAC else "PC"


# ── payload assembly ───────────────────────────────────────────
class Sampler:
    def __init__(self, lhm_url):
        self.lhm_url = lhm_url
        self.prev_disk = psutil.disk_io_counters()
        self.prev_net = net_counters()
        self.prev_t = time.time()
        self.static_mac_gpus = mac_gpus() if IS_MAC else []
        self.linux_cards = linux_amd_cards() if IS_LINUX else []
        psutil.cpu_percent()  # prime

    def sample(self):
        now = time.time()
        dt = max(0.1, now - self.prev_t)
        self.prev_t = now

        vm = psutil.virtual_memory()
        freq = psutil.cpu_freq()
        du = psutil.disk_usage("C:\\" if IS_WIN else "/")

        dio = psutil.disk_io_counters()
        nio = net_counters()
        disk_r = (dio.read_bytes - self.prev_disk.read_bytes) / dt / 2**20
        disk_w = (dio.write_bytes - self.prev_disk.write_bytes) / dt / 2**20
        net_dl = (nio[0] - self.prev_net[0]) / dt / 2**20
        net_ul = (nio[1] - self.prev_net[1]) / dt / 2**20
        self.prev_disk, self.prev_net = dio, nio

        cpu_temp = None
        gpus = nvidia_gpus()

        if IS_WIN and self.lhm_url:
            lhm = lhm_extract(lhm_fetch(self.lhm_url))
            cpu_temp = lhm["cpu_temp"]
            gpus += lhm["gpus"]
        if IS_MAC:
            cpu_temp = mac_cpu_temp()
            if not gpus:
                gpus = list(self.static_mac_gpus)
        if IS_LINUX:
            cpu_temp = linux_cpu_temp()
            gpus += linux_gpus(self.linux_cards)

        gpus.sort(key=lambda g: not g.get("discrete", False))  # discrete first

        payload = {
            "cpu": {
                "load": round(psutil.cpu_percent(), 1),
                "freq": round(freq.current, 0) if freq else 0,
                "cores": psutil.cpu_count(logical=True),
            },
            "ram": {
                "pct": round(vm.percent, 1),
                "used": round(vm.used / 2**30, 1),
                "total": round(vm.total / 2**30, 1),
            },
            "gpus": gpus[:2],
            "disk": {"pct": round(du.percent, 1), "r": round(disk_r, 1), "w": round(disk_w, 1)},
            "net": {"dl": round(net_dl, 2), "ul": round(net_ul, 2)},
            "host": {"name": host_name(),
                     "os": "win" if IS_WIN else ("mac" if IS_MAC else "linux")},
        }
        if cpu_temp is not None:
            payload["cpu"]["temp"] = round(cpu_temp, 1)
        return payload


# ── quick-launch keys ─────────────────────────────────────────
# The CYD's second page (swipe left) has six buttons; a tap sends "L <n>".
# Labels + commands live in keys.json so they can change without a reflash.
KEYS_FILE = os.path.join(os.path.expanduser("~/.config/cyd-monitor"), "keys.json")
DEFAULT_KEYS = [
    {"label": "STEAM",   "icon": "steam",   "url": "steam://open/main"},
    {"label": "BROWSER", "icon": "web",     "url": "https://duckduckgo.com"},
    {"label": "EMAIL",   "icon": "email",   "url": "https://mail.google.com"},
    {"label": "DISCORD", "icon": "discord", "url": "https://discord.com/app"},
    {"label": "YOUTUBE", "icon": "youtube", "url": "https://www.youtube.com"},
    {"label": "FILES",   "icon": "folder",  "url": "~"},
]
# icons built into the firmware (tools/make_icons.py); unknown names show the label's first letter
ICON_NAMES = ("steam web firefox chrome email discord youtube folder terminal music "
              "gamepad spotify settings chat camera power star twitch whatsapp robot")


def load_keys():
    """Read keys.json (created with defaults on first run); re-read on every
    press so edits apply without restarting the agent."""
    if not os.path.exists(KEYS_FILE):
        os.makedirs(os.path.dirname(KEYS_FILE), exist_ok=True)
        with open(KEYS_FILE, "w") as f:
            json.dump(DEFAULT_KEYS, f, indent=2)
    try:
        with open(KEYS_FILE) as f:
            keys = json.load(f)
        return keys[:6] if isinstance(keys, list) else DEFAULT_KEYS
    except (OSError, ValueError) as e:
        print(f"bad {KEYS_FILE} ({e}), using defaults")
        return DEFAULT_KEYS


def launch_key(n):
    keys = load_keys()
    if not 0 <= n < len(keys):
        return
    key = keys[n]
    cmd = key.get("cmd")
    if cmd is None and key.get("url"):
        # "url": open a link / folder with the system's default handler
        url = os.path.expanduser(key["url"])
        if IS_WIN:
            print(f"key {n} ({key.get('label')}): {url}")
            try:
                os.startfile(url)
            except OSError as e:
                print(f"launch failed: {e}")
            return
        cmd = ["open" if IS_MAC else "xdg-open", url]
    elif isinstance(cmd, str):
        cmd = ["cmd", "/c", cmd] if IS_WIN else ["sh", "-c", cmd]
    if not cmd:
        return
    # uwsm-app puts the app in its own scope, so it outlives this service
    if IS_LINUX and shutil.which("uwsm-app"):
        cmd = ["uwsm-app", "--"] + cmd
    print(f"key {n} ({key.get('label')}): {' '.join(cmd)}")
    try:
        subprocess.Popen(cmd, stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL,
                         stderr=subprocess.DEVNULL, start_new_session=True)
    except OSError as e:
        print(f"launch failed: {e}")


def handle_input(ser, buf, last, media=None, radio=None):
    """Non-blocking read of button presses from the CYD. Returns the new
    partial-line buffer."""
    n = ser.in_waiting
    if not n:
        return buf
    buf += ser.read(n)
    while b"\n" in buf:
        line, buf = buf.split(b"\n", 1)
        if line.startswith(b"mouth "):
            print(f"cyd: {line.decode(errors='ignore').strip()}", flush=True)
        parts = line.decode(errors="ignore").strip().split()
        if media and len(parts) >= 2 and parts[0] == "M":        # boombox buttons
            arg = int(parts[2]) if len(parts) > 2 and parts[2].isdigit() else None
            print(f"boombox: {' '.join(parts[1:])}")
            media.command(parts[1], arg)
            media.next_poll = 0                                   # show the result right away
        elif radio and len(parts) >= 2 and parts[0] == "R":      # radio page
            arg = int(parts[2]) if len(parts) > 2 and parts[2].isdigit() else None
            print(f"radio: {' '.join(parts[1:])}")
            radio.command(parts[1], arg)
            radio.next_poll = 0
        elif len(parts) == 2 and parts[0] == "L" and parts[1].isdigit():
            k = int(parts[1])
            if time.monotonic() - last.get(k, 0) > 1.0:  # debounce double taps
                last[k] = time.monotonic()
                launch_key(k)
    return buf


# ── boombox (Linux: MPRIS media players over D-Bus) ─────────────
# The CYD's third page (swipe left twice) is a boombox. Once a second the
# agent sends {"np": ...} with what the busiest media player is doing and,
# when the song changes, {"tl": ...} with a window of its playlist (players
# with the MPRIS TrackList interface, e.g. VLC). The CYD sends back
# "M play|next|prev|volup|voldn" or "M goto <n>".
# Needs `pip install jeepney`; without it the page just says nothing's playing.
MUSIC_PLAYLIST = os.path.expanduser("~/Music/All My Music.m3u")
MPRIS = "/org/mpris/MediaPlayer2"
try:
    from jeepney import DBusAddress, MessageType, new_method_call
    from jeepney.io.blocking import open_dbus_connection
except ImportError:
    DBusAddress = None


def ascii_text(s, n):
    """The CYD's fonts are ASCII-only: fold accents (Canción -> Cancion)."""
    import unicodedata
    s = unicodedata.normalize("NFKD", str(s)).encode("ascii", "ignore").decode()
    return re.sub(r"\s+", " ", s).strip()[:n]


class Media:
    def __init__(self):
        self.conn = None
        self.player = None
        self.tracks = []          # playlist object paths (for "goto")
        self.tl_key = None
        self.tl_sent = 0.0

    def _call(self, dest, path, iface, method, sig=None, body=()):
        if self.conn is None:
            self.conn = open_dbus_connection(bus="SESSION")
        msg = new_method_call(DBusAddress(path, bus_name=dest, interface=iface), method, sig, body)
        reply = self.conn.send_and_get_reply(msg, timeout=1)
        if reply.header.message_type == MessageType.error:
            raise RuntimeError(f"{method}: {reply.body}")
        return reply.body

    def _get(self, dest, iface, name):
        return self._call(dest, MPRIS, "org.freedesktop.DBus.Properties", "Get",
                          "ss", (iface, name))[0][1]

    def _pick_player(self):
        names = self._call("org.freedesktop.DBus", "/org/freedesktop/DBus",
                           "org.freedesktop.DBus", "ListNames")[0]
        players = [n for n in names if n.startswith("org.mpris.MediaPlayer2.")]
        if not players:
            return None
        status = {}
        for p in players:
            try:
                status[p] = self._get(p, "org.mpris.MediaPlayer2.Player", "PlaybackStatus")
            except Exception:
                status[p] = "Stopped"
        rank = {"Playing": 0, "Paused": 1}
        # playing beats paused beats stopped; then VLC (it has a playlist); then the last one used
        return min(players, key=lambda p: (rank.get(status[p], 2), "vlc" not in p, p != self.player))

    @staticmethod
    def _volume():
        try:
            out = subprocess.run(["wpctl", "get-volume", "@DEFAULT_AUDIO_SINK@"],
                                 capture_output=True, text=True, timeout=1).stdout
            v = round(float(out.split()[1]) * 100)
            return 0 if "MUTED" in out else v
        except Exception:
            return -1

    @staticmethod
    def _title(meta):
        t = meta.get("xesam:title", ("s", ""))[1]
        if not t:                                   # untagged file: use its name
            from urllib.parse import unquote
            t = unquote(meta.get("xesam:url", ("s", ""))[1])
        if "/" in t:                                # VLC titles untagged files by their path
            t = os.path.splitext(os.path.basename(t))[0]
        return t

    def sample(self):
        """(np line, tl line or None) to send to the CYD."""
        if DBusAddress is None or not IS_LINUX:
            return {"np": {"ok": 0}}, None
        try:
            self.player = p = self._pick_player()
            if not p:
                self.tracks, self.tl_key = [], None
                return {"np": {"ok": 0, "vol": self._volume()}}, None
            pl = "org.mpris.MediaPlayer2.Player"
            meta = self._get(p, pl, "Metadata")
            status = self._get(p, pl, "PlaybackStatus")
            try:
                pos = self._get(p, pl, "Position") / 1e6
            except Exception:
                pos = 0
            artist = meta.get("xesam:artist", ("as", []))[1]
            npd = {"ok": 1, "t": ascii_text(self._title(meta), 46),
                   "a": ascii_text(", ".join(artist) if isinstance(artist, list) else artist, 38),
                   "st": {"Playing": 1, "Paused": 2}.get(status, 0),
                   "pos": round(pos, 1), "len": round(meta.get("mpris:length", ("x", 0))[1] / 1e6),
                   "vol": self._volume(),
                   "pl": ascii_text(p.split(".")[3], 10).upper()}
            return {"np": npd}, self._tracklist(p, meta.get("mpris:trackid", ("o", ""))[1])
        except Exception as e:
            print(f"media: {e}")
            self.conn = None
            return {"np": {"ok": 0}}, None

    def _tracklist(self, p, current):
        tli = "org.mpris.MediaPlayer2.TrackList"
        try:
            self.tracks = list(self._get(p, tli, "Tracks"))
        except Exception:
            self.tracks = []
            return None
        cur = self.tracks.index(current) if current in self.tracks else -1
        key = (p, cur, len(self.tracks))
        if key == self.tl_key and time.monotonic() - self.tl_sent < 10:
            return None
        start = max(0, cur - 1)
        window = self.tracks[start:start + 8]
        metas = self._call(p, MPRIS, tli, "GetTracksMetadata", "ao", (window,))[0] if window else []
        self.tl_key, self.tl_sent = key, time.monotonic()
        return {"tl": {"s": start, "c": cur, "n": len(self.tracks),
                       "i": [ascii_text(self._title(m), 38) for m in metas]}}

    def command(self, cmd, arg=None):
        pl = "org.mpris.MediaPlayer2.Player"
        try:
            if cmd in ("volup", "voldn"):
                subprocess.run(["wpctl", "set-volume", "-l", "1.0", "@DEFAULT_AUDIO_SINK@",
                                "5%+" if cmd == "volup" else "5%-"], timeout=2)
                return
            p = self.player or self._pick_player()
            if not p:
                if cmd == "play" and os.path.exists(MUSIC_PLAYLIST):  # nothing open: start the library
                    print(f"media: opening {MUSIC_PLAYLIST}")
                    subprocess.Popen(["vlc", "--random", MUSIC_PLAYLIST], stdin=subprocess.DEVNULL,
                                     stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
                                     start_new_session=True)
                return
            if cmd == "goto" and arg is not None and 0 <= arg < len(self.tracks):
                self._call(p, MPRIS, "org.mpris.MediaPlayer2.TrackList", "GoTo", "o",
                           (self.tracks[arg],))
            else:
                method = {"play": "PlayPause", "next": "Next", "prev": "Previous"}.get(cmd)
                if method:
                    self._call(p, MPRIS, pl, method)
            self.tl_key = None                     # resend the playlist window right away
        except Exception as e:
            print(f"media {cmd}: {e}")
            self.conn = None


# ── radio (Linux: the Radio Atlas Omarchy plugin) ──────────────
# The CYD's fourth page drives Radio Atlas (akshar.radio-atlas) through the
# plugin's own radio-player / radio-fetch scripts. The agent sends
# {"rd": ...} (what's on) every 1.5 s and {"rl": ...} (the station list)
# when it changes; the CYD sends "R toggle|next|prev|random|volup|voldn",
# "R genre <n>" or "R play <n>".
RADIO_DIR = os.path.expanduser("~/.config/omarchy/plugins/akshar.radio-atlas")
RADIO_RUN = os.path.join(os.environ.get("XDG_RUNTIME_DIR", "/tmp"), "omarchy-radio-atlas")
RADIO_GENRES = ["rock", "jazz", "hip hop", "lofi", "news"]   # + RECENT; labels live in the firmware
RADIO_LIST_MAX = 20


class Radio:
    def __init__(self):
        self.ok = IS_LINUX and os.path.exists(os.path.join(RADIO_DIR, "radio-player"))
        self.stations, self.scope, self.label = [], "results", ""
        self.list_ver = 0          # bumped whenever self.stations changes
        self.sent_ver = -1
        self.busy = ""             # "searching..." text while a fetch runs
        self.next_poll = 0.0
        self.lock = __import__("threading").Lock()

    def _run(self, script, *args, timeout=20):
        return subprocess.run([os.path.join(RADIO_DIR, script), *args], capture_output=True,
                              text=True, timeout=timeout, stdin=subprocess.DEVNULL).stdout

    def _set_list(self, stations, scope, label):
        with self.lock:
            self.stations, self.scope, self.label = stations[:RADIO_LIST_MAX], scope, label
            self.list_ver += 1

    def _load(self, path):
        try:
            with open(path) as f:
                data = json.load(f)
            return data if isinstance(data, list) else []
        except Exception:
            return []

    def status(self):
        try:
            return json.loads(self._run("radio-player", "status", timeout=5) or "{}")
        except Exception:
            return {}

    def sample(self):
        """(rd line, rl line or None)."""
        if not self.ok:
            return {"rd": {"ok": 0}}, None
        s = self.status()
        st = s.get("station") or {}
        if s.get("running") and not self.stations:   # started from the globe: show its queue
            q = self._load(os.path.join(RADIO_RUN, "playlist.json"))
            if q:
                self._set_list(q, "queue", "PLAYING")
        rd = {"ok": 1, "on": int(bool(s.get("running"))), "p": int(bool(s.get("paused"))),
              "st": ascii_text(st.get("name", ""), 40), "c": ascii_text(st.get("country", ""), 30),
              "t": ascii_text(s.get("title", ""), 60), "v": round(float(s.get("volume") or 0)),
              "i": s.get("playlistPosition", -1), "n": s.get("playlistCount", 0),
              "b": self.busy}
        rl = None
        with self.lock:
            if self.sent_ver != self.list_ver:
                cur = next((i for i, x in enumerate(self.stations)
                            if x.get("uuid") and x.get("uuid") == st.get("uuid")), -1)
                rl = {"rl": {"l": self.label, "cur": cur,
                             "i": [ascii_text(x.get("name", "?"), 30) for x in self.stations]}}
                self.sent_ver = self.list_ver
        return {"rd": rd}, rl

    def command(self, cmd, arg=None):
        if not self.ok:
            return
        import threading
        simple = {"toggle": "toggle", "next": "next", "prev": "previous",
                  "volup": "volume-up", "voldn": "volume-down"}
        if cmd in simple:
            self._run("radio-player", simple[cmd], timeout=8)
            if cmd in ("next", "prev"):
                self.sent_ver = -1                      # re-highlight the playing station
        elif cmd == "genre" and arg is not None:
            threading.Thread(target=self._genre, args=(arg,), daemon=True).start()
        elif cmd == "random":
            threading.Thread(target=self._random, daemon=True).start()
        elif cmd == "play" and arg is not None:
            threading.Thread(target=self._play, args=(arg,), daemon=True).start()

    def _genre(self, n):
        if n < len(RADIO_GENRES):
            q = RADIO_GENRES[n]
            self.busy = f"finding {q} stations..."
            try:
                self._run("radio-fetch", "search", q, timeout=30)
                self._set_list(self._load(os.path.join(RADIO_RUN, "results.json")), "results", q.upper())
            finally:
                self.busy = ""
        else:                                            # RECENT
            recent = json.loads(self._run("radio-state", "get") or "{}").get("recent", [])
            self._set_list(recent, "recent", "RECENT")

    def _random(self):
        self.busy = "tuning a random station..."
        try:
            self._run("radio-fetch", "random", timeout=30)
            res = self._load(os.path.join(RADIO_RUN, "results.json"))
            self._set_list(res, "results", "RANDOM")
            if res:
                self._run("radio-player", "play", res[0]["uuid"], "results", timeout=30)
                self.sent_ver = -1
        finally:
            self.busy = ""

    def _play(self, n):
        with self.lock:
            if not 0 <= n < len(self.stations):
                return
            station, scope, stations = self.stations[n], self.scope, list(self.stations)
        if scope == "queue":                             # the globe's queue: hand it over as a selection
            with open(os.path.join(RADIO_RUN, "play-selection.json"), "w") as f:
                json.dump(stations, f)
            scope = "selection"
        self.busy = f"tuning {ascii_text(station.get('name', ''), 24)}..."
        try:
            self._run("radio-player", "play", station["uuid"], scope, timeout=30)
            self.sent_ver = -1
        finally:
            self.busy = ""


# ── talking mouth ──────────────────────────────────────────────
# Alice's TTS (kokoro_synth.py) sends mouth-openness frames and word timings
# as JSON datagrams while she speaks; they go to the CYD as-is.
MOUTH_PORT = 47811


def mouth_socket():
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    try:
        s.bind(("127.0.0.1", MOUTH_PORT))
    except OSError as e:
        print(f"mouth relay disabled ({e})")
        return None
    s.setblocking(False)
    return s


def relay_mouth(sock, ser, wait=0.03):
    """Wait up to `wait` s for datagrams and forward each one as a line."""
    if sock is None:
        time.sleep(wait)
        return
    if not select.select([sock], [], [], wait)[0]:
        return
    while True:
        try:
            data = sock.recv(512)
        except BlockingIOError:
            return
        if data and b"\n" not in data:
            ser.write(data + b"\n")
            relay_mouth.n += 1
            if data == b'{"talk":0}':
                print(f"mouth: relayed {relay_mouth.n} messages", flush=True)
                relay_mouth.n = 0


relay_mouth.n = 0


# ── serial ─────────────────────────────────────────────────────
def find_port():
    from serial.tools import list_ports
    ports = list(list_ports.comports())
    for p in ports:  # CH340 first (the CYD's USB-serial chip)
        if (p.vid == 0x1A86) or "CH340" in (p.description or "") \
           or "usbserial" in p.device or "wchusbserial" in p.device:
            return p.device
    return ports[0].device if ports else None


def main():
    ap = argparse.ArgumentParser(description="CYD Resource Monitor agent")
    ap.add_argument("--port", help="serial port (default: auto-detect CH340)")
    ap.add_argument("--interval", type=float, default=0.5, help="seconds between updates")
    ap.add_argument("--lhm", default="http://localhost:8085/data.json",
                    help="LibreHardwareMonitor web-server URL ('' to disable)")
    ap.add_argument("--list", action="store_true", help="list serial ports and exit")
    ap.add_argument("--print", dest="dry", action="store_true",
                    help="print JSON to stdout instead of serial")
    args = ap.parse_args()

    if args.list:
        from serial.tools import list_ports
        for p in list_ports.comports():
            print(f"{p.device:24} {p.description}")
        return

    sampler = Sampler(args.lhm if IS_WIN else None)

    if args.dry:
        while True:
            print(json.dumps(sampler.sample()))
            time.sleep(args.interval)

    import serial
    signal.signal(signal.SIGTERM, lambda *_: sys.exit(0))
    mouth = mouth_socket()
    media = Media()
    media.next_poll = 0.0
    radio = Radio()
    said_hello = False
    while True:
        port = args.port or find_port()
        if not port:
            print("no serial port found, retrying in 3 s...  (--list to inspect)")
            time.sleep(3)
            continue
        try:
            with serial.Serial(port, 115200, timeout=1) as ser:
                print(f"connected to {port}")
                buf, last, next_send = b"", {}, 0.0
                try:
                    while True:
                        if time.monotonic() >= next_send:
                            payload = sampler.sample()
                            keys = load_keys()
                            payload["keys"] = [str(k.get("label", ""))[:13] for k in keys]
                            payload["icons"] = [str(k.get("icon", ""))[:11] for k in keys]
                            line = json.dumps(payload, separators=(",", ":")) + "\n"
                            ser.write(line.encode())
                            if not said_hello:  # after the first stats, so the CYD knows the host name
                                ser.write(b'{"msg":"hello"}\n')
                                said_hello = True
                            next_send = time.monotonic() + args.interval
                        if time.monotonic() >= media.next_poll:
                            for msg in media.sample():
                                if msg:
                                    ser.write(json.dumps(msg, separators=(",", ":")).encode() + b"\n")
                            media.next_poll = time.monotonic() + 1.0
                        if time.monotonic() >= radio.next_poll:
                            for msg in radio.sample():
                                if msg:
                                    ser.write(json.dumps(msg, separators=(",", ":")).encode() + b"\n")
                            radio.next_poll = time.monotonic() + 1.5
                        buf = handle_input(ser, buf, last, media, radio)
                        relay_mouth(mouth, ser)
                except (SystemExit, KeyboardInterrupt):  # service stop / shutdown
                    ser.write(b'{"msg":"bye"}\n')
                    ser.flush()
                    time.sleep(0.3)
                    raise
        except (serial.SerialException, OSError) as e:
            print(f"serial error ({e}), reconnecting in 3 s...")
            time.sleep(3)


if __name__ == "__main__":
    try:
        main()
    except KeyboardInterrupt:
        sys.exit(0)
