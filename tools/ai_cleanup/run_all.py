import sys, json, subprocess, time; sys.path.insert(0, '/tmp/claude-0/ai')
import numpy as np, df3, metrics
S = '/tmp/claude-0/ai/set/'; O = '/tmp/claude-0/ai/out/'
import os; os.makedirs(O, exist_ok=True)
def ld(p): return np.fromfile(p, np.float32).astype(np.float64)
def classic(name, noise, room):
    r = subprocess.run(['/tmp/claude-0/ai/tool/classic', S + name + '.f32', O + name + '_classic.f32', str(noise), str(room)], capture_output=True, text=True)
    return ld(O + name + '_classic.f32'), r.stdout.strip()
def dropouts(clean, out):
    T = len(clean) // 2400; ce = np.array([np.sum(clean[t*2400:(t+1)*2400]**2) for t in range(T)]); oe = np.array([np.sum(out[t*2400:(t+1)*2400]**2) for t in range(T)])
    v = ce > ce.max() * 10 ** (-3); g = np.sum(ce[v]) / np.sum(oe[v])
    return float(np.mean(10 * np.log10(oe[v] * g / ce[v]) < -6))   # share of voiced 50 ms frames cut by more than 6 dB
SET = {'noise': (18, 0), 'room': (0, 100), 'both': (18, 80)}
rows = []; cpu = {}
for v in ['fsung', 'msung', 'frap', 'mrap']:
    c = ld(S + v + '_clean.f32')
    for cond, (nz, rm) in SET.items():
        name = f'{v}_{cond}'; x = ld(S + name + '.f32')
        yc, info = classic(name, nz, rm); cpu.setdefault('classic', info)
        tm = []; ya = df3.enhance(x, timing=tm); cpu.setdefault('ai_graph_s_per_s', tm[0] / (len(x) / 48000))
        yl = df3.enhance(x, atten_lim_db=12)
        ya.astype(np.float32).tofile(O + name + '_ai.f32'); yl.astype(np.float32).tofile(O + name + '_ai12.f32')
        for tag, y in [('input', x), ('classic', yc), ('ai', ya), ('ai12', yl)]:
            r = metrics.report(c, x, y); r['dropouts'] = dropouts(c, y)
            rows.append({'case': name, 'version': tag, **{k: round(float(val), 2) for k, val in r.items()}})
for v in ['real_don', 'real_gallas', 'real_schaf']:
    x = ld(S + v + '.f32'); yc, _ = classic(v, 18, 60); ya = df3.enhance(x); yl = df3.enhance(x, atten_lim_db=12)
    ya.astype(np.float32).tofile(O + v + '_ai.f32'); yl.astype(np.float32).tofile(O + v + '_ai12.f32')
    for tag, y in [('classic', yc), ('ai', ya), ('ai12', yl)]:
        rows.append({'case': v, 'version': tag, 'dropouts_vs_input': round(dropouts(x, y), 3)})
json.dump({'rows': rows, 'cpu': cpu}, open(O + 'results.json', 'w'), indent=1)
print(cpu)
import collections
agg = collections.defaultdict(list)
for r in rows:
    if 'sisdr_out' in r: agg[(r['case'].split('_')[1], r['version'])].append(r)
for (cond, ver), rs in sorted(agg.items()):
    m = lambda k: round(np.mean([r[k] for r in rs]), 2)
    print(f"{cond:6s} {ver:8s} sisdr {m('sisdr_out'):6.2f}  gaps {m('gap_drop_db'):6.1f} dB  lsd {m('lsd_out'):5.2f}  dropouts {m('dropouts'):.2f}  tone low {m('tone_low'):5.1f} mid {m('tone_mid'):5.1f} pres {m('tone_pres'):5.1f} air {m('tone_air'):5.1f}")
for r in rows:
    if 'dropouts_vs_input' in r: print(r)
