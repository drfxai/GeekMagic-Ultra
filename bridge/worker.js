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
 *   GET  /crypto?key=DEVICE_KEY&symbols=BTCUSDT,...     Binance prices, trimmed
 *   GET  /health                                        plain "ok"
 *   GET  /                                              human status page
 *
 * Secrets (wrangler secret put ...)
 *   WEBHOOK_KEY   what TradingView puts in its URL
 *   DEVICE_KEY    what the SmallTV puts in its URL
 * KV namespace binding: SIGNALS
 */

const BRIDGE_VERSION = "2.1.1";
const MAX_BODY = 8 * 1024;      // reject silly-sized alert bodies
const HISTORY = 24;             // how many past signals to keep
const TTL = 60 * 60 * 24 * 7;   // KV entries expire after a week

/* ---- crypto proxy ------------------------------------------------ */
//
// Binance returns 403 to Cloudflare Workers. Not a geo-block and not a missing
// User-Agent - both were tested, every UA gets 200 from a residential IP and
// none from here. Binance simply refuses Cloudflare's egress ranges, and no
// header we can send changes that.
//
// So market data comes from whichever source answers. Binance stays first
// because it has the best coverage and may work from edges we have not seen,
// and Coinbase backs it up: a public API with no key, no datacenter blocking,
// and both a 24h summary and hourly candles. Whichever wins is named in the
// reply, so a screen showing the wrong price can be traced to its source.
// 90s, not 20. The device polls every 30s and backs off to 120s after a few
// failures, so a 20s TTL guaranteed it never once hit a warm cache - every
// single fetch paid for a cold upstream round trip. Prices on a glanceable
// screen do not need 20-second freshness, and the cheaper path is the one that
// stays inside a Worker's CPU budget.
const CRYPTO_TTL = 90;
const MAX_SYMBOLS = 4;          // keeps subrequests and device memory bounded
const SPARK_POINTS = 24;        // one hourly close per point

const BINANCE = "https://data-api.binance.vision";
const COINBASE = "https://api.exchange.coinbase.com";
const COINGECKO = "https://api.coingecko.com/api/v3";
const KRAKEN = "https://api.kraken.com/0/public";
const PAPRIKA = "https://api.coinpaprika.com/v1";
const BYBIT = "https://api.bybit.com";
const OKX = "https://www.okx.com";

// Coinpaprika keys by coin id like CoinGecko, but with a "sym-name" shape.
// It is a pure aggregator - it holds no customer funds and operates no venue -
// so it has no sanctions surface and no regional gate, which is the entire
// reason it leads the chain.
const PAPRIKA_IDS = {
  BTC: "btc-bitcoin", ETH: "eth-ethereum", SOL: "sol-solana", XRP: "xrp-xrp",
  BNB: "bnb-binance-coin", ADA: "ada-cardano", DOGE: "doge-dogecoin", TRX: "trx-tron",
  AVAX: "avax-avalanche", LINK: "link-chainlink", DOT: "dot-polkadot",
  MATIC: "matic-polygon", LTC: "ltc-litecoin", BCH: "bch-bitcoin-cash",
  ATOM: "atom-cosmos", UNI: "uni-uniswap", XLM: "xlm-stellar",
  ETC: "etc-ethereum-classic", FIL: "fil-filecoin", APT: "apt-aptos",
  ARB: "arb-arbitrum", OP: "op-optimism", NEAR: "near-near-protocol",
  INJ: "inj-injective-protocol", SUI: "sui-sui", TON: "ton-the-open-network",
  SHIB: "shib-shiba-inu", PEPE: "pepe-pepe",
};

