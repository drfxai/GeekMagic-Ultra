#!/usr/bin/env python3
"""
drfx - the DrFX Ultra OS terminal client.

Talks to the device on your LAN and to the Cloudflare Worker bridge. Standard
library only, so it runs anywhere Python 3.9+ does with nothing to install.

    drfx status                       what the device is doing right now
    drfx watch                        the same, refreshed once a second
    drfx crypto                       prices the device is holding
    drfx next                         advance the carousel one screen
    drfx test                         put the demo card on the screen
    drfx push --symbol EURUSD --side SELL --score 71
    drfx send  --symbol XAUUSD --side BUY  --score 96      (through the bridge)
    drfx tz list                      the zones the device understands
    drfx tz set Europe/London         change the clock, no reboot
    drfx history                      recent signals held by the bridge
    drfx backup                       save every setting to a JSON file
    drfx restore backup.json          write a backup back to the device
    drfx doctor                       check the whole chain end to end

Connection details come from flags or the environment, flags winning:

    DRFX_DEVICE       http://godmode.local
    DRFX_DEVICE_KEY   the device key (same string as the Worker's DEVICE_KEY)
    DRFX_USER         settings username, default "admin"
    DRFX_PASS         settings password
    DRFX_BRIDGE       https://fx-godmode-bridge.you.workers.dev
    DRFX_WEBHOOK_KEY  the Worker's WEBHOOK_KEY, for `send`
"""

from __future__ import annotations

import argparse
import base64
import json
import os
import shutil
import sys
import time
import urllib.error
import urllib.parse
import urllib.request
from pathlib import Path
from typing import Any

ROOT = Path(__file__).resolve().parent.parent
ZONES_FILE = ROOT / "shared" / "timezones.json"
TIMEOUT = 8

# --------------------------------------------------------------------------
# terminal styling - the same design language as the screen: dim labels in
# small caps, one accent, hairline rules, no boxes
# --------------------------------------------------------------------------


class Style:
    def __init__(self, enabled: bool):
        self.on = enabled
        self.RESET = "\033[0m" if enabled else ""
        self.DIM = "\033[38;5;244m" if enabled else ""
        self.DIM2 = "\033[38;5;239m" if enabled else ""
        self.TXT = "\033[38;5;252m" if enabled else ""
        self.ACC = "\033[38;5;48m" if enabled else ""
        self.RED = "\033[38;5;203m" if enabled else ""
        self.AMB = "\033[38;5;214m" if enabled else ""
        self.BOLD = "\033[1m" if enabled else ""

    def rule(self, width: int = 0) -> str:
        width = width or min(shutil.get_terminal_size((80, 24)).columns, 72)
        return f"{self.DIM2}{'─' * width}{self.RESET}"

    def head(self, left: str, right: str = "") -> str:
        width = min(shutil.get_terminal_size((80, 24)).columns, 72)
        pad = max(1, width - len(left) - len(right))
        return (f"{self.ACC}{self.BOLD}{left}{self.RESET}{' ' * pad}"
                f"{self.DIM}{right}{self.RESET}\n{self.rule(width)}")

    def kv(self, key: str, value: str, colour: str = "") -> str:
        return f"{self.DIM}{key.upper():<20}{self.RESET}{colour or self.TXT}{value}{self.RESET}"


def make_style(no_color: bool) -> Style:
    if no_color or os.environ.get("NO_COLOR"):
        return Style(False)
    if not sys.stdout.isatty():
        return Style(False)
    if os.name == "nt":
        # Windows 10+ understands ANSI once virtual terminal processing is on;
        # os.system("") is the cheapest way to make the console enable it.
        os.system("")
    return Style(True)


# --------------------------------------------------------------------------
# http
# --------------------------------------------------------------------------


class ApiError(Exception):
    pass


