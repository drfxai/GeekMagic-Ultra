# Contributing

## Before you push

```bash
python tools/gen_timezones.py --check     # picker in web_ui.h is in sync
python tools/test_timezones.py            # every POSIX rule matches IANA
node --check bridge/worker.js             # Worker parses
python -m compileall -q tools/            # tools parse
```

CI runs exactly these, then builds both firmware images. You do not need a
toolchain locally — pushing to any branch produces both `.bin` files as an
Actions artifact, with their sizes reported in the run summary.

---

## Adding a screen

Open [`ui/screens-preview.html`](ui/screens-preview.html) first — it renders
every existing screen at true size, and the layout tokens it uses are the
vocabulary a new screen should be built from. Then:

1. Build it from the primitives in `firmware/src/ui.h`. If you need a pixel value
   that is not a position, it belongs in `ui.h` as a token, not in `display.h`.
2. **Check it in the slim build too.** That image drops fonts 6 and 7, so
   `UI_F_NUM` and `UI_F_CLOCK` both collapse to font 4. A layout that only holds
   at 48px is broken for anyone running the image that fits the stock OTA slot.
3. Remember fonts 6 and 7 are digit-only. Anything with letters uses font 4 or
   smaller.
4. Mirror the layout in `ui/screens-preview.html` so it can be reviewed without
   hardware.

Flash is the binding constraint, and both images are **already over** the ~440 kB
the stock updater leaves for an over-the-air install — which is why the first
flash goes over UART. That does not make the budget irrelevant: it makes growth
worth watching, since every kilobyte moves the slim image further out of reach of
ever fitting again. The build summary prints both sizes on every run. A change
that adds meaningfully to either should say in its description what it bought.

## Changing the timezone list

Edit `shared/timezones.json`, then:

```bash
python tools/gen_timezones.py     # regenerates the block inside web_ui.h
python tools/test_timezones.py    # verifies against the IANA database
```

Never hand-edit between the `TZLIST-BEGIN` / `TZLIST-END` markers.

POSIX offsets are **west-positive** — the reverse of how they are usually
written. `Asia/Kolkata` at UTC+05:30 is `IST-5:30`. The test catches this, which
is the whole reason it exists.

## Changing the bridge

`GET /latest` is a wire contract with every device already in the field. Adding
fields is fine — the firmware ignores what it does not know. Renaming or removing
one is a breaking change and needs a firmware release alongside it.

Test it without deploying:

```bash
node --check bridge/worker.js
npx wrangler dev            # then: curl 'http://localhost:8787/health'
```

## Style

- Comments explain *why*, and especially why an obvious alternative was not
  taken. The codebase is full of constraints that are invisible from the code —
  40 kB of heap, a 440 kB OTA slot, digit-only fonts — and a comment that records
  one saves the next person an afternoon.
- No new runtime dependencies in `tools/`. Standard library only, Python 3.9+.
- Keep `firmware/src/web_ui.h` self-contained: no CDN, no framework. It has to
  work on a device with no route to the internet.

## Secrets

Never commit keys. `SECRETS.local.md`, `*.local.env` and `.dev.vars` are already
ignored — keep it that way. Worker secrets belong in `wrangler secret put`, not
in `wrangler.toml`.

Found a vulnerability rather than a bug? [SECURITY.md](SECURITY.md) has the
reporting process and the list of known trade-offs that are not vulnerabilities.

## Where things go

| If you are adding… | It goes in |
|---|---|
| code that runs on the device | `firmware/src/` |
| code that runs on Cloudflare | `bridge/` |
| a script that runs on your machine | `tools/`, standard library only |
| data more than one component needs | `shared/` |
| a third-party file, unmodified | `vendor/`, with provenance in its README |

Setup instructions belong in [`QUICKSTART.md`](QUICKSTART.md) and nowhere else.
It is deliberately the only guide in the repository: when the same step lived in
several files, the copies drifted and readers followed whichever was wrong.
Superseded material gets deleted rather than archived — Git already remembers
it, and a folder of stale artifacts only invites people to follow them.