// Exchanges enforce sanctions and refuse whole regions. Binance and Coinbase
// both answer 403 to this Worker while answering 200 to the same request from
// a home connection a few metres away - the block follows the egress, not the
// caller. Aggregators and smaller venues do not carry the same exposure, so the
// chain leads with CoinGecko: it is not an exchange, it serves price, 24h range,
// change and a sparkline in ONE request, and it has no regional gate.
//
// CoinGecko keys prices by coin id, not by trading pair. Only the pairs listed
// here can be resolved; anything else falls through to the exchange sources,
// which is the right order of preference anyway when they are reachable.
const GECKO_IDS = {
  BTC: "bitcoin", ETH: "ethereum", SOL: "solana", XRP: "ripple",
  BNB: "binancecoin", ADA: "cardano", DOGE: "dogecoin", TRX: "tron",
  AVAX: "avalanche-2", LINK: "chainlink", DOT: "polkadot", MATIC: "matic-network",
  LTC: "litecoin", BCH: "bitcoin-cash", ATOM: "cosmos", UNI: "uniswap",
  XLM: "stellar", ETC: "ethereum-classic", FIL: "filecoin", APT: "aptos",
  ARB: "arbitrum", OP: "optimism", NEAR: "near", INJ: "injective-protocol",
  SUI: "sui", TON: "the-open-network", SHIB: "shiba-inu", PEPE: "pepe",
};

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
      if (path === "/crypto") return handleCrypto(url, env);
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
/* Binance proxy                                                       */
/*                                                                     */
/* The device could call Binance itself, but it does not negotiate      */
/* small TLS fragments, so every direct HTTPS call would need a 16 kB   */
/* receive buffer out of roughly 39 kB of free heap - alongside the     */
/* bridge poll already doing the same thing. Proxying here means one    */
/* TLS host, a payload measured in hundreds of bytes, and the sparkline */
/* arithmetic done on hardware that has floating point.                 */
/* ------------------------------------------------------------------ */

/**
 * Decimal places that suit the magnitude.
 *
 * The device draws the integer part in a 48px font that has digits, '.', '-'
 * and '+' but NO COMMA glyph, so no thousands separators are sent. Two decimals
 * on a five-figure price is already more precision than a glanceable screen
 * needs; sub-dollar coins get more so they are not all just "0.00".
 */
function priceDecimals(v) {
  if (v >= 1000) return 2;
  if (v >= 1) return 3;
  if (v >= 0.01) return 5;
  return 8;
}

function formatPrice(raw) {
  const n = Number(raw);
  if (!isFinite(n)) return "0";
  return n.toFixed(priceDecimals(Math.abs(n)));
}

/** Strip the quote asset so the screen shows "BTC" rather than "BTCUSDT". */
function shortName(symbol) {
  for (const quote of ["USDT", "FDUSD", "BUSD", "USDC", "TUSD", "BTC", "ETH", "EUR", "GBP"]) {
    if (symbol.length > quote.length && symbol.endsWith(quote)) {
      return symbol.slice(0, -quote.length);
    }
  }
  return symbol;
}

/**
 * Closing prices -> 24 integers from 0 to 100.
 *
 * Sending scaled points rather than prices keeps the payload tiny and means the
 * firmware plots them with two multiplications and no floating point at all. A
 * flat series would divide by zero, so it is pinned to the middle of the range.
 */
function scaleSpark(closes) {
  const vals = closes.map(Number).filter((n) => isFinite(n));
  if (vals.length < 2) return [];
  const lo = Math.min(...vals);
  const hi = Math.max(...vals);
  const span = hi - lo;
  if (span <= 0) return vals.map(() => 50);
  return vals.map((v) => Math.round(((v - lo) / span) * 100));
}

/**
 * One cached upstream GET.
 *
 * cacheEverything makes Cloudflare hold the answer at the edge, so a second
 * device - or a browser on the same URL - costs the exchange nothing. The
 * User-Agent is set because some exchanges reject requests without one; it is
 * not what Binance objects to, but Coinbase does care.
 */
async function upstream(url) {
  const r = await fetch(url, {
    cf: { cacheTtl: CRYPTO_TTL, cacheEverything: true },
    headers: {
      accept: "application/json",
      "user-agent": `DrFX-UltraOS/${BRIDGE_VERSION} (+https://github.com/drfxai/GeekMagic-Ultra)`,
    },
  });
  if (!r.ok) throw new Error(`${r.status}`);
  return r.json();
}

/* ---- source: Binance --------------------------------------------- */

