"""DeepFilterNet3's DSP around the network (libDF): STFT, features, synthesis. numpy; shared by inference and training."""
import numpy as np
from scipy.signal import lfilter
SR, N, H, NB_ERB, NB_DF = 48000, 960, 480, 32, 96
ALPHA = 0.99
def _freq2erb(f): return 9.265 * np.log1p(f / (24.7 * 9.265))
def _erb2freq(e): return 24.7 * 9.265 * (np.exp(e / 9.265) - 1.0)
def erb_widths():
    lo, hi = _freq2erb(0.0), _freq2erb(SR / 2); step = (hi - lo) / NB_ERB; fw = SR / N
    erb = []; prev = 0; over = 0
    for i in range(1, NB_ERB + 1):
        fb = int(round(float(_erb2freq(lo + i * step)) / fw)); k = fb - prev - over
        if k < 2: over = 2 - k; k = 2
        else: over = 0
        erb.append(k); prev = fb
    erb[-1] += 1; extra = sum(erb) - (N // 2 + 1)
    if extra > 0: erb[-1] -= extra
    return np.array(erb)
W = np.sin(0.5 * np.pi * np.sin(0.5 * np.pi * (np.arange(N) + 0.5) / (N // 2)) ** 2)
WIDTHS = erb_widths(); BAND = np.repeat(np.arange(NB_ERB), WIDTHS)
def _ema(x, init):
    y, _ = lfilter([1 - ALPHA], [1, -ALPHA], x, axis=0, zi=(ALPHA * init)[None, :]); return y
def stft(x, tail=True):
    xp = np.concatenate([np.zeros(N - H), x, np.zeros(N if tail else 0)])
    T = (len(xp) - (N - H)) // H
    idx = np.arange(T)[:, None] * H + np.arange(N)[None, :]
    return np.fft.rfft(xp[idx] * W, axis=1) / N
def features(X):
    pw = np.abs(X) ** 2; T = X.shape[0]
    e = np.zeros((T, NB_ERB)); np.add.at(e.T, BAND, pw.T); e /= WIDTHS[None, :]
    e = 10 * np.log10(e + 1e-10)
    e = (e - _ema(e, np.linspace(-60, -90, NB_ERB))) / 40.0
    s = _ema(np.abs(X[:, :NB_DF]), np.linspace(0.001, 0.0001, NB_DF))
    c = X[:, :NB_DF] / np.sqrt(s)
    return e.astype(np.float32), np.stack([c.real, c.imag], -1).astype(np.float32)   # [T,32], [T,96,2]
def istft(Y, n0):
    T = Y.shape[0]; fr = np.fft.irfft(Y, n=N, axis=1) * N * W
    out = np.zeros(T * H + N)
    for t in range(T): out[t * H:t * H + N] += fr[t]
    return out[(N - H):(N - H) + n0]
