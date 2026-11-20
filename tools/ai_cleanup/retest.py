"""Retest a fine-tuned DeepFilterNet3 on the step-1 set (+ a held-out real noise) and export its ONNX graph (same contract)."""
import sys, os, json, subprocess, collections
sys.path.insert(0, '/tmp/claude-0/ai/kcode'); sys.path.insert(0, '/tmp/claude-0/ai')
import numpy as np, torch, soundfile as sf
from scipy.signal import resample_poly
import dfdsp, metrics, load
torch.set_num_threads(4)
S, O = '/tmp/claude-0/ai/set/', '/tmp/claude-0/ai/out/'
TAG = sys.argv[2] if len(sys.argv) > 2 else 'sing'
def ld(p): return np.fromfile(p, np.float32).astype(np.float64)
def model_from(sd_path):
    m = load.load_df3('/tmp/claude-0/ai/kcode', '/tmp/claude-0/ai/kcode/model_120.ckpt.best')
    if sd_path: m.load_state_dict(torch.load(sd_path, map_location='cpu'))
    return m.eval()
def enhance(m, x, atten_lim_db=None):
    X = dfdsp.stft(x); fe, fs = dfdsp.features(X)
    spec = torch.from_numpy(np.stack([X.real, X.imag], -1).astype(np.float32))[None, None]
    with torch.no_grad(): o = m(spec, torch.from_numpy(fe)[None, None], torch.from_numpy(fs)[None, None])[0][0, 0].numpy()
    Y = o[..., 0] + 1j * o[..., 1]
    if atten_lim_db: lim = 10 ** (-atten_lim_db / 20); Y = X * lim + Y * (1 - lim)
    return dfdsp.istft(Y, len(x))
def dropouts(clean, out):
    T = len(clean) // 2400; ce = np.array([np.sum(clean[t*2400:(t+1)*2400]**2) for t in range(T)]); oe = np.array([np.sum(out[t*2400:(t+1)*2400]**2) for t in range(T)])
    v = ce > ce.max() * 1e-3; g = np.sum(ce[v]) / np.sum(oe[v]); return float(np.mean(10 * np.log10(oe[v] * g / ce[v] + 1e-20) < -6))
def gaps(clean, inp, out):
    def fe(x): T = len(x) // 2400; return np.array([np.mean(x[t*2400:(t+1)*2400]**2) for t in range(T)]) + 1e-20
    c = fe(clean); q = c < c.max() * 1e-4
    return float(10 * np.log10(fe(out)[q].sum() / fe(inp)[q].sum())) if q.sum() > 3 else None
if __name__ == '__main__':
    m = model_from(sys.argv[1] if len(sys.argv) > 1 and sys.argv[1] != '-' else None)
    rows = []
    # held-out real noise (the DeepFilterNet repo's freesound sample: never used in training)
    nz, nsr = sf.read('/tmp/claude-0/ai/DeepFilterNet/assets/noise_freesound_573577.wav')
    nz = nz.mean(1) if nz.ndim > 1 else nz
    if nsr != 48000: nz = resample_poly(nz, 48000, nsr)
    for v in ['fsung', 'msung', 'frap', 'mrap']:
        c = ld(S + v + '_clean.f32')
        n2 = np.resize(nz, len(c)); fr = c[:len(c)//2400*2400].reshape(-1, 2400); e = np.mean(fr**2, 1); vr = np.sqrt(np.mean(e[e > e.max()*1e-3]))
        x2 = c + n2 / np.std(n2) * vr * 10 ** (-10 / 20)
        x2.astype(np.float32).tofile(S + f'{v}_real.f32')
        for cond in ['noise', 'room', 'both', 'real']:
            name = f'{v}_{cond}'; x = ld(S + name + '.f32'); y = enhance(m, x); y.astype(np.float32).tofile(O + f'{name}_{TAG}.f32')
            r = metrics.report(c, x, y); r['dropouts'] = dropouts(c, y); r['gaps'] = gaps(c, x, y)
            rows.append({'case': name, 'version': TAG, **{k: (None if val is None else round(float(val), 2)) for k, val in r.items()}})
            print(f"{name:12s} sisdr in {r['sisdr_in']:6.2f} out {r['sisdr_out']:6.2f} drop {r['dropouts']:.2f} gaps {r['gaps']} air {r['tone_air']:5.1f}", flush=True)
    for v in ['real_don', 'real_gallas', 'real_schaf']:
        x = ld(S + v + '.f32'); y = enhance(m, x); y.astype(np.float32).tofile(O + f'{v}_{TAG}.f32')
        rows.append({'case': v, 'version': TAG, 'dropouts_vs_input': round(dropouts(x, y), 3)}); print(v, rows[-1])
    json.dump(rows, open(O + f'results_{TAG}.json', 'w'), indent=1)
