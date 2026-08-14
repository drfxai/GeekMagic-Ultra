#!/usr/bin/env python3
"""
Check every POSIX rule in shared/timezones.json against the real IANA database.

This is the highest-risk code in the project. POSIX offsets are west-positive,
the reverse of how people write them, and each daylight-saving rule encodes its
own changeover dates. A sign error or a wrong changeover is invisible until the
device shows the wrong hour for half the year - by which time nobody connects
it to a settings change.

So: for each zone, set TZ to our POSIX rule and ask the C library what the
offset is on four dates spread across the year, then compare against what
zoneinfo says the real zone does. Any disagreement is a bug in the JSON.

    python tools/test_timezones.py

Requires Python 3.9+ (zoneinfo) on a platform with tzset - Linux, macOS, or
GitHub Actions. Windows has no tzset, so it skips with a clear message.
"""

from __future__ import annotations

import json
import os
import sys
import time
from datetime import datetime, timezone
from pathlib import Path

try:
    from zoneinfo import ZoneInfo
except ImportError:                                    # pragma: no cover
    print("needs Python 3.9+ for zoneinfo", file=sys.stderr)
    raise SystemExit(2)

ROOT = Path(__file__).resolve().parent.parent
ZONES = ROOT / "shared" / "timezones.json"

# Deliberately either side of both hemispheres' changeovers.
SAMPLES = [
    datetime(2026, 1, 15, 12, tzinfo=timezone.utc),
    datetime(2026, 4, 15, 12, tzinfo=timezone.utc),
    datetime(2026, 7, 15, 12, tzinfo=timezone.utc),
    datetime(2026, 10, 15, 12, tzinfo=timezone.utc),
]


def posix_offset(rule: str, when: datetime) -> int:
    """Offset in seconds east of UTC that the C library derives from `rule`."""
    os.environ["TZ"] = rule
    time.tzset()
    lt = time.localtime(when.timestamp())
    # tm_gmtoff is what the device's newlib exposes through the same rule.
    return lt.tm_gmtoff


def iana_offset(name: str, when: datetime) -> int:
    return int(when.astimezone(ZoneInfo(name)).utcoffset().total_seconds())


def main() -> int:
    if not hasattr(time, "tzset"):
        print("skipped: this platform has no time.tzset (Windows)")
        return 0

    zones = json.loads(ZONES.read_text(encoding="utf-8"))["zones"]
    saved = os.environ.get("TZ")
    failures: list[str] = []
    checked = 0

    try:
        for z in zones:
            for when in SAMPLES:
                try:
                    want = iana_offset(z["name"], when)
                except Exception as e:                 # unknown zone on this box
                    failures.append(f"{z['name']}: cannot resolve IANA zone ({e})")
                    break
                got = posix_offset(z["posix"], when)
                checked += 1
                if got != want:
                    failures.append(
                        f"{z['name']:<32} {when:%d %b}  rule {z['posix']!r}  "
                        f"gives UTC{got / 3600:+.2f} but IANA says UTC{want / 3600:+.2f}"
                    )
    finally:
        if saved is None:
            os.environ.pop("TZ", None)
        else:
            os.environ["TZ"] = saved
        time.tzset()

    if failures:
        print(f"{len(failures)} problem(s) in {ZONES.name}:\n", file=sys.stderr)
        for f in failures:
            print("  " + f, file=sys.stderr)
        return 1

    print(f"ok - {len(zones)} zones, {checked} offset checks across the year")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
