# DrFX GodMode — setup guide

Turn the GeekMagic SmallTV Ultra into a live TradingView signal display.

---

## The problem, and how this solves it

TradingView will only send alerts to an address that starts with **`https://`**. Your
SmallTV is a small chip on your home Wi‑Fi with a plain **`http://192.168.x.x`** address
that the internet cannot see at all. Even if you forced HTTPS onto it, TradingView still
could not reach it without opening a hole in your router.

So we don't try. We **turn the direction around**:

```
  TradingView  ──HTTPS──▶  Cloudflare Worker  ◀──asks every few seconds──  SmallTV
   (must push)              (public, free,                (on your Wi-Fi,
                             always on)                    outbound only)
```

The Cloudflare Worker is a tiny free program with a real `https://` address. TradingView
is happy — it's posting to proper HTTPS. The Worker remembers the newest signal. Your
SmallTV then simply *asks* the Worker "anything new?" every few seconds, the same way it
already asks a weather server for the forecast.

What this buys you:

- **Nothing on your router changes.** No port forwarding, no dynamic DNS, no certificates.
- **Nothing on your home network is exposed** to the internet.
- **No computer has to stay switched on.** Cloudflare runs the middle bit.
- **It's free.** Cloudflare's free tier covers this many times over.

---

## What you'll need

