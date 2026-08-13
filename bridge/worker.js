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
 *   POST /tv?key=WEBHOOK_KEY[&device=main]   TradingView alert lands here
 *   GET  /latest?key=DEVICE_KEY&device=main&since=<ts>   device polls here
 *   GET  /health                              plain "ok"
 *   GET  /                                    small human status page
 *
 * Secrets (wrangler secret put ...)
 *   WEBHOOK_KEY   what TradingView puts in its URL
 *   DEVICE_KEY    what the SmallTV puts in its URL
 * KV namespace binding: SIGNALS
 */

const MAX_BODY = 8 * 1024;      // reject silly-sized alert bodies
const HISTORY = 8;              // how many past signals to keep for the status page
const TTL = 60 * 60 * 24 * 7;   // KV entries expire after a week

export default {
  async fetch(request, env) {
    const url = new URL(request.url);
    const path = url.pathname.replace(/\/+$/, "") || "/";

    try {
      if (path === "/health") return text("ok");
      if (path === "/tv") return handleWebhook(request, url, env);
      if (path === "/latest") return handleLatest(request, url, env);
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

  return json(store.latest);
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
        `<tr><td>${new Date(r.ts).toISOString().replace("T", " ").slice(0, 19)}Z</td>` +
        `<td>${esc(r.symbol)}</td><td class="${r.side === "SELL" ? "s" : "b"}">${esc(r.side)}</td>` +
        `<td>${r.score}</td><td>${esc(r.tp1)}</td><td>${esc(r.tp2)}</td><td>${esc(r.sl)}</td></tr>`
    )
    .join("");

  const body = `<!doctype html><meta charset="utf-8"><title>DrFX GodMode Bridge</title>
<style>
 body{background:#0b0b12;color:#e6e6f0;font:14px/1.5 system-ui,sans-serif;margin:0;padding:32px}
 h1{font-size:18px;letter-spacing:.12em;color:#a78bfa;margin:0 0 4px}
 p{color:#8b8ba7;margin:0 0 24px}
 table{border-collapse:collapse;width:100%;max-width:760px}
 th,td{text-align:left;padding:8px 12px;border-bottom:1px solid #22223a;font-variant-numeric:tabular-nums}
 th{color:#8b8ba7;font-weight:500;font-size:12px;text-transform:uppercase;letter-spacing:.08em}
 .b{color:#34d399}.s{color:#f87171}
 code{background:#16162a;padding:2px 6px;border-radius:4px;color:#c4b5fd}
</style>
<h1>DRFX GODMODE BRIDGE</h1>
<p>Device <code>${esc(device)}</code> &middot; ${store && store.latest ? "last signal " + new Date(store.latest.ts).toUTCString() : "no signals received yet"}</p>
<table><tr><th>Time</th><th>Symbol</th><th>Side</th><th>Score</th><th>TP1</th><th>TP2</th><th>SL</th></tr>${rows || '<tr><td colspan="7" style="color:#8b8ba7">Waiting for the first TradingView alert&hellip;</td></tr>'}</table>`;

  return new Response(body, {
    headers: { "content-type": "text/html;charset=utf-8", "cache-control": "no-store" },
  });
}

/* ------------------------------------------------------------------ */
/* Alert parsing - accepts JSON or loose text                          */
/* ------------------------------------------------------------------ */

/**
 * TradingView sends whatever you typed in the alert box. We accept:
 *   1. JSON:  {"symbol":"XAUUSD","side":"BUY","score":96,"tp1":3378,...}
 *   2. Text:  XAUUSD BUY score=96 tp1=3378 tp2=3386 sl=3362 conf=94
 * Aliases are generous because TradingView placeholders vary between scripts.
 */
function parseAlert(raw) {
  const body = (raw || "").trim();
  if (!body) return null;

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
    sl: pick("sl", "stop", "stoploss", "stop_loss"),
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

const json = (o, status = 200) =>
  new Response(JSON.stringify(o), {
    status,
    headers: { "content-type": "application/json", "cache-control": "no-store" },
  });

const text = (s, status = 200) =>
  new Response(s, { status, headers: { "content-type": "text/plain", "cache-control": "no-store" } });
