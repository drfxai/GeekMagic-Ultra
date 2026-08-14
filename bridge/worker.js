/**
 * DrFX GodMode Bridge - Cloudflare Worker
 * ---------------------------------------
 * Solves the "TradingView only speaks HTTPS" problem.
 *
 *   TradingView  --HTTPS POST-->  this Worker  --stores in KV-->  SmallTV polls it
 *
 * The Worker is the public HTTPS endpoint TradingView demands. The SmallTV never
 * needs to be reachable from the internet: it *pulls* from here. No port
 * forwarding, no dynamic DNS, no certificate on your LAN, no PC left switched on.
 *
 * Routes
 *   POST /tv?key=WEBHOOK_KEY[&device=main]              TradingView alert lands here
 *   GET  /latest?key=DEVICE_KEY&device=main&since=<ts>  device polls here
 *   GET  /history?key=DEVICE_KEY&device=main            recent signals, JSON
 *   GET  /stats?key=DEVICE_KEY&device=main              counts and clock check
 *   GET  /health                                        plain "ok"
 *   GET  /                                              human status page
 *
 * Secrets (wrangler secret put ...)
 *   WEBHOOK_KEY   what TradingView puts in its URL
 *   DEVICE_KEY    what the SmallTV puts in its URL
 * KV namespace binding: SIGNALS
 */

const BRIDGE_VERSION = "2.0.0";
const MAX_BODY = 8 * 1024;      // reject silly-sized alert bodies
const HISTORY = 24;             // how many past signals to keep
const TTL = 60 * 60 * 24 * 7;   // KV entries expire after a week

export default {
  async fetch(request, env) {
    const url = new URL(request.url);
    const path = url.pathname.replace(/\/+$/, "") || "/";

    // The read endpoints are meant to be usable from a dashboard or a local
    // tool in a browser, so answer preflights rather than making callers
    // proxy around CORS. Writes (/tv) are excluded on purpose.
    if (request.method === "OPTIONS") {
      return new Response(null, { status: 204, headers: corsHeaders() });
    }

    try {
      if (path === "/health") return text("ok");
      if (path === "/tv") return handleWebhook(request, url, env);
      if (path === "/latest") return handleLatest(request, url, env);
      if (path === "/history") return handleHistory(url, env);
      if (path === "/stats") return handleStats(url, env);
      if (path === "/") return handleStatus(url, env);
      return text("not found", 404);
    } catch (err) {
      return text("error: " + (err && err.message ? err.message : String(err)), 500);
    }
  },
};

/* ------------------------------------------------------------------ */
/* TradingView -> here                                                 */
/* ------------------------------------------------------------------ */

async function handleWebhook(request, url, env) {
  if (request.method !== "POST") return text("POST only", 405);

  const key = url.searchParams.get("key") || "";
  if (!env.WEBHOOK_KEY || !timingSafeEqual(key, env.WEBHOOK_KEY)) {
    return text("bad key", 403);
  }

  const device = sanitiseId(url.searchParams.get("device") || "main");

  const raw = await request.text();
  if (raw.length > MAX_BODY) return text("body too large", 413);

  const parsed = parseAlert(raw);
  if (!parsed) return text("could not parse alert body", 400);

  const rec = {
    ts: Date.now(),
    symbol: parsed.symbol || "",
    side: parsed.side || "",
    score: clampInt(parsed.score, 0, 100, 0),
    conf: clampInt(parsed.conf, 0, 100, 0),
    entry: str(parsed.entry, 12),
    tp1: str(parsed.tp1, 12),
    tp2: str(parsed.tp2, 12),
    sl: str(parsed.sl, 12),
    tf: str(parsed.tf, 6),
    note: str(parsed.note, 24),
  };

  const store = (await readStore(env, device)) || { latest: null, history: [] };
  store.latest = rec;
  store.history = [rec, ...(store.history || [])].slice(0, HISTORY);

  await env.SIGNALS.put(kvKey(device), JSON.stringify(store), { expirationTtl: TTL });

  return json({ ok: true, stored: rec });
}

