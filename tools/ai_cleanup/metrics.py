import numpy as np
SR = 48000
def sisdr(ref, est):
    a = np.dot(est, ref) / np.dot(ref, ref); t = a * ref; e = est - t
    return 10 * np.log10(np.sum(t ** 2) / np.sum(e ** 2))
def frames(x, n=2048, h=1024):
    T = (len(x) - n) // h; idx = np.arange(T)[:, None] * h + np.arange(n)
    return np.fft.rfft(x[idx] * np.hanning(n), axis=1)
def report(clean, inp, out):
    C, I, O = (np.abs(frames(v)) ** 2 for v in (clean, inp, out))
    ce = C.sum(1); thr = ce.max() * 10 ** (-4.5)          # voiced: within 45 dB of the loudest frame
    voiced, quiet = ce > thr * 30, ce < thr / 30           # quiet: gaps (> 15 dB under the voiced threshold)
    f = np.fft.rfftfreq(2048, 1 / SR)
    def band(M, lo, hi, sel): return 10 * np.log10(M[sel][:, (f >= lo) & (f < hi)].sum() + 1e-20)
    g = 10 * np.log10(C[voiced].sum() / O[voiced].sum())    # level match on the voiced frames
    r = {"sisdr_in": sisdr(clean, inp), "sisdr_out": sisdr(clean, out)}
    r["gap_drop_db"] = band(O, 50, 20000, quiet) - band(I, 50, 20000, quiet) if quiet.any() else float('nan')
    for name, lo, hi in [("low", 80, 300), ("mid", 300, 2000), ("pres", 2000, 6000), ("air", 6000, 16000)]:
        r["tone_" + name] = band(O, lo, hi, voiced) + g - band(C, lo, hi, voiced)
    sel = (f > 80) & (f < 16000)
    lsd = np.sqrt(np.mean((10 * np.log10(O[voiced][:, sel] * 10 ** (g / 10) + 1e-12) - 10 * np.log10(C[voiced][:, sel] + 1e-12)) ** 2, 1))
    lsdi = np.sqrt(np.mean((10 * np.log10(I[voiced][:, sel] + 1e-12) - 10 * np.log10(C[voiced][:, sel] + 1e-12)) ** 2, 1))
    r["lsd_in"], r["lsd_out"] = float(np.mean(lsdi)), float(np.mean(lsd))
    return r
