# The SongAlong app icons, from the round logo in scripts/icon-source.webp:
#   python3 scripts/make-icons.py scripts/icon-source.webp public
# A true circle on transparency (the supplied ring is a little oval and has glints outside it),
# and on black where a platform shows no transparency (iPhone home screen, Android masked icons).
import sys
import numpy as np
from PIL import Image

src, out = sys.argv[1], sys.argv[2]
im = Image.open(src).convert("RGBA")
cx, cy, rx, ry, pad = 625.0, 613.0, 522.0, 504.0, 6.0  # the ring's centre and outer edge in the source (measured)
box = (round(cx - rx - pad), round(cy - ry - pad), round(cx + rx + pad), round(cy + ry + pad))
w = box[2] - box[0]
disc = im.crop(box).resize((w, w), Image.LANCZOS)  # stretched to a true circle

# a clean round edge just outside the ring (anti-aliased): nothing outside it
a = np.asarray(disc).astype(np.float32)
yy, xx = np.mgrid[0:w, 0:w] + 0.5
dist = np.hypot(xx - w / 2, yy - w / 2)
edge = rx + 3.0
a[..., 3] *= np.clip(edge - dist + 0.5, 0, 1)
disc = Image.fromarray(a.round().astype(np.uint8), "RGBA")
side = int(2 * edge) + 2
off = (w - side) // 2
master = disc.crop((off, off, off + side, off + side)).resize((1024, 1024), Image.LANCZOS)

def round_icon(size):
    return master.resize((size, size), Image.LANCZOS)

def on_black(size, share):
    """The round logo, `share` of the width across, centred on black (no transparency)."""
    bg = Image.new("RGBA", (size, size), (0, 0, 0, 255))
    d = round(size * share)
    logo = master.resize((d, d), Image.LANCZOS)
    bg.alpha_composite(logo, ((size - d) // 2, (size - d) // 2))
    return bg.convert("RGB")

round_icon(64).save(f"{out}/favicon.png", optimize=True)
round_icon(192).save(f"{out}/icon-192.png", optimize=True)
round_icon(512).save(f"{out}/icon-512.png", optimize=True)
# Android masks these to a circle or rounded square: the logo stays inside the 80% safe circle
on_black(512, 0.80).save(f"{out}/icon-maskable-512.png", optimize=True)
# an iPhone shows no transparency (it would turn black anyway) and rounds the corners itself
on_black(180, 0.94).save(f"{out}/apple-touch-icon.png", optimize=True)
print("made", side, "px disc")
