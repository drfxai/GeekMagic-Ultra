#!/usr/bin/env python3
"""
DrFX GodMode - send a test signal without waiting for TradingView.

Two targets:

  Through the bridge (exactly what TradingView does):
    python send-test-signal.py --bridge https://drfx-godmode-bridge.you.workers.dev \
                               --key YOUR_WEBHOOK_KEY --symbol XAUUSD --side BUY --score 96

  Straight to the device on your LAN (skips Cloudflare entirely):
    python send-test-signal.py --device http://godmode.local \
                               --key YOUR_DEVICE_KEY --symbol EURUSD --side SELL --score 71

Only needs the Python standard library.
"""

import argparse
import json
import sys
import urllib.error
import urllib.parse
import urllib.request


def main() -> int:
    p = argparse.ArgumentParser(description="Send a GodMode test signal")
    tgt = p.add_mutually_exclusive_group(required=True)
    tgt.add_argument("--bridge", help="Cloudflare Worker base URL")
    tgt.add_argument("--device", help="SmallTV base URL, e.g. http://godmode.local")

    p.add_argument("--key", required=True,
                   help="WEBHOOK_KEY when using --bridge, DEVICE_KEY when using --device")
    p.add_argument("--id", default="main", help="device id (default: main)")

    p.add_argument("--symbol", default="XAUUSD")
    p.add_argument("--side", default="BUY", choices=["BUY", "SELL", "FLAT"])
    p.add_argument("--score", type=int, default=96)
    p.add_argument("--conf", type=int, default=94)
    p.add_argument("--tp1", default="3378")
    p.add_argument("--tp2", default="3386")
    p.add_argument("--sl", default="3362")
    p.add_argument("--tf", default="15")
    p.add_argument("--note", default="")
    args = p.parse_args()

    payload = {
        "symbol": args.symbol,
        "side": args.side,
        "score": args.score,
        "conf": args.conf,
        "tp1": args.tp1,
        "tp2": args.tp2,
        "sl": args.sl,
        "tf": args.tf,
        "note": args.note,
    }

    if args.bridge:
        base = args.bridge.rstrip("/")
        url = f"{base}/tv?" + urllib.parse.urlencode({"key": args.key, "device": args.id})
    else:
        base = args.device.rstrip("/")
        url = f"{base}/api/push?" + urllib.parse.urlencode({"key": args.key})
        # The device wants a timestamp it can compare against; any increasing
        # number works. Milliseconds since the epoch is what the bridge sends.
        import time
        payload["ts"] = int(time.time() * 1000)

    body = json.dumps(payload).encode()
    req = urllib.request.Request(
        url, data=body,
        headers={"Content-Type": "application/json", "User-Agent": "drfx-godmode-test/1.0"},
        method="POST",
    )

    try:
        with urllib.request.urlopen(req, timeout=15) as r:
            print(f"{r.status} {r.reason}")
            text = r.read().decode(errors="replace").strip()
            if text:
                print(text[:800])
    except urllib.error.HTTPError as e:
        detail = e.read().decode(errors="replace").strip()
        print(f"HTTP {e.code}: {detail or e.reason}", file=sys.stderr)
        if e.code == 403:
            print("\nThat key was rejected. --bridge wants WEBHOOK_KEY; "
                  "--device wants DEVICE_KEY.", file=sys.stderr)
        return 1
    except urllib.error.URLError as e:
        print(f"Could not reach {url}\n  {e.reason}", file=sys.stderr)
        return 1

    print("\nSent. The screen should update within your poll interval "
          "(a few seconds by default).")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
