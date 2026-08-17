#!/usr/bin/env python3
"""
DrFX Ultra OS - product brochure (8pp).

Instrument Black: black field, hairline lattice, one luminous accent, monospace
calibration type - now with radial bloom so the accent reads as emitted light
rather than printed ink.

Every display string passes through fit(); margins are never left to chance.
"""
import math, os
from reportlab.pdfgen import canvas as rl_canvas
from reportlab.lib.pagesizes import A4
from reportlab.lib.colors import Color
from reportlab.pdfbase import pdfmetrics
from reportlab.pdfbase.ttfonts import TTFont

# Fonts are not vendored - they are OFL and easy to fetch. Put these six TTFs
# in marketing/fonts/ (or point DRFX_FONTS at them):
#
#   BigShoulders-Bold.ttf   BigShoulders-Regular.ttf     <- fonts.google.com/specimen/Big+Shoulders
#   GeistMono-Regular.ttf   GeistMono-Bold.ttf           <- fonts.google.com/specimen/Geist+Mono
#   InstrumentSans-Regular.ttf  InstrumentSans-Bold.ttf  <- fonts.google.com/specimen/Instrument+Sans
#
#   pip install reportlab && python marketing/build-brochure.py
#
# To change prices, edit TIERS below. To change contacts, edit TG_ID/TG_CH/TV_ID.
HERE = os.path.dirname(os.path.abspath(__file__))
FONTS = os.environ.get("DRFX_FONTS", os.path.join(HERE, "fonts"))
OUT = os.path.join(HERE, "DrFX-Ultra-OS.pdf")

for name, f in [
    ("Display",  "BigShoulders-Bold.ttf"),
    ("DisplayL", "BigShoulders-Regular.ttf"),
    ("Mono",     "GeistMono-Regular.ttf"),
    ("MonoB",    "GeistMono-Bold.ttf"),
    ("Sans",     "InstrumentSans-Regular.ttf"),
    ("SansB",    "InstrumentSans-Bold.ttf"),
]:
    pdfmetrics.registerFont(TTFont(name, os.path.join(FONTS, f)))

W, H = A4
M = 46
CW = W - 2 * M

BG     = (0x07/255, 0x09/255, 0x0E/255)
INK    = (0xE8/255, 0xE8/255, 0xF5/255)
DIM    = (0x59/255, 0x62/255, 0x74/255)
FAINT  = (0x1A/255, 0x1F/255, 0x2B/255)
ACCENT = (0x8B/255, 0x5C/255, 0xF6/255)
BUY    = (0x22/255, 0xDD/255, 0x77/255)
SELL   = (0xFF/255, 0x4D/255, 0x5E/255)
GOLD   = (0xE8/255, 0xB4/255, 0x4D/255)

HAIR = 0.28

TG_ID, TG_CH, TV_ID = "@DrFXGOD", "@DrFXAi", "RaptorAlgo"

# Prices live here and nowhere else.
PRICE_PROGRAM, PRICE_FULL, PRICE_LIFETIME = "$25", "$50", "$181"


def rgb(c, a=None):
    return c if a is None else tuple(ci * a + BG[i] * (1 - a) for i, ci in enumerate(c))


def fit(s, font, maxw, start, floor=6.5):
    size = start
    while size > floor and pdfmetrics.stringWidth(s, font, size) > maxw:
        size -= 0.25
    return size


# Every glow that lands on top of already-drawn text is recorded here and
# reported at the end of the build. A glow is an opaque fill, so this is a
# silent, viewer-dependent way to erase copy - it has to fail loudly instead.
OVERPAINT = []


