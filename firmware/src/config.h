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
#define FW_VERSION "1.0.0"

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
  int16_t tzMinutes = 0;     // minutes offset from UTC, e.g. 210 for +03:30

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
  cfg.tzMinutes = doc["tzMinutes"] | cfg.tzMinutes;
  cfg.cAccent = doc["cAccent"] | cfg.cAccent;
  cfg.cBuy = doc["cBuy"] | cfg.cBuy;
  cfg.cSell = doc["cSell"] | cfg.cSell;
  cfg.cText = doc["cText"] | cfg.cText;
  cfg.cBg = doc["cBg"] | cfg.cBg;

  if (cfg.pollSec < 2) cfg.pollSec = 2;
  if (cfg.rotation > 3) cfg.rotation = 0;
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
  doc["tzMinutes"] = cfg.tzMinutes;
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