def request(url: str, *, method: str = "GET", body: Any = None,
            user: str = "", password: str = "", timeout: int = TIMEOUT) -> Any:
    data = None
    headers = {"User-Agent": "drfx-cli/2.0"}

    if body is not None:
        data = json.dumps(body).encode()
        headers["Content-Type"] = "application/json"

    if user or password:
        token = base64.b64encode(f"{user}:{password}".encode()).decode()
        headers["Authorization"] = "Basic " + token

    req = urllib.request.Request(url, data=data, headers=headers, method=method)
    try:
        with urllib.request.urlopen(req, timeout=timeout) as r:
            raw = r.read().decode("utf-8", "replace")
            if r.status == 204 or not raw.strip():
                return None
            ctype = r.headers.get("content-type", "")
            return json.loads(raw) if "json" in ctype else raw
    except urllib.error.HTTPError as e:
        detail = e.read().decode("utf-8", "replace").strip()[:200]
        if e.code == 401:
            raise ApiError("401 unauthorised - pass --user/--pass "
                           "(or set DRFX_USER / DRFX_PASS)") from None
        if e.code == 403:
            raise ApiError("403 rejected - the key does not match the one on "
                           "the other end") from None
        raise ApiError(f"HTTP {e.code}{' - ' + detail if detail else ''}") from None
    except urllib.error.URLError as e:
        raise ApiError(f"could not reach {url} - {e.reason}") from None
    except json.JSONDecodeError:
        raise ApiError("the reply was not valid JSON") from None


# --------------------------------------------------------------------------
# zones
# --------------------------------------------------------------------------


def load_zones() -> list[dict]:
    try:
        return json.loads(ZONES_FILE.read_text(encoding="utf-8"))["zones"]
    except FileNotFoundError:
        raise ApiError(f"cannot find {ZONES_FILE} - run this from a checkout "
                       "of the repository") from None


def find_zone(query: str) -> dict | None:
    zones = load_zones()
    q = query.strip().lower()
    for z in zones:                                     # exact IANA name
        if z["name"].lower() == q:
            return z
    for z in zones:                                     # city label
        if z["label"].lower() == q:
            return z
    matches = [z for z in zones if q in z["name"].lower() or q in z["label"].lower()]
    return matches[0] if len(matches) == 1 else None


# --------------------------------------------------------------------------
# rendering
# --------------------------------------------------------------------------


def render_status(s: dict, st: Style, bridge_name: str = "") -> str:
    sig = s.get("signal") or {}
    clk = s.get("clock") or {}
    out = [st.head(f"DRFX ULTRA OS  v{s.get('fw', '?')}",
                   "SETUP MODE" if s.get("ap") else str(s.get("ip", "")))]

    def line(k: str, v: str, colour: str = "") -> None:
        out.append(st.kv(k, v, colour))

    if s.get("ap"):
        line("connection", "setup mode - device is its own access point", st.AMB)
    else:
        line("network", f"{s.get('ssid','?')}   {s.get('rssi','?')} dBm")

    code = s.get("httpCode")
    if code == 200:
        line("bridge", "connected, new signal received", st.ACC)
    elif code == 204:
        line("bridge", "connected, nothing new", st.ACC)
    else:
        line("bridge", s.get("error") or "not configured", st.RED)

    if s.get("timeOk"):
        via = "   via bridge - NTP blocked" if clk.get("src") == "bridge" else ""
        line("clock", f"{clk.get('time','')}  {clk.get('abbr','')}  {clk.get('offset','')}{via}",
             st.AMB if clk.get("src") == "bridge" else "")
        line("time zone", clk.get("tzName", "-")
             + ("   (night mode)" if clk.get("night") else ""))
        line("date", f"{clk.get('weekday','')} {clk.get('date','')}")
    else:
        line("clock", "waiting for NTP", st.AMB)

    rot = s.get("rotation") or {}
    if rot.get("every"):
        pinned = "   pinned" if rot.get("pinned") else ""
        line("rotation", f"every {rot['every']}s   showing {rot.get('showing','-')}"
                         f" ({int(rot.get('pos', 0)) + 1} of {rot.get('slots', 1)}){pinned}")
    else:
        line("rotation", "off", st.DIM)

    cry = s.get("crypto") or {}
    if not cry.get("on"):
        line("crypto", "off", st.DIM)
    elif cry.get("ok"):
        pairs = cry.get("tickers") or []
        line("crypto", f"{len(pairs)} pairs   {cry.get('ageSec', 0)}s ago", st.ACC)
        for t in pairs:
            chg = float(t.get("change") or 0)
            colour = st.ACC if chg >= 0 else st.RED
            line(f"  {t.get('name','?')}",
                 f"{t.get('price','-'):>14}   {chg:+.2f}%", colour)
    else:
        line("crypto", cry.get("error") or "no data yet", st.RED)

    line("free memory", f"{s.get('heap','?')} bytes",
         st.RED if isinstance(s.get("heap"), int) and s["heap"] < 12000 else "")
    up = int(s.get("uptime", 0))
    line("uptime", f"{up // 3600}h {up % 3600 // 60}m")

    if not s.get("cfgOnFlash"):
        line("settings", "NOT SAVED - will reset on reboot", st.RED)

    out.append(st.rule())
    if sig.get("valid"):
        colour = st.RED if sig.get("side") == "SELL" else st.ACC
        stale = "" if sig.get("fresh") else "  (expired)"
        line("signal", f"{sig.get('symbol','')} {sig.get('side','')}{stale}", colour)
        line("score / conf", f"{sig.get('score',0)}  /  {sig.get('conf',0)}%")
        line("levels", f"TP1 {sig.get('tp1') or '-'}   TP2 {sig.get('tp2') or '-'}   "
                       f"SL {sig.get('sl') or '-'}")
        line("age", f"{sig.get('ageSec',0)}s")
    else:
        line("signal", "none yet", st.DIM)

    if bridge_name:
        out.append(st.rule())
        out.append(st.kv("bridge url", bridge_name))
    return "\n".join(out)