/* ------------------------------------------------------------------ */
/* Device -> here                                                      */
/* ------------------------------------------------------------------ */

async function handleLatest(request, url, env) {
  const key = url.searchParams.get("key") || "";
  if (!env.DEVICE_KEY || !timingSafeEqual(key, env.DEVICE_KEY)) {
    return text("bad key", 403);
  }

  const device = sanitiseId(url.searchParams.get("device") || "main");
  const since = Number(url.searchParams.get("since") || 0) || 0;

  const store = await readStore(env, device);
  if (!store || !store.latest) return new Response(null, { status: 204 });

  // Nothing new since the device last asked - answer with an empty 204 so the
  // ESP8266 does not have to allocate memory to parse a payload it already has.
  if (since && store.latest.ts <= since) return new Response(null, { status: 204 });

  // Shape unchanged from bridge 1.x. The firmware ignores unknown keys, so
  // nothing here breaks an older device that has not been updated yet.
  return json(store.latest);
}

/* ------------------------------------------------------------------ */
/* Read-only extras - for the CLI, dashboards and debugging            */
/* ------------------------------------------------------------------ */

function authDevice(url, env) {
  const key = url.searchParams.get("key") || "";
  return env.DEVICE_KEY && timingSafeEqual(key, env.DEVICE_KEY);
}

async function handleHistory(url, env) {
  if (!authDevice(url, env)) return text("bad key", 403);

  const device = sanitiseId(url.searchParams.get("device") || "main");
  const limit = Math.min(HISTORY, Math.max(1, Number(url.searchParams.get("limit") || HISTORY) || HISTORY));
  const store = await readStore(env, device);

  return json({
    device,
    count: ((store && store.history) || []).length,
    signals: ((store && store.history) || []).slice(0, limit),
  });
}

/**
 * Counts, plus the Worker's own clock.
 *
 * `now` is the useful part: the device has no battery-backed RTC and depends
 * on NTP, so comparing this against what the screen shows is the quickest way
 * to tell a wrong timezone from a clock that never synced at all.
 */
async function handleStats(url, env) {
  if (!authDevice(url, env)) return text("bad key", 403);

  const device = sanitiseId(url.searchParams.get("device") || "main");
  const store = await readStore(env, device);
  const hist = (store && store.history) || [];

  const bySide = { BUY: 0, SELL: 0, FLAT: 0, OTHER: 0 };
  const bySymbol = {};
  for (const r of hist) {
    if (bySide[r.side] === undefined) bySide.OTHER++;
    else bySide[r.side]++;
    if (r.symbol) bySymbol[r.symbol] = (bySymbol[r.symbol] || 0) + 1;
  }

  const latest = (store && store.latest) || null;
  return json({
    version: BRIDGE_VERSION,
    device,
    now: Date.now(),
    nowIso: new Date().toISOString(),
    kept: hist.length,
    bySide,
    bySymbol,
    lastTs: latest ? latest.ts : null,
    lastAgeSec: latest ? Math.round((Date.now() - latest.ts) / 1000) : null,
  });
}

/* ------------------------------------------------------------------ */
/* Human status page                                                   */
/* ------------------------------------------------------------------ */

