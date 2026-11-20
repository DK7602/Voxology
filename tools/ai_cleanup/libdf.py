"""Pure-Python stand-in for the parts of DeepFilterNet's Rust `libdf` the model code needs (erb widths, norm init)."""
import numpy as np
MEAN_NORM_INIT = (-60.0, -90.0); UNIT_NORM_INIT = (0.001, 0.0001)
def _freq2erb(f): return 9.265 * np.log1p(f / (24.7 * 9.265))
def _erb2freq(e): return 24.7 * 9.265 * (np.exp(e / 9.265) - 1.0)
def erb_widths(sr=48000, n=960, nb=32, min_nb=2):
    lo, hi = _freq2erb(0.0), _freq2erb(sr / 2); step = (hi - lo) / nb; fw = sr / n
    erb = []; prev = 0; over = 0
    for i in range(1, nb + 1):
        fb = int(round(float(_erb2freq(lo + i * step)) / fw)); k = fb - prev - over
        if k < min_nb: over = min_nb - k; k = min_nb
        else: over = 0
        erb.append(k); prev = fb
    erb[-1] += 1; extra = sum(erb) - (n // 2 + 1)
    if extra > 0: erb[-1] -= extra
    return np.array(erb, dtype=np.uint64)
class DF:
    def __init__(self, sr=48000, fft_size=960, hop_size=480, nb_bands=32, min_nb_erb_freqs=2):
        self._sr, self._n, self._h, self._nb, self._min = sr, fft_size, hop_size, nb_bands, min_nb_erb_freqs
    def erb_widths(self): return erb_widths(self._sr, self._n, self._nb, self._min)
    def fft_size(self): return self._n
    def hop_size(self): return self._h
    def sr(self): return self._sr
def unit_norm_init(num_freq_bins):
    return np.linspace(UNIT_NORM_INIT[0], UNIT_NORM_INIT[1], num_freq_bins, dtype=np.float32).reshape(1, num_freq_bins)
