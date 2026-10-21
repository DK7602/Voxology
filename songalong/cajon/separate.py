# Pulls the drums out of a song for its cajon part, with Demucs (htdemucs, MIT licence) on the CPU.
#   python separate.py song.wav drums.wav
# Reads a 44.1 kHz WAV; writes the drums alone as mono 22.05 kHz (what cajon/drums.ts reads).
import os
import sys

import numpy as np
import soundfile as sf
import torch
from demucs.apply import apply_model
from demucs.pretrained import get_model
from scipy.signal import resample_poly

src, out = sys.argv[1:3]
torch.set_num_threads(int(os.environ.get("THREADS", "4")))
model = get_model("htdemucs")
model.eval()
x, sr = sf.read(src, dtype="float32", always_2d=True)
if sr != 44100:
    sys.exit(f"expected 44.1 kHz audio, got {sr}")
if x.shape[1] == 1:
    x = np.repeat(x, 2, axis=1)
wav = torch.from_numpy(x[:, :2].T.copy())
ref = wav.mean(0)
mean, std = ref.mean(), ref.std() + 1e-8
with torch.no_grad():
    # overlap 0.1 (not 0.25) is a fifth faster and hears the drums as well
    sources = apply_model(model, ((wav - mean) / std)[None], device="cpu", progress=False, overlap=0.1)[0] * std + mean
drums = sources[model.sources.index("drums")].mean(0).numpy()
sf.write(out, resample_poly(drums, 1, 2).astype(np.float32), 22050, subtype="FLOAT")
