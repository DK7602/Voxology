"""Safety net without random hiss: add the hiss's EXPECTED power to each ERB band of the features (deterministic).
Compares with featdither.py (random hiss in the time domain) on the round-3 set."""
import sys; sys.path[:0] = ['/tmp/claude-0/ai/kcode', '/tmp/claude-0/ai', '/tmp/claude-0/ai/train']
import numpy as np, torch, dfdsp
from featdither import hiss
def hiss_bin_power(lvl_db=-50.0, secs=60):
    h = hiss(48000 * secs) * 10 ** (lvl_db / 20); X = dfdsp.stft(h); return np.mean(np.abs(X) ** 2, 0)   # [481]
PB = hiss_bin_power()
def band_floor(pb=PB):
    e = np.zeros(dfdsp.NB_ERB); np.add.at(e, dfdsp.BAND, pb); return e / dfdsp.WIDTHS
FLOOR = band_floor()
def features_floor(X, floor=FLOOR):
    pw = np.abs(X) ** 2; T = X.shape[0]
    e = np.zeros((T, dfdsp.NB_ERB)); np.add.at(e.T, dfdsp.BAND, pw.T); e /= dfdsp.WIDTHS[None, :]
    e = 10 * np.log10(e + floor[None, :] + 1e-10)
    e = (e - dfdsp._ema(e, np.linspace(-60, -90, dfdsp.NB_ERB))) / 40.0
    s = dfdsp._ema(np.abs(X[:, :dfdsp.NB_DF]), np.linspace(0.001, 0.0001, dfdsp.NB_DF))
    c = X[:, :dfdsp.NB_DF] / np.sqrt(s)
    return e.astype(np.float32), np.stack([c.real, c.imag], -1).astype(np.float32)
def enhance_ff(m, x):
    X = dfdsp.stft(x); fe, fs = features_floor(X)
    spec = torch.from_numpy(np.stack([X.real, X.imag], -1).astype(np.float32))[None, None]
    with torch.no_grad(): o = m(spec, torch.from_numpy(fe)[None, None], torch.from_numpy(fs)[None, None])[0][0, 0].numpy()
    return dfdsp.istft(o[..., 0] + 1j * o[..., 1], len(x))
if __name__ == '__main__':
    import metrics
    from retest import ld, dropouts, S, O, model_from
    print('band floor dB:', np.round(10 * np.log10(FLOOR + 1e-30), 1))
    m = model_from('/tmp/claude-0/ai/kout_r3/best.pt')
    for v in ['fsung', 'msung', 'frap', 'mrap']:
        c = ld(S + v + '_clean.f32')
        for cond in ['noise', 'room', 'real', 'cleantake']:
            name = f'{v}_{cond}'; x = ld(S + name + '.f32'); y = enhance_ff(m, x); y.astype(np.float32).tofile(O + f'{name}_r3ff.f32')
            yd = ld(O + f'{name}_r3fd.f32'); r = metrics.report(c, x, y); rd = metrics.report(c, x, yd)
            print('%-16s floor %6.2f %4.0f%%   hiss %6.2f %4.0f%%' % (name, r['sisdr_out'], dropouts(c, y) * 100, rd['sisdr_out'], dropouts(c, yd) * 100), flush=True)
