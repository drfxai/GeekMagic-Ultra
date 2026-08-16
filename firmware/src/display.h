/**
 * DrFX Ultra OS - screen rendering
 *
 * Three screens, all built from the primitives in ui.h:
 *
 *   drawBanner   boot / setup / error states
 *   drawSignal   the trade card
 *   drawClock    the world clock, shown whenever no fresh signal is on hand
 *
 * The previous build drew a 300-degree smooth arc gauge for the AI score. It
 * has been removed on purpose: drawSmoothArc pulls in a chunk of float and
 * anti-aliasing code that the slim image can ill afford, and at 240x240 a flat
 * bar plus a 48px numeral is both more legible and cheaper. Nothing else read
 * the gauge, so the change is local to this file.
 */
#pragma once

#include <Arduino.h>
#include <TFT_eSPI.h>
#include "config.h"
#include "ui.h"
#include "signal_model.h"
#include "crypto_model.h"

#define TFT_BL_PIN 5      // backlight, PWM, active LOW on this board

/* Everything the clock screen needs, filled in by main.cpp so that this file
   stays free of time.h and the timezone handling. */
struct ClockView {
  bool valid = false;      // false until NTP has answered
  char hhmm[8] = "";       // "15:46"
  char ss[4] = "";         // "09"
  char date[16] = "";      // "14 AUG 2026"
  char weekday[12] = "";   // "FRIDAY"
  char zone[34] = "";      // "Europe/London"
  char abbr[8] = "";       // "BST"
  char offset[12] = "";    // "UTC+01:00"
  int secPct = 0;          // 0-100, drives the seconds rule
};

inline void setBacklight(uint8_t level) {
  analogWriteRange(255);
  analogWriteFreq(1000);
  analogWrite(TFT_BL_PIN, 255 - level);   // inverted on this hardware
}

inline void displayBegin() {
  pinMode(TFT_BL_PIN, OUTPUT);
  setBacklight(0);                 // stay dark until the first frame is drawn
  tft.init();
  tft.setRotation(cfg.rotation);
  uiClear();
}

/* ------------------------------------------------------------------ */
/* Boot / status banner                                                */
/* ------------------------------------------------------------------ */

inline void drawBanner(const char *line1, const char *line2, const char *line3,
                       uint32_t colour) {
  uiClear();
  uiHeader("DRFX ULTRA OS", String(FW_VERSION), cfg.cAccent);

  uiText(UI_PAD, 64, String(line1 ? line1 : ""), UI_F_HEAD, colour);

  if (line2 && *line2) uiText(UI_PAD, 108, String(line2), UI_F_BODY, cfg.cText);
  if (line3 && *line3) uiText(UI_PAD, 132, String(line3), UI_F_BODY, uiDim());

  uiFooter("DRFX AI", "", cfg.cAccent);
  setBacklight(cfg.brightDay);
}

/* ------------------------------------------------------------------ */
/* The trade card                                                      */
/* ------------------------------------------------------------------ */

/* Risk:reward from the levels, when all three parse as numbers.
   Returns an empty String when it cannot be worked out honestly. */
inline String riskReward(const Signal &s) {
  float e = s.entry.toFloat();
  float t = s.tp1.toFloat();
  float l = s.sl.toFloat();
  if (e <= 0 || t <= 0 || l <= 0) return String();

  float reward = fabsf(t - e);
  float risk = fabsf(e - l);
  if (risk < 1e-6f || reward < 1e-6f) return String();

  float rr = reward / risk;
  if (rr > 99.0f) return String();

  char buf[16];
  snprintf(buf, sizeof(buf), "1:%.1f", (double)rr);
  return String(buf);
}

