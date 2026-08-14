/**
 * DrFX GodMode - GeekMagic SmallTV Ultra (ESP8266)
 *
 * Shows TradingView signals on the little cube.
 *
 * TradingView refuses to post to anything but HTTPS, and this device sits on a
 * private LAN with only a plain-HTTP web server. Rather than trying to make the
 * ESP8266 publicly reachable over TLS - which would mean port forwarding, a
 * dynamic DNS name and a certificate it cannot really validate - the flow is
 * turned around: a free Cloudflare Worker is the HTTPS endpoint TradingView
 * talks to, and this firmware *pulls* from that Worker. Outbound only. Nothing
 * on your router changes and nothing is exposed to the internet.
 *
 * Settings are edited in a browser at http://godmode.local (or the IP shown on
 * screen at boot). Firmware updates go to /update in the same UI.
 */

#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <ESP8266WiFiMulti.h>
#include <ESP8266WebServer.h>
#include <ESP8266HTTPClient.h>
#include <ESP8266HTTPUpdateServer.h>
#ifndef GODMODE_SLIM
#include <ESP8266mDNS.h>
#endif
#include <WiFiClientSecureBearSSL.h>
#include <DNSServer.h>
#include <LittleFS.h>
#include <TFT_eSPI.h>
#include <time.h>
#include <sys/time.h>   // settimeofday, for the bridge clock fallback
#include <stdlib.h>     // setenv, for the timezone rule

#include "config.h"
#include "signal_model.h"
#include "crypto_model.h"
#include "display.h"
#include "web_ui.h"

Config cfg;
TFT_eSPI tft = TFT_eSPI();

ESP8266WebServer server(80);
ESP8266HTTPUpdateServer updater;
DNSServer dnsServer;
// Kept at file scope on purpose: destroying the WiFiMulti object would throw
// away the saved AP list that the reconnect logic relies on.
ESP8266WiFiMulti wifiMulti;

bool apMode = false;
bool mflnOk = false;
bool timeReady = false;

Signal current;
CryptoSet crypto;

uint32_t lastPoll = 0;
uint32_t lastClock = 0;
uint32_t lastNightCheck = 0;
uint32_t lastCrypto = 0;
uint32_t lastTimeTry = 0;
uint32_t pollFails = 0;
uint32_t cryptoFails = 0;
int lastHttpCode = 0;
String lastError;
const char *timeSource = "none";   // none / ntp / bridge

enum Screen { SCR_BOOT, SCR_SIGNAL, SCR_CLOCK, SCR_CRYPTO };
Screen screen = SCR_BOOT;

/* ------------------------------------------------------------------ */
/* The carousel                                                        */
/*                                                                     */
/* Screens are not a fixed list: a slot only exists while it has        */
/* something to show, so an expired signal or a crypto fetch that has   */
/* never succeeded simply drops out of the rotation instead of showing  */
/* an empty card. The list is rebuilt whenever content changes.         */
/* ------------------------------------------------------------------ */

enum SlotKind : uint8_t { SLOT_SIGNAL, SLOT_CLOCK, SLOT_CRYPTO };

struct Slot {
  uint8_t kind;
  uint8_t idx;    // which ticker, for SLOT_CRYPTO
};

Slot slots[2 + CRYPTO_MAX];
uint8_t slotCount = 0;
uint8_t slotPos = 0;
uint8_t cryptoShown = 0xFF;    // which ticker is currently drawn
uint32_t lastRotate = 0;
uint32_t pinUntil = 0;         // millis() until which a fresh signal holds

/* Signed comparison so this still behaves at the 49-day millis() rollover. */
inline bool pinActive() {
  return pinUntil != 0 && (int32_t)(pinUntil - millis()) > 0;
}

/* ================================================================== */
/* small helpers                                                       */
/* ================================================================== */

String urlEncode(const char *s) {
  String out;
  for (const char *p = s; *p; ++p) {
    char c = *p;
    if (isalnum((unsigned char)c) || c == '-' || c == '_' || c == '.' || c == '~') {
      out += c;
    } else {
      char b[4];
      snprintf(b, sizeof(b), "%%%02X", (unsigned char)c);
      out += b;
    }
  }
  return out;
}

/* Pull the hostname out of "https://foo.bar.workers.dev" */
String bridgeHost() {
  String u(cfg.bridge);
  int p = u.indexOf("://");
  if (p >= 0) u = u.substring(p + 3);
  int s = u.indexOf('/');
  if (s >= 0) u = u.substring(0, s);
  int c = u.indexOf(':');
  if (c >= 0) u = u.substring(0, c);
  return u;
}

bool bridgeIsHttps() { return String(cfg.bridge).startsWith("https://"); }

/* ------------------------------------------------------------------ */
/* Time and timezones                                                  */
/*                                                                     */
/* The zone is a POSIX TZ rule such as "GMT0BST,M3.5.0/1,M10.5.0", not a  */
/* fixed offset, so newlib applies daylight saving itself and the screen  */
/* is right on the mornings either side of the changeover.                */
/* ------------------------------------------------------------------ */