# --------------------------------------------------------------------------
# commands
# --------------------------------------------------------------------------


def cmd_status(a, st) -> int:
    print(render_status(request(f"{a.device}/api/status"), st))
    return 0


def cmd_watch(a, st) -> int:
    print("\033[?25l", end="")           # hide the cursor
    try:
        while True:
            try:
                frame = render_status(request(f"{a.device}/api/status"), st)
            except ApiError as e:
                frame = f"{st.RED}{e}{st.RESET}"
            stamp = time.strftime("%H:%M:%S")
            # Home the cursor and clear to end of screen rather than clearing
            # first: no flash between frames.
            print(f"\033[H\033[J{frame}\n\n{st.DIM2}refreshed {stamp} · ctrl-c to stop{st.RESET}",
                  flush=True)
            time.sleep(a.interval)
    except KeyboardInterrupt:
        return 0
    finally:
        print("\033[?25h", end="")       # show it again


def cmd_test(a, st) -> int:
    request(f"{a.device}/api/test", method="POST", user=a.user, password=a.password)
    print(f"{st.ACC}test card is on the screen{st.RESET}")
    return 0


def cmd_next(a, st) -> int:
    """Step the carousel by hand - useful when checking a layout."""
    r = request(f"{a.device}/api/next", method="POST",
                user=a.user, password=a.password) or {}
    print(f"{st.ACC}advanced to screen {int(r.get('pos', 0)) + 1} "
          f"of {r.get('slots', '?')}{st.RESET}")
    return 0


def cmd_crypto(a, st) -> int:
    """What the device currently holds, and a sparkline of what it is drawing."""
    s = request(f"{a.device}/api/status")
    cry = s.get("crypto") or {}

    print(st.head("CRYPTO", cry.get("symbols", "")))
    if not cry.get("on"):
        print(f"{st.DIM}switched off - enable it on the Crypto tab, "
              f"or with --symbols on the device{st.RESET}")
        return 0
    if not cry.get("ok"):
        print(f"{st.RED}{cry.get('error') or 'no data yet'}{st.RESET}")
        return 1

    for t in cry.get("tickers") or []:
        chg = float(t.get("change") or 0)
        colour = st.ACC if chg >= 0 else st.RED
        arrow = "▲" if chg >= 0 else "▼"
        print(f"{st.TXT}{t.get('name','?'):<6}{st.RESET}"
              f"{t.get('price','-'):>16}   {colour}{arrow} {chg:+.2f}%{st.RESET}"
              f"   {st.DIM}{t.get('sym','')}  {t.get('spark',0)} spark pts{st.RESET}")

    print(f"\n{st.DIM2}updated {cry.get('ageSec',0)}s ago · "
          f"{cry.get('fails',0)} consecutive failures{st.RESET}")
    return 0


def signal_payload(a) -> dict:
    return {
        "symbol": a.symbol, "side": a.side, "score": a.score, "conf": a.conf,
        "entry": a.entry, "tp1": a.tp1, "tp2": a.tp2, "sl": a.sl,
        "tf": a.tf, "note": a.note,
    }


def cmd_push(a, st) -> int:
    """Straight to the device over the LAN - skips Cloudflare entirely."""
    if not a.device_key:
        raise ApiError("push needs the device key: --device-key or DRFX_DEVICE_KEY")
    url = f"{a.device}/api/push?key={urllib.parse.quote(a.device_key)}"
    request(url, method="POST", body=signal_payload(a))
    print(f"{st.ACC}pushed {a.symbol} {a.side} to {a.device}{st.RESET}")
    return 0


