"""Listening MP3s: 4 versions in a row, marked by 1/2/3/4 beeps. Default: before, Voxology now, original AI, retrained AI.
Args: <dst> [tag3 tag4] (e.g. sing r2: round 1 and round 2)."""
import subprocess, sys, numpy as np
S, O = '/tmp/claude-0/ai/set/', '/tmp/claude-0/ai/out/'; DST = sys.argv[1]; FS = 48000
T3, T4 = (sys.argv[2], sys.argv[3]) if len(sys.argv) > 3 else ('orig', 'sing')
CASES = [('1-female-singer-home-noise', 'fsung_noise'), ('2-female-rapper-home-noise', 'frap_noise'),
         ('3-male-singer-bedroom-echo-and-noise', 'msung_both'), ('4-male-trap-chant-home-noise', 'mrap_noise'),
         ('5-your-take-don-birthday', 'real_don')] if T4 == 'sing' else [
         ('1-female-singer-home-noise', 'fsung_noise'), ('2-female-singer-clean-take', 'fsung_cleantake'),
         ('3-male-singer-bedroom-echo-and-noise', 'msung_both'), ('4-male-trap-chant-home-noise', 'mrap_noise'),
         ('5-female-rapper-home-noise', 'frap_noise'), ('6-your-take-don-birthday', 'real_don')]
def ld(p): return np.fromfile(p, np.float32).astype(np.float64)
def beeps(k):
    t = np.arange(int(0.08 * FS)) / FS; b = 0.1 * np.sin(2 * np.pi * 1000 * t) * np.hanning(len(t)); gap = np.zeros(int(0.12 * FS))
    return np.concatenate(sum([[b, gap] for _ in range(k)], []))
for title, n in CASES:
    parts = []
    for k, y in enumerate([ld(S + n + '.f32'), ld(O + n + '_classic.f32'), ld(O + n + f'_{T3}.f32'), ld(O + n + f'_{T4}.f32')], 1):
        seg = y[72000:72000 + int(8.5 * FS)].copy(); f = int(0.02 * FS); seg[:f] *= np.linspace(0, 1, f); seg[-f:] *= np.linspace(1, 0, f)
        parts += [beeps(k), np.zeros(int(0.4 * FS)), seg, np.zeros(int(1.0 * FS))]
    a = np.concatenate(parts).astype(np.float32); out = f'{DST}/ai-' + ('retest' if T4 == 'sing' else 'round2') + f'-{title}.mp3'
    subprocess.run(['ffmpeg', '-nostdin', '-v', 'error', '-y', '-f', 'f32le', '-ar', str(FS), '-ac', '1', '-i', '-', '-b:a', '160k', out], input=a.tobytes(), check=True)
    print(out, '%.1f s' % (len(a) / FS))
