"""Runs Voxology's C++ AI engine (local harness) on the retest set and compares with the Python model + safety net.
Args: [rate] (48000 default; 44100 resamples in and out)."""
import sys, subprocess; sys.path[:0] = ['/tmp/claude-0/ai/kcode', '/tmp/claude-0/ai', '/tmp/claude-0/ai/train']
import numpy as np, metrics
from scipy.signal import resample_poly
from retest import ld, dropouts, gaps, S, O
RATE = int(sys.argv[1]) if len(sys.argv) > 1 else 48000
H = '/tmp/claude-0/ai/cpp/h/'
def run(x, strength=30):
    xi = resample_poly(x, 147, 160) if RATE == 44100 else x
    xi.astype(np.float32).tofile(H + 'ci.f32')
    r = subprocess.run([H + 'run', H + 'ci.f32', H + 'co.f32', str(RATE), str(strength), '1', '0'], capture_output=True, text=True)
    y = np.fromfile(H + 'co.f32', np.float32).astype(np.float64)
    if RATE == 44100: y = resample_poly(y, 160, 147)[:len(x)]
    return y, r.stderr.strip()
rows = []
for v in ['fsung', 'msung', 'frap', 'mrap']:
    c = ld(S + v + '_clean.f32')
    for cond in ['noise', 'room', 'both', 'real', 'cleantake']:
        name = f'{v}_{cond}'; x = ld(S + name + '.f32'); y, info = run(x)
        y.astype(np.float32).tofile(O + f'{name}_cpp{RATE // 1000}.f32')
        r = metrics.report(c, x, y); g = gaps(c, x, y)
        ref = ld(O + f'{name}_r3fd.f32'); rr = metrics.report(c, x, ref)
        print('%-16s C++ %6.2f %4.0f%% gaps %6s air %5.1f | python+net %6.2f %4.0f%%' % (name, r['sisdr_out'], dropouts(c, y) * 100, None if g is None else round(g, 1), r['tone_air'], rr['sisdr_out'], dropouts(c, ref) * 100), flush=True)
print(info)
