# Fine-tune DeepFilterNet3 (MIT / Apache-2.0) so it keeps sung and rapped vocals while it removes noise and room echo.
# Data (all CC BY 4.0 or generated): VocalSet 1.1 (singing), VCTK (speech), MUSAN noise, procedural noise, simulated rooms.
# Runs on a Kaggle GPU kernel, or locally (SMOKE=1) with whatever data it finds.
import glob, json, math, os, random, sys, time, zipfile, subprocess
from multiprocessing import Pool

import numpy as np
import soundfile as sf
from scipy.signal import resample_poly, fftconvolve, butter, sosfilt, lfilter

ENV = os.environ
SMOKE = ENV.get("SMOKE", "0") == "1"
CODE = ENV.get("DFX_CODE") or next(iter(glob.glob("/kaggle/input/**/dfdsp.py", recursive=True)), "").rsplit("/", 1)[0]
OUT = ENV.get("OUT_DIR", "/kaggle/working")
WORK = ENV.get("WORK_DIR", "/kaggle/temp" if os.path.isdir("/kaggle") else "/tmp/claude-0/ai/work")
HOURS = float(ENV.get("TRAIN_HOURS", "0.02" if SMOKE else "5.0"))
BATCH = int(ENV.get("BATCH", "4" if SMOKE else "32"))
LR = float(ENV.get("LR", "2e-4"))
SEG = 3 * 48000
WORKERS = int(ENV.get("WORKERS", "0" if SMOKE else "4"))
os.makedirs(OUT, exist_ok=True); os.makedirs(WORK, exist_ok=True)
sys.path.insert(0, CODE)
import dfdsp  # noqa: E402

LOG = open(os.path.join(OUT, "log.txt"), "a")
def log(*a):
    s = time.strftime("%H:%M:%S ") + " ".join(str(x) for x in a)
    print(s, flush=True); LOG.write(s + "\n"); LOG.flush()

# ---------------------------------------------------------------- data
def find(pattern):
    return sorted(glob.glob(pattern, recursive=True))

def get_vocalset():
    files = find("/kaggle/input/**/VocalSet*/**/*.wav") + find(os.path.join(WORK, "VocalSet*/**/*.wav")) + find(os.path.join(WORK, "FULL/**/*.wav"))
    if files: return files
    z = ENV.get("VOCALSET_ZIP") or os.path.join(WORK, "VocalSet11.zip")
    if not os.path.exists(z):
        log("downloading VocalSet 1.1 (CC BY 4.0) from Zenodo")
        subprocess.run(["curl", "-sL", "--retry", "5", "-o", z, "https://zenodo.org/records/1203819/files/VocalSet11.zip?download=1"], check=True)
    with zipfile.ZipFile(z) as f: f.extractall(WORK)
    return find(os.path.join(WORK, "**/*.wav"))