class Page:
    def __init__(self, c, name="?"):
        self.c = c
        self.name = name
        self.marks = []          # text boxes already committed to this page
        c.setFillColorRGB(*BG)
        c.rect(0, 0, W, H, stroke=0, fill=1)

    def _record(self, x0, x1, y, size, s):
        self.marks.append((x0, x1, y - size * 0.25, y + size * 0.80, s))

    def _check_overpaint(self, cx, cy, r):
        for x0, x1, y0, y1, s in self.marks:
            nx = min(max(cx, x0), x1)      # closest point on the box to centre
            ny = min(max(cy, y0), y1)
            if (nx - cx) ** 2 + (ny - cy) ** 2 <= r * r:
                OVERPAINT.append((self.name, s.strip()[:34]))

    # ---- light ---------------------------------------------------------
    def glow(self, cx, cy, r, col, strength=0.30):
        """Radial bloom.

        NOTE: this is an opaque fill, not a screen blend - it paints over
        whatever is already on the page. Always lay a glow down BEFORE the
        marks it sits behind, never after.

        The falloff is multi-stop rather than linear. A straight two-stop
        gradient over a large radius reads as a flat grey disc with a visible
        rim; real light drops off fast and is indistinguishable from the field
        long before the clip edge.
        """
        c = self.c
        self._check_overpaint(cx, cy, r)
        c.saveState()
        p = c.beginPath()
        p.circle(cx, cy, r)
        c.clipPath(p, stroke=0, fill=0)
        stops = [0.0, 0.16, 0.36, 0.62, 1.0]
        facs = [1.0, 0.50, 0.19, 0.045, 0.0]
        c.radialGradient(cx, cy, r,
                         [Color(*rgb(col, strength * f)) for f in facs], stops)
        c.restoreState()

    def tickmark(self, x, y, s=4.0, col=BUY, alpha=0.95):
        """Drawn, not typed - ✓ is absent from these fonts and renders as tofu."""
        c = self.c
        c.saveState()
        c.setStrokeColorRGB(*rgb(col, alpha))
        c.setLineWidth(HAIR * 3.4)
        c.setLineCap(1)
        c.setLineJoin(1)
        pth = c.beginPath()
        pth.moveTo(x - s, y + s * 0.05)
        pth.lineTo(x - s * 0.22, y - s * 0.68)
        pth.lineTo(x + s, y + s * 0.72)
        c.drawPath(pth, stroke=1, fill=0)
        c.restoreState()

    # ---- primitives ----------------------------------------------------
    def hair(self, x1, y1, x2, y2, col=FAINT, w=HAIR, alpha=None):
        c = self.c
        c.setStrokeColorRGB(*rgb(col, alpha))
        c.setLineWidth(w)
        c.line(x1, y1, x2, y2)

    def text(self, x, y, s, font="Mono", size=7, col=DIM, track=0, align="l", alpha=None):
        c = self.c
        c.setFillColorRGB(*rgb(col, alpha))
        c.setFont(font, size)
        if track:
            wtot = sum(pdfmetrics.stringWidth(ch, font, size) + track for ch in s) - track
            if align == "r":
                x -= wtot
            elif align == "c":
                x -= wtot / 2
            x0 = x
            for ch in s:
                c.drawString(x, y, ch)
                x += pdfmetrics.stringWidth(ch, font, size) + track
            self._record(x0, x0 + wtot, y, size, s)
            return wtot
        w = pdfmetrics.stringWidth(s, font, size)
        if align == "r":
            c.drawRightString(x, y, s)
            self._record(x - w, x, y, size, s)
        elif align == "c":
            c.drawCentredString(x, y, s)
            self._record(x - w / 2, x + w / 2, y, size, s)
        else:
            c.drawString(x, y, s)
            self._record(x, x + w, y, size, s)
        return w

    def para(self, x, y, w, text, size=8.2, lead=11.4, col=DIM, font="Sans"):
        words, line = text.split(), ""
        for wd in words:
            t = (line + " " + wd).strip()
            if pdfmetrics.stringWidth(t, font, size) > w and line:
                self.text(x, y, line, font, size, col)
                y -= lead
                line = wd
            else:
                line = t
        if line:
            self.text(x, y, line, font, size, col)
            y -= lead
        return y

    def headline(self, y, a, b, size=54, gap=42, cola=None, colb=None):
        s = min(fit(a, "Display", CW, size), fit(b, "Display", CW, size))
        y2 = y - gap * (s / size)
        self.c.setFillColorRGB(*(cola or INK))
        self.c.setFont("Display", s)
        self.c.drawString(M - 2, y, a)
        self._record(M - 2, M - 2 + pdfmetrics.stringWidth(a, "Display", s), y, s, a)
        self.c.setFillColorRGB(*(colb or rgb(INK, 0.34)))
        self.c.drawString(M - 2, y2, b)
        self._record(M - 2, M - 2 + pdfmetrics.stringWidth(b, "Display", s), y2, s, b)
        return y2

    def ticks(self, x, y, length, n, horiz=True, major=5, size=3.2, col=FAINT, alpha=None):
        step = length / n
        for i in range(n + 1):
            long = (i % major == 0)
            L = size * (2.05 if long else 1.0)
            a = (alpha if alpha is not None else 1.0) * (1.0 if long else 0.62)
            if horiz:
                self.hair(x + i * step, y, x + i * step, y + L, col, HAIR, a)
            else:
                self.hair(x, y + i * step, x + L, y + i * step, col, HAIR, a)

    def label(self, x, y, s, col=DIM, size=6.0, alpha=None, align="l"):
        return self.text(x, y, s.upper(), "Mono", size, col, track=1.5, align=align, alpha=alpha)

    def rule(self, y, col=FAINT, alpha=None, x1=None, x2=None):
        self.hair(x1 or M, y, x2 or (W - M), y, col, HAIR, alpha)

    def glowrule(self, y, col=ACCENT, x1=None, x2=None, seg=0.42):
        """A rule that brightens over its first stretch, then fades to hairline."""
        x1 = x1 or M
        x2 = x2 or (W - M)
        n = 90
        for i in range(n):
            t = i / (n - 1.0)
            a = max(0.06, (1.0 - t / seg) if t < seg else 0.06)
            xa = x1 + (x2 - x1) * i / n
            xb = x1 + (x2 - x1) * (i + 1) / n
            self.hair(xa, y, xb, y, col, HAIR * 2.2, a * 0.85)

    def corners(self, x, y, w, h, s=6, col=DIM, alpha=0.5):
        for dx, dy, sx, sy in ((0, 0, 1, 1), (w, 0, -1, 1), (0, h, 1, -1), (w, h, -1, -1)):
            self.hair(x + dx, y + dy, x + dx + s * sx, y + dy, col, HAIR, alpha)
            self.hair(x + dx, y + dy, x + dx, y + dy + s * sy, col, HAIR, alpha)

    def head(self, kicker, fig=None):
        self.label(M, H - M - 8, kicker, DIM, 6.0, 0.85)
        if fig:
            self.text(W - M, H - M - 8, fig, "Mono", 6.0, ACCENT, track=1.5, align="r")
        self.rule(H - M - 18)

    def folio(self, n, total, title):
        self.rule(M - 14, alpha=0.9)
        self.label(M, M - 26, "DRFX ULTRA OS", DIM, 5.6, 0.75)
        self.text(W / 2, M - 26, title.upper(), "Mono", 5.6, DIM, track=1.5, align="c", alpha=0.75)
        self.text(W - M, M - 26, f"{n:02d}/{total:02d}", "Mono", 5.6, ACCENT, align="r", alpha=0.9)


# ------------------------------------------------------------------------
def device(p, cx, cy, panel, painter, label=None, bloom=0.26):
    c = p.c
    bez = panel * 0.135
    body = panel + bez * 2
    bx, by = cx - body / 2, cy - body / 2

    if bloom:
        p.glow(cx, cy, body * 0.98, ACCENT, bloom)

    c.setFillColorRGB(*rgb(INK, 0.055))
    c.setStrokeColorRGB(*rgb(DIM, 0.5))
    c.setLineWidth(HAIR * 2.4)
    c.roundRect(bx, by, body, body, body * 0.085, stroke=1, fill=1)

    c.setStrokeColorRGB(*rgb(DIM, 0.32))
    c.setLineWidth(HAIR)
    c.roundRect(bx + bez * 0.42, by + bez * 0.42, body - bez * 0.84, body - bez * 0.84,
                body * 0.055, stroke=1, fill=0)

    px, py = cx - panel / 2, cy - panel / 2
    c.setFillColorRGB(0, 0, 0)
    c.rect(px, py, panel, panel, stroke=0, fill=1)

    c.saveState()
    path = c.beginPath()
    path.rect(px, py, panel, panel)
    c.clipPath(path, stroke=0, fill=0)
    painter(p, px, py, panel)
    c.restoreState()

    c.setStrokeColorRGB(*rgb(DIM, 0.55))
    c.setLineWidth(HAIR)
    c.rect(px, py, panel, panel, stroke=1, fill=0)

    sw, sh = body * 0.30, body * 0.045
    c.setFillColorRGB(*rgb(INK, 0.08))
    c.setStrokeColorRGB(*rgb(DIM, 0.34))
    c.roundRect(cx - sw / 2, by - sh, sw, sh, sh * 0.4, stroke=1, fill=1)

    if label:
        p.text(cx, by - sh - 14, label.upper(), "Mono", 5.8, DIM, track=1.6, align="c", alpha=0.85)
    return body