async function fromBinance(symbols) {
  const tickers = await upstream(
    `${BINANCE}/api/v3/ticker/24hr?symbols=${encodeURIComponent(JSON.stringify(symbols))}`
  );

  const bySymbol = new Map();
  for (const t of Array.isArray(tickers) ? tickers : [tickers]) {
    if (t && t.symbol) bySymbol.set(t.symbol, t);
  }

  // Sparklines are a nice-to-have. One candle request failing must not cost the
  // user their prices, so each is settled independently and an empty series
  // simply means the device draws no chart.
  const sparks = await Promise.all(
    symbols.map(async (s) => {
      try {
        const k = await upstream(
          `${BINANCE}/api/v3/klines?symbol=${encodeURIComponent(s)}&interval=1h&limit=${SPARK_POINTS}`
        );
        return scaleSpark((k || []).map((row) => row[4]));
      } catch (_) {
        return [];
      }
    })
  );

  const out = [];
  symbols.forEach((s, i) => {
    const t = bySymbol.get(s);
    if (!t) return;                      // unknown pair - skip, do not invent
    out.push({
      s,
      d: shortName(s),
      p: formatPrice(t.lastPrice),
      c: Number(Number(t.priceChangePercent).toFixed(2)),
      h: formatPrice(t.highPrice),
      l: formatPrice(t.lowPrice),
      k: sparks[i],
    });
  });
  return out;
}

/* ---- source: Coinbase -------------------------------------------- */

/** BTCUSDT -> BTC-USD. Coinbase quotes in USD far more widely than USDT. */
function coinbaseProduct(symbol) {
  return `${shortName(symbol)}-USD`;
}

async function fromCoinbase(symbols) {
  // A 404 means that one pair is not listed, which is the caller's problem and
  // should not condemn the source. Anything else - 500, a block, a timeout - is
  // Coinbase being unavailable, and has to surface with its real status so the
  // next source is tried and the error names the cause. Without this split, an
  // exchange that is entirely down reports as "no matching pairs".
  const failures = [];

  const results = await Promise.all(
    symbols.map(async (s) => {
      const product = coinbaseProduct(s);

      // /stats is the 24h summary. It has no change field, so it is derived
      // from open and last - which is what "24h change" means anyway.
      let stats;
      try {
        stats = await upstream(`${COINBASE}/products/${encodeURIComponent(product)}/stats`);
      } catch (err) {
        const status = String(err.message || err);
        if (status !== "404") failures.push(status);
        return null;
      }

      const last = Number(stats.last);
      const open = Number(stats.open);
      if (!isFinite(last) || last <= 0) return null;

      const change = isFinite(open) && open > 0 ? ((last - open) / open) * 100 : 0;

      let spark = [];
      try {
        // Candles come back newest-first as [time, low, high, open, close, vol].
        //
        // The window is pinned to the last SPARK_POINTS hours. Without start and
        // end Coinbase returns its maximum of 300+ rows, and we then parse and
        // throw away 92% of them - on a Worker with a 10 ms CPU budget that is
        // most of the budget spent on data nobody sees. Rounding the bounds to
        // the hour also means every device asking within the same hour produces
        // an identical URL, so the edge cache actually gets hit.
        const hour = 3600 * 1000;
        const end = Math.floor(Date.now() / hour) * hour;
        const start = end - SPARK_POINTS * hour;
        const c = await upstream(
          `${COINBASE}/products/${encodeURIComponent(product)}/candles` +
            `?granularity=3600&start=${new Date(start).toISOString()}` +
            `&end=${new Date(end).toISOString()}`
        );
        spark = scaleSpark((c || []).slice(0, SPARK_POINTS).reverse().map((row) => row[4]));
      } catch (_) {
        spark = [];
      }

      return {
        s,
        d: shortName(s),
        p: formatPrice(last),
        c: Number(change.toFixed(2)),
        h: formatPrice(stats.high),
        l: formatPrice(stats.low),
        k: spark,
      };
    })
  );

  const out = results.filter(Boolean);
  if (!out.length && failures.length) throw new Error(failures[0]);
  return out;
}

/* ---- source: CoinGecko ------------------------------------------- */

