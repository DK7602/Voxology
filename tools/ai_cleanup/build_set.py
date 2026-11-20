"""Test set: 15 s of each clean dry vocal; + home noise, + bedroom, + both. Writes set/<voice>_<cond>.f32 and the clean refs."""
import numpy as np, pyroomacoustics as pra, json
from scipy.signal import fftconvolve, butter, sosfilt
SR = 48000; rng = np.random.default_rng(7)
def load(n): return np.fromfile(f"/tmp/claude-0/ai/src/{n}.f32", dtype=np.float32).astype(np.float64)
def busiest(x, sec=15.0):
    L = int(sec * SR); hop = SR // 2
    best, bi = -1, 0
    for i in range(0, len(x) - L, hop):
        seg = x[i:i + L]; fr = seg[: len(seg) // 2400 * 2400].reshape(-1, 2400)
        act = np.mean(10 * np.log10(np.mean(fr ** 2, 1) + 1e-12) > 10 * np.log10(np.mean(seg ** 2) + 1e-12) - 15)
        if act > best: best, bi = act, i
    return x[bi:bi + L]
def active_rms(x):
    fr = x[: len(x) // 2400 * 2400].reshape(-1, 2400); e = np.mean(fr ** 2, 1)
    return np.sqrt(np.mean(e[e > np.max(e) * 10 ** (-3)]))
def home_noise(n):
    t = np.arange(n) / SR
    pink = np.cumsum(rng.standard_normal(n)); pink -= np.convolve(pink, np.ones(4801) / 4801, 'same')   # brownish, no DC
    fan = sosfilt(butter(2, [80, 1800], 'band', fs=SR, output='sos'), rng.standard_normal(n)) * 1.0 + sosfilt(butter(2, 400, 'low', fs=SR, output='sos'), pink) * 0.02
    fan *= 1 + 0.15 * np.sin(2 * np.pi * 0.7 * t)                                                    # slow fan swirl
    hum = sum(a * np.sin(2 * np.pi * f * t + rng.uniform(0, 6)) for f, a in [(60, 1), (120, .6), (180, .5), (240, .25), (300, .15)])
    hiss = sosfilt(butter(1, 3000, 'high', fs=SR, output='sos'), rng.standard_normal(n))
    def rms(v): return np.sqrt(np.mean(v ** 2))
    return fan / rms(fan) * 1.0 + hum / rms(hum) * 0.35 + hiss / rms(hiss) * 0.45
def bedroom_rir():
    rt60 = 0.55; room_dim = [3.6, 3.0, 2.5]
    e_abs, max_order = pra.inverse_sabine(rt60, room_dim)
    room = pra.ShoeBox(room_dim, fs=SR, materials=pra.Material(e_abs), max_order=max_order)
    room.add_source([1.2, 1.5, 1.6]); room.add_microphone([1.5, 1.47, 1.6]); room.compute_rir()
    h = np.array(room.rir[0][0]); d = int(np.argmax(np.abs(h)))
    return h, d
h, d = bedroom_rir()
direct = np.abs(h[d]); drr = 10 * np.log10(np.sum(h[max(0, d - 48):d + 120] ** 2) / np.sum(h[d + 120:] ** 2))
meta = {"rt60_s": 0.55, "room_m": [3.6, 3.0, 2.5], "mic_dist_m": 0.3, "drr_db": round(float(drr), 1), "snr_db": 15}
for v in ["fsung", "msung", "frap", "mrap"]:
    x = busiest(load(v)); x = x / np.max(np.abs(x)) * 0.5
    x.astype(np.float32).tofile(f"/tmp/claude-0/ai/set/{v}_clean.f32")
    n = home_noise(len(x)); n *= active_rms(x) / np.sqrt(np.mean(n ** 2)) * 10 ** (-15 / 20)
    wet = fftconvolve(x, h)[d:d + len(x)] / direct            # aligned with the dry, direct path at unity
    for cond, y in [("noise", x + n), ("room", wet), ("both", wet + n)]:
        y = y / np.max(np.abs(y)) * 0.5 * (np.max(np.abs(y)) / np.max(np.abs(y)))
        y.astype(np.float32).tofile(f"/tmp/claude-0/ai/set/{v}_{cond}.f32")
for v in ["real_don", "real_gallas", "real_schaf"]:
    x = busiest(load(v)); x.astype(np.float32).tofile(f"/tmp/claude-0/ai/set/{v}.f32")
json.dump(meta, open("/tmp/claude-0/ai/set/meta.json", "w")); print(meta)