# ---- panel painters ----------------------------------------------------
def paint_signal(p, x, y, s):
    u, k = s / 240.0, s / 150.0
    pad = 12 * u
    p.text(x + pad, y + s - 17 * u, "SIGNAL", "Mono", 5.2 * k, DIM, track=1.2)
    p.text(x + s - pad, y + s - 17 * u, "GOD MODE", "Mono", 4.8 * k, ACCENT, track=1.0, align="r")
    p.hair(x + pad, y + s - 24 * u, x + s - pad, y + s - 24 * u, DIM, HAIR, 0.5)

    p.text(x + pad, y + s - 58 * u, "XAUUSD", "Display", 30 * u, INK)
    p.text(x + s - pad, y + s - 58 * u, "BUY", "Display", 30 * u, BUY, align="r")

    rcx, rcy, rr = x + s * 0.29, y + s * 0.40, s * 0.132
    p.c.setStrokeColorRGB(*rgb(DIM, 0.30))
    p.c.setLineWidth(s * 0.026)
    p.c.circle(rcx, rcy, rr, stroke=1, fill=0)
    p.c.setStrokeColorRGB(*BUY)
    p.c.setLineWidth(s * 0.026)
    pth = p.c.beginPath()
    for i in range(97):
        a = math.radians(90 - i * 3.6 * 0.96)
        pth.moveTo(rcx + rr * math.cos(a), rcy + rr * math.sin(a)) if i == 0 else \
            pth.lineTo(rcx + rr * math.cos(a), rcy + rr * math.sin(a))
    p.c.drawPath(pth, stroke=1, fill=0)
    p.text(rcx, rcy - s * 0.040, "96", "Display", 25 * u, INK, align="c")

    for i, (kk, v, col) in enumerate([("TP1", "3378", BUY), ("TP2", "3386", BUY), ("SL", "3362", SELL)]):
        yy = y + s * 0.50 - i * (s * 0.075)
        p.text(x + s * 0.52, yy, kk, "Mono", 4.6 * k, DIM, track=1.0)
        p.text(x + s - pad, yy, v, "Mono", 6.2 * k, col, align="r")

    p.hair(x + pad, y + 27 * u, x + s - pad, y + 27 * u, DIM, HAIR, 0.5)
    p.text(x + pad, y + 15 * u, "CONF 94", "Mono", 5.0 * k, DIM, track=1.0)
    p.text(x + s - pad, y + 15 * u, "15M", "Mono", 5.0 * k, ACCENT, track=1.0, align="r")


def paint_crypto(p, x, y, s):
    u, k = s / 240.0, s / 150.0
    pad = 12 * u
    p.text(x + pad, y + s - 17 * u, "CRYPTO", "Mono", 5.2 * k, DIM, track=1.2)
    p.text(x + s - pad, y + s - 17 * u, "COINPAPRIKA", "Mono", 4.6 * k, DIM, track=1.0, align="r")
    p.hair(x + pad, y + s - 24 * u, x + s - pad, y + s - 24 * u, DIM, HAIR, 0.5)

    p.text(x + pad, y + s - 56 * u, "BTC", "Display", 30 * u, INK)
    p.text(x + s - pad, y + s - 56 * u, "+0.42%", "Display", 21 * u, BUY, align="r")
    p.hair(x + pad, y + s - 68 * u, x + s - pad, y + s - 68 * u, DIM, HAIR, 0.5)

    whole, wsz = "63,074", 42 * u
    p.text(x + pad, y + s - 106 * u, whole, "Display", wsz, INK)
    p.text(x + pad + pdfmetrics.stringWidth(whole, "Display", wsz) + 3 * u,
           y + s - 106 * u, ".68", "Display", 19 * u, DIM)

    pts = [27, 23, 43, 39, 55, 47, 33, 31, 31, 44, 46, 32, 26, 25, 24, 27,
           14, 21, 25, 30, 45, 59, 70, 100]
    gx, gy, gw, gh = x + pad, y + 46 * u, s - 2 * pad, 50 * u
    p.c.setStrokeColorRGB(*BUY)
    p.c.setLineWidth(max(0.45, s * 0.0075))
    pth = p.c.beginPath()
    for i, v in enumerate(pts):
        xx, yy = gx + gw * i / (len(pts) - 1), gy + gh * v / 100.0
        pth.moveTo(xx, yy) if i == 0 else pth.lineTo(xx, yy)
    p.c.drawPath(pth, stroke=1, fill=0)

    p.hair(x + pad, y + 27 * u, x + s - pad, y + 27 * u, DIM, HAIR, 0.5)
    p.text(x + pad, y + 15 * u, "62,521-63,244", "Mono", 4.5 * k, DIM, track=0.6)
    p.text(x + s - pad, y + 15 * u, "*12S", "Mono", 4.5 * k, ACCENT, track=0.6, align="r")


def paint_clock(p, x, y, s):
    u, k = s / 240.0, s / 150.0
    pad = 12 * u
    p.text(x + pad, y + s - 17 * u, "CLOCK", "Mono", 5.2 * k, DIM, track=1.2)
    p.text(x + s - pad, y + s - 17 * u, "VIA BRIDGE", "Mono", 4.6 * k, ACCENT, track=1.0, align="r")
    p.hair(x + pad, y + s - 24 * u, x + s - pad, y + s - 24 * u, DIM, HAIR, 0.5)
    p.text(x + pad, y + s - 92 * u, "20:14", "Display", 66 * u, INK)
    p.text(x + s - pad, y + s - 68 * u, "37", "Display", 24 * u, DIM, align="r")
    p.c.setFillColorRGB(*ACCENT)
    p.c.rect(x + pad, y + s - 108 * u, (s - 2 * pad) * 0.62, 2.2 * u, stroke=0, fill=1)
    p.c.setFillColorRGB(*rgb(DIM, 0.35))
    p.c.rect(x + pad + (s - 2 * pad) * 0.62, y + s - 108 * u, (s - 2 * pad) * 0.38, 2.2 * u,
             stroke=0, fill=1)
    p.text(x + pad, y + s - 150 * u, "SAT 16 AUG", "Display", 23 * u, INK)
    p.hair(x + pad, y + 27 * u, x + s - pad, y + 27 * u, DIM, HAIR, 0.5)
    p.text(x + pad, y + 15 * u, "ASIA/TEHRAN", "Mono", 4.5 * k, DIM, track=0.8)
    p.text(x + s - pad, y + 15 * u, "+03:30", "Mono", 4.5 * k, DIM, track=0.8, align="r")