/**
 * One request covers every symbol: price, 24h range, 24h change and a
 * sparkline. That makes this both the most reachable source and by far the
 * cheapest - a single subrequest against four for two pairs on Coinbase.
 *
 * sparkline_in_7d is 168 hourly points; the last 24 are the same window the
 * other sources produce.
 */
async function fromCoinGecko(symbols) {
  const wanted = [];
  for (const s of symbols) {
    const id = GECKO_IDS[shortName(s)];
    if (id) wanted.push({ symbol: s, id });
  }
  if (!wanted.length) return [];      // nothing this source can resolve

  const ids = [...new Set(wanted.map((w) => w.id))].join(",");
  const rows = await upstream(
    `${COINGECKO}/coins/markets?vs_currency=usd&ids=${encodeURIComponent(ids)}` +
      `&sparkline=true&price_change_percentage=24h&precision=full`
  );

  const byId = new Map();
  for (const r of Array.isArray(rows) ? rows : []) if (r && r.id) byId.set(r.id, r);

  const out = [];
  for (const w of wanted) {
    const r = byId.get(w.id);
    if (!r || !isFinite(Number(r.current_price))) continue;
    const series = (r.sparkline_in_7d && r.sparkline_in_7d.price) || [];
    out.push({
      s: w.symbol,
      d: shortName(w.symbol),
      p: formatPrice(r.current_price),
      c: Number(Number(r.price_change_percentage_24h || 0).toFixed(2)),
      h: formatPrice(r.high_24h),
      l: formatPrice(r.low_24h),
      k: scaleSpark(series.slice(-SPARK_POINTS)),
    });
  }
  return out;
}

/* ---- source: Kraken ---------------------------------------------- */

/** Kraken quotes in USD and uses XBT for bitcoin. */
function krakenPair(symbol) {
  const base = shortName(symbol);
  return `${base === "BTC" ? "XBT" : base}USD`;
}

/**
 * Last resort, and price-only: Kraken's OHLC endpoint is a second request per
 * symbol and this source exists to keep a number on the screen when everything
 * else is blocked, not to draw charts.
 */
async function fromKraken(symbols) {
  const pairs = symbols.map(krakenPair);
  const res = await upstream(
    `${KRAKEN}/Ticker?pair=${encodeURIComponent(pairs.join(","))}`
  );
  if (res && Array.isArray(res.error) && res.error.length && !res.result) {
    throw new Error(res.error.join(" "));
  }

  const result = (res && res.result) || {};
  const entries = Object.entries(result);

  const out = [];
  symbols.forEach((s, i) => {
    // Kraken answers under its own canonical names, which interleave legacy
    // asset-class markers: XBTUSD comes back as XXBTZUSD and ETHUSD as
    // XETHZUSD. A substring match on the whole pair therefore fails - the Z
    // sits between base and quote. Match base and quote independently instead.
    const base = pairs[i].replace(/USD$/, "");
    const hit = entries.find(([k]) => k.includes(base) && k.includes("USD"));
    if (!hit) return;
    const t = hit[1];
    const last = Number(t.c && t.c[0]);
    const open = Number(t.o);
    if (!isFinite(last) || last <= 0) return;
    out.push({
      s,
      d: shortName(s),
      p: formatPrice(last),
      c: Number((isFinite(open) && open > 0 ? ((last - open) / open) * 100 : 0).toFixed(2)),
      h: formatPrice(t.h && t.h[1]),
      l: formatPrice(t.l && t.l[1]),
      k: [],
    });
  });
  return out;
}

/* ---- source: Coinpaprika ------------------------------------------ */

/**
 * Aggregator, no key, no regional gate.
 *
 * Coinbase and Kraken both answer 403 to this Worker from Middle East colos -
 * they enforce sanctions against the egress IP, and no header changes that.
 * Coinpaprika runs no exchange and custodies nothing, so it has no such gate.
 *
 * The trade is that /tickers carries no 24h high/low and no candles on the free
 * tier. High and low are returned empty rather than zero, which the firmware
 * reads as "no range" and omits from the footer instead of printing "0 - 0".
 * The sparkline is empty for the same reason: the device simply draws no chart.
 * A price with no chart beats a chart with no price.
 */
