# DrFX Ultra OS

Live trading signals and crypto prices on a £20 desk screen.

Replaces the stock software on a **GeekMagic SmallTV Ultra** with a clean
terminal-style display that shows your TradingView alerts the moment they fire,
alongside live prices and a clock.

### → **[Start here: QUICKSTART.md](QUICKSTART.md)** ←

About 20 minutes. No coding. Everything you need is free.

```
TradingView ──HTTPS POST──▶ Cloudflare Worker ──▶ KV store
                                                    ▲
                              SmallTV ──HTTPS GET───┘   (outbound only)
```

---

## What you get

**Signal cards.** Symbol, direction, a score ring, take-profit and stop levels —
on screen within about five seconds of the alert firing.

**Live crypto prices.** Up to four pairs with 24-hour change and an hourly
sparkline, fetched through the bridge from whichever source is reachable.

**A clock that survives blocked NTP.** Plenty of networks silently drop time
sync. If yours does, the bridge supplies the time instead.

**A carousel.** Screens rotate on a timer. A fresh signal interrupts and holds
the display, because that is the thing you actually care about.

Open [`ui/screens-preview.html`](ui/screens-preview.html) in a browser to see
every screen at true size — no hardware needed.

---

## Why there's a bridge

TradingView only posts alerts to `https://` addresses. The SmallTV is an ESP8266
on your home Wi-Fi, serving plain `http://` on a private IP the internet cannot
see. Exposing it would mean port forwarding, dynamic DNS and a certificate the
chip cannot really validate.

So the flow is reversed. A free Cloudflare Worker is the public HTTPS endpoint
TradingView wants, and the SmallTV polls it outbound every few seconds. Nothing
on your network is exposed, nothing on your router changes, and no computer has
to stay switched on.

The bridge also does the work the chip cannot. The ESP8266 has roughly 39 kB of
usable RAM and each TLS connection wants a 16 kB buffer, so it can barely hold
one conversation at a time — never mind four exchanges. The Worker fetches from
whichever price source answers and hands back a few hundred bytes with the
sparkline already scaled to 0–100, so the device does no maths.

---

## The pieces

| | What it is |
|---|---|
| **[firmware/](firmware/)** | ESP8266 firmware — Wi-Fi, settings page, poll loop, the screens |
| **[bridge/](bridge/)** | The Cloudflare Worker, plus one-shot deploy scripts |
| **[tools/](tools/)** | `keygen.html` key generator, the `drfx` terminal client, timezone tooling |
| **[ui/](ui/)** | Every screen rendered at true size, openable in a browser |
| **[tradingview/](tradingview/)** | Alert message templates and the full field reference |
| **[vendor/](vendor/)** | GeekMagic's stock firmware — the way back to factory |

---

## The terminal client

Optional, but the fastest way to see what is going on. Standard library only,
Python 3.9+, nothing to install:

```bash
python tools/drfx.py status     # what the device is doing right now
python tools/drfx.py doctor     # test every link in the chain
python tools/drfx.py crypto     # prices the device is holding
python tools/drfx.py test       # put a demo card on the screen
```

`doctor` is the one to reach for when something is wrong — it checks the device,
the bridge and the price sources in turn and names whichever is broken.

---

## Building the firmware yourself

You do not need to. [Releases](https://github.com/drfxai/GeekMagic-Ultra/releases)
has both images prebuilt, and every push is compiled by GitHub Actions.

If you want to anyway:

```bash
cd firmware && pio run          # needs PlatformIO
```

Two images are produced. `smalltv_ultra_slim` is trimmed to fit through the
stock updater and is what you flash first. `smalltv_ultra` is the full build —
mDNS, smooth fonts, the complete cipher list — and you can move up to it from
the device itself once the slim image is running.

**Hardware:** ESP-12F (ESP8266), 4 MB flash, 1.54" 240×240 IPS ST7789V panel.

---

## Security

The two keys are all that stand between the open internet and your screen.
Keep them out of Git — `SECRETS.local.md` is gitignored for this reason. Rotate
them any time by generating new ones and updating both Cloudflare and the
device.

Found a vulnerability? [SECURITY.md](SECURITY.md).

---

## Contributing

Pull requests welcome — see [CONTRIBUTING.md](CONTRIBUTING.md).

MIT licensed. Not affiliated with GeekMagic or TradingView.
Nothing here is financial advice.
