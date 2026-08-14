# `drfx` — the terminal client

One Python file, standard library only, Python 3.9+. Nothing to install.

```bash
python tools/drfx.py status
```

Optionally put it on your path:

```bash
chmod +x tools/drfx.py
ln -s "$PWD/tools/drfx.py" ~/.local/bin/drfx     # macOS / Linux
drfx status
```

The CLI reads `shared/timezones.json` from the repository, so run it from a
checkout rather than copying the single file elsewhere.

---

## Connection settings

Flags win over environment variables. Setting the environment once is usually
easier:

```bash
export DRFX_DEVICE=http://godmode.local
export DRFX_DEVICE_KEY=the-device-key
export DRFX_PASS=your-settings-password
export DRFX_BRIDGE=https://fx-godmode-bridge.you.workers.dev
export DRFX_WEBHOOK_KEY=the-webhook-key
```

| Variable | Flag | Default | Used by |
|---|---|---|---|
| `DRFX_DEVICE` | `--device` | `http://godmode.local` | everything local |
| `DRFX_DEVICE_KEY` | `--device-key` | — | `push`, `history`, `doctor` |
| `DRFX_USER` | `--user` | `admin` | `test`, `tz set` |
| `DRFX_PASS` | `--pass` | — | `test`, `tz set` |
| `DRFX_BRIDGE` | `--bridge` | — | `send`, `history`, `doctor` |
| `DRFX_WEBHOOK_KEY` | `--webhook-key` | — | `send` |

`--id main` selects which device slot on the bridge to talk to, when one Worker
drives more than one screen.

---

## Commands

### `status` / `watch`

```
$ drfx status
DRFX ULTRA OS  v2.0.0                              192.168.1.42
───────────────────────────────────────────────────────────────
NETWORK             DRFX-5G   -51 dBm
BRIDGE              connected, nothing new
CLOCK               15:20:45  BST  UTC+01:00
TIME ZONE           Europe/London
DATE                FRIDAY 14 AUG 2026
FREE MEMORY         24880 bytes
UPTIME              2h 35m
───────────────────────────────────────────────────────────────
SIGNAL              XAUUSD BUY
SCORE / CONF        96  /  94%
LEVELS              TP1 3378   TP2 3386   SL 3362
AGE                 42s
```

`watch` redraws the same view every second (`--interval` to change it). It homes
the cursor rather than clearing the screen, so there is no flash between frames.

`/api/status` is unauthenticated, so these two need no password.

### `test`

Puts the demo card on the screen — the quickest way to check a layout or a theme
change. Needs the settings password.

### `push` — device, over the LAN

```bash
drfx push --symbol EURUSD --side SELL --score 71 --tp1 1.0910 --sl 1.0990
```

Goes straight to `/api/push` on the device, authenticated with the device key.
Cloudflare is not involved, so this works with no internet at all. Useful for
driving the screen from a local script or from Home Assistant.

### `send` — through the bridge

```bash
drfx send --symbol XAUUSD --side BUY --score 96
```

Posts to the Worker's `/tv` endpoint exactly as TradingView would, using the
webhook key. This is the honest end-to-end test: if `send` shows up on the
screen, a real alert will too.

### `tz`

```bash
drfx tz list                  # every zone the picker knows, grouped
drfx tz get                   # what the device is set to right now
drfx tz set Europe/London     # IANA name…
drfx tz set Tokyo             # …or just the city
```

`set` takes effect immediately — no reboot. It stores the POSIX rule, so
daylight saving is handled by the device from then on.

### `history`

Recent signals held by the bridge, newest first. Needs `--bridge` and the device
key.

### `doctor`

Walks the whole chain and names the broken link:

```
$ drfx doctor
DIAGNOSTICS                                        http://godmode.local
───────────────────────────────────────────────────────────────────────
  pass  device reachable          http://godmode.local
  pass  settings persisted        saved to flash
  pass  clock synced              15:20:46 Europe/London UTC+01:00
  FAIL  free heap                 only 9600 bytes - TLS handshakes will be skipped
  pass  device -> bridge          polling fine
  pass  bridge reachable          https://fx-godmode-bridge.you.workers.dev
  pass  bridge key accepted       3 signals kept
  pass  clocks agree              within 1s
───────────────────────────────────────────────────────────────────────
  1 check(s) failed
```

Exit code is non-zero when anything failed, so it drops into a monitoring script
without extra work.

The **clocks agree** check compares the Worker's clock against this machine's.
The device has no battery-backed RTC and depends entirely on NTP; a drifting
clock is otherwise invisible until a signal expires at the wrong moment.

---

## Exit codes

| Code | Meaning |
|---|---|
| 0 | fine |
| 1 | the request failed, or `doctor` found a problem |
| 130 | ctrl-c |

Colour is on for interactive terminals and off when piped. `--no-color` or the
conventional `NO_COLOR` environment variable disables it explicitly.
