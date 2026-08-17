/**
 * DrFX GodMode - the signal record and the two ways it can arrive.
 *
 *   1. Pulled from the Cloudflare Worker over HTTPS  (TradingView path)
 *   2. Pushed straight to the device over the LAN     (local scripts, testing)
 *
 * Both funnel into the same Signal struct so the renderer only has one input.
 *
 * NOTE: this file is deliberately NOT called signal.h - that name collides with
 * the C standard library's <signal.h>, which the toolchain pulls in.
 */
#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>

struct Signal {
  uint64_t ts = 0;      // milliseconds since epoch, set by the bridge
  String symbol;
  String side;          // BUY / SELL / FLAT
  int score = 0;        // 0-100
  int conf = 0;         // 0-100
  String entry;
  String tp1;
  String tp2;
  String tp3;
  String sl;
  String tf;
  String note;
  // How many targets have been reached, 0-3. The device cannot work this out
  // for itself - it has no price feed for anything but crypto - so the bridge
  // reports it. See parseDrfxTag() in bridge/worker.js.
  uint8_t hit = 0;
  uint32_t rxMillis = 0;  // millis() when this device received it

  bool valid() const { return symbol.length() > 0 || side.length() > 0; }

  /* Only the targets actually supplied are drawn. An alert that carries two
     levels shows two rungs, not three with a blank. */
  uint8_t targetCount() const {
    uint8_t n = 0;
    if (tp1.length()) n++;
    if (tp2.length()) n++;
    if (tp3.length()) n++;
    return n;
  }

  const String &target(uint8_t i) const {
    return (i == 0) ? tp1 : (i == 1) ? tp2 : tp3;
  }

  /* The level still in play, or empty once every target is made. */
  String nextTarget() const {
    return (hit < targetCount()) ? target(hit) : String();
  }
};

/**
 * Cap the decimals on a price so it cannot run off a 240px panel.
 *
 * The bridge rounds properly before sending, so this is a backstop for the
 * /api/push path, where a local script talks to the device directly and never
 * passes through the Worker. It truncates rather than rounds - a last defence
 * does not need to be perfect, it needs to keep the stop-loss on the screen.
 *
 * Three or more integer digits means metals, indices or crypto: two decimals.
 * Anything smaller is an FX pair and keeps the precision it needs.
 */
inline String trimLevel(const String &s) {
  const int dot = s.indexOf('.');
  if (dot < 0) return s;
  const int start = (s.length() && s[0] == '-') ? 1 : 0;

  // Is the integer part just zero? Then every significant digit is to the
  // right of the point and cutting to five would render a token as 0.00001.
  bool subUnit = true;
  for (int i = start; i < dot; i++) {
    if (s[i] != '0') { subUnit = false; break; }
  }

  const int intDigits = dot - start;
  const int keep = subUnit ? 8 : (intDigits >= 3 ? 2 : 5);
  if ((int)s.length() - dot - 1 <= keep) return s;
  return s.substring(0, dot + 1 + keep);
}

/* uint64 -> decimal string. Arduino's String has no 64-bit constructor and
   newlib-nano on the ESP8266 does not print %llu, so do it by hand. */
inline String u64str(uint64_t v) {
  char buf[21];
  int i = 20;
  buf[i] = '\0';
  if (v == 0) buf[--i] = '0';
  while (v > 0 && i > 0) {
    buf[--i] = (char)('0' + (uint32_t)(v % 10));
    v /= 10;
  }
  return String(&buf[i]);
}

inline bool signalFromJson(const String &body, Signal &out) {
  JsonDocument doc;
  if (deserializeJson(doc, body)) return false;

  out.ts = doc["ts"].is<uint64_t>() ? doc["ts"].as<uint64_t>() : 0;
  out.symbol = String(doc["symbol"].as<const char *>() ? doc["symbol"].as<const char *>() : "");
  out.side = String(doc["side"].as<const char *>() ? doc["side"].as<const char *>() : "");
  out.score = doc["score"] | 0;
  out.conf = doc["conf"] | 0;
  out.entry = trimLevel(String(doc["entry"].as<const char *>() ? doc["entry"].as<const char *>() : ""));
  out.tp1 = trimLevel(String(doc["tp1"].as<const char *>() ? doc["tp1"].as<const char *>() : ""));
  out.tp2 = trimLevel(String(doc["tp2"].as<const char *>() ? doc["tp2"].as<const char *>() : ""));
  out.tp3 = trimLevel(String(doc["tp3"].as<const char *>() ? doc["tp3"].as<const char *>() : ""));
  out.sl = trimLevel(String(doc["sl"].as<const char *>() ? doc["sl"].as<const char *>() : ""));
  out.hit = (uint8_t)(doc["hit"] | 0);
  out.tf = String(doc["tf"].as<const char *>() ? doc["tf"].as<const char *>() : "");
  out.note = String(doc["note"].as<const char *>() ? doc["note"].as<const char *>() : "");
  out.rxMillis = millis();

  out.side.toUpperCase();
  out.symbol.toUpperCase();
  if (out.score < 0) out.score = 0;
  if (out.score > 100) out.score = 100;
  if (out.conf < 0) out.conf = 0;
  if (out.conf > 100) out.conf = 100;
  // A hit count larger than the number of supplied targets would light pips
  // that have no rung above them.
  if (out.hit > out.targetCount()) out.hit = out.targetCount();

  return out.valid();
}