async function handleStatus(url, env) {
  const device = sanitiseId(url.searchParams.get("device") || "main");
  const store = await readStore(env, device);
  const rows = ((store && store.history) || [])
    .map(
      (r) =>
        `<tr><td class="t">${new Date(r.ts).toISOString().replace("T", " ").slice(0, 19)}Z</td>` +
        `<td>${esc(r.symbol)}</td><td class="${r.side === "SELL" ? "s" : r.side === "FLAT" ? "f" : "b"}">${esc(r.side)}</td>` +
        `<td>${r.score}</td><td>${esc(r.tp1)}</td><td>${esc(r.tp2)}</td><td>${esc(r.sl)}</td></tr>`
    )
    .join("");

  // Styled to match the device: black field, hairline rules, mono type, one
  // accent. Opening the Worker in a browser should look like the same product
  // as the screen on the desk.
  const body = `<!doctype html><meta charset="utf-8"><title>DrFX Ultra OS Bridge</title>
<meta name="viewport" content="width=device-width,initial-scale=1">
<style>
 :root{--bg:#07080a;--line:#1b2027;--line2:#11151a;--txt:#e8ecf1;--dim:#79828f;--dim2:#4b535e;--acc:#31ff9a;--bad:#ff3b52}
 *{box-sizing:border-box}
 body{background:var(--bg);color:var(--txt);margin:0;padding:36px 22px 72px;
   font:13px/1.55 ui-monospace,SFMono-Regular,Menlo,Consolas,monospace;-webkit-font-smoothing:antialiased}
 .wrap{max-width:820px;margin:0 auto}
 h1{font-size:13px;letter-spacing:.28em;color:var(--acc);margin:0 0 6px;font-weight:700}
 .sub{color:var(--dim);margin:0 0 26px;font-size:11px;letter-spacing:.14em;text-transform:uppercase}
 table{border-collapse:collapse;width:100%}
 th,td{text-align:left;padding:9px 12px 9px 0;border-bottom:1px solid var(--line2);font-variant-numeric:tabular-nums}
 th{color:var(--dim2);font-weight:400;font-size:10px;text-transform:uppercase;letter-spacing:.2em;
   border-bottom:1px solid var(--line)}
 td.t{color:var(--dim)}
 .b{color:var(--acc)}.s{color:var(--bad)}.f{color:var(--dim)}
 .empty{color:var(--dim2)}
 code{background:#0f1318;padding:2px 6px;border-radius:2px;color:var(--acc)}
 footer{margin-top:34px;padding-top:18px;border-top:1px solid var(--line);color:var(--dim2);
   font-size:10px;letter-spacing:.18em;text-transform:uppercase;display:flex;justify-content:space-between;gap:12px;flex-wrap:wrap}
</style>
<div class="wrap">
<h1>DRFX ULTRA OS &middot; BRIDGE</h1>
<p class="sub">Device <code>${esc(device)}</code> &middot; ${
    store && store.latest
      ? "last signal " + new Date(store.latest.ts).toUTCString()
      : "no signals received yet"
  }</p>
<table><tr><th>Time (UTC)</th><th>Symbol</th><th>Side</th><th>Score</th><th>TP1</th><th>TP2</th><th>SL</th></tr>${
    rows || '<tr><td colspan="7" class="empty">Waiting for the first TradingView alert&hellip;</td></tr>'
  }</table>
<footer><span>Bridge v${BRIDGE_VERSION}</span><span>${esc(new Date().toISOString().slice(0, 19))}Z</span></footer>
</div>`;

  return new Response(body, {
    headers: {
      "content-type": "text/html;charset=utf-8",
      "cache-control": "no-store",
      "x-drfx-bridge": BRIDGE_VERSION,
    },
  });
}

/* ------------------------------------------------------------------ */
/* Alert parsing - accepts JSON or loose text                          */
/* ------------------------------------------------------------------ */

/**
 * Read the DrFX GodMode [[DRFX]] telemetry tag.
 *
 * entry events carry the levels; tp1/tp2/tp3/sl/close are terminal events that
 * carry only symbol, price and outcome, so they are shown as FLAT with a short
 * note rather than pretending to be a fresh trade.
 */