| | |
|---|---|
| The device | GeekMagic SmallTV Ultra (the ESP8266 one, currently on firmware V9.0.50) |
| TradingView | A plan that includes webhook alerts — Essential or above. The free Basic plan cannot send webhooks at all. |
| Cloudflare | A free account at [dash.cloudflare.com](https://dash.cloudflare.com) |
| Time | About 20 minutes |

---

## Part 1 — Put the bridge online (≈8 minutes)

This is the piece that gives TradingView its HTTPS address.

> Want each setting explained rather than just listed — what KV is, why the
> binding must be called `SIGNALS`, what the free plan actually allows?
> See **[CLOUDFLARE.md](CLOUDFLARE.md)**.

### 1.1 Make the storage bucket

1. Sign in to Cloudflare and go to **Storage & Databases → KV**.
2. Click **Create instance**, name it `godmode-signals`, create it.
3. Copy the **Namespace ID** it shows you.

### 1.2 Create the Worker

1. Go to **Compute → Workers & Pages → Create → Start with Hello World**.
2. Name it `drfx-godmode-bridge`, deploy it, then click **Edit code**.
3. Delete everything in the editor and paste the whole contents of
   **`bridge/worker.js`** from this folder. Click **Deploy**.

### 1.3 Wire up storage and passwords

Still in the Worker, open **Settings → Bindings**:

- **Add binding → KV namespace.** Variable name `SIGNALS`, pick `godmode-signals`.

Then **Settings → Variables and Secrets → Add**, twice, both as type **Secret**:

| Name | Value |
|---|---|
| `WEBHOOK_KEY` | A long random string. TradingView will use this. |
| `DEVICE_KEY` | A *different* long random string. The SmallTV will use this. |

> Two 40-character random keys have already been generated for you in
> **`SECRETS.local.md`** at the root of this repository. That file is gitignored, so it
> never leaves your machine. Use those values, or make your own — 30+ characters of
> letters and numbers. They are the
> only thing stopping a stranger who guesses your Worker address from posting fake
> signals to your screen. Never reuse one for the other.

Click **Deploy** again.

### 1.4 Check it

Your Worker now has an address like:

```
https://drfx-godmode-bridge.YOURNAME.workers.dev
```

Open it in a browser. You should see a dark **DRFX GODMODE BRIDGE** page saying
"no signals received yet". That means it's alive.

*Prefer the command line?* Skip 1.1–1.3 entirely and run the deploy script — it
creates the KV namespace, writes the id into `wrangler.toml`, prompts for the two
secrets and deploys:

```
# macOS / Linux
cd bridge && ./deploy.sh

# Windows PowerShell, from the repo root
.\bridge\deploy.ps1
```

---

## Part 2 — Get the firmware file (≈3 minutes)

You do **not** need to install a compiler.

1. Push this repository to GitHub (GitHub Desktop → **Commit to main** → **Push origin**).
   Keep the folder structure — the `.github` folder at the root is what triggers the build.
2. Open the repository's **Actions** tab, wait for the green tick (about two minutes).
3. Open the finished run and download the **firmware** artifact. Unzip it. You get two
   files:

   | File | What it is |
   |---|---|
   | `drfx-godmode-smalltv-ultra.bin` | **full** — mDNS (`godmode.local`), smooth fonts, the 48 px numerals and the seven-segment clock |
   | `drfx-godmode-smalltv-ultra-SLIM.bin` | **slim** — the same firmware with those pieces stripped out to make the image smaller |

**Read the size table in the run summary before going on.** It prints both sizes and
whether each is likely to fit the stock updater's slot. At the time of writing neither
does — full is around 587 kB and slim around 512 kB, against a slot of roughly 440 kB —
so plan on Part 3 needing a serial cable once. That is a one-off: after the first flash
the device runs our layout, with about 3 MB of program area, and everything after that is
a browser upload.

> Building locally instead? Install [PlatformIO](https://platformio.org), then
> `cd firmware && pio run`. The file lands in `.pio/build/smalltv_ultra/firmware.bin`.

---

## Part 3 — Flash the device (≈5 minutes)

**Back up first.** This replaces the stock firmware completely and erases the photos
stored on the device. Your way back is
`vendor/stock-firmware/FW-Smalltv-Ultra-V9.0.50/FW-Smalltv-Ultra-V9.0.50.bin`, kept in
this repository — see [vendor/README.md](../vendor/README.md).

The stock *Ultra* firmware reserves most of the chip's flash for its photo album, so the
slot it leaves for an over-the-air update is roughly 440 kB. Our images are currently
bigger than that, so **the first install almost always needs a serial cable**. It is
genuinely once — after this the device runs our flash layout and updates are a browser
upload.

### 3.1 Try the stock updater first (two minutes, might just work)

Worth a try if the size table in Part 2 says `SLIM` fits, or if you simply want to find
out — the failure is harmless.

1. Find the SmallTV's IP address (shown on screen, or in your router's device list).
2. In a browser go to `http://<that-ip>` and find the firmware update section of the
   stock web console.
3. Upload `drfx-godmode-smalltv-ultra-SLIM.bin`.

If it reboots into **SETUP MODE**, skip to Part 4. If it says *"Not Enough Space"* — the
usual outcome — carry on below. Nothing has been damaged.

### 3.2 Flash over UART (the reliable path)

Open the case: two screws underneath, then slide the back off. Wire a **3.3 V**
USB‑to‑serial adapter to the pads — **3V3, GND, TX→RX, RX→TX** — and hold **GPIO0 to GND
while powering on** to enter flash mode. Pin photos are on the
[ESPHome page for this board](https://devices.esphome.io/devices/geekmagic-ultra/).

```bash
pip install esptool

# 1. back up the whole chip first - this is your undo, and it takes a minute
esptool --port COM3 read_flash 0x0 0x400000 stock-backup.bin

# 2. write the full image (you have a cable attached, so there is no reason
#    to settle for the slim one)
esptool --port COM3 --baud 460800 write_flash 0x0 drfx-godmode-smalltv-ultra.bin
```

> **3.3 V only.** The ESP8266 is not 5 V tolerant. An adapter with a voltage jumper is
> worth the couple of pounds.

Power-cycle normally afterwards, without GPIO0 grounded.

**You never need the cable again.** Later updates go through **Admin → Firmware update**
in the new web UI, and from there the full image fits comfortably.

---

## Part 4 — First run (≈4 minutes)

1. The screen shows **SETUP MODE**. On your phone or laptop, join the Wi‑Fi network
   **`DrFX-GodMode`**, password **`godmode123`**.
2. A settings page should open by itself. If it doesn't, go to `http://192.168.4.1`.
3. **Wi‑Fi tab** → press *Scan*, choose your home network (2.4 GHz only — the chip
   cannot see 5 GHz networks), type the password, press **Save & reboot**.
4. The screen shows **READY** with its new address. Reconnect your phone to your normal
   Wi‑Fi and open **`http://godmode.local`** (or the IP shown on screen).
5. **Bridge tab** → fill in:
   - **Bridge URL** — your Worker address, no slash at the end
   - **Device key** — the `DEVICE_KEY` value from step 1.3
   - leave *Device ID* as `main`
   - Save.
6. **Admin tab** → change the settings password from `godmode`. Save.
7. **Status tab** → *Bridge* should read **"connected, nothing new"**. That's success.
   Press **Show a test signal** to see the card on the screen.

---

## Part 5 — Point TradingView at it (≈3 minutes)

1. On a TradingView chart, create an alert as usual.
2. Open **Notifications**, tick **Webhook URL**, and paste:

   ```
   https://drfx-godmode-bridge.YOURNAME.workers.dev/tv?key=YOUR_WEBHOOK_KEY
   ```

   (that's your `WEBHOOK_KEY` from step 1.3, not the device key)

3. In the alert's **Message** box, paste one of the templates from
   `tradingview/alert-message.json`. The simplest:

   ```json
   {"symbol":"{{ticker}}","side":"BUY","score":90,"conf":85,
    "tp1":"{{plot_0}}","tp2":"{{plot_1}}","sl":"{{plot_2}}"}
   ```

4. Save the alert. When it fires, the card appears on the SmallTV within a few seconds.

Plain text works too, if that's easier — `XAUUSD BUY score=96 tp1=3378 tp2=3386 sl=3362`
is parsed just as happily as JSON.

---

## Everyday use

Open **`http://godmode.local`** any time.

| Tab | What's there |
|---|---|
| **Status** | Is it connected, is the bridge answering, what's on screen now |
| **Wi‑Fi** | Network, plus an optional backup network |
| **Bridge** | Worker URL, device key, how often to check, when a signal goes stale |
| **Display** | Brightness, night dimming hours, rotation, time zone, all five colours |
| **Admin** | Password, firmware update, reboot, factory reset |

---

## When something isn't right

**Status says "connect failed"** — check the Bridge URL has no trailing slash and starts
with `https://`. Open it in a browser to confirm the Worker is up.

**Status says "HTTP 403"** — the device key doesn't match the Worker's `DEVICE_KEY`
secret. Retype it in the Bridge tab; blank means "keep the old one", so you must type
the whole thing.

**Nothing ever arrives from TradingView** — visit your Worker's address in a browser.
If the table is empty, TradingView isn't reaching it: check the `?key=` part of the
webhook URL matches `WEBHOOK_KEY`, and check your TradingView plan includes webhooks.

**Device reboots at random** — the ESP8266 is tight on memory when it opens an HTTPS
connection. Set *Check every* to 10 seconds or more in the Bridge tab.

**Can't find `godmode.local`** — some Windows setups don't do `.local` names. Use the IP
address shown on screen at boot, or find it in your router's device list.

**Want the stock firmware back** — flash
`vendor/stock-firmware/FW-Smalltv-Ultra-V9.0.50/FW-Smalltv-Ultra-V9.0.50.bin` through
**Admin → Firmware update**, or over UART the same way as Part 3.2. Check its md5 first;
the expected value is in [vendor/README.md](../vendor/README.md).

---

## A note on security

The device skips TLS certificate checking when it talks to your Worker. Storing and
validating a full certificate chain doesn't fit comfortably in this chip's memory
alongside the display. In practice this means someone already positioned on your network
could impersonate the bridge and show you a fake signal — they cannot read anything
sensitive, since only signal data travels this path.

Given that, treat the screen as a **glanceable notification, never as a trade
instruction**. Confirm on your platform before acting. Use long random keys, keep the
Worker address to yourself, and change the settings password from the default.
