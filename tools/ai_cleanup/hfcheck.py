"""How often does training-style clean material have voiced moments with (almost) nothing above 8 kHz?"""
import sys, zipfile, os; sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import numpy as np, calib as C
T = C.T; from multiprocessing import Pool
def hf_dead(x):
    T_ = len(x) // 2400; e = []; h = []
    for t in range(T_):
        a = x[t*2400:(t+1)*2400]; A = np.abs(np.fft.rfft(a * np.hanning(2400)))**2; f = np.fft.rfftfreq(2400, 1/48000)
        e.append(A.sum() + 1e-20); h.append(A[(f >= 8000) & (f < 16000)].sum() + 1e-30)
    e, h = np.array(e), np.array(h); v = e > e.max() * 1e-3
    return v.sum(), (v & (10 * np.log10(h / e) < -55)).sum()
if __name__ == "__main__":
    with zipfile.ZipFile(C.ZIP) as z: names = [n for n in z.namelist() if n.endswith(".wav") and "/female9/" not in n and "/male11/" not in n]
    names = list(np.random.default_rng(5).choice(names, 200, replace=False))
    with Pool(4) as p: vs = [c for c in p.map(C._zload, names) if c is not None]
    pk, _ = T.get_packs()
    with Pool(4) as p: pks = [c for c in p.map(T._load, [(f, 48000, None) for f in pk]) if c is not None]
    for name, bank in [("VocalSet", vs), ("packs", pks)]:
        tot = [hf_dead(c.astype(np.float64)) for c in bank]; v = sum(a for a, b in tot); d = sum(b for a, b in tot)
        per = sorted(((b / max(a, 1)), i) for i, (a, b) in enumerate(tot))[::-1][:5]
        print("%-9s voiced moments %d, top end dead (8-16 kHz > 55 dB below) %.1f %%; worst files: %s" % (name, v, 100 * d / v, " ".join("%.0f%%" % (100 * p) for p, i in per)))
    c = np.fromfile("/tmp/claude-0/ai/set/fsung_clean.f32", np.float32).astype(np.float64); a, b = hf_dead(c); print("test female singer: %.1f %%" % (100 * b / a))