void applyTimezone() {
  setenv("TZ", cfg.tz[0] ? cfg.tz : "UTC0", 1);
  tzset();
}

/* Days since 1970-01-01 from a civil date. Howard Hinnant's algorithm, valid
   for any date this device will ever see. Used only to measure the offset
   between local and UTC, because newlib on the ESP8266 does not reliably
   expose struct tm::tm_gmtoff. */
static long daysFromCivil(int y, unsigned m, unsigned d) {
  y -= m <= 2;
  const int era = (y >= 0 ? y : y - 399) / 400;
  const unsigned yoe = (unsigned)(y - era * 400);
  const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return (long)era * 146097 + (long)doe - 719468;
}

static long tmToEpoch(const struct tm &t) {
  return daysFromCivil(t.tm_year + 1900, (unsigned)t.tm_mon + 1, (unsigned)t.tm_mday) * 86400L
         + t.tm_hour * 3600L + t.tm_min * 60L + t.tm_sec;
}

/* Seconds that local time is ahead of UTC right now, DST included. */
long tzOffsetSeconds() {
  time_t now = time(nullptr);
  struct tm lt, gt;
  localtime_r(&now, &lt);
  gmtime_r(&now, &gt);
  return tmToEpoch(lt) - tmToEpoch(gt);
}

void tzOffsetString(char *buf, size_t n) {
  long off = tzOffsetSeconds();
  char sign = off < 0 ? '-' : '+';
  long a = off < 0 ? -off : off;
  snprintf(buf, n, "UTC%c%02ld:%02ld", sign, a / 3600, (a % 3600) / 60);
}

/* Everything the clock screen shows, in one pass over localtime. */
void fillClock(ClockView &c) {
  c.valid = timeReady;
  if (!timeReady) {
    strlcpy(c.zone, cfg.tzName, sizeof(c.zone));
    strlcpy(c.offset, "--", sizeof(c.offset));
    return;
  }

  time_t now = time(nullptr);
  struct tm lt;
  localtime_r(&now, &lt);

  snprintf(c.hhmm, sizeof(c.hhmm), "%02d:%02d", lt.tm_hour, lt.tm_min);
  snprintf(c.ss, sizeof(c.ss), "%02d", lt.tm_sec);
  c.secPct = (lt.tm_sec * 100) / 60;

  strftime(c.date, sizeof(c.date), "%d %b %Y", &lt);
  strftime(c.weekday, sizeof(c.weekday), "%A", &lt);
  strftime(c.abbr, sizeof(c.abbr), "%Z", &lt);
  for (char *p = c.date; *p; ++p) *p = toupper((unsigned char)*p);
  for (char *p = c.weekday; *p; ++p) *p = toupper((unsigned char)*p);

  strlcpy(c.zone, cfg.tzName[0] ? cfg.tzName : cfg.tz, sizeof(c.zone));
  tzOffsetString(c.offset, sizeof(c.offset));
}

/* Local wall-clock hour, or -1 if we have no time yet */
int localHour() {
  if (!timeReady) return -1;
  time_t now = time(nullptr);
  struct tm lt;
  localtime_r(&now, &lt);
  return lt.tm_hour;
}

bool isNight() {
  int h = localHour();
  if (h < 0) return false;
  if (cfg.nightStart == cfg.nightEnd) return false;
  if (cfg.nightStart < cfg.nightEnd) return h >= cfg.nightStart && h < cfg.nightEnd;
  return h >= cfg.nightStart || h < cfg.nightEnd;   // wraps past midnight
}

void applyBrightness() {
  setBacklight(isNight() ? cfg.brightNight : cfg.brightDay);
}

bool signalIsFresh() {
  if (!current.valid()) return false;
  if (cfg.staleMin == 0) return true;
  return (millis() - current.rxMillis) < ((uint32_t)cfg.staleMin * 60000UL);
}

/* ================================================================== */
/* screen refresh                                                      */
/* ================================================================== */

void buildSlots() {
  uint8_t before = slotCount;
  slotCount = 0;

  if (signalIsFresh()) slots[slotCount++] = {SLOT_SIGNAL, 0};
  if (cfg.showClock) slots[slotCount++] = {SLOT_CLOCK, 0};

  if (cfg.showCrypto && crypto.ok) {
    for (uint8_t i = 0; i < crypto.n && slotCount < (uint8_t)(2 + CRYPTO_MAX); i++) {
      if (crypto.has(i)) slots[slotCount++] = {SLOT_CRYPTO, i};
    }
  }

  // Never leave the panel with nothing to draw. With the clock switched off and
  // no data yet, the clock slot renders a plain "no signal" card instead.
  if (!slotCount) slots[slotCount++] = {SLOT_CLOCK, 0};

  if (slotCount != before) slotPos = 0;
  if (slotPos >= slotCount) slotPos = 0;
}

/* Draw whatever slot is current. `force` redraws even if it is already up,
   which is what a settings change or a new signal needs. */