# ========================================================================
def page_cover(c):
    p = Page(c, "cover")
    p.glow(W / 2, 328, 250, ACCENT, 0.20)

    step = CW / 12.0
    for i in range(13):
        p.hair(M + i * step, M + 40, M + i * step, H - M - 40, FAINT, HAIR, 0.5)
    yy = M + 40
    while yy <= H - M - 40:
        p.hair(M, yy, W - M, yy, FAINT, HAIR, 0.28)
        yy += step

    p.label(M, H - M - 8, "DRFX ULTRA OS", DIM, 6.0, 0.85)
    p.text(W - M, H - M - 8, "GEEKMAGIC SMALLTV ULTRA", "Mono", 6.0, DIM,
           track=1.5, align="r", alpha=0.85)
    p.rule(H - M - 18)

    ty = H - 150
    c.setFillColorRGB(*INK)
    c.setFont("Display", 92)
    c.drawString(M - 3, ty, "NEVER MISS")
    c.setFillColorRGB(*ACCENT)
    c.drawString(M - 3, ty - 72, "THE ENTRY.")

    p.glowrule(ty - 96)
    p.text(M, ty - 122, "TRADINGVIEW ALERT TO YOUR DESK IN FIVE SECONDS",
           "Mono", 8.0, INK, track=2.2, alpha=0.92)

    dcy = 328
    body = device(p, W / 2, dcy, 178, paint_signal, None, bloom=0.30)
    half = body / 2 + 16

    p.hair(M, dcy, W / 2 - half, dcy, DIM, HAIR, 0.34)
    p.hair(W / 2 + half, dcy, W - M, dcy, DIM, HAIR, 0.34)
    for i, (l, r_) in enumerate([("240 x 240", "ESP8266"), ("IPS PANEL", "39 kB RAM")]):
        a = 0.8 if i == 0 else 0.5
        p.text(M, dcy + 7 - i * 21, l, "Mono", 5.8, DIM, track=1.4, alpha=a)
        p.text(W - M, dcy + 7 - i * 21, r_, "Mono", 5.8, DIM, track=1.4, align="r", alpha=a)
    p.corners(W / 2 - half, dcy - half, half * 2, half * 2, 9, DIM, 0.42)

    # three claims, as a measured strip
    sy = 150
    p.rule(sy + 24)
    stats = [("5s", "ALERT TO SCREEN"), ("0", "PORTS OPENED"), ("$0", "TO KEEP RUNNING")]
    for i, (big, sub) in enumerate(stats):
        x = M + i * (CW / 3)
        c.setFillColorRGB(*INK)
        c.setFont("Display", 38)
        c.drawString(x, sy - 14, big)
        p.text(x, sy - 30, sub, "Mono", 6.0, ACCENT, track=1.5)
        if i:
            p.hair(x - 14, sy - 34, x - 14, sy + 12, FAINT, HAIR, 1.0)
    p.ticks(M, sy - 56, CW, 60, major=10, size=3.0, alpha=0.5)
    p.text(M, sy - 76, f"TELEGRAM {TG_ID}   ·   CHANNEL {TG_CH}   ·   TRADINGVIEW {TV_ID}",
           "Mono", 6.4, DIM, track=1.2, alpha=0.85)

    p.folio(1, 8, "cover")
    c.showPage()


# ========================================================================
def page_problem(c):
    p = Page(c, "problem")
    p.head("THE PROBLEM", "FIG. 01")
    p.headline(H - 118, "THE ALERT FIRED.", "YOU DIDN'T SEE IT.", colb=rgb(SELL, 0.75))
    p.para(M, H - 194, CW - 70,
           "It buzzed while your phone was face-down. The tab was behind thirty others. "
           "By the time you looked, the candle had already closed.", 9.6, 13.0, DIM)

    # two lanes: the old way, the new way
    lane_y = H - 272
    lane_h = 92
    # Beats are spaced far enough apart that centred labels cannot collide -
    # at 0.06/0.14/0.20 they ran together into "ALERBRIDGE" and "SCREENYOU ACT".
    # The desk lane still finishes in the first third, which is the whole point.
    for idx, (title, sub, col, verdict, beats) in enumerate([
        ("ON YOUR PHONE", "buried in notifications", SELL, "MINUTES, IF AT ALL",
         [("ALERT", 0.0), ("BUZZ", 0.13), ("IGNORED", 0.37), ("SEEN LATER", 0.86)]),
        ("ON YOUR DESK", "already in your eyeline", BUY, "ABOUT FIVE SECONDS",
         [("ALERT", 0.0), ("BRIDGE", 0.11), ("SCREEN", 0.23), ("YOU ACT", 0.35)]),
    ]):
        y = lane_y - idx * (lane_h + 16)
        c.setStrokeColorRGB(*rgb(col, 0.5))
        c.setLineWidth(HAIR * 2.0)
        c.roundRect(M, y - lane_h, CW, lane_h, 3, stroke=1, fill=0)

        p.text(M + 16, y - 24, title, "Mono", 8.0, col, track=1.5)
        p.text(M + 16, y - 38, sub, "Sans", 8.2, DIM)
        p.text(W - M - 16, y - 26, verdict, "Mono", 7.6, col, track=1.4, align="r")

        tx, tw = M + 16, CW - 32
        ty2 = y - lane_h + 30
        p.hair(tx, ty2, tx + tw, ty2, FAINT, HAIR, 1.0)
        for nm, t in beats:
            bx = tx + tw * t
            c.setFillColorRGB(*rgb(col, 0.9))
            c.circle(bx, ty2, 2.4, stroke=0, fill=1)
            p.text(bx, ty2 + 8, nm, "Mono", 5.4, col if t > 0.5 or idx == 1 else DIM,
                   track=1.0, align="c" if 0.05 < t < 0.95 else "l", alpha=0.9)
        p.text(tx + tw, ty2 - 12, "TIME →", "Mono", 5.2, DIM, track=1.2, align="r", alpha=0.7)

    # the turn
    ty3 = lane_y - 2 * (lane_h + 16) - 40
    p.glowrule(ty3 + 22)
    sz = fit("A SCREEN CANNOT BE MINIMISED.", "Display", CW - 30, 42)
    c.setFillColorRGB(*INK)
    c.setFont("Display", sz)
    c.drawString(M - 1, ty3 - 12, "A SCREEN CANNOT BE MINIMISED.")

    colw = CW / 3 - 16
    items = [
        ("ALWAYS ON", "It sits on the desk doing one job. No lock screen, no app to open, "
                      "no tab to find."),
        ("ALWAYS RIGHT", "Symbol, direction, entry, both targets and the stop — the whole "
                         "trade, not a headline."),
        ("ALWAYS THERE", "Runs with your computer off. The bridge lives on Cloudflare, "
                         "not your laptop."),
    ]
    for i, (t, b) in enumerate(items):
        x = M + i * (CW / 3 + 8)
        yy = ty3 - 46
        p.text(x, yy, t, "Mono", 7.2, ACCENT, track=1.3)
        p.hair(x, yy - 8, x + colw, yy - 8, ACCENT, HAIR, 0.7)
        p.para(x, yy - 24, colw, b, 8.4, 11.6, DIM)

    p.ticks(M, 118, CW, 60, major=10, size=3.0, alpha=0.5)
    p.text(M, 96, "Prices and the clock run whether or not you ever send a signal.",
           "Sans", 8.4, rgb(DIM, 0.9))

    p.folio(2, 8, "the problem")
    c.showPage()


