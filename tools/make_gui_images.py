#!/usr/bin/env python3
"""Generate the bitmap assets of the DopplerIt modgui.

    python3 tools/make_gui_images.py

Requires Pillow. Writes into dopplerit.lv2/modgui/:
  - knob-dopplerit.png        65-frame horizontal film strip (96 px frames)
  - footswitch-dopplerit.png  2 frames stacked vertically (top = on/pressed)

Screenshots and thumbnails are produced from the real HTML/CSS by
tools/make_screenshots.mjs.
"""

import math
import os

from PIL import Image, ImageDraw, ImageFilter

OUT = os.path.join(os.path.dirname(__file__), "..", "dopplerit.lv2", "modgui")

SS = 4            # supersampling
FRAME = 96        # final frame size of the knob
FRAMES = 65
START, END = 135.0, 405.0  # degrees, PIL angles (0 = 3 o'clock, clockwise)

BLUE = (64, 160, 255)
RED = (255, 84, 72)


def lerp(a, b, t):
    return tuple(int(round(x + (y - x) * t)) for x, y in zip(a, b))


def knob_frame(t):
    s = FRAME * SS
    img = Image.new("RGBA", (s, s), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    c = s / 2

    # value arc track
    r_arc = s * 0.46
    w_arc = int(s * 0.055)
    box = [c - r_arc, c - r_arc, c + r_arc, c + r_arc]
    d.arc(box, START, END, fill=(40, 46, 58, 255), width=w_arc)

    # value arc: blue (pitch up) -> red (pitch down) gradient, drawn in slices
    steps = max(1, int(270 * t))
    for i in range(steps):
        a0 = START + i
        a1 = START + i + 1.5
        if a1 > START + 270 * t:
            a1 = START + 270 * t
        col = lerp(BLUE, RED, i / 270.0)
        d.arc(box, a0, a1, fill=col + (255,), width=w_arc)

    # drop shadow of the cap
    shadow = Image.new("RGBA", (s, s), (0, 0, 0, 0))
    sd = ImageDraw.Draw(shadow)
    r_cap = s * 0.36
    sd.ellipse([c - r_cap, c - r_cap + s * 0.03, c + r_cap, c + r_cap + s * 0.03], fill=(0, 0, 0, 170))
    shadow = shadow.filter(ImageFilter.GaussianBlur(s * 0.03))
    img = Image.alpha_composite(img, shadow)
    d = ImageDraw.Draw(img)

    # cap with a vertical metallic gradient
    cap = Image.new("RGBA", (s, s), (0, 0, 0, 0))
    cd = ImageDraw.Draw(cap)
    for y in range(int(c - r_cap), int(c + r_cap) + 1):
        k = (y - (c - r_cap)) / (2 * r_cap)
        col = lerp((92, 100, 116), (28, 31, 38), k)
        cd.line([(0, y), (s, y)], fill=col + (255,))
    mask = Image.new("L", (s, s), 0)
    ImageDraw.Draw(mask).ellipse([c - r_cap, c - r_cap, c + r_cap, c + r_cap], fill=255)
    img.paste(cap, (0, 0), mask)
    d = ImageDraw.Draw(img)

    # rim
    d.ellipse([c - r_cap, c - r_cap, c + r_cap, c + r_cap], outline=(14, 16, 20, 255), width=int(s * 0.012))
    r_in = r_cap * 0.82
    d.ellipse([c - r_in, c - r_in, c + r_in, c + r_in], outline=(120, 128, 144, 90), width=int(s * 0.008))

    # pointer
    ang = math.radians(START + 270 * t)
    x0, y0 = c + math.cos(ang) * r_cap * 0.30, c + math.sin(ang) * r_cap * 0.30
    x1, y1 = c + math.cos(ang) * r_cap * 0.90, c + math.sin(ang) * r_cap * 0.90
    d.line([(x0, y0), (x1, y1)], fill=(240, 244, 250, 255), width=int(s * 0.04))
    d.ellipse([x1 - s * 0.02, y1 - s * 0.02, x1 + s * 0.02, y1 + s * 0.02], fill=(240, 244, 250, 255))

    return img.resize((FRAME, FRAME), Image.LANCZOS)


def make_knob():
    strip = Image.new("RGBA", (FRAME * FRAMES, FRAME), (0, 0, 0, 0))
    for i in range(FRAMES):
        strip.paste(knob_frame(i / (FRAMES - 1)), (i * FRAME, 0))
    strip.save(os.path.join(OUT, "knob-dopplerit.png"), optimize=True)


def footswitch_frame(pressed):
    size = 140
    s = size * SS
    img = Image.new("RGBA", (s, s), (0, 0, 0, 0))
    c = s / 2

    shadow = Image.new("RGBA", (s, s), (0, 0, 0, 0))
    r = s * 0.44
    off = s * (0.015 if pressed else 0.04)
    ImageDraw.Draw(shadow).ellipse([c - r, c - r + off, c + r, c + r + off], fill=(0, 0, 0, 190))
    shadow = shadow.filter(ImageFilter.GaussianBlur(s * 0.03))
    img = Image.alpha_composite(img, shadow)

    # outer nut
    grad = Image.new("RGBA", (s, s), (0, 0, 0, 0))
    gd = ImageDraw.Draw(grad)
    hi, lo = ((150, 156, 168), (60, 64, 72)) if not pressed else ((120, 126, 138), (50, 54, 62))
    for y in range(s):
        gd.line([(0, y), (s, y)], fill=lerp(hi, lo, y / s) + (255,))
    mask = Image.new("L", (s, s), 0)
    ImageDraw.Draw(mask).ellipse([c - r, c - r, c + r, c + r], fill=255)
    img.paste(grad, (0, 0), mask)

    # inner button
    r2 = r * 0.72
    grad2 = Image.new("RGBA", (s, s), (0, 0, 0, 0))
    gd2 = ImageDraw.Draw(grad2)
    hi2, lo2 = ((210, 214, 222), (96, 100, 110)) if not pressed else ((96, 100, 110), (180, 184, 192))
    for y in range(s):
        gd2.line([(0, y), (s, y)], fill=lerp(hi2, lo2, y / s) + (255,))
    mask2 = Image.new("L", (s, s), 0)
    ImageDraw.Draw(mask2).ellipse([c - r2, c - r2, c + r2, c + r2], fill=255)
    img.paste(grad2, (0, 0), mask2)

    d = ImageDraw.Draw(img)
    d.ellipse([c - r, c - r, c + r, c + r], outline=(20, 22, 26, 255), width=int(s * 0.012))
    d.ellipse([c - r2, c - r2, c + r2, c + r2], outline=(30, 32, 38, 255), width=int(s * 0.01))
    return img.resize((size, size), Image.LANCZOS)


def make_footswitch():
    on, off = footswitch_frame(True), footswitch_frame(False)
    sheet = Image.new("RGBA", (on.width, on.height * 2), (0, 0, 0, 0))
    sheet.paste(on, (0, 0))
    sheet.paste(off, (0, on.height))
    sheet.save(os.path.join(OUT, "footswitch-dopplerit.png"), optimize=True)


if __name__ == "__main__":
    os.makedirs(OUT, exist_ok=True)
    make_knob()
    make_footswitch()
    print("modgui images written to", os.path.normpath(OUT))