void drawCurrentSlot(bool force) {
  if (!slotCount) return;
  const Slot s = slots[slotPos];

  switch (s.kind) {
    case SLOT_SIGNAL:
      if (force || screen != SCR_SIGNAL) {
        drawSignal(current);
        screen = SCR_SIGNAL;
        applyBrightness();
      }
      break;

    case SLOT_CRYPTO:
      if (force || screen != SCR_CRYPTO || cryptoShown != s.idx) {
        drawCrypto(crypto.t[s.idx], crypto);
        screen = SCR_CRYPTO;
        cryptoShown = s.idx;
        applyBrightness();
      }
      break;

    case SLOT_CLOCK:
    default:
      if (!cfg.showClock) {
        // Nothing on this card ticks, so draw it once and leave the panel alone.
        if (force || screen != SCR_CLOCK) {
          drawBanner("NO SIGNAL",
                     current.valid() ? "SIGNAL EXPIRED" : "WAITING FOR SIGNAL",
                     cfg.tzName, cfg.cAccent);
          screen = SCR_CLOCK;
          applyBrightness();
        }
        return;
      }
      {
        ClockView c;
        fillClock(c);
        if (force || screen != SCR_CLOCK) {
          drawClock(c, current.valid() ? "SIGNAL EXPIRED" : "WAITING FOR SIGNAL");
          screen = SCR_CLOCK;
          applyBrightness();
        } else {
          updateClock(c);   // partial redraw, once a second, no flicker
        }
      }
      break;
  }
}

void refreshScreen(bool force = false) {
  buildSlots();
  drawCurrentSlot(force);
}

/* Move to the next slot. Called by the rotation timer. */
void advanceSlot() {
  buildSlots();
  if (slotCount < 2) {
    drawCurrentSlot(false);   // only one screen available; let the clock tick
    return;
  }
  slotPos = (uint8_t)((slotPos + 1) % slotCount);
  drawCurrentSlot(true);
}

/* Bring the signal to the front and hold it there. A new entry is the thing the
   user actually cares about - it should not wait its turn behind a price. */
void pinSignal() {
  buildSlots();
  for (uint8_t i = 0; i < slotCount; i++) {
    if (slots[i].kind == SLOT_SIGNAL) {
      slotPos = i;
      break;
    }
  }
  pinUntil = cfg.pinSec ? (millis() + (uint32_t)cfg.pinSec * 1000UL) : 0;
  lastRotate = millis();
  drawCurrentSlot(true);
}

/* ================================================================== */
/* polling the bridge                                                  */
/* ================================================================== */

/**
 * One authenticated GET against the bridge.
 *
 * Shared by the signal poll, the crypto fetch and the clock fallback so that
 * the TLS setup - and the heap guard in front of it - exists in exactly one
 * place. `path` is everything after the base URL, key included.
 *
 * Returns the HTTP status, or a negative number if the request was never
 * attempted. Only ONE of these should run per pass through loop(): each holds a
 * 16 kB buffer for its lifetime, and two at once is what a reboot looks like.
 */
int bridgeGet(const String &path, String &payload) {
  if (apMode || WiFi.status() != WL_CONNECTED) return -1;
  // Report which half is missing. Returning silently here made an unsaved
  // bridge URL and an unsaved device key look identical on the Status tab.
  if (!cfg.bridge[0]) {
    lastError = F("no bridge URL saved");
    return -1;
  }
  if (!cfg.devKey[0]) {
    lastError = F("no device key saved");
    return -1;
  }

  // A TLS session needs roughly 22 kB (16 kB receive buffer plus BearSSL's own
  // state). Attempting it with less free heap than that is how an ESP8266 ends
  // up in a reboot loop, so skip this round instead and try again later - the
  // heap usually recovers once a browser session on the settings page closes.
  if (bridgeIsHttps()) {
    uint32_t need = mflnOk ? 9000 : 24000;
    if (ESP.getFreeHeap() < need) {
      lastError = F("low memory, skipped");
      return -2;
    }
  }

  String url(cfg.bridge);
  url += path;

  HTTPClient http;
  http.setReuse(false);
  http.setTimeout(9000);
  http.setUserAgent(F("DrFX-UltraOS/" FW_VERSION));

  int code = -1;
  payload = "";

  if (bridgeIsHttps()) {
    // Allocated per request and freed immediately: the TLS receive buffer is
    // the single biggest RAM consumer on this chip.
    std::unique_ptr<BearSSL::WiFiClientSecure> sc(new BearSSL::WiFiClientSecure());
    sc->setInsecure();   // no root store fits comfortably; the shared device
                         // key in the URL is what authenticates the exchange
    sc->setBufferSizes(mflnOk ? 1024 : 16384, 512);
    if (http.begin(*sc, url)) {
      code = http.GET();
      if (code == HTTP_CODE_OK) payload = http.getString();
      http.end();
    }
  } else {
    WiFiClient c;
    if (http.begin(c, url)) {
      code = http.GET();
      if (code == HTTP_CODE_OK) payload = http.getString();
      http.end();
    }
  }
  return code;
}