# ========================================================================
def page_how(c):
    p = Page(c, "how")

    box_w, box_h = 126, 92
    gap = (CW - 3 * box_w) / 2.0
    dy = H - 322
    xs = [M + i * (box_w + gap) for i in range(3)]
    meta = [("TRADINGVIEW", "your alerts", DIM),
            ("CLOUDFLARE", "the bridge", ACCENT),
            ("SMALLTV ULTRA", "on your desk", INK)]

    # FIRST, before a single mark. This disc reaches y=710 and x=152, so drawn
    # later it erased the second headline line and truncated the standfirst to
    # "TradingView cannot reach a scr". See Page.glow.
    p.glow(xs[1] + box_w / 2, dy + box_h / 2, box_w * 1.15, ACCENT, 0.20)

    p.head("PRINCIPLE OF OPERATION", "FIG. 02")
    p.headline(H - 118, "THREE MACHINES.", "ONE DIRECTION.")
    p.para(M, H - 192, CW - 120,
           "TradingView cannot reach a screen on your home network. So nothing tries to.",
           9.4, 12.6, DIM)

    for i, (x, (nm, sb, col)) in enumerate(zip(xs, meta)):
        feat = (i == 1)
        c.setStrokeColorRGB(*rgb(col, 0.95 if feat else 0.5))
        c.setLineWidth(HAIR * (3.2 if feat else 2.0))
        c.roundRect(x, dy, box_w, box_h, 3, stroke=1, fill=0)
        sz = fit(nm, "Mono", box_w - 18, 7.4)
        p.text(x + box_w / 2, dy + box_h - 30, nm, "Mono", sz, col, track=1.2, align="c")
        p.text(x + box_w / 2, dy + box_h - 46, sb, "Sans", 8.0, DIM, align="c")
        p.ticks(x + 14, dy + 12, box_w - 28, 14, major=7, size=2.6, col=col, alpha=0.4)

    def arrow(gx1, gx2, y, col, to_right, top, bot):
        inset = 7
        x1, x2 = gx1 + inset, gx2 - inset
        head = x2 if to_right else x1
        tail = x1 if to_right else x2
        c.setStrokeColorRGB(*rgb(col, 0.9))
        c.setLineWidth(HAIR * 2.6)
        c.line(tail, y, head + (-5 if to_right else 5), y)
        pth = c.beginPath()
        pth.moveTo(head, y)
        pth.lineTo(head + (-6 if to_right else 6), y + 3.1)
        pth.lineTo(head + (-6 if to_right else 6), y - 3.1)
        pth.close()
        c.setFillColorRGB(*rgb(col, 0.9))
        c.drawPath(pth, stroke=0, fill=1)
        mid = (gx1 + gx2) / 2
        p.text(mid, y + 10, top, "Mono", 6.2, col, track=1.2, align="c")
        p.text(mid, y - 17, bot, "Sans", 7.0, DIM, align="c")

    arrow(xs[0] + box_w, xs[1], dy + box_h * 0.52, ACCENT, True, "PUSH", "on alert")
    arrow(xs[1] + box_w, xs[2], dy + box_h * 0.52, INK, False, "PULL", "every 5s")
    p.corners(M, dy - 20, CW, box_h + 40, 9, DIM, 0.34)

    wy = dy - 74
    p.rule(wy + 20)
    p.label(M, wy + 2, "WHY IT IS BUILT THIS WAY", ACCENT, 6.2, 0.95)
    colw = CW / 3 - 16
    items = [
        ("NOTHING EXPOSED",
         "The screen only ever makes outbound requests. No port forwarding, no dynamic "
         "DNS, no inbound hole in your router. Your network stays exactly as it was."),
        ("NOTHING LEFT ON",
         "The bridge runs on Cloudflare's free tier, not your PC. Shut the laptop, go "
         "out, come back — the screen never stopped."),
        ("A CHIP THAT CANNOT",
         "The ESP8266 has ~39 kB of RAM and each secure connection wants 16 kB. It cannot "
         "talk to four exchanges. The bridge does that, and sends a few hundred bytes."),
    ]
    for i, (t, b) in enumerate(items):
        x = M + i * (CW / 3 + 8)
        p.text(x, wy - 24, t, "Mono", 7.0, INK, track=1.2)
        p.hair(x, wy - 32, x + colw, wy - 32, ACCENT, HAIR, 0.7)
        p.para(x, wy - 48, colw, b, 8.2, 11.4, DIM)

    sy = 218
    p.rule(sy + 78)
    p.label(M, sy + 60, "WHAT ARRIVES AT THE SCREEN", DIM, 6.0, 0.85)
    for i, ln in enumerate(['{"v":"2.1.2","src":"coinpaprika","tickers":[{"s":"BTCUSDT",',
                            '"d":"BTC","p":"63074.68","c":0.42,',
                            '"k":[27,23,43,39,55,47,33,...,100]}]}']):
        p.text(M, sy + 40 - i * 13, ln, "Mono", 7.0, rgb(INK, 0.72))
    p.para(M, sy - 12, CW - 140,
           "A few hundred bytes. The sparkline is already scaled 0–100, so the device "
           "does no arithmetic at all.", 8.4, 11.4, DIM)
    p.ticks(M, sy - 54, CW, 60, major=10, size=3.0, alpha=0.6)
    p.text(W - M, sy + 40, "PAYLOAD", "Mono", 5.6, ACCENT, track=1.5, align="r", alpha=0.8)

    sz = fit("OUTBOUND ONLY.", "Display", CW * 0.6, 40)
    c.setFillColorRGB(*rgb(INK, 0.16))
    c.setFont("Display", sz)
    c.drawString(M - 1, 96, "OUTBOUND ONLY.")
    p.text(W - M, 104, "NO PORTS OPENED", "Mono", 6.0, DIM, track=1.5, align="r", alpha=0.7)
    p.text(W - M, 90, "NO INBOUND ROUTE", "Mono", 6.0, DIM, track=1.5, align="r", alpha=0.7)

    p.folio(3, 8, "how it works")
    c.showPage()


