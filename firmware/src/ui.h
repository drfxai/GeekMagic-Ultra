/**
 * DrFX Ultra OS - shared drawing primitives and layout grid.
 *
 * Every screen is built from the same small vocabulary so they stay visually
 * consistent and so a change to the design language happens in one place.
 *
 * The design is "minimal terminal": black field, hairline rules instead of
 * boxes, one accent colour per screen, small letter-spaced caps for labels and
 * one large value carrying the meaning. Boxes and glows were dropped because
 * on a 240x240 panel every border steals two rows of pixels that the content
 * needs more than the decoration does.
 *
 * No sprites: a full 240x240 16-bit sprite is 115 kB and the ESP8266 does not
 * have it. Flicker is handled by drawing text with an explicit background
 * colour plus setTextPadding, so a redraw overwrites its own previous glyphs.
 */
#pragma once

#include <Arduino.h>
#include <TFT_eSPI.h>
#include "config.h"

extern TFT_eSPI tft;

/* ------------------------------------------------------------------ */
/* Layout grid                                                         */
/*                                                                     */
/*   0                                                            239  */
/*   +--------------------------------------------------------------+  */
/*   | header band                                        27px tall |  */
/*   +---------------------------------------------------- UI_HDR_Y +  */
/*   | body                                                         |  */
/*   +---------------------------------------------------- UI_FTR_Y +  */
/*   | footer band                                        26px tall |  */
/*   +--------------------------------------------------------------+  */
/* ------------------------------------------------------------------ */

#define UI_W       240
#define UI_H       240
#define UI_PAD     12      // left/right margin for everything
#define UI_HDR_Y   27      // y of the hairline under the header
#define UI_FTR_Y   213     // y of the hairline above the footer
#define UI_MIDX    120

/* Font map. The slim build (see platformio.ini) drops the big font tables to
 * fit the stock Ultra's OTA slot. Asking TFT_eSPI for a font that was not
 * compiled in draws nothing at all rather than falling back, so these have to
 * resolve at build time.
 *
 * Note fonts 6 and 7 are DIGIT-ONLY faces in TFT_eSPI - font 6 carries
 * 0-9, ':', '.', 'a', 'p', 'm' and font 7 is a 7-segment face with digits and
 * a colon. Any screen element containing letters must use font 4 or smaller.
 */
#define UI_F_LABEL 1       // 6x8 caps, used for every small label
#define UI_F_BODY  2       // 16px, the workhorse
#define UI_F_HEAD  4       // 26px, the largest face that can render letters

#if defined(LOAD_FONT6)
  #define UI_F_NUM 6       // 48px numerals
#else
  #define UI_F_NUM 4
#endif

#if defined(LOAD_FONT7)
  #define UI_F_CLOCK 7     // 7-segment face
#else
  #define UI_F_CLOCK 4
#endif

/* ------------------------------------------------------------------ */
/* Colour                                                              */
/* ------------------------------------------------------------------ */

/* 24-bit RGB from the settings page -> 16-bit RGB565 for the panel */
inline uint16_t rgb(uint32_t c) {
  return tft.color565((c >> 16) & 0xFF, (c >> 8) & 0xFF, c & 0xFF);
}

/* Blend two 24-bit colours. t is 0-255, 0 = all a, 255 = all b. */
inline uint32_t mix24(uint32_t a, uint32_t b, uint8_t t) {
  uint32_t r = (((a >> 16) & 0xFF) * (255 - t) + ((b >> 16) & 0xFF) * t) / 255;
  uint32_t g = (((a >> 8) & 0xFF) * (255 - t) + ((b >> 8) & 0xFF) * t) / 255;
  uint32_t bl = ((a & 0xFF) * (255 - t) + (b & 0xFF) * t) / 255;
  return (r << 16) | (g << 8) | bl;
}

/* The three greys the design uses, all derived from the user's own text and
   background colours so a custom theme stays coherent. */
inline uint32_t uiDim()  { return mix24(cfg.cText, cfg.cBg, 150); }   // labels
inline uint32_t uiDim2() { return mix24(cfg.cText, cfg.cBg, 205); }   // hairlines
inline uint32_t uiLine() { return mix24(cfg.cText, cfg.cBg, 224); }   // faintest

/* ------------------------------------------------------------------ */
/* Primitives                                                          */
/* ------------------------------------------------------------------ */