void pollBridge() {
  String path("/latest?key=");
  path += urlEncode(cfg.devKey);
  path += "&device=";
  path += urlEncode(cfg.devId);
  path += "&since=";
  path += u64str(current.ts);

  String payload;
  int code = bridgeGet(path, payload);
  if (code < 0) return;

  lastHttpCode = code;

  if (code == HTTP_CODE_OK) {
    pollFails = 0;
    lastError = "";
    Signal s;
    if (signalFromJson(payload, s)) {
      current = s;
      pinSignal();   // interrupt the carousel: a fresh entry is the point
    }
  } else if (code == HTTP_CODE_NO_CONTENT) {
    pollFails = 0;   // 204 = nothing new, which is the normal quiet answer
    lastError = "";
  } else {
    pollFails++;
    lastError = (code > 0) ? String("HTTP ") + code : String("connect failed");
  }
}

/**
 * Market prices, by way of the bridge.
 *
 * The Worker talks to Binance and hands back a few hundred bytes with the
 * sparkline already scaled - see bridge/worker.js. Doing it here directly would
 * mean a second TLS host and a second 16 kB buffer on a chip that has about
 * 39 kB to play with.
 */
void pollCrypto() {
  if (!cfg.showCrypto || !cfg.symbols[0]) return;

  String path("/crypto?key=");
  path += urlEncode(cfg.devKey);
  path += "&symbols=";
  path += urlEncode(cfg.symbols);

  String payload;
  int code = bridgeGet(path, payload);
  if (code < 0) return;

  if (code == HTTP_CODE_OK && cryptoFromJson(payload, crypto)) {
    cryptoFails = 0;
    // A first success adds slots to the rotation, so rebuild - but do not force
    // a redraw, or prices would yank the screen away from whatever you are
    // reading every thirty seconds.
    buildSlots();
    if (screen == SCR_CRYPTO) drawCurrentSlot(true);   // refresh in place
  } else {
    cryptoFails++;
    if (crypto.error.length() == 0) {
      crypto.error = (code > 0) ? String("HTTP ") + code : String("connect failed");
    }
  }
}

/**
 * Clock of last resort.
 *
 * NTP is UDP port 123, which plenty of routers and captive networks quietly
 * drop - and when they do, the device has no way to know the time at all. The
 * bridge is already trusted, already reachable over TLS, and /stats reports the
 * Worker's own clock, so it makes a serviceable fallback. Accuracy is whatever
 * the round trip costs, which is a second or two: irrelevant for a wall clock
 * and for deciding whether a signal has gone stale.
 *
 * NTP still wins if it ever answers - this only fills the gap.
 */
void tryBridgeTime() {
  String path("/stats?key=");
  path += urlEncode(cfg.devKey);
  path += "&device=";
  path += urlEncode(cfg.devId);

  String payload;
  if (bridgeGet(path, payload) != HTTP_CODE_OK) return;

  JsonDocument doc;
  if (deserializeJson(doc, payload)) return;

  uint64_t ms = doc["now"] | (uint64_t)0;
  if (ms < 1700000000000ULL) return;    // clearly not a real epoch

  struct timeval tv;
  tv.tv_sec = (time_t)(ms / 1000ULL);
  tv.tv_usec = (suseconds_t)((ms % 1000ULL) * 1000ULL);
  settimeofday(&tv, nullptr);

  timeReady = true;
  timeSource = "bridge";
  refreshScreen(true);
}

/* ================================================================== */
/* web server                                                          */
/* ================================================================== */

bool requireAuth() {
  if (apMode) return true;            // never lock the user out of first setup
  if (!cfg.adminPass[0]) return true;
  if (server.authenticate(cfg.adminUser, cfg.adminPass)) return true;
  server.requestAuthentication();
  return false;
}

void sendJson(const JsonDocument &doc, int code = 200) {
  String out;
  serializeJson(doc, out);
  server.send(code, F("application/json"), out);
}

void handleRoot() {
  // Authenticate on the page itself, not just on /api/*. A fetch() that gets a
  // 401 does not raise the browser's password dialog, so if only the API were
  // protected the settings page would load and then quietly fail to populate.
  // Prompting on navigation makes the browser cache the credentials and attach
  // them to every fetch that follows.
  if (!requireAuth()) return;
  server.send_P(200, PSTR("text/html"), INDEX_HTML);
}

