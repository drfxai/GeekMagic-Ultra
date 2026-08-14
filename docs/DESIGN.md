# The DrFX Ultra OS design system

Everything that draws a pixel — the 240×240 panel, the settings page and the
bridge's status page — follows the rules below. Open
[`ui/screens-preview.html`](../ui/screens-preview.html) in a browser to see the
screens at true size before flashing anything.

---

## The idea

**Minimal terminal.** Black field, hairline rules instead of boxes, mono type,
one accent colour per screen, and a single large value carrying the meaning.

The device is 240×240. At that size every decorative border costs two rows of
pixels that the content needs more, and every extra colour costs the reader a
moment deciding what matters. So the design spends its budget on exactly one
thing per screen and lets the rest recede.

Three rules follow from that, and they are the ones worth remembering:

1. **One accent per screen.** Green for trading, cyan for infrastructure, amber
   for prices, violet for consensus, red for alerts. Body text stays near-white.
   Colour marks the thing that matters, not the thing that is easiest to colour.
2. **Hairlines, not boxes.** A single 1px rule separates regions. Nested
   bordered panels are the failure mode this design exists to avoid.
3. **Labels are small, values are large.** 8px letter-spaced caps for the label,
   26–48px for the value. If a screen has no obvious hero value, the screen is
   probably doing two jobs and should be split.

---

## Layout grid

Defined once in [`firmware/src/ui.h`](../firmware/src/ui.h) and mirrored in CSS.

```
 0                                                            239
 ┌──────────────────────────────────────────────────────────────┐
 │  SCREEN NAME                                     status      │  header, 27px
 ├──────────────────────────────────────────────────── y = 27 ──┤
 │                                                              │
 │  body                                                        │
 │                                                              │
 ├─────────────────────────────────────────────────── y = 213 ──┤
 │  secondary left                             secondary right  │  footer, 26px
 └──────────────────────────────────────────────────────────────┘
      12px margin                                    12px margin
```

| Token | Value | Notes |
|---|---|---|
| `UI_PAD` | 12px | left/right margin, never violated |
| `UI_HDR_Y` | 27 | hairline under the header |
| `UI_FTR_Y` | 213 | hairline above the footer |
| header band | 27px | screen name in accent, status right in dim |
| footer band | 26px | two secondary facts, `*` prefix marks one as live |

## Type

TFT_eSPI fonts, chosen at build time so the slim image can drop the large tables.

| Token | Font | Use |
|---|---|---|
| `UI_F_LABEL` | 1 (6×8) | every small cap label, drawn with 1px tracking |
| `UI_F_BODY` | 2 (16px) | secondary values |
| `UI_F_HEAD` | 4 (26px) | symbols, direction, dates — **the largest face with letters** |
| `UI_F_NUM` | 6 (48px) | the AI score |
| `UI_F_CLOCK` | 7 (7-seg) | the clock digits |

> **Fonts 6 and 7 are digit-only.** Font 6 carries `0-9 : . a p m`; font 7 is a
> seven-segment face with digits and a colon. Anything containing letters must
> use font 4 or smaller. This is why the clock shows `SYNCING` in font 4 rather
> than a `--:--` placeholder before NTP answers, and it is the single easiest
> mistake to make when adding a screen.

In the slim build `UI_F_NUM` and `UI_F_CLOCK` both fall back to font 4. Layouts
must still hold at that size — check both images before shipping a screen.

## Colour

The user picks five colours; everything else is derived, so a custom theme stays
coherent instead of producing unreadable grey-on-grey.

| Token | Default | Derived from |
|---|---|---|
| `cAccent` | `#8B5CF6` | user |
| `cBuy` | `#22DD77` | user |
| `cSell` | `#FF4D5E` | user |
| `cText` | `#E8E8F5` | user |
| `cBg` | `#000000` | user |
| `uiDim()` | — | `cText` → `cBg`, 59% — labels |
| `uiDim2()` | — | `cText` → `cBg`, 80% |
| `uiLine()` | — | `cText` → `cBg`, 88% — hairlines |

## Primitives

| Function | Draws |
|---|---|
| `uiClear()` | black field, resets datum and padding |
| `uiHeader(id, right, accent)` | header band and its rule |
| `uiFooter(left, right, accent)` | footer band; leading `*` colours a side |
| `uiRule(y)` / `uiVRule(x, y0, y1)` | hairlines |
| `uiLabel(x, y, text)` | letter-spaced small cap |
| `uiText(x, y, s, font, colour, datum, padding)` | any value |
| `uiBar(x, y, w, h, pct, colour)` | flat progress bar |
| `uiCell(x, y, label, value, colour, datum)` | label-over-value pair |

No sprites anywhere. A full 240×240 16-bit sprite is 115 kB and the ESP8266 does
not have it — the poll loop needs ~17 kB free for the TLS receive buffer.
Flicker is handled instead by drawing text with an explicit background colour
plus `setTextPadding`, so each redraw overwrites its own previous glyphs.

---

## The screens

### Signal

```
 SIGNAL                                    15M
 ─────────────────────────────────────────────
 XAUUSD                                 ▲ BUY
 ─────────────────────────────────────────────
 AI SCORE      │  CONFIDENCE
 96            │                          94%
               │  ▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓░░
 ─────────────────────────────────────────────
 TP1              TP2                      SL
 3378             3386                   3362
 ─────────────────────────────────────────────
 ENTRY 3371.4                       R:R 1:2.4
```

Risk:reward is computed on the device from entry, TP1 and SL, and is simply
omitted when the three do not all parse as numbers — an invented ratio is worse
than a blank.

A `FLAT` signal (a TP-hit or stop event from the indicator) carries no fresh
levels, so it renders the note as the headline instead of an empty grid.

### Clock

```
 CLOCK                                     BST
 ─────────────────────────────────────────────
 15:46                                      09
 ▓▓▓▓▓▓▓▓▓░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░
 ─────────────────────────────────────────────
 DATE                                     DAY
 14 AUG 2026                            FRIDAY
 ─────────────────────────────────────────────
 WAITING FOR SIGNAL
 Europe/London                      UTC+01:00
```

Shown whenever no fresh signal is in hand. Ticks once a second: `updateClock()`
redraws two text fields and a 2px bar without clearing, so there is no flicker.

### Banner

Boot, setup mode and error states. Header, one large line, two secondary lines.

---

## Changing the design

`ui.h` is the only file that should know pixel values. If a screen in
`display.h` contains a magic number that is not a position, it belongs in `ui.h`
as a token. The preview gallery in `ui/` is a separate hand-written mirror of
these rules for reviewing layouts quickly — it is documentation, not a build
artifact, so keep it in step by hand when the grid changes.
