import csv, numpy as np, sys
def load(p): return {r['name']: r for r in csv.DictReader(open(p))}
a, b = load('study_real.csv'), load('study_split.csv')
ids = [i for i in a if i in b and float(a[i]['voiced']) >= 3 and float(b[i]['voiced']) >= 3]
print('songs', len(ids))
def d(k, lo=-100):
    v = []
    for i in ids:
        x, y = float(a[i][k]), float(b[i][k])
        if x > lo and y > lo: v.append(y - x)
    v = np.array(v)
    return len(v), np.median(v), np.percentile(np.abs(v), 50), np.percentile(np.abs(v), 90)
for k in ['sib', 'microDyn', 'tail', 'range', 'lowBass', 'f0']:
    n, med, ad50, ad90 = d(k)
    print('%-9s n=%3d  split-real median %+6.2f  |diff| P50 %5.2f  P90 %5.2f' % (k, n, med, ad50, ad90))
bands = [k for k in a[ids[0]] if k.startswith('b')]
print('tone bands (split - real): median / P90 |diff|')
for k in bands:
    v = np.array([float(b[i][k]) - float(a[i][k]) for i in ids])
    print('  %-7s %+6.2f  %5.2f' % (k, np.median(v), np.percentile(np.abs(v), 90)))
# whole-shape error: mean abs over bands 100 Hz - 10 kHz per song
fb = [k for k in bands if 100 <= float(k[1:]) <= 10000]
e = np.array([np.mean([abs(float(b[i][k]) - float(a[i][k])) for k in fb]) for i in ids])
print('tone shape error per song (mean |dB| 100 Hz-10 kHz): P50 %.2f P90 %.2f' % (np.median(e), np.percentile(e, 90)))
