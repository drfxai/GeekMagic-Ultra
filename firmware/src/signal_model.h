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
  String sl;
  String tf;
  String note;
  uint32_t rxMillis = 0;  // millis() when this device received it

  bool valid() const { return symbol.length() > 0 || side.length() > 0; }
};

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
  out.entry = String(doc["entry"].as<const char *>() ? doc["entry"].as<const char *>() : "");
  out.tp1 = String(doc["tp1"].as<const char *>() ? doc["tp1"].as<const char *>() : "");
  out.tp2 = String(doc["tp2"].as<const char *>() ? doc["tp2"].as<const char *>() : "");
  out.sl = String(doc["sl"].as<const char *>() ? doc["sl"].as<const char *>() : "");
  out.tf = String(doc["tf"].as<const char *>() ? doc["tf"].as<const char *>() : "");
  out.note = String(doc["note"].as<const char *>() ? doc["note"].as<const char *>() : "");
  out.rxMillis = millis();

  out.side.toUpperCase();
  out.symbol.toUpperCase();
  if (out.score < 0) out.score = 0;
  if (out.score > 100) out.score = 100;
  if (out.conf < 0) out.conf = 0;
  if (out.conf > 100) out.conf = 100;

  return out.valid();
}
