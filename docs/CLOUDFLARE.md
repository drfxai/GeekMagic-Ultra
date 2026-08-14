# The Cloudflare Worker, explained

Everything you need to configure the bridge by hand in the Cloudflare dashboard —
what each setting is, what to type into it, and what breaks if you get it wrong.

If you'd rather not click through any of this, `bridge/deploy.ps1` does the whole
of Part 1 for you. This document is for doing it manually.

---

## What the Worker actually is

A Worker is a small program that Cloudflare runs on their servers, at a public
`https://` address, for free. Ours is about 250 lines and does exactly two things:

- **Catches** alerts that TradingView posts to it, and remembers the newest one.
- **Hands** that newest alert to your SmallTV when the SmallTV asks for it.

That's the whole job. It's a pigeonhole: TradingView drops a note in, the SmallTV
checks the pigeonhole every few seconds to see if there's a note.

The reason this exists at all is that TradingView refuses to send alerts to
anything that isn't `https://`, and your SmallTV is a small chip on your home
Wi‑Fi at a `http://192.168.x.x` address that the internet cannot see. Rather than
exposing your home network, we put a free public letterbox in the middle and let
the device reach *outward* to it.

---

## The four settings

Only four things need configuring. Here they are in full.

| # | Setting | Type | What it is |
|---|---|---|---|
| 1 | `godmode-signals` | KV namespace | The storage. Where the newest signal is kept. |
| 2 | `SIGNALS` | KV binding | The label the code uses to reach that storage. |
| 3 | `WEBHOOK_KEY` | Secret | The password **TradingView** uses. |
| 4 | `DEVICE_KEY` | Secret | The password **the SmallTV** uses. |

### 1. The KV namespace — `godmode-signals`

**What it is.** KV stands for key–value: a very simple database that stores a
piece of text under a name. Ours stores one entry per device, holding the latest
signal plus the last eight for the status page.

**What to type.** The name `godmode-signals`. Nothing else to configure — no
size, no region, no schema.

**Why it's needed.** A Worker forgets everything the moment it finishes handling
a request. Without KV, a signal from TradingView would vanish before your SmallTV
could ask for it.

### 2. The binding — `SIGNALS`

**What it is.** A binding connects a name in the code to a real resource in your
account. The Worker calls `env.SIGNALS.get(...)`, so the variable name must be
exactly `SIGNALS`.

**What to type.** Variable name `SIGNALS`, and pick `godmode-signals` from the
dropdown.

> **This is the one people get wrong.** `signals`, `SIGNAL` or `godmode-signals`
> as the variable name will all deploy without complaint and then fail at runtime
> with a 500 error, because `env.SIGNALS` will be undefined. It is case-sensitive
> and it must be the plural `SIGNALS`.

### 3 and 4. The two secrets

**What they are.** Your Worker has a public address. Anyone who learns it could
post fake signals to your screen — so both routes require a password in the URL.

**What to type.** Two different long random strings. Two 40-character keys were
already generated for you in **`SECRETS.local.md`** at the root of this repo;
that file is gitignored so it never leaves your machine. Copy them from there.

| Secret | Used by | Appears in |
|---|---|---|
| `WEBHOOK_KEY` | TradingView | the webhook URL you paste into the alert |
| `DEVICE_KEY` | your SmallTV | the Bridge tab of the device's settings page |

**Set the type to "Secret", not "Text".** Both work, but Secret hides the value
from the dashboard afterwards. Choose Text and anyone glancing at your screen
can read your keys.

**Never use the same value for both.** They protect different things. The
webhook key ends up pasted into TradingView's servers; the device key is typed
into a device on your Wi‑Fi. If one leaks you want to be able to rotate it
without touching the other.

---

## Doing it in the dashboard

Roughly ten minutes. Order matters — create the storage first, or the Worker
will error when you test it.

### Step 1 — Create the storage