async function fromCoinpaprika(symbols) {
  const wanted = [];
  for (const s of symbols) {
    const id = PAPRIKA_IDS[shortName(s)];
    if (id) wanted.push({ symbol: s, id });
  }
  if (!wanted.length) return [];

  const rows = await Promise.all(
    wanted.map(async (w) => {
      try {
        return { w, r: await upstream(`${PAPRIKA}/tickers/${encodeURIComponent(w.id)}?quotes=USD`) };
      } catch (_) {
        return null;      // one missing coin must not condemn the source
      }
    })
  );

  const out = [];
  for (const row of rows) {
    if (!row || !row.r) continue;
    const q = row.r.quotes && row.r.quotes.USD;
    if (!q || !isFinite(Number(q.price))) continue;
    out.push({
      s: row.w.symbol,
      d: shortName(row.w.symbol),
      p: formatPrice(q.price),
      c: Number(Number(q.percent_change_24h || 0).toFixed(2)),
      h: "",
      l: "",
      k: [],
    });
  }
  return out;
}

/* ---- source: Bybit ------------------------------------------------- */

/** Bybit quotes USDT pairs under the symbol as given - BTCUSDT stays BTCUSDT. */
async function fromBybit(symbols) {
  const results = await Promise.all(
    symbols.map(async (s) => {
      let t;
      try {
        const r = await upstream(`${BYBIT}/v5/market/tickers?category=spot&symbol=${encodeURIComponent(s)}`);
        t = r && r.result && r.result.list && r.result.list[0];
      } catch (_) {
        return null;
      }
      if (!t || !isFinite(Number(t.lastPrice))) return null;

      let spark = [];
      try {
        // list is newest-first: [start, open, high, low, close, volume, turnover]
        const k = await upstream(
          `${BYBIT}/v5/market/kline?category=spot&symbol=${encodeURIComponent(s)}` +
            `&interval=60&limit=${SPARK_POINTS}`
        );
        const rows = (k && k.result && k.result.list) || [];
        spark = scaleSpark(rows.map((row) => row[4]).reverse());
      } catch (_) {
        spark = [];
      }

      // price24hPcnt is a fraction ("0.0032"), not a percentage.
      const pct = Number(t.price24hPcnt);
      return {
        s,
        d: shortName(s),
        p: formatPrice(t.lastPrice),
        c: Number(((isFinite(pct) ? pct : 0) * 100).toFixed(2)),
        h: formatPrice(t.highPrice24h),
        l: formatPrice(t.lowPrice24h),
        k: spark,
      };
    })
  );
  return results.filter(Boolean);
}

/* ---- source: OKX ---------------------------------------------------- */

/** BTCUSDT -> BTC-USDT. OKX uses a dash and quotes USDT widely. */
function okxInst(symbol) {
  return `${shortName(symbol)}-USDT`;
}

async function fromOkx(symbols) {
  const results = await Promise.all(
    symbols.map(async (s) => {
      const inst = okxInst(s);
      let t;
      try {
        const r = await upstream(`${OKX}/api/v5/market/ticker?instId=${encodeURIComponent(inst)}`);
        t = r && r.data && r.data[0];
      } catch (_) {
        return null;
      }
      const last = Number(t && t.last);
      if (!isFinite(last) || last <= 0) return null;

      // OKX has no change field either, so it comes from open24h and last.
      const open = Number(t.open24h);
      const change = isFinite(open) && open > 0 ? ((last - open) / open) * 100 : 0;

      let spark = [];
      try {
        // data is newest-first: [ts, open, high, low, close, vol, volCcy]
        const k = await upstream(
          `${OKX}/api/v5/market/candles?instId=${encodeURIComponent(inst)}&bar=1H&limit=${SPARK_POINTS}`
        );
        spark = scaleSpark(((k && k.data) || []).map((row) => row[4]).reverse());
      } catch (_) {
        spark = [];
      }

      return {
        s,
        d: shortName(s),
        p: formatPrice(last),
        c: Number(change.toFixed(2)),
        h: formatPrice(t.high24h),
        l: formatPrice(t.low24h),
        k: spark,
      };
    })
  );
  return results.filter(Boolean);
}