void handleGetConfig() {
  if (!requireAuth()) return;
  JsonDocument doc;
  doc["ssid"] = cfg.ssid;
  doc["ssid2"] = cfg.ssid2;
  doc["host"] = cfg.host;
  doc["bridge"] = cfg.bridge;
  doc["devId"] = cfg.devId;
  doc["pollSec"] = cfg.pollSec;
  doc["staleMin"] = cfg.staleMin;
  doc["rotation"] = cfg.rotation;
  doc["brightDay"] = cfg.brightDay;
  doc["brightNight"] = cfg.brightNight;
  doc["nightStart"] = cfg.nightStart;
  doc["nightEnd"] = cfg.nightEnd;
  doc["showClock"] = cfg.showClock;
  doc["rotateSec"] = cfg.rotateSec;
  doc["pinSec"] = cfg.pinSec;
  doc["showCrypto"] = cfg.showCrypto;
  doc["cryptoSec"] = cfg.cryptoSec;
  doc["symbols"] = cfg.symbols;
  doc["tz"] = cfg.tz;
  doc["tzName"] = cfg.tzName;
  doc["cAccent"] = cfg.cAccent;
  doc["cBuy"] = cfg.cBuy;
  doc["cSell"] = cfg.cSell;
  doc["cText"] = cfg.cText;
  doc["cBg"] = cfg.cBg;
  doc["adminUser"] = cfg.adminUser;
  // Secrets are never sent back to the browser; a blank field means "unchanged".
  doc["hasPass"] = cfg.pass[0] != 0;
  doc["hasPass2"] = cfg.pass2[0] != 0;
  doc["hasDevKey"] = cfg.devKey[0] != 0;
  sendJson(doc);
}

void handleSetConfig() {
  if (!requireAuth()) return;

  JsonDocument doc;
  if (deserializeJson(doc, server.arg("plain"))) {
    server.send(400, F("text/plain"), F("bad json"));
    return;
  }

  // Blank secret fields mean "keep whatever is already stored", so the browser
  // never has to be told the current password or key.
  auto putStr = [&](const char *k, char *dst, size_t cap, bool secret) -> bool {
    if (!doc[k].is<const char *>()) return false;
    const char *v = doc[k].as<const char *>();
    if (!v) return false;
    if (secret && !*v) return false;
    bool changed = (strcmp(dst, v) != 0);
    strlcpy(dst, v, cap);
    return changed;
  };

  bool netChanged = false;
  netChanged |= putStr("ssid", cfg.ssid, sizeof(cfg.ssid), false);
  netChanged |= putStr("pass", cfg.pass, sizeof(cfg.pass), true);
  netChanged |= putStr("ssid2", cfg.ssid2, sizeof(cfg.ssid2), false);
  netChanged |= putStr("pass2", cfg.pass2, sizeof(cfg.pass2), true);
  netChanged |= putStr("host", cfg.host, sizeof(cfg.host), false);

  putStr("bridge", cfg.bridge, sizeof(cfg.bridge), false);
  putStr("devKey", cfg.devKey, sizeof(cfg.devKey), true);
  putStr("devId", cfg.devId, sizeof(cfg.devId), false);
  putStr("adminUser", cfg.adminUser, sizeof(cfg.adminUser), false);
  putStr("adminPass", cfg.adminPass, sizeof(cfg.adminPass), true);

  if (doc["pollSec"].is<unsigned int>()) {
    uint16_t p = doc["pollSec"].as<uint16_t>();
    cfg.pollSec = (p < 2) ? 2 : p;
  }
  if (doc["staleMin"].is<unsigned int>()) cfg.staleMin = doc["staleMin"].as<uint16_t>();
  if (doc["rotation"].is<unsigned int>()) cfg.rotation = doc["rotation"].as<uint8_t>() & 3;
  if (doc["brightDay"].is<unsigned int>()) cfg.brightDay = doc["brightDay"].as<uint8_t>();
  if (doc["brightNight"].is<unsigned int>()) cfg.brightNight = doc["brightNight"].as<uint8_t>();
  if (doc["nightStart"].is<unsigned int>()) cfg.nightStart = doc["nightStart"].as<uint8_t>() % 24;
  if (doc["nightEnd"].is<unsigned int>()) cfg.nightEnd = doc["nightEnd"].as<uint8_t>() % 24;
  if (doc["showClock"].is<bool>()) cfg.showClock = doc["showClock"].as<bool>();
  if (doc["rotateSec"].is<unsigned int>()) {
    uint16_t r = doc["rotateSec"].as<uint16_t>();
    cfg.rotateSec = (r && r < 4) ? 4 : r;   // 0 stops the carousel
  }
  if (doc["pinSec"].is<unsigned int>()) cfg.pinSec = doc["pinSec"].as<uint16_t>();
  if (doc["cryptoSec"].is<unsigned int>()) {
    uint16_t c = doc["cryptoSec"].as<uint16_t>();
    cfg.cryptoSec = (c < 10) ? 10 : c;      // the Worker caches for 20 anyway
  }

  // A change to the watchlist should show up now, not at the next tick, so the
  // fetch timer is reset and loop() picks it up on its next pass.
  bool cryptoChanged = putStr("symbols", cfg.symbols, sizeof(cfg.symbols), false);
  if (doc["showCrypto"].is<bool>()) {
    bool want = doc["showCrypto"].as<bool>();
    cryptoChanged |= (want != cfg.showCrypto);
    cfg.showCrypto = want;
  }
  if (cryptoChanged) {
    lastCrypto = 0;
    crypto.ok = false;
    crypto.error = "";
  }

  // A zone change takes effect immediately - no reboot. setenv/tzset is what
  // configTime() does internally, so re-running it is enough; the NTP servers
  // and the time already fetched are untouched.
  bool tzChanged = putStr("tz", cfg.tz, sizeof(cfg.tz), false);
  putStr("tzName", cfg.tzName, sizeof(cfg.tzName), false);
  if (tzChanged) applyTimezone();
  if (doc["cAccent"].is<unsigned int>()) cfg.cAccent = doc["cAccent"].as<uint32_t>();
  if (doc["cBuy"].is<unsigned int>()) cfg.cBuy = doc["cBuy"].as<uint32_t>();
  if (doc["cSell"].is<unsigned int>()) cfg.cSell = doc["cSell"].as<uint32_t>();
  if (doc["cText"].is<unsigned int>()) cfg.cText = doc["cText"].as<uint32_t>();
  if (doc["cBg"].is<unsigned int>()) cfg.cBg = doc["cBg"].as<uint32_t>();

  bool ok = cfgSave();

  tft.setRotation(cfg.rotation);
  applyBrightness();
  refreshScreen(true);

  JsonDocument res;
  res["ok"] = ok;
  res["reboot"] = netChanged || apMode;
  sendJson(res, ok ? 200 : 500);

  if (netChanged || apMode) {
    delay(400);
    ESP.restart();
  }
}

