"""Plug-in-side safety net: the AI looks at the take plus a faint hiss above 8 kHz; its mask and filter go on the real take."""
import sys; sys.path.insert(0, '/tmp/claude-0/ai/kcode'); sys.path.insert(0, '/tmp/claude-0/ai')
import numpy as np, torch, dfdsp
from scipy.signal import butter, sosfiltfilt
_W = {}
def hiss(n, seed=1):
    if n not in _W:
        w = sosfiltfilt(butter(6, 8000, 'hp', fs=48000, output='sos'), np.random.default_rng(seed).standard_normal(n)); _W[n] = w / np.std(w)
    return _W[n]
def enhance_fd(m, x, lvl_db=-50.0):
    X = dfdsp.stft(x); fe, fs = dfdsp.features(dfdsp.stft(x + hiss(len(x)) * 10 ** (lvl_db / 20)))
    spec = torch.from_numpy(np.stack([X.real, X.imag], -1).astype(np.float32))[None, None]
    with torch.no_grad(): o = m(spec, torch.from_numpy(fe)[None, None], torch.from_numpy(fs)[None, None])[0][0, 0].numpy()
    return dfdsp.istft(o[..., 0] + 1j * o[..., 1], len(x))
