/**
 * DrFX Ultra OS - market tickers from the bridge.
 *
 * The device never talks to an exchange directly. It does not negotiate small
 * TLS fragments, so a direct HTTPS call would want a 16 kB receive buffer out of
 * roughly 39 kB of free heap - while the bridge poll is periodically asking for
 * the same thing. The Worker fetches upstream instead and hands back a payload
 * of a few hundred bytes with the sparkline already scaled, which is the whole
 * reason this struct is as small as it is.
 *
 * Which upstream that is changes at runtime: the Worker walks a chain of
 * sources and reports the winner in "src". Do not assume Binance - it has been
 * answering the Worker with 403 for some time, and Coinbase usually wins.
 *
 * Wire format (bridge GET /crypto):
 *
 *   {"v":"2.1.1","src":"coinbase","ts":1755180000000,"tickers":[
 *     {"s":"BTCUSDT","d":"BTC","p":"118420.50","c":2.84,
 *      "h":"119802.00","l":"114210.00","k":[0,12,...,100]}]}
 *
 * Prices arrive as strings, deliberately. They are only ever drawn, never
 * arithmetic, and a float would quietly lose precision on a five-figure price
 * with two decimals.
 */
#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>

#define CRYPTO_MAX 4     // matches MAX_SYMBOLS in the Worker
#define SPARK_MAX 24     // matches SPARK_POINTS in the Worker

struct Ticker {
  String sym;            // BTCUSDT - the pair, shown small
  String name;           // BTC     - the asset, shown large
  String price;          // "118420.50"
  float change = 0;      // 24h change, per cent
  String high;
  String low;
  uint8_t spark[SPARK_MAX] = {0};   // 0-100, already scaled by the Worker
  uint8_t sparkN = 0;

  bool valid() const { return name.length() > 0 && price.length() > 0; }
};

struct CryptoSet {
  Ticker t[CRYPTO_MAX];
  uint8_t n = 0;
  uint32_t rxMillis = 0;    // millis() when this set arrived
  bool ok = false;          // false until the first successful fetch
  String error;
  // Which upstream the Worker actually used - "COINBASE", "KRAKEN", ... The
  // bridge falls through a chain of sources, so this is not knowable at build
  // time, and the header used to claim BINANCE regardless of who answered.
  String src;

  bool has(uint8_t i) const { return i < n && t[i].valid(); }
  uint32_t ageSec() const { return ok ? (millis() - rxMillis) / 1000 : 0; }
};

/* Split "118420.50" into "118420" and ".50".
 *
 * The 48px font carries digits, '.', '-' and '+' but no comma, so the integer
 * part is drawn there and the decimals follow in a smaller face. Sub-dollar
 * prices would render as a huge "0", so the caller is told to draw the whole
 * string small instead by returning an empty integer part. */
inline void splitPrice(const String &price, String &whole, String &frac) {
  int dot = price.indexOf('.');
  if (dot < 0) {
    whole = price;
    frac = "";
  } else {
    whole = price.substring(0, dot);
    frac = price.substring(dot);
  }
  if (whole == "0" || whole == "-0") {
    whole = "";           // tells drawCrypto to use one small font throughout
    frac = price;
  }
}

inline bool cryptoFromJson(const String &body, CryptoSet &out) {
  JsonDocument doc;
  if (deserializeJson(doc, body)) {
    out.error = F("bad JSON from bridge");
    return false;
  }

  JsonArray arr = doc["tickers"].as<JsonArray>();
  if (arr.isNull()) {
    const char *e = doc["error"] | "";
    out.error = e[0] ? String(e) : String(F("no tickers in reply"));
    return false;
  }

  uint8_t n = 0;
  for (JsonObject o : arr) {
    if (n >= CRYPTO_MAX) break;
    Ticker &t = out.t[n];

    t.sym = String(o["s"].as<const char *>() ? o["s"].as<const char *>() : "");
    t.name = String(o["d"].as<const char *>() ? o["d"].as<const char *>() : "");
    t.price = String(o["p"].as<const char *>() ? o["p"].as<const char *>() : "");
    t.change = o["c"] | 0.0f;
    t.high = String(o["h"].as<const char *>() ? o["h"].as<const char *>() : "");
    t.low = String(o["l"].as<const char *>() ? o["l"].as<const char *>() : "");

    t.sparkN = 0;
    JsonArray k = o["k"].as<JsonArray>();
    if (!k.isNull()) {
      for (JsonVariant v : k) {
        if (t.sparkN >= SPARK_MAX) break;
        int p = v | 0;
        t.spark[t.sparkN++] = (uint8_t)(p < 0 ? 0 : (p > 100 ? 100 : p));
      }
    }

    if (t.valid()) n++;
  }

  if (!n) {
    out.error = F("no usable tickers");
    return false;
  }

  out.n = n;
  out.rxMillis = millis();
  out.ok = true;
  out.error = "";

  const char *src = doc["src"] | "";
  out.src = String(src);
  out.src.toUpperCase();      // the header face is caps-only

  return true;
}