void handleStatus() {
  JsonDocument doc;
  doc["fw"] = FW_VERSION;
  doc["ap"] = apMode;
  doc["ssid"] = WiFi.SSID();
  doc["ip"] = apMode ? WiFi.softAPIP().toString() : WiFi.localIP().toString();
  doc["rssi"] = WiFi.RSSI();
  doc["heap"] = ESP.getFreeHeap();
  doc["uptime"] = millis() / 1000;
  doc["mfln"] = mflnOk;
  // Whether settings actually reached flash. If this is false after a Save,
  // LittleFS is not persisting and everything will revert on the next reboot.
  doc["cfgOnFlash"] = LittleFS.exists(CFG_PATH);
  doc["timeOk"] = timeReady;

  // The clock, as the device itself sees it. The settings page shows this next
  // to the browser's own time so a wrong zone is obvious at a glance.
  {
    ClockView c;
    fillClock(c);
    JsonObject t = doc["clock"].to<JsonObject>();
    t["tz"] = cfg.tz;
    t["tzName"] = cfg.tzName;
    t["abbr"] = c.abbr;
    t["offset"] = c.offset;
    t["time"] = c.valid ? (String(c.hhmm) + ":" + c.ss) : String("");
    t["date"] = c.date;
    t["weekday"] = c.weekday;
    t["night"] = isNight();
    // "ntp", "bridge" or "none" - the difference matters when a network blocks
    // UDP 123 and the clock is quietly coming from the Worker instead.
    t["src"] = timeSource;
  }

  // What the carousel is doing, so the settings page can show it live.
  {
    JsonObject r = doc["rotation"].to<JsonObject>();
    r["every"] = cfg.rotateSec;
    r["slots"] = slotCount;
    r["pos"] = slotPos;
    r["pinned"] = pinActive();
    const char *kind = "clock";
    if (slotCount && slots[slotPos].kind == SLOT_SIGNAL) kind = "signal";
    else if (slotCount && slots[slotPos].kind == SLOT_CRYPTO) kind = "crypto";
    r["showing"] = kind;
  }

  {
    JsonObject cr = doc["crypto"].to<JsonObject>();
    cr["on"] = cfg.showCrypto;
    cr["symbols"] = cfg.symbols;
    cr["ok"] = crypto.ok;
    cr["ageSec"] = crypto.ageSec();
    cr["fails"] = cryptoFails;
    cr["error"] = crypto.error;
    JsonArray arr = cr["tickers"].to<JsonArray>();
    for (uint8_t i = 0; i < crypto.n; i++) {
      JsonObject o = arr.add<JsonObject>();
      o["sym"] = crypto.t[i].sym;
      o["name"] = crypto.t[i].name;
      o["price"] = crypto.t[i].price;
      o["change"] = crypto.t[i].change;
      o["spark"] = crypto.t[i].sparkN;
    }
  }

  doc["httpCode"] = lastHttpCode;
  doc["fails"] = pollFails;
  doc["error"] = lastError;

  JsonObject s = doc["signal"].to<JsonObject>();
  s["valid"] = current.valid();
  s["fresh"] = signalIsFresh();
  s["symbol"] = current.symbol;
  s["side"] = current.side;
  s["score"] = current.score;
  s["conf"] = current.conf;
  s["tp1"] = current.tp1;
  s["tp2"] = current.tp2;
  s["sl"] = current.sl;
  s["ageSec"] = current.valid() ? (millis() - current.rxMillis) / 1000 : 0;

  sendJson(doc);
}

/* Direct LAN push - lets a local script or Home Assistant drive the screen
   without going through Cloudflare at all. Authenticated with the device key. */
