/**
 * DrFX GodMode - persistent settings
 *
 * Everything the user can change lives in one struct that is saved to
 * /config.json on the LittleFS partition. Nothing is hard-coded, so the
 * firmware never has to be rebuilt to change a key, colour or URL.
 */
#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include <LittleFS.h>

#define CFG_PATH "/config.json"
#define FW_VERSION "2.2.0"

struct Config {
  // --- network ---
  char ssid[33] = "";
  char pass[65] = "";
  char ssid2[33] = "";   // optional second network
  char pass2[65] = "";
  char host[25] = "godmode";   // http://godmode.local

  // --- bridge ---
  // Base URL of the Cloudflare Worker, no trailing slash.
  // https:// is the norm. http:// is allowed if you put the Worker on your own
  // domain with "Always Use HTTPS" turned off - that saves ~16 kB of RAM.
  char bridge[96] = "";
  char devKey[49] = "";
  char devId[25] = "main";
  uint16_t pollSec = 5;
  uint16_t staleMin = 240;   // after this long with no new signal, show IDLE

  // --- display ---
  uint8_t rotation = 0;
  uint8_t brightDay = 200;
  uint8_t brightNight = 40;
  uint8_t nightStart = 23;   // hour, 0-23
  uint8_t nightEnd = 7;
  bool showClock = true;

  // --- screen rotation ---
  // Seconds each screen holds before the next one takes over. 0 stops the
  // carousel: whatever is on screen stays there.
  uint16_t rotateSec = 15;
  // A fresh signal interrupts the rotation and holds the screen for this long
  // before rejoining it. 0 means it simply takes its turn like anything else.
  uint16_t pinSec = 60;

  // --- crypto ---
  bool showCrypto = true;
  uint16_t cryptoSec = 30;               // how often to ask the bridge
  char symbols[64] = "BTCUSDT,ETHUSDT";  // comma separated, up to 4 used

  // --- time ---
  // A POSIX TZ rule, not a plain offset. "GMT0BST,M3.5.0/1,M10.5.0" carries the
  // daylight-saving changeover dates with it, so the clock corrects itself in
  // spring and autumn without anyone touching the settings. The settings page
  // and the CLI both pick these from shared/timezones.json.
  char tz[48] = "UTC0";
  char tzName[34] = "UTC";       // the human label, e.g. "Europe/London"

  // Superseded by tz. Kept so that a config written by firmware 1.x still
  // produces the right clock on first boot after the update - see cfgLoad.
  int16_t tzMinutes = 0;

  // --- theme (24-bit RGB) ---
  uint32_t cAccent = 0x8B5CF6;   // purple
  uint32_t cBuy = 0x22DD77;      // green
  uint32_t cSell = 0xFF4D5E;     // red
  uint32_t cText = 0xE8E8F5;
  uint32_t cBg = 0x000000;

  // --- admin ---
  char adminUser[17] = "admin";
  char adminPass[33] = "godmode";
};

extern Config cfg;

inline void cfgSetStr(char *dst, size_t cap, const char *src) {
  if (!src) return;
  strlcpy(dst, src, cap);
}

/**
 * Build a POSIX TZ rule from a plain minute offset.
 *
 * POSIX offsets are west-positive, the opposite sign to the "UTC+05:30" people
 * write, which is the single most common way to get this wrong. There are no
 * DST rules in the result because a bare offset does not carry any - it simply
 * stays put all year, which is exactly what firmware 1.x did.
 */
inline void tzFromMinutes(int16_t m, char *out, size_t cap) {
  if (m == 0) {
    strlcpy(out, "UTC0", cap);
    return;
  }
  int a = (m < 0) ? -m : m;
  snprintf(out, cap, "<%c%02d%02d>%c%d:%02d",
           (m < 0) ? '-' : '+', a / 60, a % 60,
           (m < 0) ? '+' : '-', a / 60, a % 60);
}

