"""Recompute a trained model's BatchNorm running averages from fresh training-style clips (they went NaN on Kaggle).
Usage: python calib.py <in_state_dict.pt> <out.pt> [batches]. Data here: VocalSet (training singers only, read from the zip),
the user's licensed pack stems (training songs only), procedural noise and simulated rooms (no VCTK / MUSAN locally)."""
import io, os, sys, zipfile, time
os.environ.setdefault("DFX_CODE", "/tmp/claude-0/ai/kcode"); os.environ.setdefault("OUT_DIR", "/tmp/claude-0/ai/work/calib")
os.environ.setdefault("WORK_DIR", "/tmp/claude-0/ai/work"); os.environ.setdefault("PACKS_DIR", "/tmp/claude-0/ai/packs")
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import numpy as np, soundfile as sf
from multiprocessing import Pool
import train_sing as T
ZIP = "/tmp/claude-0/ai/data/VocalSet11.zip"
def _zload(name):
    with zipfile.ZipFile(ZIP) as z: b = z.read(name)
    tmp = os.path.join(T.WORK, "z_" + str(os.getpid()) + ".wav"); open(tmp, "wb").write(b)
    r = T._load((tmp, 48000, None)); os.remove(tmp); return r
_maker = None
def _make(seed):
    return _maker(np.random.default_rng(seed))
if __name__ == "__main__":
    import torch
    src, dst = sys.argv[1], sys.argv[2]; NB = int(sys.argv[3]) if len(sys.argv) > 3 else 120; B = 16
    with zipfile.ZipFile(ZIP) as z: names = [n for n in z.namelist() if n.endswith(".wav") and "/female9/" not in n and "/male11/" not in n]
    rng = np.random.default_rng(7); names = list(rng.choice(names, 700, replace=False))
    with Pool(4) as p: sing = T.Bank(p.map(_zload, names))
    pk_tr, _ = T.get_packs(); packs = T.load_bank(pk_tr)
    with Pool(4) as p: rirs = [x for x in p.map(T.make_rir, range(400)) if x is not None]
    T.log("calib data: sing %.1f h, packs %.2f h (%d files), rooms %d" % (sing.seconds() / 3600, packs.seconds() / 3600, len(pk_tr), len(rirs)))
    _maker = T.Maker(sing, None, None, rirs, packs)
    from load import load_df3
    m = load_df3(T.CODE, os.path.join(T.CODE, "model_120.ckpt.best"))
    sd = torch.load(src, map_location="cpu"); m.load_state_dict(sd)
    bns = [x for x in m.modules() if isinstance(x, torch.nn.modules.batchnorm._BatchNorm)]
    for bn in bns: bn.reset_running_stats(); bn.momentum = None   # cumulative average over every calibration batch
    T.log("batchnorm layers", len(bns)); m.train(); torch.set_num_threads(4); t0 = time.time()
    with Pool(4) as p:
        for i in range(NB):
            ex = p.map(_make, range(i * B + 1000, (i + 1) * B + 1000))
            spec, fe, fs = [torch.as_tensor(np.stack([e[k] for e in ex])) for k in range(3)]
            with torch.no_grad(): m(spec[:, None], fe[:, None], fs[:, None])
            if i % 20 == 0: T.log("calib batch", i, "%.0f s" % (time.time() - t0))
    for bn in bns: bn.momentum = 0.1
    out = m.state_dict()
    bad = [k for k, v in out.items() if v.is_floating_point() and not torch.isfinite(v).all()]
    assert not bad, bad
    torch.save(out, dst); T.log("saved", dst)
