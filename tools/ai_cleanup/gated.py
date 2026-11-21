"""DeepFilterNet's own per-frame stages (libDF tract.rs apply_stages): local-SNR estimate > 30 dB -> pass the frame
through, > 20 dB -> ERB mask only (no deep filter), < -10 dB -> silence. Optional: thresholds as arguments."""
import sys; sys.path.insert(0, '/tmp/claude-0/ai/kcode'); sys.path.insert(0, '/tmp/claude-0/ai')
import numpy as np, torch, dfdsp
def enhance_gated(m, x, erb_th=30.0, df_th=20.0, min_th=-10.0, zero=True, want_lsnr=False):
    X = dfdsp.stft(x); fe, fs = dfdsp.features(X)
    spec = torch.from_numpy(np.stack([X.real, X.imag], -1).astype(np.float32))[None, None]
    with torch.no_grad():
        out, msk, lsnr, _ = m(spec, torch.from_numpy(fe)[None, None], torch.from_numpy(fs)[None, None])
    o = out[0, 0].numpy(); Y = o[..., 0] + 1j * o[..., 1]; l = lsnr[0, :, 0].numpy()
    # ERB-mask-only output: the mask on every bin (no deep filter)
    g = msk[0, 0].numpy() @ m.mask.erb_inv_fb.numpy() if hasattr(m.mask, 'erb_inv_fb') else None
    Ym = X * g if g is not None else Y
    Z = np.where((l > erb_th)[:, None], X, np.where((l > df_th)[:, None], Ym, Y))
    if zero: Z = np.where((l < min_th)[:, None], 0, Z)
    y = dfdsp.istft(Z, len(x))
    return (y, l) if want_lsnr else y