1. Sign in at [dash.cloudflare.com](https://dash.cloudflare.com).
2. In the left sidebar, find **Storage & Databases → KV**.
3. **Create instance**, name it `godmode-signals`, create.

You'll see a Namespace ID — a 32-character hex string. You don't need to copy it
for the dashboard route; it only matters if you later use `wrangler`.

### Step 2 — Create the Worker

1. Left sidebar → **Compute → Workers & Pages**.
2. **Create** → **Start with Hello World** → name it `drfx-godmode-bridge`.
3. **Deploy**, then **Edit code**.
4. Select everything in the editor and delete it. Open `bridge/worker.js` from
   this repo, copy the whole file, paste it in.
5. **Deploy**.

At this point the Worker is live but has no storage and no passwords, so it will
throw a 500. That's expected — keep going.

### Step 3 — Attach the storage

1. In the Worker, go to **Settings → Bindings** (some accounts show **Bindings**
   as its own tab — either is fine).
2. **Add binding** → **KV namespace**.
3. Variable name: `SIGNALS`. Namespace: `godmode-signals`.
4. Save.

### Step 4 — Add the two passwords

1. **Settings → Variables and Secrets** → **Add**.
2. Type **Secret**, name `WEBHOOK_KEY`, value = the webhook key from
   `SECRETS.local.md`. Save.
3. **Add** again. Type **Secret**, name `DEVICE_KEY`, value = the device key.
   Save.
4. **Deploy** once more so the new bindings and secrets take effect.

### Step 5 — Check it

Your Worker now lives at something like:

```
https://drfx-godmode-bridge.YOURNAME.workers.dev
```

Open that in a browser. A dark **DRFX GODMODE BRIDGE** page saying *"no signals
received yet"* means everything is wired correctly.

Then send it a real signal from a terminal — substitute your address and your
webhook key:

```powershell
curl.exe -X POST "https://drfx-godmode-bridge.YOURNAME.workers.dev/tv?key=YOUR_WEBHOOK_KEY" `
  -H "content-type: application/json" `
  -d "{\"symbol\":\"XAUUSD\",\"side\":\"BUY\",\"score\":96,\"tp1\":\"3378\",\"sl\":\"3362\"}"
```

Refresh the page — XAUUSD should now be in the table. `SECRETS.local.md` has this
same command with your actual key already filled in.

---

## What the Worker's four addresses do

You never call these by hand, but knowing them makes the troubleshooting obvious.

| Address | Who calls it | What happens |
|---|---|---|
| `POST /tv?key=WEBHOOK_KEY` | TradingView | Stores the alert. Wrong key → `403`. |
| `GET /latest?key=DEVICE_KEY` | your SmallTV | Returns the newest signal, or `204` if nothing changed. |
| `GET /health` | you | Replies `ok`. Quickest way to confirm it's alive. |
| `GET /` | you | The human status page with the last eight signals. |

The `204` is deliberate. The device sends the timestamp of what it already has;
if nothing is newer, the Worker replies with an empty response rather than
re-sending data. The ESP8266 has around 40 kB of usable memory, so not having to
parse a payload it already holds matters.

---

## Does this stay free?

Yes, comfortably — but it's worth knowing where the ceilings are, because one of
them is lower than you'd expect.

| Free plan allowance | What uses it | Your usage |
|---|---|---|
| 100,000 Worker requests/day | every poll + every alert | ~17,300/day at the default 5s polling |
| 100,000 KV reads/day | every poll | same ~17,300/day |
| **1,000 KV writes/day** | **only TradingView alerts** | 1 per alert that fires |
| 1 write/second to the same key | two alerts in the same second | rare, but possible |
| 1 GB storage | a few hundred bytes total | irrelevant |

**The write limit is the one to watch.** Polling costs you reads, which you have
100,000 of. Alerts cost you writes, and you only get 1,000 a day. That's plenty
for discretionary trading, but a busy strategy firing on every bar close across
several symbols could reach it. If you hit it, writes fail until 00:00 UTC.

Polling at 1 second instead of 5 would put you near 86,400 reads and requests per
day — still under, but with far less headroom. **5 seconds is a sensible default;
10 is safer** and also reduces the memory pressure that causes random reboots.

---

## The whole installation, end to end

Five stages, about 45 minutes in total. Detailed instructions for stages 2–5 are
in [SETUP.md](SETUP.md); this is the map.

| Stage | Time | What happens |
|---|---|---|
| **1. Cloudflare** | 10 min | The steps above. You end with a working `https://` address. |
| **2. Build the firmware** | 5 min | Push this repo to GitHub; the Actions tab compiles the `.bin` for you. No compiler to install. |
| **3. Flash the device** | 5 min | Upload that `.bin` through the SmallTV's existing web page. |
| **4. First run** | 10 min | Join the device's temporary Wi‑Fi, give it your home network, then paste in the Worker URL and device key. |
| **5. TradingView** | 5 min | Create an alert, tick Webhook URL, paste your address with the webhook key. |

### A few things worth knowing before you start

**Stage 2 needs a push.** Both commits are already made locally — open GitHub
Desktop and press **Push origin**. The build starts by itself; the artifact is
called `firmware` and unzips to `drfx-godmode-smalltv-ultra.bin`.

**Stage 3 erases the device.** Flashing replaces the stock firmware completely,
including any photos stored on it. Your way back is
`vendor/stock-firmware/FW-Smalltv-Ultra-V9.0.50/FW-Smalltv-Ultra-V9.0.50.bin`, which is
already in this repo.

**Stage 3 may refuse the upload.** The stock Ultra firmware reserves most of the
flash for its photo album, leaving a small update slot. The build log prints the
file size and warns you if it's over ~500 kB. If it's rejected, you need a
3.3 V USB-to-serial adapter once — the wiring and commands are in SETUP.md Part 3.
After that first cable flash, all future updates go over Wi‑Fi.

**Stage 4 is 2.4 GHz only.** The ESP8266 cannot see 5 GHz networks at all. If
your router broadcasts both under one name, you may need to connect to the 2.4 GHz
band specifically.

**Stage 5 needs a paid TradingView plan.** Webhook alerts start at Essential. The
free Basic plan cannot send webhooks, and there's no way around that.

---

## When it doesn't work

| Symptom | Cause | Fix |
|---|---|---|
| Worker URL shows `error: Cannot read properties of undefined` | KV binding missing or misnamed | Settings → Bindings. The variable name must be exactly `SIGNALS`. |
| Device status says **HTTP 403** | `DEVICE_KEY` mismatch | Retype the whole key in the device's Bridge tab. Leaving it blank means "keep the old one", so a partial edit won't take. |
| TradingView reports a failed webhook | `WEBHOOK_KEY` mismatch, or wrong path | The URL must end `/tv?key=...`. Test with the curl command above. |
| Status page stays empty | TradingView isn't reaching the Worker | Check your plan includes webhooks; check the alert is actually firing. |
| Device says **connect failed** | Wrong Bridge URL | No trailing slash, must start with `https://`. Open it in a browser to confirm. |
| Device reboots at random | TLS handshake memory pressure | Set *Check every* to 10 seconds or more. |
| Everything worked, then stopped at a fixed time each day | Hit the 1,000 KV writes/day limit | Reduce how often your alerts fire. Resets at 00:00 UTC. |
| Can't reach `godmode.local` | Windows mDNS quirk | Use the IP shown on the device's screen at boot. |

---

## One caution about editing later

If you configure the Worker through the dashboard and then later run
`wrangler deploy` from `bridge/`, wrangler overwrites the dashboard's copy of the
code with whatever is in `worker.js`. Bindings and secrets survive; the code does
not. Pick one route and stay on it, or make sure `worker.js` in this repo is
always the source of truth.

---

## And a word on trust

The device deliberately skips TLS certificate validation when it talks to your
Worker — a full certificate store doesn't fit in the ESP8266's memory alongside
the display driver. In practice that means someone already on your network could
impersonate the bridge and put a fake card on your screen. They cannot read
anything sensitive, since only signal data travels this path.

So treat the screen as a **glanceable notification, never a trade instruction**.
Confirm on your platform before acting on anything it shows you.

---

Sources: [Workers KV limits](https://developers.cloudflare.com/kv/platform/limits/) ·
[Workers pricing](https://developers.cloudflare.com/workers/platform/pricing/) ·
[KV bindings](https://developers.cloudflare.com/kv/concepts/kv-bindings/)
