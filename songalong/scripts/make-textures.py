"""Generates the UI textures: liquid gold and frosted glass (green, black, white, cream).

    python3 scripts/make-textures.py      -> public/textures/*.webp

Pure numpy + Pillow, deterministic (fixed seeds), so the art can be regenerated or tweaked.
Glass tiles are seamless (periodic noise) so panels can repeat them.
"""
import os
import numpy as np
from PIL import Image

OUT = os.path.join(os.path.dirname(__file__), "..", "public", "textures")
os.makedirs(OUT, exist_ok=True)


def box_blur(a, r, wrap=False):
    """Separable box blur (3 passes ~ Gaussian)."""
    r = int(max(1, r))
    for _ in range(3):
        for axis in (0, 1):
            pad = [(0, 0), (0, 0)]
            pad[axis] = (r + 1, r)
            c = np.cumsum(np.pad(a, pad, mode="wrap" if wrap else "edge"), axis=axis)
            if axis == 0:
                a = (c[2 * r + 1:] - c[:-2 * r - 1]) / (2 * r + 1)
            else:
                a = (c[:, 2 * r + 1:] - c[:, :-2 * r - 1]) / (2 * r + 1)
    return a


def value_noise(h, w, cells, seed, periodic=False):
    """Smooth noise from a random grid (float bicubic upsampling). periodic=True tiles seamlessly."""
    r = np.random.default_rng(seed)
    gh = max(2, cells)
    gw = max(2, int(round(cells * w / h)))
    grid = r.random((gh, gw)).astype(np.float32)
    if periodic:
        big = np.tile(grid, (3, 3))
        img = Image.fromarray(big, mode="F").resize((w * 3, h * 3), Image.BICUBIC)
        a = np.asarray(img, dtype=np.float32)[h:2 * h, w:2 * w]
        return box_blur(a, h / gh / 10, wrap=True)
    pad = np.pad(grid, ((0, 3), (0, 3)), mode="reflect")
    img = Image.fromarray(pad, mode="F").resize((w * (gw + 3) // gw, h * (gh + 3) // gh), Image.BICUBIC)
    a = np.asarray(img, dtype=np.float32)[:h, :w]
    return box_blur(a, h / gh / 10)


def fbm(h, w, base, octaves, seed, periodic=False, gain=0.5):
    out = np.zeros((h, w), np.float32)
    amp, tot = 1.0, 0.0
    for o in range(octaves):
        out += amp * value_noise(h, w, base * 2 ** o, seed + o * 31, periodic)
        tot += amp
        amp *= gain
    return out / tot


def sample(img, y, x):
    """Bilinear sample of img at float coords (wrapping)."""
    h, w = img.shape
    y = np.mod(y, h - 1)
    x = np.mod(x, w - 1)
    y0 = np.floor(y).astype(int)
    x0 = np.floor(x).astype(int)
    fy = y - y0
    fx = x - x0
    y1 = np.minimum(y0 + 1, h - 1)
    x1 = np.minimum(x0 + 1, w - 1)
    return img[y0, x0] * (1 - fx) * (1 - fy) + img[y0, x1] * fx * (1 - fy) + img[y1, x0] * (1 - fx) * fy + img[y1, x1] * fx * fy


def normals(height):
    gy, gx = np.gradient(height)
    nz = 1.0 / np.sqrt(gx * gx + gy * gy + 1.0)
    return -gx * nz, -gy * nz, nz


def liquid_gold(h, w, seed):
    """Molten gold: big swirling folds, bright yellow with amber shadows and white gloss."""
    n1 = fbm(h, w, 2, 3, seed)
    n2 = fbm(h, w, 2, 3, seed + 100)
    n3 = fbm(h, w, 3, 3, seed + 200)
    yy, xx = np.mgrid[0:h, 0:w].astype(np.float32)
    s = min(h, w)
    qy = yy + (n1 - 0.5) * s * 1.4
    qx = xx + (n2 - 0.5) * s * 1.4
    v = sample(n3, qy, qx)
    v = sample(n1, yy + (v - 0.5) * s * 0.9, xx + (sample(n2, qy, qx) - 0.5) * s * 0.9)
    v = (v - v.min()) / (v.max() - v.min())
    height = v + 0.12 * np.sin(v * np.pi * 6.0)
    height = box_blur(height.astype(np.float32), 3)
    nx, ny, nz = normals(height * 260.0)
    env = np.clip(0.6 - ny * 0.5 - nx * 0.3, 0, 1)  # reflected sky: bright from the top-left
    light = np.array([-0.35, -0.55, 0.76])
    light /= np.linalg.norm(light)
    hv = light + np.array([0, 0, 1.0])
    hv /= np.linalg.norm(hv)
    spec = np.clip(nx * hv[0] + ny * hv[1] + nz * hv[2], 0, 1) ** 22
    stops = [
        (0.0, (150, 72, 4)),
        (0.35, (226, 140, 16)),
        (0.62, (250, 196, 40)),
        (0.85, (255, 228, 110)),
        (1.0, (255, 246, 200)),
    ]
    col = np.zeros((h, w, 3), np.float32)
    for (t0, c0), (t1, c1) in zip(stops, stops[1:]):
        m = (env >= t0) & (env <= t1)
        f = ((env - t0) / (t1 - t0))[m][:, None]
        col[m] = np.array(c0) * (1 - f) + np.array(c1) * f
    col += spec[..., None] * np.array([255, 252, 235]) * 0.9
    return np.clip(col, 0, 255)


def frosted(h, w, seed, base, shade_dark, shade_light, periodic=True, blotch=0.5, cells=None, relief=1.0):
    """Frosted / hammered glass: soft rounded ripples, gently lit, over broad light / dark patches."""
    c = cells or max(16, h // 14)
    fine = fbm(h, w, c, 2, seed, periodic, gain=0.45)
    ripple = fbm(h, w, max(5, c // 3), 2, seed + 7, periodic)
    height = box_blur(fine * 0.7 + ripple * 0.3, max(1, h // (c * 3)), wrap=periodic)
    nx, ny, nz = normals(height * 30.0 * relief)
    light = np.array([0.45, -0.55, 0.7])
    light /= np.linalg.norm(light)
    diff = np.clip(nx * light[0] + ny * light[1] + nz * light[2], 0, 1)
    hv = light + np.array([0, 0, 1.0])
    hv /= np.linalg.norm(hv)
    spec = np.clip(nx * hv[0] + ny * hv[1] + nz * hv[2], 0, 1) ** 40
    soft = fbm(h, w, 2, 2, seed + 50, periodic)  # light / dark patches seen through the glass
    soft = (soft - soft.min()) / (soft.max() - soft.min() + 1e-6)
    ripple_t = np.clip(0.5 + (diff - diff.mean()) * 1.1, 0, 1)
    t = ripple_t * (1 - blotch) + soft * blotch
    base = np.array(base, np.float32)
    col = np.where(
        t[..., None] < 0.5,
        np.array(shade_dark) + (base - np.array(shade_dark)) * (t[..., None] / 0.5),
        base + (np.array(shade_light) - base) * ((t[..., None] - 0.5) / 0.5),
    )
    col += spec[..., None] * 35
    return np.clip(col, 0, 255)


def save(arr, name, q=82):
    path = os.path.join(OUT, name)
    Image.fromarray(np.clip(arr, 0, 255).astype(np.uint8)).save(path, "WEBP", quality=q, method=6)
    print(name, os.path.getsize(path) // 1024, "KB")


# remove art from earlier versions
for old in ("panel-dark.webp", "panel-light.webp"):
    p = os.path.join(OUT, old)
    if os.path.exists(p):
        os.remove(p)

H, W = 1080, 1920
gold = liquid_gold(H, W, 11)
yy, xx = np.mgrid[0:H, 0:W].astype(np.float32)
flow = fbm(H, W, 2, 4, 42)

green_glass = frosted(H, W, 21, base=(46, 132, 58), shade_dark=(18, 84, 30), shade_light=(116, 186, 116), periodic=False, cells=90)

# Light theme: liquid gold with green frosted glass coming in at the bottom right.
m = np.clip(((xx / W) * 0.9 + (yy / H) * 0.8 - 1.3 + (flow - 0.5) * 0.7) * 6.0, 0, 1)[..., None]
save(gold * (1 - m) + green_glass * m, "bg-light.webp")

# Dark theme: deep green frosted glass, gold pouring in at the top-left, the left and right edges.
deep_green = frosted(H, W, 23, base=(18, 70, 34), shade_dark=(6, 30, 14), shade_light=(52, 120, 66), periodic=False, cells=90)
tl = 0.42 - np.hypot(xx / W * 1.1, yy / H * 0.9)
left = 0.06 - xx / W
right = (xx / W) - 0.955
bottom_left = 0.25 - np.hypot(xx / W, (H - yy) / H * 1.4)
gm = np.clip((np.maximum.reduce([tl, left, right, bottom_left]) + (flow - 0.5) * 0.18) * 10.0, 0, 1)[..., None]
save(deep_green * (1 - gm) + gold * gm, "bg-dark.webp")

# Seamless glass tiles for panels and controls.
T = 512
save(frosted(T, T, 31, base=(244, 244, 242), shade_dark=(222, 223, 222), shade_light=(255, 255, 255), blotch=0.35), "glass-white.webp", 80)
save(frosted(T, T, 33, base=(236, 222, 196), shade_dark=(206, 186, 152), shade_light=(250, 243, 226), blotch=0.35), "glass-cream.webp", 80)
save(frosted(T, T, 35, base=(34, 36, 35), shade_dark=(12, 13, 13), shade_light=(70, 72, 71), blotch=0.45), "glass-black.webp", 80)
save(frosted(T, T, 37, base=(40, 126, 56), shade_dark=(16, 78, 30), shade_light=(108, 180, 108), blotch=0.45), "glass-green.webp", 80)
save(gold[200:712, 300:812], "gold-tile.webp", 82)