void handlePush() {
  String key = server.hasArg("key") ? server.arg("key") : server.header("X-Device-Key");
  if (!cfg.devKey[0] || key != cfg.devKey) {
    if (!requireAuth()) return;
  }
  Signal s;
  if (!signalFromJson(server.arg("plain"), s)) {
    server.send(400, F("text/plain"), F("bad signal"));
    return;
  }
  if (s.ts == 0) s.ts = current.ts + 1;
  current = s;
  pinSignal();
  server.send(200, F("application/json"), F("{\"ok\":true}"));
}

/* Step the carousel by hand - handy from the settings page or the CLI when you
   are checking a layout and do not want to wait for the timer. */
void handleNext() {
  if (!requireAuth()) return;
  pinUntil = 0;          // a manual step cancels any signal pin
  lastRotate = millis();
  advanceSlot();
  JsonDocument doc;
  doc["ok"] = true;
  doc["pos"] = slotPos;
  doc["slots"] = slotCount;
  sendJson(doc);
}

/* Draw a fake card so the user can see the layout before any alert fires. */
void handleTest() {
  if (!requireAuth()) return;
  Signal s;
  s.ts = current.ts + 1;
  s.symbol = "XAUUSD";
  s.side = "BUY";
  s.score = 96;
  s.conf = 94;
  s.entry = "3371.4";   // gives the footer a real risk:reward to compute
  s.tp1 = "3378";
  s.tp2 = "3386";
  s.sl = "3362";
  s.tf = "15M";
  s.rxMillis = millis();
  current = s;
  pinSignal();
  server.send(200, F("application/json"), F("{\"ok\":true}"));
}

void handleScan() {
  if (!requireAuth()) return;
  int n = WiFi.scanNetworks(false, true);
  JsonDocument doc;
  JsonArray arr = doc.to<JsonArray>();
  for (int i = 0; i < n && i < 20; i++) {
    JsonObject o = arr.add<JsonObject>();
    o["ssid"] = WiFi.SSID(i);
    o["rssi"] = WiFi.RSSI(i);
    o["lock"] = WiFi.encryptionType(i) != ENC_TYPE_NONE;
  }
  WiFi.scanDelete();
  sendJson(doc);
}

void handleReboot() {
  if (!requireAuth()) return;
  server.send(200, F("text/plain"), F("rebooting"));
  delay(300);
  ESP.restart();
}

void handleFactory() {
  if (!requireAuth()) return;
  LittleFS.remove(CFG_PATH);
  server.send(200, F("text/plain"), F("wiped, rebooting"));
  delay(300);
  ESP.restart();
}

void setupServer() {
  // ESP8266 core 3.1 replaced the old collectHeaders(array, count) overload with
  // a variadic template, so passing an array plus a count now tries to convert
  // the count to a String and fails to compile. Header names are listed
  // directly instead. Authorization and ETag are always collected by the
  // library itself, so only our own header needs naming here.
  server.collectHeaders("X-Device-Key");

  server.on("/", HTTP_GET, handleRoot);
  server.on("/api/config", HTTP_GET, handleGetConfig);
  server.on("/api/config", HTTP_POST, handleSetConfig);
  server.on("/api/status", HTTP_GET, handleStatus);
  server.on("/api/push", HTTP_POST, handlePush);
  server.on("/api/test", HTTP_POST, handleTest);
  server.on("/api/next", HTTP_POST, handleNext);
  server.on("/api/scan", HTTP_GET, handleScan);
  server.on("/api/reboot", HTTP_POST, handleReboot);
  server.on("/api/factory", HTTP_POST, handleFactory);
  server.on("/health", HTTP_GET, []() { server.send(200, F("text/plain"), F("ok")); });

  // Anything unknown goes to the settings page. In AP mode that plus the DNS
  // server below gives a captive-portal style "just open your browser" setup.
  server.onNotFound(handleRoot);

  updater.setup(&server, "/update", cfg.adminUser, cfg.adminPass);
  server.begin();
}

/* ================================================================== */
/* WiFi                                                               */
/* ================================================================== */

void startAP() {
  apMode = true;
  WiFi.mode(WIFI_AP);
  WiFi.softAP("DrFX-GodMode", "godmode123");
  dnsServer.start(53, "*", WiFi.softAPIP());
  drawBanner("SETUP MODE", "Join WiFi: DrFX-GodMode", "Pass: godmode123", cfg.cAccent);
  delay(1200);
  drawBanner("SETUP MODE", "Then open in a browser:", "http://192.168.4.1", cfg.cAccent);
}

bool connectWiFi() {
  if (!cfg.ssid[0] && !cfg.ssid2[0]) return false;

  WiFi.mode(WIFI_STA);
  WiFi.persistent(false);
  WiFi.setAutoReconnect(true);
  WiFi.hostname(cfg.host);

  if (cfg.ssid[0]) wifiMulti.addAP(cfg.ssid, cfg.pass);
  if (cfg.ssid2[0]) wifiMulti.addAP(cfg.ssid2, cfg.pass2);

  drawBanner("CONNECTING", cfg.ssid, "", cfg.cAccent);

  uint32_t start = millis();
  while (millis() - start < 25000) {
    if (wifiMulti.run() == WL_CONNECTED) return true;
    delay(300);
  }
  return false;
}