inline void drawSignal(const Signal &s) {
  const bool sell = (s.side == "SELL");
  const bool flat = (s.side == "FLAT");
  const uint32_t dir = flat ? cfg.cAccent : (sell ? cfg.cSell : cfg.cBuy);

  uiClear();

  String hdrRight = s.tf.length() ? s.tf : String("LIVE");
  uiHeader("SIGNAL", hdrRight, cfg.cAccent);

  /* --- symbol and direction ------------------------------------- */
  uiText(UI_PAD, 38, s.symbol.length() ? s.symbol : String("---"),
         UI_F_HEAD, cfg.cText);

  if (s.side.length()) {
    int sideW = tft.textWidth(s.side, UI_F_HEAD);
    uiText(UI_W - UI_PAD, 38, s.side, UI_F_HEAD, dir, TR_DATUM);

    if (!flat) {
      // Triangle sits just left of the word, vertically centred on it.
      int ax = UI_W - UI_PAD - sideW - 14;
      int top = 42, bot = 60;
      if (sell) tft.fillTriangle(ax - 6, top, ax + 6, top, ax, bot, rgb(dir));
      else      tft.fillTriangle(ax - 6, bot, ax + 6, bot, ax, top, rgb(dir));
    }
  }

  uiRule(72);

  /* --- terminal events (TP hit, stopped out) --------------------- */
  // These carry no fresh levels, so showing an empty TP/SL grid would be a
  // lie. The note becomes the headline instead.
  if (flat) {
    uiLabel(UI_PAD, 88, "POSITION CLOSED");
    uiText(UI_PAD, 104, s.note.length() ? s.note : String("closed"),
           UI_F_HEAD, cfg.cText);
    if (s.entry.length()) {
      uiLabel(UI_PAD, 150, "PRICE");
      uiText(UI_PAD, 163, s.entry, UI_F_HEAD, uiDim());
    }
    uiFooter(String("*") + hdrRight, "", cfg.cAccent);
    return;
  }

  /* --- score and confidence -------------------------------------- */
  uiLabel(UI_PAD, 82, "AI SCORE");
  {
    char buf[8];
    snprintf(buf, sizeof(buf), "%d", s.score);
    uiText(UI_PAD, 94, String(buf), UI_F_NUM, dir);
  }

  uiVRule(112, 80, 148);

  uiLabel(126, 82, "CONFIDENCE");
  uiText(UI_W - UI_PAD, 94, String(s.conf) + "%", UI_F_HEAD, cfg.cText, TR_DATUM);
  uiBar(126, 128, UI_W - UI_PAD - 126, 3, s.conf, cfg.cAccent);

  uiRule(152);

  /* --- levels ----------------------------------------------------- */
  uiCell(UI_PAD, 162, "TP1", s.tp1, cfg.cBuy);
  uiCell(96, 162, "TP2", s.tp2, cfg.cBuy);
  uiCell(UI_W - UI_PAD, 162, "SL", s.sl, cfg.cSell, TR_DATUM);

  /* --- footer ------------------------------------------------------ */
  String rr = riskReward(s);
  String left = s.entry.length() ? (String("ENTRY ") + s.entry) : String("");
  String right = rr.length() ? (String("*R:R ") + rr)
                             : (s.note.length() ? s.note : String(""));
  uiFooter(left, right, cfg.cAccent);
}

/* ------------------------------------------------------------------ */
/* World clock                                                         */
/* ------------------------------------------------------------------ */

/* Shared by the full redraw and the once-a-second update so the two can never
   drift apart. Padding is what stops the previous digits showing through. */
inline void drawClockDigits(const ClockView &c) {
  if (!c.valid) return;
  uiText(UI_PAD, 44, String(c.hhmm), UI_F_CLOCK, cfg.cText, TL_DATUM, 176);
  uiText(UI_W - UI_PAD, 66, String(c.ss), UI_F_HEAD, uiDim(), TR_DATUM, 44);
  uiBar(UI_PAD, 108, UI_W - 2 * UI_PAD, 2, c.secPct, cfg.cAccent);
}

inline void drawClock(const ClockView &c, const char *sub) {
  uiClear();
  uiHeader("CLOCK", String(c.valid ? c.abbr : "NO TIME"), cfg.cAccent);

  if (c.valid) {
    drawClockDigits(c);
  } else {
    // Font 7 is a 7-segment face with no letters, so a placeholder has to be
    // drawn in a text font rather than as "--:--".
    uiText(UI_PAD, 54, String("SYNCING"), UI_F_HEAD, uiDim());
    uiLabel(UI_PAD, 92, "WAITING FOR NTP");
  }

  uiRule(124);

  uiLabel(UI_PAD, 134, "DATE");
  uiText(UI_PAD, 147, String(c.valid ? c.date : "--"), UI_F_HEAD, cfg.cText);

  uiLabel(UI_W - UI_PAD, 134, "DAY", uiDim(), TR_DATUM);
  uiText(UI_W - UI_PAD, 149, String(c.valid ? c.weekday : "--"),
         UI_F_BODY, uiDim(), TR_DATUM);

  uiRule(180);
  uiLabel(UI_PAD, 190, sub ? sub : "", cfg.cAccent);

  uiFooter(String(c.zone), String(c.offset), cfg.cAccent);
}