def cmd_send(a, st) -> int:
    """Through the bridge - the exact path a TradingView alert takes."""
    if not a.bridge:
        raise ApiError("send needs the bridge URL: --bridge or DRFX_BRIDGE")
    if not a.webhook_key:
        raise ApiError("send needs the webhook key: --webhook-key or DRFX_WEBHOOK_KEY")
    url = (f"{a.bridge}/tv?key={urllib.parse.quote(a.webhook_key)}"
           f"&device={urllib.parse.quote(a.id)}")
    r = request(url, method="POST", body=signal_payload(a))
    stored = (r or {}).get("stored", {})
    print(f"{st.ACC}bridge stored {stored.get('symbol','?')} {stored.get('side','?')}"
          f"{st.RESET}\n{st.DIM}the device will show it within its poll interval{st.RESET}")
    return 0


def cmd_history(a, st) -> int:
    if not a.bridge or not a.device_key:
        raise ApiError("history needs --bridge and --device-key")
    url = (f"{a.bridge}/history?key={urllib.parse.quote(a.device_key)}"
           f"&device={urllib.parse.quote(a.id)}&limit={a.limit}")
    r = request(url) or {}
    rows = r.get("signals", [])
    print(st.head("RECENT SIGNALS", f"device {r.get('device','?')}"))
    if not rows:
        print(f"{st.DIM}nothing stored yet{st.RESET}")
        return 0
    for s in rows:
        when = time.strftime("%d %b %H:%M", time.gmtime(s.get("ts", 0) / 1000))
        colour = st.RED if s.get("side") == "SELL" else (
            st.DIM if s.get("side") == "FLAT" else st.ACC)
        print(f"{st.DIM}{when}Z{st.RESET}  {s.get('symbol',''):<10} "
              f"{colour}{s.get('side',''):<5}{st.RESET} "
              f"{st.DIM}score{st.RESET} {s.get('score',0):<4} "
              f"{st.DIM}tp1{st.RESET} {s.get('tp1') or '-':<9}"
              f"{st.DIM}sl{st.RESET} {s.get('sl') or '-'}")
    return 0


def cmd_tz(a, st) -> int:
    if a.tz_action == "list":
        group = None
        for z in load_zones():
            if z["group"] != group:
                group = z["group"]
                print(f"\n{st.ACC}{group.upper()}{st.RESET}")
            print(f"  {z['name']:<34}{st.DIM}{z['label']}{st.RESET}")
        print(f"\n{st.DIM2}set one with:  drfx tz set Europe/London{st.RESET}")
        return 0

    if a.tz_action == "get":
        s = request(f"{a.device}/api/status")
        c = s.get("clock") or {}
        print(st.kv("time zone", c.get("tzName", "-")))
        print(st.kv("posix rule", c.get("tz", "-")))
        print(st.kv("device time", f"{c.get('time','-')} {c.get('abbr','')} {c.get('offset','')}"))
        return 0

    zone = find_zone(a.zone)
    if not zone:
        raise ApiError(f"no zone matches {a.zone!r} - try: drfx tz list")

    request(f"{a.device}/api/config", method="POST",
            body={"tz": zone["posix"], "tzName": zone["name"]},
            user=a.user, password=a.password)

    time.sleep(0.6)
    c = (request(f"{a.device}/api/status").get("clock") or {})
    print(f"{st.ACC}{zone['name']}{st.RESET}  {st.DIM}({zone['posix']}){st.RESET}")
    print(st.kv("device now", f"{c.get('time','-')} {c.get('abbr','')} {c.get('offset','')}"))
    return 0


def cmd_backup(a, st) -> int:
    """Snapshot every setting the device will return, to a local JSON file.

    Secrets (WiFi passwords, device key, admin password) are never sent back
    by the firmware, so the backup records only WHETHER they exist - restoring
    leaves those fields untouched on the device thanks to blank-means-keep.
    """
    cfg = request(f"{a.device}/api/config", user=a.user, password=a.password)
    out = Path(a.file)
    out.write_text(json.dumps(cfg, indent=2) + "\n", encoding="utf-8")
    bridge = cfg.get("bridge") or "(none)"
    print(f"{st.ACC}backed up {len(cfg)} fields to {out}{st.RESET}")
    print(st.kv("bridge url", bridge))
    print(st.kv("secrets", "recorded as present/absent only - restore will keep "
                          "whatever is already on the device"))
    return 0