inline void uiClear() {
  tft.fillScreen(rgb(cfg.cBg));
  tft.setTextDatum(TL_DATUM);
  tft.setTextPadding(0);
}

inline void uiRule(int y, int x0 = 0, int x1 = UI_W, uint32_t colour = 0xFFFFFFFF) {
  tft.drawFastHLine(x0, y, x1 - x0, rgb(colour == 0xFFFFFFFF ? uiLine() : colour));
}

inline void uiVRule(int x, int y0, int y1, uint32_t colour = 0xFFFFFFFF) {
  tft.drawFastVLine(x, y0, y1 - y0, rgb(colour == 0xFFFFFFFF ? uiLine() : colour));
}

/* Small letter-spaced cap. TFT_eSPI has no tracking control, so the spacing is
   faked by drawing one character at a time. At font 1 this is what separates a
   label from a value at a glance, and it costs about 40 bytes of code. */
inline void uiLabel(int x, int y, const char *s, uint32_t colour = 0xFFFFFFFF,
                    uint8_t datum = TL_DATUM) {
  if (!s || !*s) return;
  uint32_t c = (colour == 0xFFFFFFFF) ? uiDim() : colour;
  tft.setTextColor(rgb(c), rgb(cfg.cBg));
  tft.setTextPadding(0);

  const int track = 1;   // extra pixels between glyphs
  int w = 0;
  for (const char *p = s; *p; ++p) w += tft.textWidth(String(*p), UI_F_LABEL) + track;
  if (w > 0) w -= track;

  int cx = x;
  if (datum == TC_DATUM) cx = x - w / 2;
  else if (datum == TR_DATUM) cx = x - w;

  tft.setTextDatum(TL_DATUM);
  for (const char *p = s; *p; ++p) {
    char buf[2] = {*p, 0};
    tft.drawString(buf, cx, y, UI_F_LABEL);
    cx += tft.textWidth(buf, UI_F_LABEL) + track;
  }
}

inline void uiText(int x, int y, const String &s, uint8_t font, uint32_t colour,
                   uint8_t datum = TL_DATUM, int padding = 0) {
  tft.setTextDatum(datum);
  tft.setTextPadding(padding);
  tft.setTextColor(rgb(colour), rgb(cfg.cBg));
  tft.drawString(s, x, y, font);
  tft.setTextPadding(0);
}

/* Flat progress bar. No rounded ends, no gradient - it reads as a measurement
   rather than as decoration. */
inline void uiBar(int x, int y, int w, int h, int pct, uint32_t colour) {
  if (pct < 0) pct = 0;
  if (pct > 100) pct = 100;
  tft.fillRect(x, y, w, h, rgb(uiLine()));
  int fill = (w * pct) / 100;
  if (fill > 0) tft.fillRect(x, y, fill, h, rgb(colour));
}

/* Header band: screen name on the left in the accent, status on the right. */
inline void uiHeader(const char *id, const String &right, uint32_t accent) {
  uiLabel(UI_PAD, 9, id, accent);
  if (right.length()) {
    tft.setTextDatum(TR_DATUM);
    tft.setTextColor(rgb(uiDim()), rgb(cfg.cBg));
    tft.setTextPadding(0);
    tft.drawString(right, UI_W - UI_PAD, 8, UI_F_LABEL);
  }
  uiRule(UI_HDR_Y);
}

/* Footer band. A leading '*' on either string draws it in the accent colour -
   a cheap way to mark the one live item without another parameter. */
inline void uiFooter(const String &left, const String &right, uint32_t accent) {
  uiRule(UI_FTR_Y);
  int y = UI_FTR_Y + 9;

  if (left.length()) {
    bool hot = left[0] == '*';
    uiLabel(UI_PAD, y, (hot ? left.substring(1) : left).c_str(),
            hot ? accent : uiDim(), TL_DATUM);
  }
  if (right.length()) {
    bool hot = right[0] == '*';
    uiLabel(UI_W - UI_PAD, y, (hot ? right.substring(1) : right).c_str(),
            hot ? accent : uiDim(), TR_DATUM);
  }
}

/* A labelled cell: small cap above, value below. Used for the TP/SL row and
   anywhere three numbers sit side by side. */
inline void uiCell(int x, int y, const char *label, const String &value,
                   uint32_t valueColour, uint8_t datum = TL_DATUM) {
  uiLabel(x, y, label, uiDim(), datum);
  uiText(x, y + 13, value.length() ? value : String("-"), UI_F_BODY, valueColour, datum);
}