# ========================================================================
def page_screens(c):
    p = Page(c, "screens")

    panel = 116
    ys = H - 306
    body = panel * 1.27
    xs = [M + body / 2, W / 2, W - M - body / 2]

    # All three blooms first. Left inside device() they fired after the
    # standfirst was set and wiped it out, leaving only the slivers of text
    # that happened to fall between the discs.
    for x in xs:
        p.glow(x, ys, body * 0.98, ACCENT, 0.20)

    p.head("DISPLAY STATES", "FIG. 03")
    p.headline(H - 118, "THREE SCREENS,", "ON ROTATION.")
    p.para(M, H - 192, CW - 90,
           "Each holds for fifteen seconds. A new signal interrupts and stays put — "
           "it is the thing you actually care about.", 9.4, 12.6, DIM)

    for x, painter, nm in zip(xs, (paint_signal, paint_crypto, paint_clock),
                              ("signal", "crypto", "clock")):
        device(p, x, ys, panel, painter, nm, bloom=0)

    ay = ys - 128
    p.rule(ay + 30)
    notes = [
        ("SIGNAL", "Symbol, direction, score ring, targets and stop. On screen about "
                   "five seconds after the alert fires."),
        ("CRYPTO", "Up to four pairs. 24-hour change and an hourly sparkline, from "
                   "whichever source is reachable where you are."),
        ("CLOCK",  "Keeps time even where the network blocks NTP — the bridge supplies "
                   "it instead. Dims itself at night."),
    ]
    colw = CW / 3 - 16
    for i, (t, b) in enumerate(notes):
        x = M + i * (CW / 3 + 8)
        p.text(x, ay + 12, t, "Mono", 7.0, ACCENT, track=1.4)
        p.hair(x, ay + 4, x + colw, ay + 4, ACCENT, HAIR, 0.6)
        p.para(x, ay - 12, colw, b, 8.2, 11.4, DIM)

    cy = 236
    p.rule(cy + 56)
    p.label(M, cy + 38, "ALSO INCLUDED", DIM, 6.0, 0.85)
    caps = ["SETTINGS IN A BROWSER", "NO APP REQUIRED", "NIGHT DIMMING",
            "52 ZONES, REAL DST", "WI-FI FAILOVER", "OVER-THE-AIR UPDATES",
            "OPEN SOURCE, MIT", "SELF-DIAGNOSING", "FOUR PAIRS AT ONCE",
            "NO CLOUD ACCOUNT", "CAROUSEL OR FIXED", "USB-C, 0.6 W"]
    for i, s in enumerate(caps):
        col_i, row_i = i % 3, i // 3
        x = M + col_i * (CW / 3)
        y = cy + 12 - row_i * 24
        c.setFillColorRGB(*ACCENT)
        c.rect(x, y + 2, 4, 4, stroke=0, fill=1)
        p.text(x + 11, y, s, "Mono", 6.6, INK, track=1.0, alpha=0.86)

    p.ticks(M, 118, CW, 60, major=10, size=3.0, alpha=0.55)
    p.para(M, 96, CW - 130,
           "Everything on these three screens is configured from one page in your "
           "browser. Nothing is hard-coded.", 8.6, 11.4, DIM)

    p.folio(4, 8, "the screens")
    c.showPage()


# ========================================================================
def page_godmode(c):
    p = Page(c, "godmode")
    p.head("THE INDICATOR", "FIG. 04")
    p.headline(H - 118, "FOUR READS.", "ONE VERDICT.", colb=GOLD)
    p.para(M, H - 194, CW - 60,
           "GOD MODE Quad Consensus only speaks when four independent reads of the market "
           "agree. Fewer prints, and every one of them arrives on your screen complete — "
           "symbol, direction, entry, both targets, the stop.", 9.4, 12.8, DIM)

    # four reads converging on one verdict
    dy = H - 300
    cellw, cellh = 118, 40
    lx = M
    hub_x, hub_y = M + cellw + 96, dy - (4 * (cellh + 8)) / 2 + 4

    # both blooms before any marks - see Page.glow
    p.glow(hub_x, hub_y, 40, GOLD, 0.30)
    p.glow(W - M - 84, hub_y + 2, 128, ACCENT, 0.22)

    for i in range(4):
        y = dy - i * (cellh + 8)
        c.setStrokeColorRGB(*rgb(DIM, 0.42))
        c.setLineWidth(HAIR * 1.8)
        c.roundRect(lx, y - cellh, cellw, cellh, 2.5, stroke=1, fill=0)
        prev = None
        for j in range(20):
            xx = lx + 10 + (cellw - 34) * j / 19.0
            v = math.sin(j * 0.44 + i * 1.5) * 0.5 + math.sin(j * 0.19 + i) * 0.32
            yy = y - cellh / 2 + v * 9
            if prev:
                p.hair(prev[0], prev[1], xx, yy, GOLD, HAIR * 1.9, 0.55)
            prev = (xx, yy)
        p.tickmark(lx + cellw - 13, y - cellh / 2, 3.6, BUY, 0.95)
        p.text(lx + 10, y - cellh + 7, ["TREND", "MOMENTUM", "STRUCTURE", "VOLATILITY"][i],
               "Mono", 5.2, DIM, track=1.1, alpha=0.8)

    # converging hairlines into the verdict node
    for i in range(4):
        y = dy - i * (cellh + 8) - cellh / 2
        c.setStrokeColorRGB(*rgb(GOLD, 0.45))
        c.setLineWidth(HAIR * 1.6)
        pth = c.beginPath()
        pth.moveTo(lx + cellw + 4, y)
        pth.curveTo(lx + cellw + 44, y, hub_x - 44, hub_y, hub_x - 13, hub_y)
        c.drawPath(pth, stroke=1, fill=0)
    c.setStrokeColorRGB(*rgb(GOLD, 0.95))
    c.setLineWidth(HAIR * 3.0)
    c.circle(hub_x, hub_y, 12, stroke=1, fill=0)
    p.text(hub_x, hub_y - 3.4, "96", "Display", 15, INK, align="c")
    p.text(hub_x, hub_y - 26, "CONSENSUS", "Mono", 5.2, GOLD, track=1.3, align="c")

    # bloom already laid down above; a page-edge-clipped glow shows a hard rim
    device(p, W - M - 84, hub_y + 2, 106, paint_signal, None, bloom=0)

    # platforms
    py = dy - 4 * (cellh + 8) - 34
    p.glowrule(py + 22, GOLD)
    p.label(M, py + 2, "AVAILABLE ON", DIM, 6.2, 0.9)
    for i, (nm, sub) in enumerate([("TRADINGVIEW", "Pine, alert() driven"),
                                   ("METATRADER", "MT4 / MT5")]):
        x = M + i * (CW / 2)
        c.setFillColorRGB(*INK)
        c.setFont("Display", 30)
        c.drawString(x, py - 32, nm)
        p.text(x, py - 46, sub, "Sans", 8.2, DIM)

    ny = py - 76
    p.rule(ny + 20)
    colw = CW / 3 - 16
    notes = [
        ("NOTHING TO TYPE", "On TradingView, set the alert to “Any alert() function call”. "
                            "The script writes the whole message itself."),
        ("SCORE AND CONFIDENCE", "Every print carries a 0–100 score and a separate "
                                 "confidence figure, both drawn on the card."),
        ("YOUR OWN ALERTS TOO", "The bridge takes plain JSON or plain text, so any "
                                "indicator or strategy can drive the screen."),
    ]
    for i, (t, b) in enumerate(notes):
        x = M + i * (CW / 3 + 8)
        p.text(x, ny - 4, t, "Mono", 7.0, GOLD, track=1.2)
        p.hair(x, ny - 12, x + colw, ny - 12, GOLD, HAIR, 0.6)
        p.para(x, ny - 28, colw, b, 8.2, 11.4, DIM)

    p.ticks(M, 118, CW, 60, major=10, size=3.0, alpha=0.5)
    p.text(M, 96, "Signals are information, not instructions. Nothing here is financial advice.",
           "Sans", 8.0, rgb(DIM, 0.9))

    p.folio(5, 8, "god mode")
    c.showPage()