def cmd_restore(a, st) -> int:
    """Push a backup file back to the device. Blank secret fields are dropped
    before posting so the device's blank-means-keep rule leaves stored
    passwords and keys alone."""
    cfg = json.loads(Path(a.file).read_text(encoding="utf-8"))
    # hasX flags are read-only status, not settable fields
    for k in ("hasPass", "hasPass2", "hasDevKey"):
        cfg.pop(k, None)
    r = request(f"{a.device}/api/config", method="POST", body=cfg,
                user=a.user, password=a.password) or {}
    if not r.get("ok"):
        raise ApiError("the device refused to write to flash - try again")
    print(f"{st.ACC}restored {len(cfg)} fields from {a.file}{st.RESET}")
    if r.get("reboot"):
        print(f"{st.AMB}device is rebooting (network change) - "
              f"give it ~20 seconds{st.RESET}")
    else:
        back = request(f"{a.device}/api/config", user=a.user, password=a.password)
        print(st.kv("bridge url", back.get("bridge") or "(none)"))
        print(st.kv("verify", "restored values match the backup"
                    if all(back.get(k) == v for k, v in cfg.items()
                           if not isinstance(v, bool) or k in back)
                    else "re-check: some values differ"))
    return 0


def cmd_doctor(a, st) -> int:
    """Walk the whole chain and say which link is broken, not just that it is."""
    failures = 0
    print(st.head("DIAGNOSTICS", a.device))

    def report(label: str, ok: bool, note: str) -> None:
        nonlocal failures
        if ok:
            print(f"  {st.ACC}pass{st.RESET}  {label:<26}{st.DIM}{note}{st.RESET}")
        else:
            failures += 1
            print(f"  {st.RED}FAIL{st.RESET}  {label:<26}{st.RED}{note}{st.RESET}")

    def skip(label: str, note: str) -> None:
        print(f"  {st.DIM2}skip{st.RESET}  {label:<26}{st.DIM2}{note}{st.RESET}")

    # --- the device ----------------------------------------------------
    status: dict | None = None
    try:
        status = request(f"{a.device}/api/status")
        report("device reachable", True, a.device)
    except ApiError as e:
        report("device reachable", False, str(e))

    if isinstance(status, dict):
        clock = status.get("clock") or {}
        heap = status.get("heap", 0)

        report("settings persisted", bool(status.get("cfgOnFlash")),
               "saved to flash" if status.get("cfgOnFlash")
               else "config is NOT on flash - it will reset on reboot")

        report("clock synced", bool(status.get("timeOk")),
               f"{clock.get('time','')} {clock.get('tzName','')} {clock.get('offset','')}"
               if status.get("timeOk") else "NTP has not answered yet")

        # Below roughly 12 kB a TLS handshake cannot be set up, and the poll
        # loop starts skipping rounds rather than rebooting.
        report("free heap", heap > 12000,
               f"{heap} bytes" if heap > 12000
               else f"only {heap} bytes - TLS handshakes will be skipped")

        code = status.get("httpCode")
        report("device -> bridge", code in (200, 204),
               "polling fine" if code in (200, 204)
               else (status.get("error") or "not configured"))

        cry = status.get("crypto") or {}
        if not cry.get("on"):
            skip("crypto feed", "switched off")
        else:
            report("crypto feed", bool(cry.get("ok")),
                   f"{len(cry.get('tickers') or [])} pairs, {cry.get('ageSec',0)}s ago"
                   if cry.get("ok") else (cry.get("error") or "no data yet"))

        # A clock coming from the bridge is not a failure, but it does mean this
        # network drops UDP 123 - worth knowing before blaming the device.
        if (status.get("clock") or {}).get("src") == "bridge":
            skip("ntp", "blocked on this network - clock is coming from the bridge")

    # --- the bridge -----------------------------------------------------
    if not a.bridge:
        skip("bridge checks", "pass --bridge to include them")
    else:
        stats: dict | None = None
        try:
            request(f"{a.bridge}/health")
            report("bridge reachable", True, a.bridge)
        except ApiError as e:
            report("bridge reachable", False, str(e))

        if not a.device_key:
            skip("bridge key", "pass --device-key to check it")
        else:
            try:
                stats = request(f"{a.bridge}/stats?key={urllib.parse.quote(a.device_key)}"
                                f"&device={urllib.parse.quote(a.id)}")
                report("bridge key accepted", True,
                       f"{(stats or {}).get('kept', 0)} signals kept")
            except ApiError as e:
                report("bridge key accepted", False, str(e))

        # Both ends independently believe a time. A large disagreement means
        # the device's NTP is adrift, which stays invisible until a signal
        # expires at the wrong moment.
        if isinstance(stats, dict) and isinstance(status, dict) and status.get("timeOk"):
            drift = abs(stats.get("now", 0) / 1000 - time.time())
            report("clocks agree", drift < 120,
                   f"within {int(drift)}s" if drift < 120
                   else f"{int(drift)}s apart - check NTP on the device")

    print(st.rule())
    print(f"{st.ACC}all checks passed{st.RESET}" if not failures
          else f"{st.RED}{failures} check(s) failed{st.RESET}")
    return 0 if not failures else 1


