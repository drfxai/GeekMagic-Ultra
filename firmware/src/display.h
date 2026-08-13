/**
 * DrFX GodMode - screen rendering
 *
 * No sprites are used anywhere. A full 240x240 16-bit sprite would be 115 kB,
 * which the ESP8266 does not have, and we need to keep ~17 kB free for the
 * TLS receive buffer while polling. Flicker is avoided by drawing text with an
 * explicit background colour plus text padding instead of clearing regions.
 */
#pragma once

#include <Arduino.h>
#include <TFT_eSPI.h>
#include "config.h"
#include "signal_model.h"

#define TFT_BL_PIN 5      // backlight, PWM, active LOW on this board

extern TFT_eSPI tft;

/* 24-bit RGB from the settings page -> 16-bit RGB565 for the panel */
inline uint16_t rgb(uint32_t c) {
  return tft.color565((c >> 16) & 0xFF, (c >> 8) & 0xFF, c & 0xFF);
}

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
  tft.fillScreen(rgb(cfg.cBg));
  tft.setTextDatum(MC_DATUM);
}

/* ------------------------------------------------------------------ */
/* Boot / status screens                                               */
/* ------------------------------------------------------------------ */

inline void drawBanner(const char *line1, const char *line2, const char *line3, uint32_t colour) {
  tft.fillScreen(rgb(cfg.cBg));
  tft.setTextDatum(MC_DATUM);
  tft.setTextPadding(0);

  tft.setTextColor(rgb(cfg.cAccent), rgb(cfg.cBg));
  tft.drawString("GOD MODE", 120, 34, 4);

  tft.setTextColor(rgb(colour), rgb(cfg.cBg));
  tft.drawString(line1, 120, 104, 4);

  tft.setTextColor(rgb(cfg.cText), rgb(cfg.cBg));
  if (line2 && *line2) tft.drawString(line2, 120, 146, 2);
  if (line3 && *line3) tft.drawString(line3, 120, 172, 2);

  setBacklight(cfg.brightDay);
}

/* ------------------------------------------------------------------ */
/* The GodMode signal card                                             */
/* ------------------------------------------------------------------ */

inline void drawGauge(int score, uint32_t colour) {
  const int cx = 120, cy = 104, r = 46, ir = 36;
  const uint16_t track = tft.color565(0x2A, 0x2A, 0x40);
  const uint16_t bg = rgb(cfg.cBg);

  if (score < 0) score = 0;
  if (score > 100) score = 100;

  // TFT_eSPI angles: 0 is at 6 o'clock and increases clockwise.
  // A 300-degree arc from 30 to 330 leaves a gap at the bottom.
  tft.drawSmoothArc(cx, cy, r, ir, 30, 330, track, bg, true);
  uint32_t end = 30 + (uint32_t)(300.0f * score / 100.0f);
  if (end > 30) tft.drawSmoothArc(cx, cy, r, ir, 30, end, rgb(colour), bg, true);

  char buf[8];
  snprintf(buf, sizeof(buf), "%d", score);
  tft.setTextDatum(MC_DATUM);
  tft.setTextPadding(70);
  tft.setTextColor(rgb(cfg.cText), bg);
  tft.drawString(buf, cx, cy - 6, 6);          // font 6 = 48px numerals

  tft.setTextPadding(0);
  tft.setTextColor(tft.color565(0x8B, 0x8B, 0xA7), bg);
  tft.drawString("AI SCORE", cx, cy + 26, 1);
}

inline void drawSignal(const Signal &s) {
  const uint16_t bg = rgb(cfg.cBg);
  const bool sell = (s.side == "SELL");
  const uint32_t dir = sell ? cfg.cSell : cfg.cBuy;

  tft.fillScreen(bg);
  tft.setTextDatum(MC_DATUM);
  tft.setTextPadding(0);

  // Title
  tft.setTextColor(rgb(cfg.cAccent), bg);
  tft.drawString("GOD MODE", 120, 22, 4);

  // Gauge
  drawGauge(s.score, dir);

  // Symbol + direction.
  // Note: Arduino's String has no operator+(const char*, String), so every
  // concatenation below has to start from a String on the left.
  String head(s.symbol);
  if (s.side.length()) {
    head += "  ";
    head += s.side;
  }
  tft.setTextColor(rgb(dir), bg);
  tft.drawString(head, 120, 166, 4);

  // Arrow just past the end of the text, if it fits
  int ax = 120 + tft.textWidth(head, 4) / 2 + 14;
  if (ax < 230 && s.side.length() && s.side != "FLAT") {
    if (sell) tft.fillTriangle(ax - 7, 159, ax + 7, 159, ax, 174, rgb(dir));
    else      tft.fillTriangle(ax - 7, 174, ax + 7, 174, ax, 159, rgb(dir));
  }

  auto orDash = [](const String &v) { return v.length() ? v : String("-"); };

  // Targets
  tft.setTextColor(rgb(cfg.cBuy), bg);
  tft.setTextDatum(ML_DATUM);
  tft.drawString(String("TP1 ") + orDash(s.tp1), 16, 198, 2);
  tft.setTextDatum(MR_DATUM);
  tft.drawString(String("TP2 ") + orDash(s.tp2), 224, 198, 2);

  tft.setTextColor(rgb(cfg.cSell), bg);
  tft.setTextDatum(ML_DATUM);
  tft.drawString(String("SL ") + orDash(s.sl), 16, 222, 2);

  tft.setTextColor(rgb(cfg.cAccent), bg);
  tft.setTextDatum(MR_DATUM);
  tft.drawString(String("CONF ") + s.conf + "%", 224, 222, 2);

  tft.setTextDatum(MC_DATUM);
}

/* ------------------------------------------------------------------ */
/* Idle clock (shown when no fresh signal)                             */
/* ------------------------------------------------------------------ */

inline void drawIdle(const char *hhmm, const char *sub) {
  const uint16_t bg = rgb(cfg.cBg);
  tft.fillScreen(bg);
  tft.setTextDatum(MC_DATUM);

  tft.setTextColor(rgb(cfg.cAccent), bg);
  tft.drawString("GOD MODE", 120, 40, 4);

  if (cfg.showClock && hhmm && *hhmm) {
    tft.setTextColor(rgb(cfg.cText), bg);
    tft.setTextPadding(200);
    tft.drawString(hhmm, 120, 118, 7);       // font 7 = 7-segment
    tft.setTextPadding(0);
  }

  tft.setTextColor(tft.color565(0x8B, 0x8B, 0xA7), bg);
  tft.drawString(sub ? sub : "WAITING FOR SIGNAL", 120, 190, 2);
}

/* Redraw just the clock digits, no full clear - avoids visible flicker. */
inline void updateIdleClock(const char *hhmm) {
  if (!cfg.showClock || !hhmm || !*hhmm) return;
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(rgb(cfg.cText), rgb(cfg.cBg));
  tft.setTextPadding(200);
  tft.drawString(hhmm, 120, 118, 7);
  tft.setTextPadding(0);
}
