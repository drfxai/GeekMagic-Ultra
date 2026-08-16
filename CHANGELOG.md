# Changelog

Notable changes, newest first. Versions follow the firmware, and the bridge
tracks the same major number.

## 2.1.2 — crypto: stop failing silently, and stop leading with a dead source

Both halves need deploying: redeploy the Worker **and** reflash the firmware.

**A crypto fetch could fail without reporting anything, anywhere.** The Worker
was healthy the whole time — probed from two unrelated Cloudflare colos it
returned Coinbase prices in under 200 ms — but a device that never completed a
fetch showed no crypto screen and no error, which is indistinguishable from
having crypto switched off.

- `pollCrypto()` returned early on any negative status from `bridgeGet()`. That
  made the `-2` and fallback branches at the end of the same function
  unreachable, so **"skipped, low memory", "WiFi not connected", "no bridge URL
  saved" and "no device key saved" could never reach the screen or the status
  page** — the four failures that are hardest to guess from the outside were the
  four that said nothing. The early return is gone; negative codes now fall
  through to the reporting branches.
- `buildSlots()` only created a crypto slot when `crypto.ok` was true. Since
  `crypto.ok` stays false until the first good fetch, `drawCrypto`'s "NO DATA"
  card — the only thing that renders `crypto.error` on the panel — was
  unreachable on exactly the devices that needed it. Crypto now gets one slot
  when it is enabled and has an error to report.
- An empty `symbols` list is reported as `no symbols configured` instead of
  sharing the silent return.
- `bridgeGet()` distinguishes AP mode from a dropped WiFi association rather
  than returning a bare `-1` and leaving callers to print a stale `lastError`.

**Source order now follows measurement rather than intent.** CoinGecko led the
chain as the "safest default" aggregator, but its free tier rate-limits Workers:
it returns 429 on every call, so it never won — it only added a round trip in
front of the source that did. Binance costs ~950 ms to return 403.

- Chain is now Coinbase → Kraken → CoinGecko → Binance. The two that answer go
  first; the two that do not stay in as last-resort coverage.
- The device header showed a hardcoded `BINANCE` while displaying Coinbase
  prices. It now reads `src` off the wire and names whoever actually answered.

**Saving the settings page could silently erase the bridge URL.** `save()` posts
every field, and `loadConfig()` populates them — but nothing stopped a save from
running after a *failed* load. When that happened the browser posted empty
strings over `bridge`, `ssid`, `host`, `devId` and `symbols`. Secret fields
survived, because blank means "keep" for those. The signature of the bug is
therefore a device that still holds its device key but has lost its bridge URL,
and a settings page that looks merely empty rather than broken.

- `save()` now refuses to run until `/api/config` has been read back once.
- `putStr`'s `secret` flag is renamed `blankKeeps` and now also covers `bridge`.
  An empty bridge URL is never intentional — it disables the clock, the signals
  and the prices at once. Factory reset is the way to actually clear it.