function parseDrfxTag(jsonText) {
  let j;
  try {
    j = JSON.parse(jsonText);
  } catch (_) {
    return null;
  }

  const ev = String(j.event || "").toLowerCase();
  const dir = String(j.direction || "").toUpperCase();
  let side = dir === "LONG" ? "BUY" : dir === "SHORT" ? "SELL" : "";
  let note = "";

  if (ev === "tp1" || ev === "tp2" || ev === "tp3") {
    side = "FLAT";
    note = ev.toUpperCase() + " hit";
  } else if (ev === "sl") {
    side = "FLAT";
    note = j.result === "win" ? "stop, TP2 made" : "stopped out";
  } else if (ev === "close") {
    side = "FLAT";
    note = String(j.reason || "closed");
  }

  const symbol = String(j.symbol || "").toUpperCase().slice(0, 12);
  if (!symbol && !side) return null;

  return {
    symbol,
    side,
    // "strength" is the consensus quality score the indicator already computes
    // on a 0-100 scale, which is exactly what the gauge wants.
    score: j.strength,
    // There is no separate confidence field; how many of the four systems
    // agreed is the honest equivalent.
    conf: j.systems === undefined || j.systems === null ? "" : (Number(j.systems) / 4) * 100,
    entry: j.entry !== undefined ? j.entry : j.price,
    tp1: j.tp1,
    tp2: j.tp2,
    sl: j.sl,
    tf: j.tf,
    note,
  };
}

/**
 * TradingView sends whatever you typed in the alert box. We accept:
 *   1. JSON:  {"symbol":"XAUUSD","side":"BUY","score":96,"tp1":3378,...}
 *   2. Text:  XAUUSD BUY score=96 tp1=3378 tp2=3386 sl=3362 conf=94
 * Aliases are generous because TradingView placeholders vary between scripts.
 */
