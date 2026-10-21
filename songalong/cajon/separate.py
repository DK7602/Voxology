# Pulls the drums out of a song for its cajon part, with Demucs (MIT licence) on the CPU: the
# drums model of htdemucs_ft (f7e0c4bc), fine-tuned on drums. htdemucs_ft is four such models,
# one per part, and only this one makes its drums, so the drums are the same as htdemucs_ft's
# in a quarter of the time (as fast as plain htdemucs).
#   python separate.py song.wav drums.wav
# Reads a 44.1 kHz WAV; writes the drums alone as mono 22.05 kHz (what cajon/drums.ts reads), and
# drums.wav.json: {"level": how loud the drums are against the whole song, in dB}. Real drums
# come out at -13 to -1 dB; in songs without drums, what Demucs still calls drums is far quieter.
import json
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
model = get_model("f7e0c4bc")
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
drums = sources[model.sources.index("drums")]
rms = lambda a: float(np.sqrt(np.mean(np.square(a))) + 1e-9)
level = 20 * np.log10(rms(drums.numpy()) / rms(x[:, :2].T))
sf.write(out, resample_poly(drums.mean(0).numpy(), 1, 2).astype(np.float32), 22050, subtype="FLOAT")
with open(out + ".json", "w") as f:
    json.dump({"level": round(float(level), 1)}, f)
