"""Why does the trained model turn down the female singer's quieter held notes on a clean take? Change one thing at a time."""
import sys, glob, zipfile, io; sys.path.insert(0, '/tmp/claude-0/ai/kcode'); sys.path.insert(0, '/tmp/claude-0/ai')
import numpy as np, soundfile as sf
from scipy.signal import resample_poly
from retest import ld, dropouts, S, model_from, enhance
M = model_from(sys.argv[1] if len(sys.argv) > 1 else '/tmp/claude-0/ai/kout_r2/best.pt')
c = ld(S + 'fsung_clean.f32'); FS = 48000
fr = c[:len(c)//2400*2400].reshape(-1, 2400); e = np.mean(fr**2, 1); vr = np.sqrt(np.mean(e[e > e.max()*1e-3]))
def d(x, ref=None): return '%3.0f%%' % (100 * dropouts(x if ref is None else ref, enhance(M, x)))
def row(k, v): print('%-58s %s' % (k, v), flush=True)
rng = np.random.default_rng(0)
print('peak %.1f dBFS, voice rms %.1f dBFS' % (20*np.log10(np.abs(c).max()), 20*np.log10(vr)))
row('as is', d(c))
for g in (12, 6, -6, -12, -20): row('level %+d dB' % g, d(c * 10**(g/20)))
for snr in (50, 40, 30, 25, 20): row('+ steady pink-ish hiss, SNR %d dB' % snr, d(c + np.cumsum(rng.standard_normal(len(c)))*0 + rng.standard_normal(len(c)) * vr * 10**(-snr/20), c) if False else '')
def pink(n):
    X = np.fft.rfft(rng.standard_normal(n)); f = np.fft.rfftfreq(n, 1/FS); f[0] = f[1]; return np.fft.irfft(X / np.sqrt(f), n)
for snr in (50, 40, 30, 25, 20):
    nz = pink(len(c)); x = c + nz / np.std(nz) * vr * 10**(-snr/20); y = enhance(M, x); row('+ steady pink noise, SNR %d dB' % snr, '%3.0f%%' % (100*dropouts(c, y)))
# context: what comes before the note
seg = c[int(4.0*FS):int(6.0*FS)]
def dseg(pre):
    x = np.concatenate([pre, seg]); y = enhance(M, x); return '%3.0f%%' % (100*dropouts(seg, y[len(pre):]))
row('4.0-6.0 s alone', dseg(np.zeros(0)))
row('4.0-6.0 s after 3 s of silence', dseg(np.zeros(3*FS)))
row('4.0-6.0 s after 2 s of her loud singing (0-2 s)', dseg(c[:2*FS]))
row('4.0-6.0 s after 2 s of pink noise', dseg(pink(2*FS) * vr * 0.3))
# pitch / vibrato
for st in (-3, -1, 1, 3):
    r = 2 ** (st / 12); x = resample_poly(c, 1000, int(round(1000 * r)))
    row('pitch (and formants) %+d semitones' % st, d(x))
def vibrato(x, cents, hz=5.5):
    t = np.arange(len(x)) / FS; ratio = 2 ** (cents / 1200 * np.sin(2*np.pi*hz*t)); pos = np.cumsum(ratio); pos = pos[pos < len(x) - 1]
    return np.interp(pos, np.arange(len(x)), x)
for cents in (15, 40):
    x = vibrato(c, cents); row('+ vibrato +-%d cents' % cents, d(x))
# which part of the spectrum
from scipy.signal import butter, sosfiltfilt
for name, sos in [('high-passed 1 kHz', butter(4, 1000, 'hp', fs=FS, output='sos')), ('low-passed 4 kHz', butter(4, 4000, 'lp', fs=FS, output='sos')),
                  ('low-passed 8 kHz', butter(4, 8000, 'lp', fs=FS, output='sos'))]:
    x = sosfiltfilt(sos, c); row(name, d(x))
