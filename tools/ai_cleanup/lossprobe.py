"""Size of each loss term for a model on fresh training-style clips (to set UNDER_W / LEVEL_W)."""
import os, sys
sys.argv += [] ; import calib as C   # sets env defaults, imports train_sing as T
import numpy as np, torch, torch.nn.functional as F, zipfile
from multiprocessing import Pool
T = C.T
if __name__ == "__main__":
    with zipfile.ZipFile(C.ZIP) as z: names = [n for n in z.namelist() if n.endswith(".wav") and "/female9/" not in n and "/male11/" not in n]
    names = list(np.random.default_rng(3).choice(names, 300, replace=False))
    with Pool(4) as p: sing = T.Bank(p.map(C._zload, names))
    pk, _ = T.get_packs(); packs = T.load_bank(pk)
    with Pool(4) as p: rirs = [x for x in p.map(T.make_rir, range(100)) if x is not None]
    C._maker = T.Maker(sing, None, None, rirs, packs)
    from load import load_df3
    m = load_df3(T.CODE, os.path.join(T.CODE, "model_120.ckpt.best")); m.load_state_dict(torch.load(sys.argv[1], map_location="cpu")); m.eval()
    W = torch.tensor(T.dfdsp.W, dtype=torch.float32); N, H = T.dfdsp.N, T.dfdsp.H
    def istft(spec, n0):
        Y = torch.complex(spec[..., 0], spec[..., 1]); fr = torch.fft.irfft(Y, n=N, dim=-1) * N * W; Tn = Y.shape[1]
        out = F.fold(fr.transpose(1, 2), output_size=(1, (Tn - 1) * H + N), kernel_size=(1, N), stride=(1, H))[:, 0, 0]
        return out[:, N - H:N - H + n0]
    tot = {"base": 0, "under": 0, "level": 0}
    with Pool(4) as p:
        for b in range(6):
            ex = p.map(C._make, range(b * 16 + 5000, (b + 1) * 16 + 5000))
            spec, fe, fs, x, t = [torch.as_tensor(np.stack([e[k] for e in ex])) for k in range(5)]
            with torch.no_grad():
                y = istft(m(spec[:, None], fe[:, None], fs[:, None])[0][:, 0], t.shape[-1])
                for n in (256, 512, 1024, 2048):
                    w = torch.hann_window(n)
                    Y = torch.view_as_real(torch.stft(y, n, n // 4, window=w, normalized=True, return_complex=True))
                    S = torch.view_as_real(torch.stft(t, n, n // 4, window=w, normalized=True, return_complex=True))
                    ym = ((Y ** 2).sum(-1) + 1e-12).sqrt(); sm = ((S ** 2).sum(-1) + 1e-12).sqrt(); Ya, Sa = ym.pow(0.3), sm.pow(0.3)
                    tot["base"] += (F.mse_loss(Ya, Sa) * 500 + F.mse_loss(Y / ym[..., None] * Ya[..., None], S / sm[..., None] * Sa[..., None]) * 500).item() / 6
                    tot["under"] += ((F.relu(Sa - Ya) ** 2).mean() * 500).item() / 6
                    if n == 1024:
                        et = (sm ** 2).sum(1) + 1e-10; ey = (ym ** 2).sum(1) + 1e-10; v = (et > et.amax(-1, keepdim=True) * 1e-3).float()
                        down = F.relu(10 * torch.log10(et / ey) - 1.0); tot["level"] += ((down * v).sum() / v.sum().clamp(min=1)).item() / 6
    print({k: round(v, 3) for k, v in tot.items()})
