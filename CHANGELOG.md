# Changelog

Notable changes, newest first. Versions follow the firmware, and the bridge
tracks the same major number.

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
