# Quick start

Get trading signals onto your SmallTV Ultra in about 20 minutes.

You do not need to know how to code. You will copy and paste a few things.
Every step tells you what you should see when it worked.

**You need three things, all free:**

| | |
|---|---|
| A GeekMagic SmallTV Ultra | the little screen |
| A Cloudflare account | [dash.cloudflare.com/sign-up](https://dash.cloudflare.com/sign-up) |
| A TradingView account | only if you want live signals; prices work without it |

---

## Step 1 — Make your two keys

Download this repository ([green **Code** button → **Download ZIP**](https://github.com/drfxai/GeekMagic-Ultra/archive/refs/heads/main.zip)),
unzip it, then **double-click `tools/keygen.html`**.

It opens in your browser and shows two long random keys. It works offline and
sends nothing anywhere.

Leave this tab open. You will copy from it three times.

> **What are these?** One key lets TradingView talk to your bridge. The other
> lets your screen talk to it. They stop strangers pushing fake signals to your
> display. Do not share them or put them on GitHub.

---

## Step 2 — Put the bridge online

The "bridge" is a tiny free program that runs on Cloudflare. TradingView sends
alerts to it, and your screen reads them from it.

### The easy way

Open a terminal in the `bridge` folder and run:

**Windows** — right-click `deploy.ps1` → *Run with PowerShell*

**Mac / Linux**
```bash
cd bridge && ./deploy.sh
```

It installs what it needs, asks you to log into Cloudflare in your browser,
creates the storage, uploads your two keys and publishes the bridge.

### The click-only way

If you would rather not use a terminal:

1. Go to [dash.cloudflare.com](https://dash.cloudflare.com) → **Workers & Pages** → **Create** → **Worker**
2. Name it `fx-godmode-bridge`, click **Deploy**, then **Edit code**
3. Delete what is there, paste in all of [`bridge/worker.js`](bridge/worker.js), click **Deploy**
4. Go to **Settings → Variables and Secrets**. Add two **secrets**:
   - `WEBHOOK_KEY` → paste key 1 from the generator
   - `DEVICE_KEY` → paste key 2 from the generator
5. Go to **Settings → Bindings** → **Add** → **KV namespace**
   - Variable name: `SIGNALS` — create a new namespace, any name

**Check it worked:** Cloudflare shows you an address ending in `.workers.dev`.
Open it in your browser. You should see a black page titled **DRFX ULTRA OS
BRIDGE**. If you do, this step is done.

Now go back to the key generator tab and type that address into box 3. The last
two boxes fill themselves in — you will need both shortly.

---

## Step 3 — Put the software on the screen

1. Go to the [**Releases** page](https://github.com/drfxai/GeekMagic-Ultra/releases)
   and download **`smalltv_ultra_slim.bin`**.

   > Start with the **slim** one. It is the only version small enough to fit
   > through the device's built-in updater. You can upgrade to the full version
   > later, from the device itself, in one click.

2. Plug the SmallTV into power. Find its address on your network — it is printed
   on the screen at startup — and open it in your browser.

3. Use its built-in firmware update page to upload the `.bin` file you downloaded.

4. Wait for it to restart. **You should see:** a black screen with `DRFX ULTRA OS`.

> **Careful:** this replaces the stock software and erases the photo album.
> The way back to factory is in [`vendor/README.md`](vendor/README.md) — the
> original firmware is kept in this repository so recovery always works.

---

## Step 4 — Connect the screen to your Wi-Fi

The first time it starts, the device makes its own Wi-Fi network.

1. On your phone or laptop, join the Wi-Fi network **`DrFX-GodMode`**
   (password: `godmode123`)
2. A settings page opens by itself. If not, go to `http://192.168.4.1`
3. Choose your home Wi-Fi, type its password, **Save**

The device restarts and joins your network. It shows its new address on screen —
something like `192.168.1.50`. Open that in your browser from now on.

---

## Step 5 — Point the screen at your bridge

On the device's settings page, open the **Bridge** tab:

| Box | What to paste |
|---|---|
| **Bridge URL** | box 5 from the key generator (ends in `.workers.dev`, no `/` at the end) |
| **Device key** | key 2 from the key generator |

Click **Save**.

**Check it worked:** open the **Status** tab. Next to **Bridge** you should see
*connected, nothing new* — that is success, it means the screen reached the
bridge and there were no signals waiting yet.

Within a minute the screen starts rotating through a clock and live crypto
prices. **If prices appear, everything is working.**

---

## Step 6 — Send signals from TradingView

Only if you want your own trading alerts on the screen. Skip it if you just
wanted prices.

1. In TradingView, create an alert on any indicator
2. Open the **Notifications** tab of the alert box
3. Tick **Webhook URL** and paste **box 4** from the key generator
4. In the **Message** box, paste one line:

```json
{"symbol":"{{ticker}}","side":"BUY","score":90,"tp1":"3378","sl":"3362"}
```

5. **Create**

Next time that alert fires, it appears on the screen within about five seconds.

> Using the **GOD MODE – Quad Consensus** Pine indicator? Do not paste anything
> into Message. Set the alert condition to *Any alert() function call* and the
> bridge reads everything from the script automatically.
>
> More message templates, and every field you can send, are in
> [`tradingview/alert-message.json`](tradingview/alert-message.json).

---

## Something not working?

Open the **Status** tab on the device. It tells you what is wrong in plain
words. The common ones:

| It says | What to do |
|---|---|
| `no bridge URL saved` | Step 5 did not save. Reload the page first, then re-enter it |
| `no device key saved` | Same — reload the settings page, then paste the key again |
| `WiFi not connected` | Wrong Wi-Fi password, or the device is out of range |
| `HTTP 403` | Your two keys do not match. The device key must equal `DEVICE_KEY` in Cloudflare |
| `HTTP 502 no source had...` | The price sources are unreachable from your region. See below |
| `waiting for NTP` | Harmless. Your network blocks time sync, so the bridge supplies the time instead |

**Prices show an error but signals work.** Some countries cannot reach certain
exchanges. Add `?probe=1` to your bridge's crypto address to see which sources
answer for you:

```
https://YOUR-WORKER.workers.dev/crypto?key=YOUR_DEVICE_KEY&symbols=BTCUSDT&probe=1
```

Any source reporting `"ok":true` will be used automatically. If they are all
blocked, open an issue with that output and we will add one that works where
you are.

**Everything looks right but nothing happens.** Run the built-in checker:

```bash
python tools/drfx.py doctor
```

It tests every link in the chain and tells you which one is broken.