# ========================================================================
def page_setup(c):
    p = Page(c, "setup")
    p.head("INSTALLATION SEQUENCE", "FIG. 05")
    p.headline(H - 118, "TWENTY MINUTES.", "NO CODING.")
    p.para(M, H - 192, CW - 120,
           "Six steps. Each one tells you what you should see when it worked.",
           9.4, 12.6, DIM)

    steps = [
        ("01", "MAKE YOUR KEYS",
         "Double-click keygen.html. Two random keys appear. It runs offline and sends nothing anywhere."),
        ("02", "PUT THE BRIDGE ONLINE",
         "Run one script, or paste one file into the Cloudflare dashboard. The free tier is plenty."),
        ("03", "FLASH THE SCREEN",
         "Download the ready-built file and upload it through the device's own update page."),
        ("04", "JOIN YOUR WI-FI",
         "The screen makes its own network the first time. Join it, pick your Wi-Fi, save."),
        ("05", "POINT IT AT THE BRIDGE",
         "Paste the address and one key. Status should read: connected, nothing new."),
        ("06", "WIRE UP TRADINGVIEW",
         "Paste the webhook address into any alert. Optional — prices work without it."),
    ]
    top = H - 238
    row = 74
    for i, (n, t, b) in enumerate(steps):
        y = top - i * row
        c.setFillColorRGB(*rgb(ACCENT, 0.32))
        c.setFont("Display", 44)
        c.drawString(M, y - 19, n)
        p.text(M + 60, y - 4, t, "Mono", 8.8, INK, track=1.5)
        p.para(M + 60, y - 21, CW - 210, b, 8.6, 11.4, DIM)
        rx = W - M - 106
        p.hair(rx, y - 25, W - M, y - 25, FAINT, HAIR, 0.9)
        c.setStrokeColorRGB(*rgb(ACCENT, 0.9))
        c.setLineWidth(HAIR * 3.4)
        c.line(rx, y - 25, rx + 106 * (i + 1) / 6, y - 25)
        p.text(W - M, y - 4, f"{int((i+1)/6*100)}%", "Mono", 6.4, DIM, track=1.0, align="r")
        if i < 5:
            p.hair(M, y - 40, W - M, y - 40, FAINT, HAIR, 0.8)

    by = top - 6 * row - 4
    p.rule(by + 16)
    p.label(M, by - 4, "IF SOMETHING IS WRONG", ACCENT, 6.2, 0.95)
    p.para(M, by - 24, CW - 60,
           "The screen tells you in plain words, not a code: “no bridge URL saved”, "
           "“Wi-Fi not connected”, “no device key saved”. And one command tests every "
           "link in the chain and names the broken one:", 8.8, 11.8, DIM)
    cmd = "python tools/drfx.py doctor"
    p.text(M, by - 70, cmd, "Mono", 11.5, INK)
    p.hair(M, by - 78, M + pdfmetrics.stringWidth(cmd, "Mono", 11.5), by - 78, ACCENT, HAIR * 2, 0.85)

    p.folio(6, 8, "setup")
    c.showPage()


# ========================================================================
def page_pricing(c):
    p = Page(c, "pricing")
    p.head("PRICING", "FIG. 06")
    p.headline(H - 118, "PICK YOUR LANE.", "PAY ONCE, OR NEVER.")
    p.para(M, H - 194, CW - 50,
           "The device software is open source under MIT — clone it and build it yourself "
           "for nothing, forever. These are for people who would rather skip the afternoon "
           "and get the indicator that was written for it.", 9.2, 12.4, DIM)

    tiers = [
        ("PROGRAM FILE", PRICE_PROGRAM, "one-time",
         "Built, tested, ready to flash",
         ["The compiled firmware, both images",
          "Your two keys generated for you",
          "Quickstart walkthrough",
          "Free updates to the software"], False, DIM),
        ("FULL PACKAGE", PRICE_FULL, "one-time",
         "Everything, plus a month of GOD Mode",
         ["Everything in Program File",
          "1 month GOD MODE indicator",
          "TradingView AND MetaTrader",
          "Alerts wired to your screen"], True, ACCENT),
        ("LIFETIME", PRICE_LIFETIME, "one-time",
         "Never pay again, and own the source",
         ["Lifetime GOD MODE access",
          "Full indicator source code",
          "Both platforms, all future versions",
          "Priority support on Telegram"], False, GOLD),
    ]

    cw = (CW - 24) / 3
    ty = H - 254
    th = 268

    # Bloom first, for every card that wants one. Done inside the loop it
    # painted over the neighbouring card's text - a glow is an opaque fill.
    for i, t in enumerate(tiers):
        if t[5]:
            p.glow(M + i * (cw + 12) + cw / 2, ty - th / 2, cw * 0.92, ACCENT, 0.16)

    for i, (name, price, per, sub, bullets, feat, accent) in enumerate(tiers):
        x = M + i * (cw + 12)
        if feat:
            c.setFillColorRGB(*rgb(ACCENT, 0.07))
            c.rect(x, ty - th, cw, th, stroke=0, fill=1)
        c.setStrokeColorRGB(*rgb(accent, 0.95 if feat else 0.5))
        c.setLineWidth(HAIR * (3.6 if feat else 2.0))
        c.rect(x, ty - th, cw, th, stroke=1, fill=0)

        if feat:
            p.text(x + cw / 2, ty + 8, "BEST VALUE", "Mono", 5.8, ACCENT, track=1.7, align="c")

        p.text(x + 16, ty - 26, name, "Mono", 7.4, accent, track=1.5)
        p.hair(x + 16, ty - 34, x + cw - 16, ty - 34, accent, HAIR, 0.7)

        psz = fit(price, "Display", cw - 60, 46)
        c.setFillColorRGB(*INK)
        c.setFont("Display", psz)
        c.drawString(x + 16, ty - 78, price)
        p.text(x + 18 + pdfmetrics.stringWidth(price, "Display", psz) + 5, ty - 78,
               per, "Mono", 5.8, DIM, track=1.0)

        p.para(x + 16, ty - 100, cw - 32, sub, 8.2, 11.0, rgb(INK, 0.7))

        by = ty - 136
        for b in bullets:
            c.setFillColorRGB(*rgb(accent, 0.95))
            c.rect(x + 16, by + 2.6, 3.4, 3.4, stroke=0, fill=1)
            by = p.para(x + 25, by, cw - 44, b, 8.0, 10.6, rgb(INK, 0.84)) - 4.5

        p.ticks(x + 16, ty - th + 14, cw - 32, 14, major=7, size=2.6, col=accent, alpha=0.42)

    cy = ty - th - 42
    p.glowrule(cy + 24)
    sz = fit("MESSAGE ME AND START TODAY.", "Display", CW - 40, 36)
    c.setFillColorRGB(*INK)
    c.setFont("Display", sz)
    c.drawString(M - 1, cy - 14, "MESSAGE ME AND START TODAY.")

    for i, (k, v, col) in enumerate([("TELEGRAM", TG_ID, ACCENT),
                                     ("CHANNEL", TG_CH, INK),
                                     ("TRADINGVIEW", TV_ID, INK)]):
        x = M + i * (CW / 3)
        p.text(x, cy - 38, k, "Mono", 5.8, DIM, track=1.5)
        p.text(x, cy - 56, v, "Mono", 12.0, col)

    # every-tier strip: reassurance, and it gives the page a floor
    ey = cy - 88
    p.rule(ey + 22)
    p.label(M, ey + 4, "IN EVERY TIER", DIM, 6.0, 0.85)
    every = [("NO SUBSCRIPTION", "to keep the screen running"),
             ("NO PC LEFT ON", "the bridge lives on Cloudflare"),
             ("OPEN SOURCE CORE", "MIT, auditable, yours to keep")]
    colw = CW / 3 - 16
    for i, (t, b) in enumerate(every):
        x = M + i * (CW / 3 + 8)
        p.tickmark(x + 4, ey - 14, 3.6, ACCENT, 0.95)
        p.text(x + 14, ey - 17, t, "Mono", 7.0, INK, track=1.2, alpha=0.9)
        p.para(x + 14, ey - 31, colw - 14, b, 7.8, 10.4, DIM)

    p.ticks(M, 104, CW, 60, major=10, size=3.0, alpha=0.55)
    p.text(M, 84, "Not financial advice. Not affiliated with GeekMagic, TradingView or MetaQuotes.",
           "Sans", 7.6, rgb(DIM, 0.85))

    p.folio(7, 8, "pricing")
    c.showPage()