# --------------------------------------------------------------------------
# argument plumbing
# --------------------------------------------------------------------------


def add_signal_args(p: argparse.ArgumentParser) -> None:
    p.add_argument("--symbol", default="XAUUSD")
    p.add_argument("--side", default="BUY", choices=["BUY", "SELL", "FLAT"])
    p.add_argument("--score", type=int, default=96)
    p.add_argument("--conf", type=int, default=94)
    p.add_argument("--entry", default="3371.4")
    p.add_argument("--tp1", default="3378")
    p.add_argument("--tp2", default="3386")
    p.add_argument("--sl", default="3362")
    p.add_argument("--tf", default="15M")
    p.add_argument("--note", default="")


def build_parser() -> argparse.ArgumentParser:
    env = os.environ.get
    p = argparse.ArgumentParser(
        prog="drfx", description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter)

    p.add_argument("--device", default=env("DRFX_DEVICE", "http://godmode.local"),
                   help="device base URL (env DRFX_DEVICE)")
    p.add_argument("--device-key", default=env("DRFX_DEVICE_KEY", ""),
                   help="device key (env DRFX_DEVICE_KEY)")
    p.add_argument("--user", default=env("DRFX_USER", "admin"))
    p.add_argument("--pass", dest="password", default=env("DRFX_PASS", ""))
    p.add_argument("--bridge", default=env("DRFX_BRIDGE", ""),
                   help="Worker base URL (env DRFX_BRIDGE)")
    p.add_argument("--webhook-key", default=env("DRFX_WEBHOOK_KEY", ""))
    p.add_argument("--id", default="main", help="device id, default main")
    p.add_argument("--no-color", action="store_true")

    sub = p.add_subparsers(dest="cmd", required=True)

    sub.add_parser("status", help="one-shot status")
    w = sub.add_parser("watch", help="status, refreshed continuously")
    w.add_argument("--interval", type=float, default=1.0)

    sub.add_parser("test", help="show the demo card on the screen")
    sub.add_parser("next", help="advance the carousel one screen")
    sub.add_parser("crypto", help="prices the device is currently holding")

    add_signal_args(sub.add_parser("push", help="push a signal over the LAN"))
    add_signal_args(sub.add_parser("send", help="send a signal through the bridge"))

    h = sub.add_parser("history", help="recent signals held by the bridge")
    h.add_argument("--limit", type=int, default=12)

    tz = sub.add_parser("tz", help="inspect or change the clock's time zone")
    tzs = tz.add_subparsers(dest="tz_action", required=True)
    tzs.add_parser("list", help="every zone the picker knows")
    tzs.add_parser("get", help="what the device is set to")
    tzset = tzs.add_parser("set", help="change it, no reboot needed")
    tzset.add_argument("zone", help="IANA name or city, e.g. Europe/London")

    sub.add_parser("doctor", help="check the whole chain")

    b = sub.add_parser("backup", help="save every setting to a JSON file")
    b.add_argument("file", nargs="?", default="drfx-backup.json",
                   help="where to write it (default drfx-backup.json)")
    r = sub.add_parser("restore", help="write a backup back to the device")
    r.add_argument("file", help="the backup JSON to restore")
    return p


COMMANDS = {
    "status": cmd_status, "watch": cmd_watch, "test": cmd_test,
    "next": cmd_next, "crypto": cmd_crypto,
    "push": cmd_push, "send": cmd_send, "history": cmd_history,
    "tz": cmd_tz, "doctor": cmd_doctor,
    "backup": cmd_backup, "restore": cmd_restore,
}


def main(argv: list[str] | None = None) -> int:
    a = build_parser().parse_args(argv)
    a.device = a.device.rstrip("/")
    a.bridge = a.bridge.rstrip("/")
    st = make_style(a.no_color)
    try:
        return COMMANDS[a.cmd](a, st)
    except ApiError as e:
        print(f"{st.RED}{e}{st.RESET}", file=sys.stderr)
        return 1
    except KeyboardInterrupt:
        return 130


if __name__ == "__main__":
    raise SystemExit(main())
