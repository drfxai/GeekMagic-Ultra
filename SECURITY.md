# Security

## Reporting a vulnerability

Open a [private security advisory](https://github.com/drfxai/GeekMagic-Ultra/security/advisories/new)
rather than a public issue. Expect a first reply within a week.

Please include what an attacker would have to already have — network position,
a key, physical access — since that is usually what decides how serious a report
is here.

---

## What this project is, in security terms

A hobbyist ESP8266 on your home network, polling a Cloudflare Worker over the
internet. The threat model is deliberately modest, and there are three things
worth knowing before you deploy it.

### 1. The screen is a notification, not an instruction

**Treat what the device shows as glanceable information and confirm on your
trading platform before acting on it.** Every point below feeds into that one.

### 2. TLS certificates are not validated

The firmware calls `setInsecure()` before polling the bridge. A root certificate
store does not fit comfortably on this chip alongside the display driver, and the
alternative — pinning a certificate that Cloudflare rotates — fails silently at
the worst moment.

What this means in practice: someone already positioned between the device and
the internet could impersonate your bridge and show you a fabricated signal. They
cannot read anything sensitive, because only signal data travels this path, and
they cannot reach the rest of your network through it.

The shared device key in the URL is what authenticates the exchange.

### 3. Authentication is by shared key

| Secret | Guards | Where it lives |
|---|---|---|
| `WEBHOOK_KEY` | who may post alerts to the bridge | Worker secret; TradingView's webhook URL |
| `DEVICE_KEY` | who may read signals, and `/api/push` on the device | Worker secret; device settings |
| settings password | the settings page and the firmware updater | device config, `admin`/`godmode` by default |

Keys travel in the query string, so they appear in Cloudflare's request logs.
They are compared with a constant-time function to avoid leaking their length
through timing.

**Change the default settings password.** `admin`/`godmode` protects the
firmware updater — anyone on your LAN who can reach the device can otherwise
replace its firmware entirely.

Use long random keys, rotate them with `wrangler secret put`, and keep your
Worker's address to yourself — it is not a secret, but it is not worth
publishing either.

---

## Hardening worth doing

- Put the device on a guest or IoT VLAN. It only needs outbound HTTPS.
- Use a distinct `DEVICE_KEY` per device via the `--id` / device-ID mechanism, so
  one leaked key does not expose the rest.
- The bridge's read endpoints send permissive CORS headers. They are gated by
  `DEVICE_KEY`, not by origin — do not embed that key in a public web page.
- Signals expire. Leave *Signal expires after* at a sane value so a stale card
  cannot sit on the screen looking current.

## Not vulnerabilities

These are known, documented trade-offs rather than bugs, and reports about them
will be closed with a link here:

- Skipped certificate validation (§2).
- Keys in query strings (§3) — the ESP8266's HTTP client makes custom headers
  awkward on the polling path.
- The settings page is HTTP, not HTTPS. It is LAN-only, and a certificate the
  chip could actually serve would have to be self-signed anyway.
- The AP-mode setup network uses a fixed password. It exists only until Wi-Fi is
  configured, and the device is in your hands at that point.

## Secrets in this repository

There are none, and there should never be any. `SECRETS.local.md`, `*.local.env`
and `.dev.vars` are gitignored. Worker secrets belong in `wrangler secret put`,
never in `wrangler.toml`. If you believe a key has been committed, treat it as
compromised, rotate it, and open an advisory.