/* ================================================================== */

void setup() {
  Serial.begin(115200);
  Serial.println(F("\nDrFX GodMode " FW_VERSION));

  if (!LittleFS.begin()) {
    LittleFS.format();
    LittleFS.begin();
  }
  cfgLoad();
  applyTimezone();   // before any localtime_r call, including in AP mode

  displayBegin();
  drawBanner("GOD MODE", "starting...", FW_VERSION, cfg.cAccent);

  if (!connectWiFi()) {
    startAP();
    setupServer();
    return;
  }

#ifndef GODMODE_SLIM
  MDNS.begin(cfg.host);
  MDNS.addService("http", "tcp", 80);
#endif

  // Time drives the clock screen and the night dimming. TLS does not need it
  // because certificate validation is skipped (see pollBridge).
  //
  // This overload takes a POSIX TZ rule rather than an offset, so newlib does
  // the daylight-saving arithmetic and localtime_r() returns real local time.
  configTime(cfg.tz, "pool.ntp.org", "time.cloudflare.com");

  // Ask the bridge whether it supports small TLS fragments. If it does we can
  // use a 1 kB receive buffer instead of 16 kB, which is a lot on a chip with
  // roughly 40 kB of usable heap.
  if (cfg.bridge[0] && bridgeIsHttps()) {
    String h = bridgeHost();
    if (h.length()) mflnOk = BearSSL::WiFiClientSecure::probeMaxFragmentLength(h.c_str(), 443, 1024);
  }

  setupServer();

  String ip = WiFi.localIP().toString();
#ifdef GODMODE_SLIM
  String hostLine = String("settings: http://") + ip;
#else
  String hostLine = String("http://") + cfg.host + ".local";
#endif
  drawBanner("READY", ip.c_str(), hostLine.c_str(), cfg.cBuy);
  delay(2500);

  refreshScreen(true);

  // Both timers start already expired so the first pass through loop() fetches
  // a signal and the pass after that fetches prices - one TLS session each,
  // never overlapping.
  lastPoll = millis() - (uint32_t)cfg.pollSec * 1000UL;
  lastCrypto = millis() - (uint32_t)cfg.cryptoSec * 1000UL;
  lastRotate = millis();
}

void loop() {
  server.handleClient();
  if (apMode) {
    dnsServer.processNextRequest();
    return;
  }
#ifndef GODMODE_SLIM
  MDNS.update();
#endif

  uint32_t now = millis();

  if (!timeReady && time(nullptr) > 1700000000) {
    timeReady = true;
    timeSource = "ntp";
    refreshScreen(true);
  }

  /* --- network: at most one TLS session per pass -------------------- */
  // Each request holds a 16 kB receive buffer for its lifetime. Two overlapping
  // is what a reboot looks like, so the three jobs below take turns rather than
  // firing whenever their timer happens to be due.
  bool didNetwork = false;

  // Back off politely if the bridge is unhappy, instead of hammering it.
  uint32_t interval = (uint32_t)cfg.pollSec * 1000UL;
  if (pollFails > 3) {
    interval *= (pollFails > 10) ? 12UL : 4UL;
    if (interval > 120000UL) interval = 120000UL;
  }

  if (now - lastPoll >= interval) {
    lastPoll = now;
    pollBridge();
    didNetwork = true;
  }

  if (!didNetwork && cfg.showCrypto) {
    uint32_t cInterval = (uint32_t)cfg.cryptoSec * 1000UL;
    if (cryptoFails > 3) cInterval *= 4UL;          // same courtesy as above
    if (cInterval > 300000UL) cInterval = 300000UL;
    if (now - lastCrypto >= cInterval) {
      lastCrypto = now;
      pollCrypto();
      didNetwork = true;
    }
  }

  // Only while the clock is unset, and never more than once a minute. Give NTP
  // a minute and a half of quiet first - it usually answers within seconds, and
  // asking the bridge before then would mask a network that is actually fine.
  if (!didNetwork && !timeReady && now > 90000UL && now - lastTimeTry >= 60000UL) {
    lastTimeTry = now;
    tryBridgeTime();
  }

  /* --- the carousel -------------------------------------------------- */
  if (cfg.rotateSec && !pinActive() &&
      now - lastRotate >= (uint32_t)cfg.rotateSec * 1000UL) {
    lastRotate = now;
    advanceSlot();
  }

  // The clock screen shows seconds, so it ticks every second. updateClock()
  // redraws two text fields and a 2px bar - a few hundred microseconds of SPI.
  // This is also where a signal ageing out of freshness gets noticed: the slot
  // list shrinks, and the screen follows.
  if (now - lastClock >= 1000) {
    lastClock = now;
    uint8_t before = slotCount;
    buildSlots();
    if (slotCount != before) drawCurrentSlot(true);
    else if (slotCount && slots[slotPos].kind == SLOT_CLOCK) drawCurrentSlot(false);
  }

  if (now - lastNightCheck >= 60000) {
    lastNightCheck = now;
    applyBrightness();
  }

  delay(2);
}