inline bool cfgLoad() {
  if (!LittleFS.exists(CFG_PATH)) return false;
  File f = LittleFS.open(CFG_PATH, "r");
  if (!f) return false;

  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, f);
  f.close();
  if (err) return false;

  cfgSetStr(cfg.ssid, sizeof(cfg.ssid), doc["ssid"] | "");
  cfgSetStr(cfg.pass, sizeof(cfg.pass), doc["pass"] | "");
  cfgSetStr(cfg.ssid2, sizeof(cfg.ssid2), doc["ssid2"] | "");
  cfgSetStr(cfg.pass2, sizeof(cfg.pass2), doc["pass2"] | "");
  cfgSetStr(cfg.host, sizeof(cfg.host), doc["host"] | "godmode");
  cfgSetStr(cfg.bridge, sizeof(cfg.bridge), doc["bridge"] | "");
  cfgSetStr(cfg.devKey, sizeof(cfg.devKey), doc["devKey"] | "");
  cfgSetStr(cfg.devId, sizeof(cfg.devId), doc["devId"] | "main");
  cfgSetStr(cfg.adminUser, sizeof(cfg.adminUser), doc["adminUser"] | "admin");
  cfgSetStr(cfg.adminPass, sizeof(cfg.adminPass), doc["adminPass"] | "godmode");

  cfg.pollSec = doc["pollSec"] | cfg.pollSec;
  cfg.staleMin = doc["staleMin"] | cfg.staleMin;
  cfg.rotation = doc["rotation"] | cfg.rotation;
  cfg.brightDay = doc["brightDay"] | cfg.brightDay;
  cfg.brightNight = doc["brightNight"] | cfg.brightNight;
  cfg.nightStart = doc["nightStart"] | cfg.nightStart;
  cfg.nightEnd = doc["nightEnd"] | cfg.nightEnd;
  cfg.showClock = doc["showClock"] | cfg.showClock;
  cfg.rotateSec = doc["rotateSec"] | cfg.rotateSec;
  cfg.pinSec = doc["pinSec"] | cfg.pinSec;
  cfg.showCrypto = doc["showCrypto"] | cfg.showCrypto;
  cfg.cryptoSec = doc["cryptoSec"] | cfg.cryptoSec;
  cfgSetStr(cfg.symbols, sizeof(cfg.symbols), doc["symbols"] | cfg.symbols);
  cfg.tzMinutes = doc["tzMinutes"] | cfg.tzMinutes;

  // Migration from firmware 1.x: those builds only stored tzMinutes. If this
  // config predates the TZ rules, turn the offset into an equivalent rule so
  // the clock is right immediately; the user can pick a named zone later and
  // gain automatic daylight saving.
  if (doc["tz"].is<const char *>() && doc["tz"].as<const char *>()[0]) {
    cfgSetStr(cfg.tz, sizeof(cfg.tz), doc["tz"] | "");
    cfgSetStr(cfg.tzName, sizeof(cfg.tzName), doc["tzName"] | "");
  } else {
    tzFromMinutes(cfg.tzMinutes, cfg.tz, sizeof(cfg.tz));
    snprintf(cfg.tzName, sizeof(cfg.tzName), "UTC%+03d:%02d",
             cfg.tzMinutes / 60, (cfg.tzMinutes < 0 ? -cfg.tzMinutes : cfg.tzMinutes) % 60);
  }
  if (!cfg.tz[0]) strlcpy(cfg.tz, "UTC0", sizeof(cfg.tz));
  if (!cfg.tzName[0]) strlcpy(cfg.tzName, "UTC", sizeof(cfg.tzName));
  cfg.cAccent = doc["cAccent"] | cfg.cAccent;
  cfg.cBuy = doc["cBuy"] | cfg.cBuy;
  cfg.cSell = doc["cSell"] | cfg.cSell;
  cfg.cText = doc["cText"] | cfg.cText;
  cfg.cBg = doc["cBg"] | cfg.cBg;

  if (cfg.pollSec < 2) cfg.pollSec = 2;
  if (cfg.rotation > 3) cfg.rotation = 0;
  // Below about 4 seconds nothing on screen can be read before it is replaced,
  // and a Binance fetch faster than 10s is wasted - the Worker caches for 20.
  if (cfg.rotateSec && cfg.rotateSec < 4) cfg.rotateSec = 4;
  if (cfg.cryptoSec < 10) cfg.cryptoSec = 10;
  return true;
}

inline bool cfgSave() {
  JsonDocument doc;
  doc["ssid"] = cfg.ssid;
  doc["pass"] = cfg.pass;
  doc["ssid2"] = cfg.ssid2;
  doc["pass2"] = cfg.pass2;
  doc["host"] = cfg.host;
  doc["bridge"] = cfg.bridge;
  doc["devKey"] = cfg.devKey;
  doc["devId"] = cfg.devId;
  doc["adminUser"] = cfg.adminUser;
  doc["adminPass"] = cfg.adminPass;
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
  doc["tzMinutes"] = cfg.tzMinutes;   // written for 1.x downgrade safety
  doc["cAccent"] = cfg.cAccent;
  doc["cBuy"] = cfg.cBuy;
  doc["cSell"] = cfg.cSell;
  doc["cText"] = cfg.cText;
  doc["cBg"] = cfg.cBg;

  File f = LittleFS.open(CFG_PATH, "w");
  if (!f) return false;
  bool ok = serializeJson(doc, f) > 0;
  f.close();
  return ok;
}