**Coinbase and Kraken return 403 from Middle East colos.** Confirmed from the
device's own 502 body: `coinbase: 403, kraken: 403, coingecko: 429,
binance: 403` — all four dead — while a probe from a European colo minutes
earlier showed Coinbase and Kraken healthy. Cloudflare does not pin a client to
one colo, so both observations are true at once, and "it works when I test it in
a browser" is not evidence that the device can reach anything.

- Added **Coinpaprika** (pure aggregator, no key, no regional gate), **Bybit**
  and **OKX**, and put them at the head of the chain. The four blocked sources
  stay on for pair coverage and for colos where they do answer.
- Coinpaprika has no 24h high/low or candles on the free tier. It returns empty
  strings rather than zeros, so the footer omits the range instead of printing
  `0 - 0`, and the device draws no sparkline. A price with no chart beats a
  chart with no price.

## 2.1.1 — crypto: Binance blocks Cloudflare, so fall back to Coinbase

Bridge only. No firmware change needed — redeploy the Worker and prices appear.

**Binance returns 403 to Cloudflare Workers.** Not a geo-block and not a missing
User-Agent: every User-Agent variant returns 200 from a residential IP and none
from a Worker. Binance refuses Cloudflare's egress ranges outright, and no header
can change that. The 2.1.0 crypto endpoint was therefore dead on arrival for
anyone running the bridge as designed — the device reported a bare `HTTP 502`.

- `/crypto` now tries a chain of sources and returns whichever answers, naming
  the winner in a `src` field. Binance stays first, since it has the best
  coverage and may work from edges not yet seen; **Coinbase** backs it up, with
  a public API, no key, no datacenter blocking, and both a 24h summary and
  hourly candles for the sparkline.
- 24h change on Coinbase is derived from open and last, since `/stats` has no
  change field. Expect a percentage a little different from Binance's — the two
  measure from slightly different points.
- `?source=binance|coinbase` pins one source, for attributing a price on screen
  or testing one that is not currently winning.
- A failing source now reports its real HTTP status. A 404 means one pair is not
  listed there and does not condemn the source; anything else does, and is named
  in the `tried` array so the cause is visible rather than guessed at.

## 2.1.0 — carousel and crypto

### The carousel

- Screens now take turns. Default 15 seconds each, configurable on the Display
  tab; `0` stops the rotation.
- **Slots are dynamic.** A screen exists only while it has something to show, so
  an expired signal or a crypto fetch that has never succeeded drops out of the
  rotation instead of displaying an empty card.
- **A fresh signal interrupts and pins** for 60 seconds, then rejoins the
  rotation. A new entry is the one genuinely time-sensitive thing on this device;
  making it wait behind a price defeats the purpose of the screen.
- `POST /api/next` and `drfx next` step the carousel by hand, which saves waiting
  out the timer while checking a layout.

### Crypto

- New crypto screen: asset, 24h change, large price, a 24-hour sparkline and the
  24h range. One screen per pair, up to four, set on the new **Crypto** tab.
- New bridge route `GET /crypto`. The Worker fetches Binance, trims the response
  to the fields a 240×240 screen can use, scales the sparkline to 24 integers and
  caches for 20 seconds at the edge.
- **The device never calls Binance directly.** It does not negotiate small TLS
  fragments, so a direct request would want a 16 kB receive buffer out of roughly
  39 kB of free heap — while the signal poll periodically wants the same. Going
  through the Worker means one TLS host, a few hundred bytes on the wire, and no
  floating-point arithmetic on the device at all.
- Market data comes from `data-api.binance.vision` rather than `api.binance.com`,
  which geo-blocks some regions and would have failed depending on which edge the
  Worker happened to run in.
- Sparklines degrade independently: if the klines request fails you still get
  prices, just without a chart.
- `drfx crypto` shows what the device is holding, which is not the same question
  as what Binance says right now.

### Clock reliability

- **The clock now falls back to the bridge when NTP is silent.** Plenty of
  routers and captive networks quietly drop UDP port 123, and without a fallback
  the device simply never learns the time — which is exactly what was happening
  on the author's own network. `/stats` already reported the Worker's clock, so it
  fills the gap after 90 seconds of NTP silence, retrying at most once a minute.
- Status, the settings page and `drfx doctor` all report whether the time came
  from NTP or the bridge. A clock arriving over the bridge is not a fault, but it
  is a fact about your network worth surfacing rather than hiding.

### Internals

- The TLS request path is now a single `bridgeGet()` used by the signal poll, the
  crypto fetch and the clock fallback, so the heap guard exists in one place.
- **At most one TLS session per pass through `loop()`.** Each holds a 16 kB
  buffer for its lifetime, and two overlapping is what a reboot looks like, so
  the three network jobs take turns rather than firing whenever their timers
  happen to coincide.
- `ui/screens-preview.html` marks which screens actually exist in firmware.
  Previously all fourteen looked equally real; only three were.

## 2.0.1 — repository tidy-up

No functional change to the firmware, the bridge or the CLI.

### Layout

The root had grown to fourteen entries with vendor binaries, guides and code all
at the same level. Everything now sits under a folder that says what it is:

- `SETUP.md` and `CLOUDFLARE.md` moved into `docs/`, alongside `DESIGN.md` and
  `CLI.md`. The root keeps only the files GitHub itself renders — README,
  LICENSE, CHANGELOG, CONTRIBUTING, SECURITY.
- GeekMagic's stock firmware moved to `vendor/stock-firmware/`, with a
  [README](vendor/README.md) recording where it came from, its checksum, and the
  fact that it is not covered by our licence.
- Superseded 1.x artifacts — the old packaged bridge and a photograph of the old
  screen design — moved to `archive/`, clearly marked as reference only.

### Removed

- `tools/send-test-signal.py` — superseded by `drfx push` and `drfx send`, which
  do the same two jobs with better diagnostics.
- `firmware/src/signal.h` — a forwarding shim that existed only to stop a file of
  that name shadowing the C library's `<signal.h>`. With the model living in
  `signal_model.h` there is nothing left to shadow, and the shim's own comment
  said it was safe to delete.

### Added

- `SECURITY.md` — reporting process, the threat model, and an explicit list of
  known trade-offs (skipped certificate validation, keys in query strings) so
  they are not repeatedly reported as vulnerabilities.

### Documentation corrected

The build was telling the truth and the docs were not. Both images exceed the
~440 kB the stock updater leaves for an over-the-air update — full is about
587 kB, slim about 512 kB — but README and SETUP still said to flash the slim
image through the stock web console. The flashing guide now leads with UART,
keeps the stock-updater attempt as an optional two-minute first try, and the
build summary explains what the 440 kB figure is and what a `NO` in the size
table actually means.

## 2.0.0

### Design

- New **minimal terminal** design language across the device, the settings page
  and the bridge's status page — black field, hairline rules, one accent per
  screen, one large value carrying the meaning. Specified in
  [docs/DESIGN.md](docs/DESIGN.md).
- New `firmware/src/ui.h`: the layout grid, colour derivation and drawing
  primitives now live in one place, so a change to the design language is a
  change to one file.
- The signal card was rebuilt around the score as a 48px numeral with a
  confidence bar, a three-column TP1/TP2/SL row, and a footer showing entry and
  a **risk:reward computed on the device** from the levels — omitted rather than
  invented when they do not all parse as numbers.
- `FLAT` events (a TP hit, a stop) now render their note as the headline instead
  of an empty grid of levels they do not carry.
- Removed the smooth-arc score gauge. It pulled in float and anti-aliasing code
  the slim image can ill afford, and at 240×240 a flat bar plus a large numeral
  is more legible.
- `ui/screens-preview.html` renders every screen at true size in a browser.

### Clock and timezones

- **New clock screen**: large digits, a seconds rule, date, weekday, zone and
  live UTC offset. It ticks once a second instead of once every fifteen.
- The device now stores a **POSIX TZ rule** rather than a fixed minute offset, so
  daylight saving is applied automatically. `Europe/London` is saved as
  `GMT0BST,M3.5.0/1,M10.5.0`.
- New **Clock** tab in the settings page: a grouped picker of 52 zones, a
  *Detect* button that matches the browser's own zone, and a live preview of the
  selected zone against your browser's clock.
- Changing the zone takes effect immediately — no reboot.
- `shared/timezones.json` is the single source of truth. `tools/gen_timezones.py`
  regenerates the picker baked into the firmware; `tools/test_timezones.py`
  checks every rule against the real IANA database at four dates across the year.
  CI runs both.
- **Upgrading from 1.x is safe**: a config carrying only the old `tzMinutes` is
  converted to an equivalent fixed-offset rule on first boot. `tzMinutes` is
  still written on save, so downgrading works too.

### Bridge

- New `GET /history` — recent signals as JSON.
- New `GET /stats` — counts by side and symbol, plus the Worker's own clock,
  which is what lets `drfx doctor` catch a device whose NTP has drifted.
- Signal history increased from 8 entries to 24.
- CORS headers and `OPTIONS` preflight on the read endpoints, so a local
  dashboard can call them directly. `/tv` is deliberately excluded.
- `x-drfx-bridge` version header on every response.
- Status page restyled to match the device.
- `/latest` is unchanged on the wire — a 1.x device keeps working against a 2.0
  bridge.

### Terminal client

- New `tools/drfx.py`, standard library only:
  `status`, `watch`, `test`, `push`, `send`, `history`, `tz list|get|set`,
  `doctor`.
- `doctor` walks the whole chain — device, flash persistence, NTP, free heap,
  the poll loop, the bridge, the key, and whether the two clocks agree — and
  names the broken link rather than just reporting failure. Non-zero exit on
  failure, so it drops into a monitoring script.
- `tools/send-test-signal.py` is superseded by `drfx push` / `drfx send` and was
  removed in the 2.0.1 tidy-up.

### Repository

- Added `LICENSE` (MIT), `CONTRIBUTING.md`, this changelog, and `docs/`.
- CI gained a `checks` job: timezone table in sync, every POSIX rule verified
  against IANA, `node --check` on the Worker, and a compile check on the Python
  tools — all before the firmware build runs.

## 1.0.0

- Initial release: ESP8266 firmware, Cloudflare Worker bridge, settings page,
  slim build that fits the stock Ultra's OTA slot, GitHub Actions firmware build.
