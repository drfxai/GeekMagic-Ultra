# DrFX Ultra OS

A small framework for putting live trading signals on a GeekMagic SmallTV Ultra —
firmware, an HTTPS bridge, a terminal client and a design system that keeps them
looking like one product.

**New here → [docs/SETUP.md](docs/SETUP.md)** · Configuring Cloudflare by hand →
**[docs/CLOUDFLARE.md](docs/CLOUDFLARE.md)**

```
TradingView ──HTTPS POST──▶ Cloudflare Worker ──▶ KV store
                                                    ▲
                              SmallTV ──HTTPS GET───┘   (outbound only)
```

---

## Why there's a bridge

TradingView only posts alerts to `https://` addresses. The SmallTV Ultra is an
ESP8266 on your home Wi‑Fi serving plain `http://` on a private IP the internet
cannot see. Making the device itself reachable over HTTPS would mean port
forwarding, dynamic DNS and a certificate the chip cannot really validate.

So the flow is reversed. A free Cloudflare Worker is the public HTTPS endpoint
TradingView wants; the SmallTV polls it outbound every few seconds. Nothing on
your network is exposed, nothing on your router changes, and no computer needs to
stay switched on.

---

## The pieces

| | What it is | Start at |
|---|---|---|
| **firmware/** | ESP8266 firmware: Wi‑Fi, settings server, poll loop, three screens | [`src/main.cpp`](firmware/src/main.cpp) |
| **bridge/** | Cloudflare Worker — the HTTPS endpoint and a week of signal history | [`worker.js`](bridge/worker.js) |
| **tools/** | `drfx` terminal client, plus the timezone generator and its test | [docs/CLI.md](docs/CLI.md) |
| **ui/** | The design system rendered at true size, openable in a browser | [`screens-preview.html`](ui/screens-preview.html) |
| **shared/** | `timezones.json` — one source of truth for the clock | below |
| **docs/** | Setup, Cloudflare, design system, CLI reference | [docs/SETUP.md](docs/SETUP.md) |
| **vendor/** | GeekMagic's stock firmware — the way back to factory | [vendor/README.md](vendor/README.md) |
| **archive/** | Superseded 1.x artifacts, kept for reference only | [archive/README.md](archive/README.md) |

```
firmware/                   what runs on the device
  platformio.ini            board + panel configuration
  src/main.cpp              boot, Wi-Fi, web server, polling loop
  src/config.h              settings struct, saved as /config.json
  src/signal_model.h        the signal record and JSON parsing
  src/ui.h                  layout grid, colour derivation, primitives
  src/display.h             the three screens
  src/web_ui.h              the settings page, served from flash
bridge/                     what runs on Cloudflare
  worker.js                 paste into the dashboard, or
  wrangler.toml             deploy from the command line
  deploy.sh, deploy.ps1     one-shot deploy scripts
shared/                     used by more than one component
  timezones.json            zones + POSIX rules, source of truth
tools/                      what runs on your machine
  drfx.py                   the terminal client
  gen_timezones.py          regenerates the picker inside web_ui.h
  test_timezones.py         checks every rule against the IANA database
ui/                         the design system, reviewable without hardware
  screens-preview.html      every screen at 240x240, in a browser
docs/                       SETUP, CLOUDFLARE, DESIGN, CLI
tradingview/
  alert-message.json        alert templates + field reference
vendor/                     third-party, unmodified — GeekMagic stock firmware
archive/                    superseded 1.x artifacts, reference only
```

---

## The screens

Minimal terminal: black field, hairline rules, one accent per screen, one large
value carrying the meaning. Open
[`ui/screens-preview.html`](ui/screens-preview.html) to see them at true size —
no hardware needed. The full specification is in [docs/DESIGN.md](docs/DESIGN.md).

**Signal** — symbol and direction, the AI score as a 48px numeral, a confidence
bar, TP1/TP2/SL across the bottom, and a risk:reward the device computes from the
levels (omitted rather than invented when they do not all parse).

**Clock** — large digits, a seconds rule, date, weekday, zone and live UTC offset.

**Crypto** — one screen per pair: asset, 24h change, a large price, a sparkline
of the last 24 hours, and the 24h range.

**Banner** — boot, setup mode and error states.

They take turns. The carousel changes screen every 15 seconds by default, and a
slot only exists while it has something to show — an expired signal or prices
that have not arrived yet simply drop out of the rotation rather than displaying
an empty card. A **fresh signal interrupts and holds the screen** for 60 seconds
before rejoining, because a new entry is the one genuinely time-sensitive thing
this device shows. Both intervals are configurable, and `0` stops the rotation.

---

## Crypto prices

Set a watchlist of up to four Binance pairs on the **Crypto** tab, or from the
terminal. Prices refresh every 30 seconds.

```bash
drfx crypto        # what the device is holding right now
```

**The device never calls Binance directly.** It does not negotiate small TLS
fragments, so every direct HTTPS request would need a 16 kB receive buffer out of
roughly 39 kB of free heap — while the signal poll is periodically asking for the
same thing. Instead the Worker fetches Binance, trims the response to the handful
of fields a 240×240 screen can use, scales the sparkline to 24 integers, and
caches for 20 seconds at Cloudflare's edge. The device receives a few hundred
bytes and does no floating-point arithmetic at all.

Market data comes from `data-api.binance.vision`, Binance's public market-data
host — no key, and no geo-blocking surprises depending on which edge the Worker
happened to run in.

---

## The clock

The device stores a **POSIX TZ rule**, not a fixed offset. `Europe/London` is
saved as `GMT0BST,M3.5.0/1,M10.5.0`, which carries its own changeover dates — so
the screen is right on the mornings either side of the daylight-saving switch
without anyone touching a setting.

Pick a zone on the **Clock** tab of the settings page, or from the terminal:

```bash
drfx tz set Europe/London      # or just: drfx tz set London
```

Both read [`shared/timezones.json`](shared/timezones.json). Edit that file and
run `python tools/gen_timezones.py` to regenerate the picker baked into the
firmware; `tools/test_timezones.py` then checks every rule against the real IANA
database at four dates across the year. CI runs both.

> POSIX offsets are **west-positive** — the opposite sign to the `UTC+05:30` that
> people write. `Asia/Kolkata` is therefore `IST-5:30`. Getting this backwards is
> the classic bug here, which is exactly why the test exists.

Upgrading from 1.x keeps working: a config that only has the old `tzMinutes` is
converted to an equivalent fixed-offset rule on first boot. Pick a named zone
afterwards to gain automatic daylight saving.

**If NTP never answers**, the device falls back to the Worker's clock. Plenty of
routers and captive networks quietly drop UDP port 123, and without a fallback
the screen simply never learns the time. `/stats` already reports the bridge's
clock, so it fills the gap — accurate to a second or two, which is ample for a
wall clock. The status page and `drfx status` both say when the time is coming
from the bridge rather than NTP, since that is a fact about your network worth
knowing.

---

## The device at a glance

| | |
|---|---|
| MCU | ESP‑12F (ESP8266), 4 MB flash |
| Panel | 1.54" 240×240 IPS, ST7789V, BGR, inverted, SPI mode 3 |
| Pins | SCLK 14, MOSI 13, DC 0, RST 2, CS not wired, backlight 5 (PWM, active low) |
| Settings | `http://godmode.local` |
| Updates | Admin → Firmware update, over Wi‑Fi |

Hardware details confirmed against the
[ESPHome device page](https://devices.esphome.io/devices/geekmagic-ultra/).

**You do not need a toolchain.** Push to GitHub and
[the build workflow](.github/workflows/build.yml) compiles both images — `full`
and `SLIM` — and attaches them to the Actions run, with their sizes in the run
summary.

> **Expect to need a serial cable for the first flash.** The stock Ultra firmware
> reserves most of the chip for its photo album, leaving roughly 440 kB for an
> over-the-air update. Both current images are larger than that, so the stock web
> updater will usually refuse them and the first install goes over UART. After
> that the device runs our flash layout, with ~3 MB of program area, and every
> later update is a browser upload via **Admin → Firmware update**.
>
> Check the size table in the build summary before you start — if `SLIM` fits your
> unit's slot, the stock updater is worth trying first. Full instructions, wiring
> included, are in [docs/SETUP.md](docs/SETUP.md).

---

## Bridge API

| Route | Auth | Purpose |
|---|---|---|
| `POST /tv?key=…&device=…` | `WEBHOOK_KEY` | where TradingView posts |
| `GET /latest?key=…&device=…&since=…` | `DEVICE_KEY` | what the device polls; `204` when nothing is new |
| `GET /history?key=…&device=…&limit=…` | `DEVICE_KEY` | recent signals as JSON |
| `GET /stats?key=…&device=…` | `DEVICE_KEY` | counts, plus the Worker's clock |
| `GET /crypto?key=…&symbols=…` | `DEVICE_KEY` | Binance prices, trimmed, with a scaled sparkline |
| `GET /health` | — | plain `ok` |
| `GET /` | — | human status page |

Alert bodies may be JSON, loose `key=value` text, a bare `XAUUSD BUY` prefix, or
the GodMode indicator's `[[DRFX]]` telemetry tag — see
[`tradingview/alert-message.json`](tradingview/alert-message.json).

---

## Design notes

**Why polling rather than a push.** An inbound connection would need the internet
to reach your LAN. Polling is outbound, so it works behind any router untouched.
The Worker answers `204 No Content` when nothing has changed, so the quiet case
costs the chip almost nothing.

**Why no sprites.** A full 240×240 16‑bit frame buffer is 115 kB; the ESP8266 has
roughly 40 kB of usable heap. Text is drawn with an explicit background colour
and padding instead, which avoids flicker without the memory.

**Why the TLS buffer is allocated per request.** BearSSL needs up to 16 kB for its
receive buffer. It is created inside the poll function and freed immediately
after, so that memory is only tied up for the second or so a request takes. On
boot the firmware probes whether the server supports smaller TLS fragments; if it
does, a 1 kB buffer is used instead.

**Why certificate validation is skipped.** No root store fits comfortably
alongside the display driver. The shared device key in the URL is what
authenticates the exchange. Only signal data travels this path — see
[SECURITY.md](SECURITY.md), and treat the screen as a glanceable notification
rather than a trade instruction.

---

## Contributing and security

See [CONTRIBUTING.md](CONTRIBUTING.md). Changes to
[`shared/timezones.json`](shared/timezones.json) must be followed by
`python tools/gen_timezones.py`; CI fails otherwise.

Security policy, threat model and reporting: [SECURITY.md](SECURITY.md).

## Licence

MIT — see [LICENSE](LICENSE). Not affiliated with GeekMagic or TradingView.
Files under [`vendor/`](vendor/README.md) belong to their original authors and
are not covered by that licence.
