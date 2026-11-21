"""Round-3 retest: every case (incl. the clean take) with and without the hiss safety net.
Args: <pt> <tag>. Writes out/{case}_{tag}.f32, out/{case}_{tag}fd.f32 and out/results3_{tag}.json."""
import sys, json
sys.path.insert(0, '/tmp/claude-0/ai/kcode'); sys.path.insert(0, '/tmp/claude-0/ai'); sys.path.insert(0, '/tmp/claude-0/ai/train')
import numpy as np, metrics
from retest import ld, dropouts, gaps, S, O, model_from, enhance
from featdither import enhance_fd
pt, tag = sys.argv[1], sys.argv[2]
m = model_from(pt if pt != '-' else None); rows = []
for v in ['fsung', 'msung', 'frap', 'mrap']:
    c = ld(S + v + '_clean.f32')
    for cond in ['noise', 'room', 'both', 'real', 'cleantake']:
        name = f'{v}_{cond}'; x = ld(S + name + '.f32')
        for t, fn in [(tag, enhance), (tag + 'fd', enhance_fd)]:
            y = fn(m, x); y.astype(np.float32).tofile(O + f'{name}_{t}.f32'); r = metrics.report(c, x, y); g = gaps(c, x, y)
            rows.append({'case': name, 'version': t, 'sisdr': round(float(r['sisdr_out']), 2), 'dropouts': round(dropouts(c, y), 3),
                         'gaps_db': None if g is None else round(g, 1), 'air_db': round(float(r['tone_air']), 1)})
            print('%-15s %-7s %6.2f %4.0f%% %6s %5.1f' % (name, t, rows[-1]['sisdr'], rows[-1]['dropouts'] * 100, rows[-1]['gaps_db'], rows[-1]['air_db']), flush=True)
for v in ['real_don', 'real_gallas', 'real_schaf']:
    x = ld(S + v + '.f32')
    for t, fn in [(tag, enhance), (tag + 'fd', enhance_fd)]:
        y = fn(m, x); y.astype(np.float32).tofile(O + f'{v}_{t}.f32')
        rows.append({'case': v, 'version': t, 'dropouts_vs_input': round(dropouts(x, y), 3)}); print(v, t, rows[-1]['dropouts_vs_input'], flush=True)
json.dump(rows, open(O + f'results3_{tag}.json', 'w'), indent=1)