def _load(args):
    path, sr_to, max_s = args
    try:
        x, sr = sf.read(path, dtype="float32", always_2d=True)
    except Exception:
        return None
    x = x.mean(1)
    if max_s: x = x[: int(max_s * sr)]
    if sr != sr_to:
        g = math.gcd(sr, sr_to); x = resample_poly(x, sr_to // g, sr // g).astype(np.float32)
    pk = np.max(np.abs(x)) if len(x) else 0
    if pk < 1e-4 or len(x) < 48000: return None
    return (x / pk * 0.5).astype(np.float16)

class Bank:
    """Many clips in one float16 array (fork-friendly)."""
    def __init__(self, clips):
        clips = [c for c in clips if c is not None]
        self.off = np.cumsum([0] + [len(c) for c in clips]); self.n = len(clips)
        self.data = np.concatenate(clips) if clips else np.zeros(1, np.float16)
    def seconds(self): return len(self.data) / 48000
    def clip(self, i): return self.data[self.off[i]:self.off[i + 1]]

def load_bank(paths, max_s=None, procs=4):
    with Pool(procs) as p: clips = p.map(_load, [(f, 48000, max_s) for f in paths], chunksize=8)
    return Bank(clips)

def split_vocalset(files):
    # held out for validation: singers female9 and male11
    val = [f for f in files if "/female9/" in f or "/male11/" in f]
    return [f for f in files if f not in val], val

def get_vctk():
    fs = find("/kaggle/input/**/VCTK-Corpus/**/wav48/**/*.wav") or find("/kaggle/input/**/wav48_silence_trimmed/**/*mic1.flac")
    spk = sorted({f.split("/")[-2] for f in fs})
    rng = random.Random(1); rng.shuffle(spk)
    val_spk, tr_spk = set(spk[:4]), set(spk[4:])
    tr = [f for f in fs if f.split("/")[-2] in tr_spk]; va = [f for f in fs if f.split("/")[-2] in val_spk]
    rng.shuffle(tr); rng.shuffle(va)
    return tr[:3500], va[:120]

def get_packs():
    # the user's licensed Cymatics / Looperman dry stems (AI training allowed, the user checked 2026-11-20); the retest's songs
    # were never uploaded. Held out for validation: the song "Out_Of_Time".
    fs = find("/kaggle/input/**/sonicology-packs/**/*.flac") or find(os.path.join(ENV.get("PACKS_DIR", "/nonexistent"), "**/*.flac"))
    return [f for f in fs if "Out_Of_Time" not in f], [f for f in fs if "Out_Of_Time" in f]

def get_musan():
    fs = find("/kaggle/input/**/musan/noise/**/*.wav")
    tr = [f for f in fs if "/sound-bible/" not in f]; va = [f for f in fs if "/sound-bible/" in f]   # sound-bible held out
    return tr, va

# ---------------------------------------------------------------- rooms
def make_rir(seed):
    import pyroomacoustics as pra
    r = np.random.default_rng(seed); fs = 48000
    dim = [r.uniform(2.4, 7.0), r.uniform(2.2, 6.0), r.uniform(2.2, 3.4)]
    rt60 = r.uniform(0.15, 1.0)
    try:
        e_abs, _ = pra.inverse_sabine(rt60, dim)
        room = pra.ShoeBox(dim, fs=fs, materials=pra.Material(min(max(e_abs, 0.02), 0.99)), max_order=3)
        src = [r.uniform(0.5, dim[0] - 0.5), r.uniform(0.5, dim[1] - 0.5), r.uniform(1.2, 1.9)]
        d = r.uniform(0.1, 1.2); ang = r.uniform(0, 2 * np.pi)
        mic = [min(max(src[0] + d * np.cos(ang), 0.3), dim[0] - 0.3), min(max(src[1] + d * np.sin(ang), 0.3), dim[1] - 0.3), src[2] + r.uniform(-0.2, 0.2)]
        room.add_source(src); room.add_microphone(mic); room.compute_rir()
        h = np.array(room.rir[0][0], dtype=np.float64)
    except Exception:
        return None
    k = int(np.argmax(np.abs(h))); h = h[max(0, k - 24):]; k = min(k, 24)
    # stochastic late tail, decaying faster at high frequencies (rt60 * 1.0 low .. 0.45 high)
    n = int(rt60 * 1.1 * fs) + k + 1
    t = np.arange(n) / fs
    tail = np.zeros(n)
    edges = [0, 250, 1000, 4000, 12000, 24000]
    for i in range(len(edges) - 1):
        lo, hi = max(edges[i], 20), min(edges[i + 1], 23900)
        b = butter(2, [lo, hi], "band", fs=fs, output="sos")
        rt = rt60 * (1.0 - 0.55 * i / (len(edges) - 2))
        tail += sosfilt(b, r.standard_normal(n)) * np.exp(-6.9 * t / rt)
    mix_t = int(0.02 * fs) + k
    ramp = np.clip((np.arange(n) - mix_t) / (0.01 * fs), 0, 1)
    tail *= ramp
    # tail level from the direct-to-reverb ratio implied by the distance and the room (Sabine critical distance)
    V = dim[0] * dim[1] * dim[2]; dc = 0.057 * math.sqrt(V / rt60)
    drr_db = 20 * math.log10(max(dc / max(d, 0.05), 1e-3))
    direct = np.sum(h[max(0, k - 24):k + 24] ** 2)
    tail *= math.sqrt(direct / max(np.sum(tail ** 2), 1e-20) * 10 ** (-drr_db / 10))
    full = np.zeros(max(len(h), n)); full[:len(h)] += h; full[:n] += tail
    early = full.copy(); e_end = k + int(0.02 * fs); fade = int(0.005 * fs)
    early[e_end:e_end + fade] *= np.linspace(1, 0, fade)[: len(early[e_end:e_end + fade])]; early[e_end + fade:] = 0
    g = 1.0 / max(np.abs(full[k]), 1e-6)
    return (full[: int(1.3 * fs)] * g).astype(np.float32), (early[: int(1.3 * fs)] * g).astype(np.float32)

# ---------------------------------------------------------------- procedural noise (not the test set's generator)
def proc_noise(r, n, fs=48000):
    out = np.zeros(n)
    # colored noise with a random slope
    X = np.fft.rfft(r.standard_normal(n)); f = np.fft.rfftfreq(n, 1 / fs); f[0] = f[1]
    X *= (f / 1000.0) ** (r.uniform(-1.0, 0.25)); out += np.fft.irfft(X, n) * r.uniform(0.2, 1.0)
    if r.random() < 0.6:   # mains hum / buzz
        f0 = r.choice([50.0, 60.0]) + r.uniform(-0.3, 0.3); t = np.arange(n) / fs
        nh = r.integers(2, 25); buzz = r.random() < 0.4
        h = sum((1.0 / (k if buzz else k * k)) * np.sin(2 * np.pi * f0 * k * t + r.uniform(0, 6)) for k in range(1, nh + 1) if not buzz or k % 2)
        out += h / (np.std(h) + 1e-9) * np.std(out) * r.uniform(0.1, 1.5)
    if r.random() < 0.4:   # fan / AC band noise with swirl
        lo = r.uniform(60, 600); hi = lo * r.uniform(2, 8)
        b = sosfilt(butter(2, [lo, min(hi, 20000)], "band", fs=fs, output="sos"), r.standard_normal(n))
        b *= 1 + r.uniform(0, 0.3) * np.sin(2 * np.pi * r.uniform(0.2, 2) * np.arange(n) / fs)
        out += b / (np.std(b) + 1e-9) * np.std(out) * r.uniform(0.3, 1.5)
    if r.random() < 0.2:   # whine
        t = np.arange(n) / fs; out += np.sin(2 * np.pi * r.uniform(6000, 17000) * t) * np.std(out) * r.uniform(0.05, 0.4)
    if r.random() < 0.15:  # crackle
        c = (r.random(n) < r.uniform(1e-4, 2e-3)) * r.standard_normal(n); out += sosfilt(butter(2, 1500, "high", fs=fs, output="sos"), c) * np.std(out) * 4
    return out

# ---------------------------------------------------------------- one training example
def eq_aug(r, x, fs=48000):
    if r.random() < 0.5:
        for kind in ("low", "high"):
            g = r.uniform(-5, 5); fc = r.uniform(120, 400) if kind == "low" else r.uniform(3000, 9000)
            b = butter(1, fc, kind, fs=fs, output="sos"); part = sosfilt(b, x)
            x = x + part * (10 ** (g / 20) - 1)
    return x

def segment(r, bank, L, tries=6):
    best = None
    for _ in range(tries):
        i = int(r.integers(bank.n)); c = bank.clip(i)
        if len(c) <= L: s = np.zeros(L); s[:len(c)] = c; s = np.roll(s, int(r.integers(L)))
        else: o = int(r.integers(len(c) - L)); s = c[o:o + L].astype(np.float64)
        fr = s[: L // 2400 * 2400].reshape(-1, 2400); act = np.mean(np.mean(fr ** 2, 1) > np.max(np.mean(fr ** 2, 1)) * 1e-3)
        if act > 0.5: return s
        if best is None or act > best[0]: best = (act, s)
    return best[1]

class Maker:
    def __init__(self, sing, speech, noise, rirs, packs=None):
        self.sing, self.speech, self.noise, self.rirs, self.packs = sing, speech, noise, rirs, packs
    def __call__(self, r):
        L = SEG
        u = r.random(); has_sp = self.speech is not None and self.speech.n > 0; has_pk = self.packs is not None and self.packs.n > 0
        src = self.speech if (has_sp and u < 0.30) else self.packs if (has_pk and u < 0.62) else self.sing
        if r.random() < 0.3:   # pitch / speed change: deeper or higher voices
            st = r.uniform(-4, 3); ratio = 2 ** (-st / 12); Lr = int(L * ratio) + 8
            c = segment(r, src, Lr); c = resample_poly(c, 1000, int(round(1000 * ratio)))[:L]
            c = np.pad(c, (0, max(0, L - len(c))))
        else:
            c = segment(r, src, L)
        c = eq_aug(r, c)
        if r.random() < 0.12: d = r.uniform(1.5, 6); c = np.tanh(c * d) / np.tanh(d)   # saturated / processed vocal
        clean_only = r.random() < 0.10
        x = c.copy(); target = c.copy()
        if not clean_only and self.rirs and r.random() < 0.45:
            full, early = self.rirs[int(r.integers(len(self.rirs)))]
            x = fftconvolve(c, full)[:L]; target = fftconvolve(c, early)[:L]
        if not clean_only and r.random() < 0.88:
            if self.noise is not None and self.noise.n and r.random() < 0.55:
                nz = segment(r, self.noise, L, tries=2)
                if r.random() < 0.3: nz = nz + proc_noise(r, L) * np.std(nz) / 3
            else:
                nz = proc_noise(r, L)
            act = target[: L // 2400 * 2400].reshape(-1, 2400); e = np.mean(act ** 2, 1); vr = np.sqrt(np.mean(e[e > e.max() * 1e-3]) + 1e-12)
            snr = r.choice([r.uniform(-5, 10), r.uniform(5, 25), r.uniform(15, 40)], p=[0.25, 0.5, 0.25])
            x = x + nz / (np.std(nz) + 1e-12) * vr * 10 ** (-snr / 20)
        pk = max(np.max(np.abs(x)), 1e-6); g = 10 ** (r.uniform(-30, -1) / 20) / pk
        x *= g; target *= g
        X = dfdsp.stft(x); fe, fs = dfdsp.features(X)
        spec = np.stack([X.real, X.imag], -1).astype(np.float32)
        return spec, fe, fs, x.astype(np.float32), target.astype(np.float32)

# ---------------------------------------------------------------- main
def main():
    import torch, torch.nn.functional as F
    from torch.utils.data import IterableDataset, DataLoader
    t_start = time.time()
    dev = "cuda" if torch.cuda.is_available() else "cpu"
    log("device", dev, torch.cuda.get_device_name(0) if dev == "cuda" else "", "code", CODE)
    vs = get_vocalset(); vs_tr, vs_va = split_vocalset(vs)
    vc_tr, vc_va = get_vctk(); mu_tr, mu_va = get_musan(); pk_tr, pk_va = get_packs()
    if SMOKE: vs_tr, vs_va, vc_tr, mu_tr = vs_tr[:40], vs_va[:8], vc_tr[:20], mu_tr[:20]
    log("files: vocalset", len(vs_tr), "/", len(vs_va), "vctk", len(vc_tr), "/", len(vc_va), "musan", len(mu_tr), "/", len(mu_va), "packs", len(pk_tr), "/", len(pk_va))
    packs = load_bank(pk_tr) if pk_tr else None; packs_va = load_bank(pk_va) if pk_va else None
    sing = load_bank(vs_tr); sing_va = load_bank(vs_va)
    speech = load_bank(vc_tr) if vc_tr else None; speech_va = load_bank(vc_va) if vc_va else None
    noise = load_bank(mu_tr, max_s=60) if mu_tr else None; noise_va = load_bank(mu_va, max_s=60) if mu_va else None
    log("hours: sing %.1f packs %.2f speech %.1f noise %.1f" % (sing.seconds() / 3600, packs.seconds() / 3600 if packs else 0, speech.seconds() / 3600 if speech else 0, noise.seconds() / 3600 if noise else 0))
    with Pool(4) as p: rirs = [x for x in p.map(make_rir, range(200 if SMOKE else 2500)) if x is not None]
    with Pool(4) as p: rirs_va = [x for x in p.map(make_rir, range(100000, 100060)) if x is not None]
    log("rooms", len(rirs), "val rooms", len(rirs_va), "%.0f s setup" % (time.time() - t_start))

    maker = Maker(sing, speech, noise, rirs, packs)
    class DS(IterableDataset):
        def __iter__(self):
            wi = torch.utils.data.get_worker_info(); r = np.random.default_rng((wi.id if wi else 0) * 7919 + int(time.time() * 1000) % 100000)
            while True: yield maker(r)
    dl = DataLoader(DS(), batch_size=BATCH, num_workers=WORKERS, persistent_workers=WORKERS > 0, prefetch_factor=4 if WORKERS > 0 else None)

    # fixed validation batch: held-out singers / speakers / noise / rooms
    vmaker = Maker(sing_va, speech_va, noise_va, rirs_va, packs_va); vr = np.random.default_rng(12345)
    val = [vmaker(vr) for _ in range(8 if SMOKE else 96)]

    from load import load_df3
    model = load_df3(CODE, os.path.join(CODE, "model_120.ckpt.best")).to(dev)
    model.train()
    W = torch.tensor(dfdsp.W, dtype=torch.float32, device=dev); N, H = dfdsp.N, dfdsp.H
    def istft(spec, n0):   # spec [B,T,F,2] -> [B,n0], same as dfdsp.istft
        Y = torch.complex(spec[..., 0], spec[..., 1]); B, T, _ = Y.shape
        fr = torch.fft.irfft(Y, n=N, dim=-1) * N * W
        out = F.fold(fr.transpose(1, 2), output_size=(1, (T - 1) * H + N), kernel_size=(1, N), stride=(1, H))[:, 0, 0]
        return out[:, N - H:N - H + n0]
    wins = {n: torch.hann_window(n, device=dev) for n in (256, 512, 1024, 2048)}
    def mr_loss(y, t):
        loss = 0.0
        for n, w in wins.items():
            # in real arithmetic with an epsilon: abs() / angle() of an exact zero (digital silence) have NaN gradients
            Y = torch.view_as_real(torch.stft(y, n, n // 4, window=w, normalized=True, return_complex=True))
            S = torch.view_as_real(torch.stft(t, n, n // 4, window=w, normalized=True, return_complex=True))
            ym = ((Y ** 2).sum(-1) + 1e-12).sqrt(); sm = ((S ** 2).sum(-1) + 1e-12).sqrt()
            Ya, Sa = ym.pow(0.3), sm.pow(0.3)
            loss = loss + F.mse_loss(Ya, Sa) * 500
            loss = loss + F.mse_loss(Y / ym[..., None] * Ya[..., None], S / sm[..., None] * Sa[..., None]) * 500
        return loss
    def run(batch):
        spec, fe, fs, x, t = [torch.as_tensor(np.asarray(b)).to(dev) for b in batch]
        out = model(spec[:, None], fe[:, None], fs[:, None])[0][:, 0]
        return istft(out, t.shape[-1]), t
    def evaluate():
        model.eval(); losses = []; drops = []; sdr = []
        with torch.no_grad():
            for i in range(0, len(val), 8):
                b = list(zip(*val[i:i + 8])); y, t = run(b); losses.append(mr_loss(y, t).item())
                y, t = y.cpu().numpy(), t.cpu().numpy()
                for yy, tt in zip(y, t):
                    a = np.dot(yy, tt) / (np.dot(tt, tt) + 1e-12); sdr.append(10 * np.log10(np.sum((a * tt) ** 2) / (np.sum((yy - a * tt) ** 2) + 1e-12) + 1e-12))
                    ft = tt[: len(tt) // 2400 * 2400].reshape(-1, 2400); fy = yy[: len(yy) // 2400 * 2400].reshape(-1, 2400)
                    et, ey = np.mean(ft ** 2, 1), np.mean(fy ** 2, 1); v = et > et.max() * 1e-3
                    if v.any():
                        gg = et[v].sum() / max(ey[v].sum(), 1e-20); drops.append(np.mean(10 * np.log10(ey[v] * gg / et[v] + 1e-20) < -6))
        model.train()
        return float(np.mean(losses)), float(np.mean(drops)), float(np.mean(sdr))
    v0 = evaluate(); log("val before: loss %.3f dropouts %.3f sisdr %.2f" % v0)
    best = v0[0]; hist = [{"step": 0, "loss": v0[0], "dropouts": v0[1], "sisdr": v0[2]}]
    opt = torch.optim.AdamW(model.parameters(), lr=LR, weight_decay=1e-3)
    budget = HOURS * 3600 - (time.time() - t_start); t0 = time.time(); step = 0; run_loss = 0.0
    eval_every = 50 if SMOKE else 1000
    for batch in dl:
        frac = min((time.time() - t0) / max(budget, 1), 1.0)
        lr = LR * min(1.0, (step + 1) / 300) * (0.02 + 0.98 * 0.5 * (1 + math.cos(math.pi * frac)))
        for gp in opt.param_groups: gp["lr"] = lr
        y, t = run(batch); loss = mr_loss(y, t)
        opt.zero_grad(set_to_none=True); loss.backward()
        gn = torch.nn.utils.clip_grad_norm_(model.parameters(), 1.0)
        if not (torch.isfinite(loss) and torch.isfinite(gn)):
            skipped = globals().get("SKIPPED", 0) + 1; globals()["SKIPPED"] = skipped
            if skipped <= 20 or skipped % 100 == 0: log("skipped a non-finite step", skipped)
            continue
        opt.step()
        step += 1; run_loss += loss.item()
        if step % 100 == 0 or SMOKE and step % 10 == 0:
            n = 100 if step % 100 == 0 else 10
            log("step %d loss %.3f lr %.2e %.2f s/step" % (step, run_loss / n, lr, (time.time() - t0) / step)); run_loss = 0.0
        if step % eval_every == 0 or frac >= 1.0:
            v = evaluate(); hist.append({"step": step, "loss": v[0], "dropouts": v[1], "sisdr": v[2]})
            log("val step %d: loss %.3f dropouts %.3f sisdr %.2f" % (step, *v))
            torch.save(model.state_dict(), os.path.join(OUT, "latest.pt"))
            if v[0] < best: best = v[0]; torch.save(model.state_dict(), os.path.join(OUT, "best.pt")); log("  best so far")
            json.dump(hist, open(os.path.join(OUT, "val.json"), "w"), indent=1)
        if frac >= 1.0: break
    log("done", step, "steps")

if __name__ == "__main__":
    main()
