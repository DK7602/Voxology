"""DeepFilterNet3 (MIT / Apache-2.0) around the soniqo ONNX graph, following libDF's DSP (lib.rs, enhance.py)."""
import numpy as np, onnxruntime as ort, time
from scipy.signal import lfilter

SR, N, H, NB_ERB, NB_DF, ORDER, LA = 48000, 960, 480, 32, 96, 5, 2
ALPHA = 0.99

def freq2erb(f): return 9.265 * np.log1p(f / (24.7 * 9.265))
def erb2freq(e): return 24.7 * 9.265 * (np.exp(e / 9.265) - 1.0)

def erb_widths(sr=SR, n=N, nb=NB_ERB, min_nb=2):
    lo, hi = freq2erb(0.0), freq2erb(sr / 2)
    step = (hi - lo) / nb; fw = sr / n
    erb = []; prev = 0; over = 0
    for i in range(1, nb + 1):
        fb = int(round(erb2freq(lo + i * step) / fw))
        k = fb - prev - over
        if k < min_nb: over = min_nb - k; k = min_nb
        else: over = 0
        erb.append(k); prev = fb
    erb[-1] += 1
    extra = sum(erb) - (n // 2 + 1)
    if extra > 0: erb[-1] -= extra
    return np.array(erb)

W = np.sin(0.5 * np.pi * np.sin(0.5 * np.pi * (np.arange(N) + 0.5) / (N // 2)) ** 2)
WIDTHS = erb_widths()
BAND = np.repeat(np.arange(NB_ERB), WIDTHS)          # bin -> band
_sess = {}

def session(threads=1):
    if threads not in _sess:
        o = ort.SessionOptions(); o.intra_op_num_threads = threads; o.inter_op_num_threads = 1
        _sess[threads] = ort.InferenceSession("/tmp/claude-0/ai/hf/soniqo/deepfilter.onnx", o, providers=["CPUExecutionProvider"])
    return _sess[threads]

def ema(x, init):   # s_t = (1-a) x_t + a s_{t-1}, along axis 0
    y, _ = lfilter([1 - ALPHA], [1, -ALPHA], x, axis=0, zi=(ALPHA * init)[None, :])
    return y

def enhance(x, atten_lim_db=None, shift_feat=False, threads=1, timing=None, identity=False):
    """x: mono float at 48 kHz -> enhanced, same length and alignment."""
    n0 = len(x)
    xp = np.concatenate([np.zeros(N - H), x, np.zeros(N)])       # analysis memory + the 960 tail
    T = (len(xp) - (N - H)) // H
    idx = np.arange(T)[:, None] * H + np.arange(N)[None, :]
    X = np.fft.rfft(xp[idx] * W, axis=1) / N                      # wnorm = 1 / (N^2 / (2H)) * ... (= 1/N for 50 %)
    X = X * (N / (N * N / (2 * H)))                               # exact libDF wnorm
    pw = np.abs(X) ** 2
    e = np.zeros((T, NB_ERB)); np.add.at(e.T, BAND, pw.T); e /= WIDTHS[None, :]
    e = 10 * np.log10(e + 1e-10)
    e = (e - ema(e, np.linspace(-60, -90, NB_ERB))) / 40.0
    s = ema(np.abs(X[:, :NB_DF]), np.linspace(0.001, 0.0001, NB_DF))
    c = X[:, :NB_DF] / np.sqrt(s)
    fe = e.astype(np.float32)[None, None]
    fs = np.stack([c.real, c.imag]).astype(np.float32)[None]
    if shift_feat:
        fe = np.concatenate([fe[:, :, LA:], np.zeros_like(fe[:, :, :LA])], axis=2)
        fs = np.concatenate([fs[:, :, LA:], np.zeros_like(fs[:, :, :LA])], axis=2)
    t0 = time.perf_counter()
    m, coefs = session(threads).run(["erb_mask", "df_coefs"], {"feat_erb": fe, "feat_spec": fs})
    if timing is not None: timing.append(time.perf_counter() - t0)
    m = m[0, 0]; cf = coefs[0, ..., 0] + 1j * coefs[0, ..., 1]   # [5, T, 96]
    Y = X * m[:, BAND]
    Xpad = np.concatenate([np.zeros((ORDER - 1 - LA, NB_DF)), X[:, :NB_DF], np.zeros((LA, NB_DF))])
    Y[:, :NB_DF] = sum(Xpad[n:n + T] * cf[n] for n in range(ORDER))
    if identity: Y = X.copy()
    if atten_lim_db:
        lim = 10 ** (-abs(atten_lim_db) / 20); Y = X * lim + Y * (1 - lim)
    fr = np.fft.irfft(Y, n=N, axis=1) * N * W
    out = np.zeros(T * H + N)
    for t in range(T): out[t * H:t * H + N] += fr[t]
    # frame t's first half is output t (analysis mem = previous hop); delay N - H
    return out[(N - H):(N - H) + n0]