/* Redraw only what changes each second - no clear, so no visible flicker. */
inline void updateClock(const ClockView &c) {
  drawClockDigits(c);
}

/* ------------------------------------------------------------------ */
/* Crypto ticker                                                       */
/* ------------------------------------------------------------------ */

/* The sparkline. Points arrive from the bridge already scaled to 0-100, so
   plotting is two integer multiplications per point and no floating point at
   all. Drawn as line segments rather than a filled area: at this size a fill
   turns into a solid block and stops carrying any information. */
inline void drawSpark(const Ticker &t, int x, int y, int w, int h, uint32_t colour) {
  if (t.sparkN < 2) return;

  const uint16_t c = rgb(colour);
  int prevX = 0, prevY = 0;

  for (uint8_t i = 0; i < t.sparkN; i++) {
    int px = x + ((int32_t)w * i) / (t.sparkN - 1);
    int py = y + h - ((int32_t)h * t.spark[i]) / 100;
    if (i) tft.drawLine(prevX, prevY, px, py, c);
    prevX = px;
    prevY = py;
  }

  // Mark where the series ends, so a glance says which way "now" is.
  tft.fillCircle(prevX, prevY, 2, c);
}

inline void drawCrypto(const Ticker &t, const CryptoSet &set) {
  uiClear();

  const bool up = t.change >= 0;
  const uint32_t dir = up ? cfg.cBuy : cfg.cSell;

  // Name whoever actually answered. Binance has been returning 403 to the
  // Worker for some time, so a hardcoded "BINANCE" here was attributing
  // Coinbase prices to an exchange that is not in the picture at all.
  uiHeader("CRYPTO", set.src.length() ? set.src : String("BRIDGE"), cfg.cAccent);

  if (!t.valid()) {
    uiText(UI_PAD, 60, String("NO DATA"), UI_F_HEAD, uiDim());
    uiLabel(UI_PAD, 98, set.error.length() ? set.error.c_str() : "waiting for the bridge");
    uiFooter("CRYPTO", "", cfg.cAccent);
    return;
  }

  /* --- asset and 24h change --------------------------------------- */
  uiText(UI_PAD, 36, t.name, UI_F_HEAD, cfg.cText);

  {
    char buf[16];
    snprintf(buf, sizeof(buf), "%+.2f%%", (double)t.change);
    String chg(buf);
    int w = tft.textWidth(chg, UI_F_HEAD);
    uiText(UI_W - UI_PAD, 36, chg, UI_F_HEAD, dir, TR_DATUM);

    int ax = UI_W - UI_PAD - w - 13;
    if (up) tft.fillTriangle(ax - 5, 56, ax + 5, 56, ax, 42, rgb(dir));
    else    tft.fillTriangle(ax - 5, 42, ax + 5, 42, ax, 56, rgb(dir));
  }

  uiRule(68);

  /* --- price -------------------------------------------------------- */
  // Font 6 has no comma glyph, so no thousands separators: the integer part
  // goes in the big face and the decimals follow smaller, baseline-aligned.
  String whole, frac;
  splitPrice(t.price, whole, frac);

  if (whole.length()) {
    uiText(UI_PAD, 78, whole, UI_F_NUM, cfg.cText);
    if (frac.length()) {
      int wx = UI_PAD + tft.textWidth(whole, UI_F_NUM) + 3;
      if (wx < UI_W - UI_PAD) uiText(wx, 104, frac, UI_F_HEAD, uiDim());
    }
  } else {
    // Sub-dollar prices would be a huge "0" and a wall of decimals.
    uiText(UI_PAD, 88, frac, UI_F_HEAD, cfg.cText);
  }

  /* --- sparkline ----------------------------------------------------- */
  const int chartY = 134, chartH = 56;
  uiRule(chartY + chartH + 6, UI_PAD, UI_W - UI_PAD);
  drawSpark(t, UI_PAD, chartY, UI_W - 2 * UI_PAD, chartH, dir);

  /* --- footer -------------------------------------------------------- */
  String range = t.low.length() && t.high.length()
                     ? (t.low + " - " + t.high)
                     : String("");
  String age = set.ok ? (String("*") + set.ageSec() + "S AGO") : String("");
  uiFooter(range, age, cfg.cAccent);
}