/* ---- the route ---------------------------------------------------- */

// Ordered by measured reachability from the edge, not by preference.
//
// Probed from two unrelated Cloudflare colos on 2026-08-16, same result both
// times: coinbase and kraken answer in under 200 ms, coingecko returns 429 on
// every call, and binance returns 403 after burning ~950 ms doing it.
//
// CoinGecko led this list because it is an aggregator and looked like the
// safest default. In practice its free tier rate-limits Workers hard, so it
// never won - it just added a wasted round trip in front of the source that
// did. Binance is last for the same reason: it is a slow, reliable failure.
// Both stay in the chain because they cost nothing once they are behind two
// sources that work, and they cover pairs the others may not list.
// Measured from the device's own colo on 2026-08-16, via the 502 body the
// firmware now reports:
//
//   coinbase: 403   kraken: 403   coingecko: 429   binance: 403
//
// All four dead at once, while the same probe from a European colo minutes
// earlier showed coinbase and kraken healthy. Cloudflare does not pin a client
// to one colo, so "it worked when I tested it" and "the device sees 403" are
// both true. Coinbase and Kraken enforce sanctions against the egress IP, which
// no header can talk its way past.
//
// The chain therefore leads with venues that have no such gate: Coinpaprika is
// a pure aggregator, and Bybit and OKX serve the region. The blocked four stay
// on as coverage for pairs the leaders do not list, and for colos where they
// do answer.
const SOURCES = [
  { name: "coinpaprika", fn: fromCoinpaprika },
  { name: "bybit", fn: fromBybit },
  { name: "okx", fn: fromOkx },
  { name: "coinbase", fn: fromCoinbase },
  { name: "kraken", fn: fromKraken },
  { name: "coingecko", fn: fromCoinGecko },
  { name: "binance", fn: fromBinance },
];

async function handleCrypto(url, env) {
  if (!authDevice(url, env)) return text("bad key", 403);

  const symbols = (url.searchParams.get("symbols") || "BTCUSDT")
    .split(",")
    .map((s) => s.trim().toUpperCase().replace(/[^A-Z0-9]/g, ""))
    .filter(Boolean)
    .slice(0, MAX_SYMBOLS);

  if (!symbols.length) return text("no usable symbols", 400);

  // ?source=coinbase pins one source. Useful when a screen shows a price you
  // want to attribute, and for testing a source that is not currently winning.
  const want = (url.searchParams.get("source") || "").toLowerCase();
  const chain = want ? SOURCES.filter((s) => s.name === want) : SOURCES;
  if (!chain.length) return json({ error: `unknown source '${want}'` }, 400);

  // ?probe=1 tries every source and reports what each one said, instead of
  // stopping at the first that works. Diagnosing a blocked source otherwise
  // means a deploy per guess, which is how this took as long as it did.
  if (url.searchParams.get("probe")) {
    const report = {};
    await Promise.all(
      SOURCES.map(async (s) => {
        const t0 = Date.now();
        try {
          const r = await s.fn(symbols);
          report[s.name] = {
            ok: r.length > 0,
            pairs: r.length,
            ms: Date.now() - t0,
            sample: r.length ? `${r[0].d} ${r[0].p}` : null,
            spark: r.length ? r[0].k.length : 0,
          };
        } catch (err) {
          report[s.name] = { ok: false, error: String(err.message || err), ms: Date.now() - t0 };
        }
      })
    );
    return json({ v: BRIDGE_VERSION, symbols, sources: report });
  }

  const tried = [];
  for (const source of chain) {
    try {
      const tickers = await source.fn(symbols);
      if (tickers.length) {
        return json({ v: BRIDGE_VERSION, src: source.name, ts: Date.now(), tickers });
      }
      tried.push(`${source.name}: no matching pairs`);
    } catch (err) {
      tried.push(`${source.name}: ${String(err.message || err)}`);
    }
  }

  // 502 rather than 500: the failure is upstream, and the device backs off on
  // any non-200 rather than hammering services that are already unhappy.
  return json({ error: `no source had ${symbols.join(",")}`, tried }, 502);
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
