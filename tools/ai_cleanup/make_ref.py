"""tests/data/ai_ref_48k.f32: the round-3 model (float16 weights, BatchNorm folded: what the C++ holds) on a synthetic
sung note in faint noise, no safety-net hiss, from the very first frame (as AiCleanup::startNowForTests). The input is a
formula both sides compute (tests/test_aicleanup.cpp: refInput). 1.2 s in, the first 1.0 s out kept."""
import sys; sys.path[:0] = ['/tmp/claude-0/ai/kcode', '/tmp/claude-0/ai', '/tmp/claude-0/ai/train']
import numpy as np
from retest import enhance
from ref16 import model16
SR = 48000
def ref_input(n):
    t = np.arange(n) / SR
    phase = 2 * np.pi * 220.0 * t + (0.02 * 220.0 / 5.0) * np.sin(2 * np.pi * 5.0 * t)   # 5 Hz vibrato, +-2 %
    env = np.clip(np.minimum((t - 0.1) / 0.02, (1.1 - t) / 0.02), 0.0, 1.0)
    voice = sum(0.1 / k * np.sin(k * phase) for k in range(1, 21)) * env
    s = 12345; noise = np.empty(n)
    for i in range(n):
        s = (s * 1664525 + 1013904223) & 0xFFFFFFFF; noise[i] = s / 4294967296.0 * 2.0 - 1.0
    return voice + 0.01 * noise
x = ref_input(int(SR * 1.2)); y = enhance(model16(), x)
y[:SR].astype(np.float32).tofile(sys.argv[1])
print('in rms %.4f out rms %.4f' % (np.sqrt(np.mean(x[:SR] ** 2)), np.sqrt(np.mean(y[:SR] ** 2))))
