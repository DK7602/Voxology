#!/usr/bin/env python3
"""Easter egg art: makes plugin/ui/assets/*_red.webp from the blue-marble art.

Only blue pixels change (hue 75 - 280 deg: blue plus its grey-green and violet edges, not grey): they get a red hue with the same brightness, a little
more colour so pale blue reads as red, not pink (less on the button marble, so text stays readable). Gold, ivory, white and shadows are untouched.
Run from the repo root: python3 tools/ui/make_red_assets.py
"""
import os
import numpy as np
from PIL import Image

ASSETS = os.path.join(os.path.dirname(__file__), "..", "..", "plugin", "ui", "assets")
# name -> colour boost. The marble tiles sit behind button text, so they stay as light as the blue was.
FILES = {"honeycomb": 1.8, "logo": 1.8, "title": 1.8, "marble_blue": 1.15, "marble_blue2": 1.15, "marble_cream": 1.3,
         "knob_blank": 1.8, "knob_db": 1.8, "knob_hz": 1.8, "knob_q": 1.8, "knob_switch": 1.8}

def ramp(x, a, b):
    return np.clip((x - a) / (b - a), 0.0, 1.0)

def hsv_to_rgb(h, s, v):
    h6 = (h % 360.0) / 60.0
    i = np.floor(h6).astype(int) % 6
    f = h6 - np.floor(h6)
    p, q, t = v * (1 - s), v * (1 - s * f), v * (1 - s * (1 - f))
    r = np.choose(i, [v, q, p, p, t, v]); g = np.choose(i, [t, v, v, q, p, p]); b = np.choose(i, [p, p, t, v, v, q])
    return np.stack([r, g, b], -1)

def redden(rgb, boost):
    r, g, b = rgb[..., 0], rgb[..., 1], rgb[..., 2]
    v = rgb.max(-1); c = v - rgb.min(-1)
    s = np.where(v > 0, c / np.maximum(v, 1e-9), 0.0)
    h = np.where(c == 0, 0.0,
        np.where(v == r, 60 * (((g - b) / np.maximum(c, 1e-9)) % 6),
        np.where(v == g, 60 * ((b - r) / np.maximum(c, 1e-9) + 2), 60 * ((r - g) / np.maximum(c, 1e-9) + 4))))
    w = ramp(h, 75, 105) * (1 - ramp(h, 260, 280)) * ramp(s, 0.005, 0.03)
    red = hsv_to_rgb(356 + (h - 210) * 0.25, np.minimum(1.0, s * boost + 0.04), v)
    return rgb * (1 - w[..., None]) + red * w[..., None]

for name, boost in FILES.items():
    src = os.path.join(ASSETS, name + ".webp")
    im = Image.open(src)
    a = np.asarray(im.convert("RGBA")).astype(np.float64) / 255
    a[..., :3] = redden(a[..., :3], boost)
    out = Image.fromarray(np.round(a * 255).astype(np.uint8), "RGBA")
    if im.mode == "RGB":
        out = out.convert("RGB")
    out.save(os.path.join(ASSETS, name + "_red.webp"), quality=90, method=6)
    print(name + "_red.webp")