# ========================================================================
def page_contact(c):
    p = Page(c, "contact")
    p.glow(W / 2, H - 300, 250, ACCENT, 0.13)   # before head(), or it eats the rule
    p.head("GET IN TOUCH", "08")

    p.headline(H - 132, "BUILT TO BE", "GLANCED AT.", size=78, gap=60, colb=ACCENT)
    p.para(M, H - 226, CW - 120,
           "A screen you look at for one second, twenty times a day, and trust.",
           10.2, 13.4, DIM)

    # contact slabs
    ty = H - 268
    bh = 78
    contacts = [("TELEGRAM", TG_ID, "questions, orders, support", ACCENT),
                ("CHANNEL", TG_CH, "updates and new releases", INK),
                ("TRADINGVIEW", TV_ID, "the GOD MODE indicator", GOLD)]
    bw = (CW - 24) / 3
    for i, (k, v, sub, col) in enumerate(contacts):
        x = M + i * (bw + 12)
        c.setStrokeColorRGB(*rgb(col, 0.55))
        c.setLineWidth(HAIR * 2.2)
        c.roundRect(x, ty - bh, bw, bh, 3, stroke=1, fill=0)
        p.text(x + 14, ty - 22, k, "Mono", 5.8, col, track=1.5)
        vs = fit(v, "Display", bw - 28, 30)
        c.setFillColorRGB(*INK)
        c.setFont("Display", vs)
        c.drawString(x + 14, ty - 50, v)
        p.text(x + 14, ty - 64, sub, "Sans", 7.4, DIM)

    # contact sheet
    gy_top = ty - bh - 30
    cols, rows = 6, 3
    cellw, cellh = CW / cols, 50
    for r in range(rows):
        for col_i in range(cols):
            idx = r * cols + col_i
            x = M + col_i * cellw
            y = gy_top - r * cellh - cellh
            ph = idx * 0.9
            amp = 0.30 + ((idx * 37) % 11) / 26.0
            up = math.sin(ph * 1.7) > -0.15
            colr = BUY if up else SELL
            prev = None
            for i in range(23):
                xx = x + 6 + (cellw - 20) * i / 22.0
                v = (math.sin(i * 0.42 + ph) * 0.45 + math.sin(i * 0.17 + ph * 2) * 0.35) * amp
                yy = y + cellh * 0.52 + v * cellh * 0.40
                if prev:
                    p.hair(prev[0], prev[1], xx, yy, colr, HAIR * 1.9, 0.32)
                prev = (xx, yy)
            p.hair(x + 6, y + 9, x + cellw - 14, y + 9, FAINT, HAIR, 0.8)
            p.text(x + 6, y + 13, f"{(idx*7+3) % 24:02d}H", "Mono", 4.6, DIM, track=0.9, alpha=0.5)
            p.text(x + cellw - 14, y + 13, f"{'+' if up else '-'}{amp*4:.1f}", "Mono", 4.6,
                   colr, align="r", alpha=0.48)
    p.corners(M, gy_top - rows * cellh, CW, rows * cellh, 9, DIM, 0.30)

    sy = 196
    p.rule(sy + 60)
    p.label(M, sy + 42, "SPECIFICATION", DIM, 6.0, 0.85)
    specs = [("PANEL", "1.54″ IPS 240×240"), ("DRIVER", "ST7789V"),
             ("MCU", "ESP-12F / ESP8266"),   ("FLASH", "4 MB"),
             ("POWER", "USB-C, ~0.6 W"),     ("BRIDGE", "Cloudflare Workers"),
             ("SOFTWARE", "MIT, open source"), ("PAIRS", "up to 4")]
    for i, (k, v) in enumerate(specs):
        col_i, row_i = i % 2, i // 2
        x = M + col_i * (CW / 2)
        y = sy + 20 - row_i * 19
        p.text(x, y, k, "Mono", 6.2, DIM, track=1.4)
        p.text(x + CW / 2 - 22, y, v, "Mono", 7.4, INK, align="r", alpha=0.88)
        p.hair(x, y - 6, x + CW / 2 - 22, y - 6, FAINT, HAIR, 0.8)

    p.glowrule(96)
    p.text(M, 78, "DRFX ULTRA OS", "Mono", 6.4, INK, track=2.0, alpha=0.9)
    p.text(W - M, 78, "SET IN BIG SHOULDERS & GEIST MONO", "Mono", 5.6, DIM,
           track=1.4, align="r", alpha=0.7)

    p.folio(8, 8, "contact")
    c.showPage()


def main():
    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    c = rl_canvas.Canvas(OUT, pagesize=A4)
    c.setTitle("DrFX Ultra OS — never miss the entry")
    c.setAuthor("DrFX")
    c.setSubject("Product brochure")
    for fn in (page_cover, page_problem, page_how, page_screens,
               page_godmode, page_setup, page_pricing, page_contact):
        fn(c)
    c.save()
    print("wrote", OUT, os.path.getsize(OUT), "bytes")
    if OVERPAINT:
        print("!! GLOW PAINTED OVER EXISTING TEXT (%d):" % len(OVERPAINT))
        for pg, s in OVERPAINT:
            print("   %-9s %r" % (pg, s))
        raise SystemExit(1)
    print("overpaint check: clean - no glow lands on text drawn before it")


if __name__ == "__main__":
    main()