function parseAlert(raw) {
  const body = (raw || "").trim();
  if (!body) return null;

  // 1. DrFX GodMode Pine indicator.
  //
  // That script sends alerts through Pine's alert() function, which means the
  // message is the script's own Telegram-formatted block - the Message box in
  // the TradingView dialog is ignored entirely. Buried in that block is a
  // machine-readable tag:
  //
  //   [[DRFX]]{"event":"entry","symbol":"XAUUSD","direction":"long",...}[[/DRFX]]
  //
  // Prefer it over scraping the decorative text: it is exact, and the pretty
  // part is full of emoji, box-drawing characters and "STOP LOSS:" labels that
  // a generic key=value scraper reads badly.
  const tag = body.match(/\[\[DRFX\]\]\s*(\{[\s\S]*?\})\s*\[\[\/DRFX\]\]/);
  if (tag) {
    const fromTag = parseDrfxTag(tag[1]);
    if (fromTag) return fromTag;
    // Malformed tag - fall through and try the generic parsers below rather
    // than dropping a signal that might still be readable.
  }

  // The JSDoc type is only here to keep editors quiet. The Cloudflare dashboard
  // runs TypeScript inference over plain .js, so a bare `obj = {}` makes every
  // later `obj.symbol` / `obj.side` report ts(2339) "does not exist on type {}".
  // Harmless at runtime - this is a bag of arbitrary keys scraped from the
  // alert body - but the red squiggles look like the Worker is broken.
  /** @type {Record<string, any> | null} */
  let obj = null;
  if (body[0] === "{") {
    try {
      const j = JSON.parse(body);
      // Fold keys to lower case so {"Symbol":...} and {"symbol":...} behave the
      // same - Pine scripts in the wild use both.
      obj = {};
      for (const k of Object.keys(j)) obj[k.toLowerCase()] = j[k];
    } catch (_) {
      obj = null;
    }
  }

  if (!obj) {
    obj = {};
    // key=value / key:value pairs anywhere in the string
    const re = /([A-Za-z_][A-Za-z0-9_]*)\s*[=:]\s*("[^"]*"|'[^']*'|[^\s,;]+)/g;
    let m;
    while ((m = re.exec(body)) !== null) {
      obj[m[1].toLowerCase()] = m[2].replace(/^["']|["']$/g, "");
    }
    // bare "XAUUSD BUY" style prefix
    const bare = body.match(/^\s*([A-Za-z0-9._:!\/-]{2,20})\s+(BUY|SELL|LONG|SHORT|FLAT|CLOSE)\b/i);
    if (bare) {
      if (!obj.symbol) obj.symbol = bare[1];
      if (!obj.side) obj.side = bare[2];
    }
  }

  const pick = (...names) => {
    for (const n of names) {
      const v = obj[n] !== undefined ? obj[n] : obj[n.toLowerCase()];
      if (v !== undefined && v !== null && String(v).trim() !== "") return String(v).trim();
    }
    return "";
  };

  const symbol = pick("symbol", "ticker", "pair", "instrument", "sym");
  let side = pick("side", "action", "signal", "direction", "order", "type").toUpperCase();
  if (side === "LONG") side = "BUY";
  if (side === "SHORT") side = "SELL";
  if (side === "CLOSE") side = "FLAT";
  if (!["BUY", "SELL", "FLAT"].includes(side)) side = side ? side.slice(0, 4) : "";

  if (!symbol && !side) return null;

  return {
    symbol: symbol.toUpperCase().slice(0, 12),
    side,
    score: pick("score", "aiscore", "ai_score", "strength", "rating"),
    conf: pick("conf", "confidence", "prob", "probability"),
    entry: pick("entry", "price", "close", "e"),
    tp1: pick("tp1", "tp", "target1", "target", "takeprofit1", "take_profit_1"),
    tp2: pick("tp2", "target2", "takeprofit2", "take_profit_2"),
    sl: pick("sl", "stop", "stoploss", "stop_loss", "loss"),
    tf: pick("tf", "timeframe", "interval"),
    note: pick("note", "comment", "msg", "message", "strategy"),
  };
}

/* ------------------------------------------------------------------ */
/* helpers                                                             */
/* ------------------------------------------------------------------ */

async function readStore(env, device) {
  const v = await env.SIGNALS.get(kvKey(device));
  if (!v) return null;
  try {
    return JSON.parse(v);
  } catch (_) {
    return null;
  }
}

const kvKey = (d) => "sig:" + d;
const sanitiseId = (s) => String(s).replace(/[^A-Za-z0-9_-]/g, "").slice(0, 24) || "main";
const str = (v, n) => String(v === undefined || v === null ? "" : v).slice(0, n);

function clampInt(v, lo, hi, dflt) {
  const n = Math.round(Number(String(v).replace(/[^0-9.\-]/g, "")));
  if (!isFinite(n)) return dflt;
  return Math.min(hi, Math.max(lo, n));
}

function timingSafeEqual(a, b) {
  a = String(a);
  b = String(b);
  if (a.length !== b.length) return false;
  let diff = 0;
  for (let i = 0; i < a.length; i++) diff |= a.charCodeAt(i) ^ b.charCodeAt(i);
  return diff === 0;
}

const esc = (s) =>
  String(s).replace(/[&<>"']/g, (c) => ({ "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;", "'": "&#39;" }[c]));

/* Read endpoints are authenticated by the key in the query string, not by
   origin, so a wildcard here gives away nothing that the key does not already
   gate - and it lets a local dashboard or the CLI's browser mode work. */
const corsHeaders = () => ({
  "access-control-allow-origin": "*",
  "access-control-allow-methods": "GET,POST,OPTIONS",
  "access-control-allow-headers": "content-type",
  "access-control-max-age": "86400",
});

const baseHeaders = () => ({
  "cache-control": "no-store",
  "x-drfx-bridge": BRIDGE_VERSION,
  ...corsHeaders(),
});

const json = (o, status = 200) =>
  new Response(JSON.stringify(o), {
    status,
    headers: { "content-type": "application/json", ...baseHeaders() },
  });

const text = (s, status = 200) =>
  new Response(s, { status, headers: { "content-type": "text/plain", ...baseHeaders() } });
