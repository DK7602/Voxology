"""One table, same measures for every version: input, Voxology now (classic), original AI, retrained v1, round 2.
Cases: home noise, bedroom, both, held-out real noise, and the clean take alone (should come out untouched)."""
import sys, os, json, subprocess
sys.path.insert(0, '/tmp/claude-0/ai/kcode'); sys.path.insert(0, '/tmp/claude-0/ai')
import numpy as np, metrics
from retest import ld, dropouts, gaps, S, O
SET = {'noise': (18, 0), 'room': (0, 100), 'both': (18, 80), 'real': (18, 0), 'cleantake': (18, 0)}
rows = []
for v in ['fsung', 'msung', 'frap', 'mrap']:
    c = ld(S + v + '_clean.f32')
    for cond, (nz, rm) in SET.items():
        name = f'{v}_{cond}'; x = ld(S + name + '.f32')
        if cond == 'real' and not os.path.exists(O + name + '_classic.f32'): subprocess.run(['/tmp/claude-0/ai/tool/classic', S + name + '.f32', O + name + '_classic.f32', str(nz), str(rm)], check=True, capture_output=True)
        for tag, y in [('input', x), ('classic', ld(O + name + '_classic.f32')), ('ai_orig', ld(O + name + '_orig.f32')), ('ai_sing', ld(O + name + '_sing.f32')), ('ai_r2', ld(O + name + '_r2.f32'))]:
            r = metrics.report(c, x, y)
            rows.append({'case': name, 'version': tag, 'sisdr': round(float(r['sisdr_out']), 2), 'dropouts': round(dropouts(c, y), 3),
                         'gaps_db': None if gaps(c, x, y) is None else round(gaps(c, x, y), 1), 'air_db': round(float(r['tone_air']), 1)})
json.dump(rows, open(O + 'compare.json', 'w'), indent=1)
print('%-12s %-8s %7s %6s %7s %6s' % ('case', 'version', 'sisdr', 'drop', 'gaps', 'air'))
for r in rows: print('%-12s %-8s %7.2f %5.0f%% %7s %6.1f' % (r['case'], r['version'], r['sisdr'], r['dropouts'] * 100, r['gaps_db'], r['air_db']))
