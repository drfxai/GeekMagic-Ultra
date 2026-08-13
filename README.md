# DrFX GodMode

TradingView signals on a GeekMagic SmallTV Ultra.

**Start here → [SETUP.md](SETUP.md)**

---

## Why there's a "bridge"

TradingView only posts alerts to `https://` addresses. The SmallTV Ultra is an
ESP8266 on your home Wi‑Fi serving plain `http://` on a private IP that the
internet cannot see. Making the device itself reachable over HTTPS would mean
port forwarding, dynamic DNS and a certificate the chip can't really validate.

Instead the flow is reversed. A free Cloudflare Worker is the public HTTPS
endpoint TradingView wants; the SmallTV polls that Worker outbound every few
seconds. Nothing on your network is exposed, nothing on your router changes, and
no computer needs to stay on.

```
TradingView ──HTTPS POST──▶ Cloudflare Worker ──▶ KV store
                                                    ▲
                              SmallTV ──HTTPS GET───┘   (outbound only)
```

---

## What's in here

```
bridge/
  worker.js                 the Cloudflare Worker - paste into the dashboard
  wrangler.toml             or deploy from the command line
firmware/
  platformio.ini            board + panel configuration for the SmallTV Ultra
  src/main.cpp              boot, Wi-Fi, web server, polling loop
  src/config.h              settings struct, saved as /config.json
  src/signal.h              the signal record and JSON parsing
  src/display.h             the GodMode card, gauge and idle clock
  src/web_ui.h              the settings page, served from flash
tradingview/
  alert-message.json        alert templates + full field reference
tools/
  send-test-signal.py       fire a test signal without TradingView
.github/workflows/build.yml GitHub compiles the .bin for you - no toolchain
SETUP.md                    the step-by-step guide
```

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
[ESPHome device page](https://devices.esphome.io/devices/geekmagic-ultra/) for this board.

---

## Design notes

**Why polling rather than a push.** An inbound connection to the device would need
the internet to reach your LAN. Polling is outbound, so it works behind any router
untouched. The Worker answers `204 No Content` when nothing has changed, so the
quiet case costs the chip almost nothing.

**Why no sprites.** A full 240×240 16‑bit frame buffer is 115 kB; the ESP8266 has
roughly 40 kB of usable heap. Text is drawn with an explicit background colour and
padding instead, which avoids flicker without the memory.

**Why the TLS buffer is allocated per request.** BearSSL needs up to 16 kB for its
receive buffer. It is created inside the poll function and freed immediately after,
so that memory is only tied up for the second or so a request takes. On boot the
firmware probes whether the server supports smaller TLS fragments; if it does, a
1 kB buffer is used instead.

**Why certificate validation is skipped.** No root store fits comfortably alongside
the display driver. The shared device key in the URL is what authenticates the
exchange. Only signal data travels this path — see the security note at the end of
SETUP.md, and treat the screen as a glanceable notification rather than a trade
instruction.

---

## Licence

MIT. Not affiliated with GeekMagic or TradingView.
