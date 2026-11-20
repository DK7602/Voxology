# AI voice / room / noise cleanup: step 1 (feasibility test), 2026-11-20

The user's go: "yes start step 1". The question: can an open AI model, one we may ship in a paid plug-in, clean noise and room
out of home vocals (sung and rapped) without hurting the voice, at a cost a plug-in can carry? No Voxology code changed.

## What was tested

- **AI: DeepFilterNet3** (Schröter et al., dual MIT / Apache-2.0: code and weights; the best-known open real-time 48 kHz
  noise remover). The neural graph: Hugging Face `soniqo/DeepFilterNet3-ONNX` (MIT, the v0.5.6 checkpoint, sha256
  e1157049...; checked here against the official `DeepFilterNet3_onnx.tar.gz` export: same masks). The DSP around it
  (STFT, ERB / unit-norm features, ERB mask, deep filter order 5 / look-ahead 2, overlap-add) written from libDF's
  source (`tools/ai_cleanup/df3.py`, numpy + onnxruntime). **Checked on speech** (the repo's own clean + noise
  sample, 5 dB SNR): SI-SDR 5.0 -> 14.6 dB, a normal result, so the wrapper is right.
  - Room models: the good open de-reverb models are GPL-3.0 (anvuew's Roformers) or non-commercial (CC-BY-NC): not
    usable in a closed paid plug-in. DeepFilterNet3 saw some reverb in training (p_reverb 0.1).
- **Voxology now**: the classic `vox::Denoiser` (Cleanup's Noise / Room), `tools/ai_cleanup/classic.cpp`
  (Noise 18 dB / Room 0 % on noise, 0 / 100 % on room, 18 / 80 % on both).
- **Material**: 15 s of four clean dry vocals (Cymatics female sung, Cymatics male sung, Looperman female trap rap,
  Looperman male trap chant), each with (a) home noise (fan band noise with swirl, 60 Hz hum + harmonics, preamp
  hiss) at 15 dB SNR, (b) a simulated bedroom (pyroomacoustics 3.6 x 3.0 x 2.5 m, RT60 0.55 s, mic 0.3 m,
  direct-to-reverb +2.3 dB), (c) both. Plus the user's own takes (Don Birthday, Gallas, Schaf acapellas).
- Measures (`tools/ai_cleanup/metrics.py`, `run_all.py`): SI-SDR against the clean vocal; **dropouts** = the share of
  voiced 50 ms moments turned down more than 6 dB against the clean (after level match); tone per band; noise left
  in the gaps; CPU (one thread).

## Results (`step1-results.json`)

| Vocal + home noise | SI-SDR in | Voxology now | AI | AI dropouts |
| --- | --- | --- | --- | --- |
| Female sung | 14.7 | 16.5 | 3.3 | 44 % |
| Male sung | 14.1 | 11.5 | 6.9 | 15 % |
| Female rap | 15.0 | 13.2 | **18.6** | 1 % |
| Male trap chant | 15.0 | 13.0 | -3.5 | 57 % |

- **The AI removes far more noise** (gaps: -15 to -30 dB, against -4 to -14 dB for Voxology now) **but cuts into sung
  notes and the deep, processed chant**: speech-trained, it takes a held note for noise. On speech-like female rap it
  is clearly the best. Limiting it to 12 dB (its attenuation limit, the would-be fader) halves the damage but still
  cuts 12-33 % of the sung moments.
- **Room**: nothing really removes the bedroom. SI-SDR / log-spectral distance get no better than the input for either;
  the AI cuts the voice (sung and the chant: 22-43 % dropouts); Voxology's Room at 100 % also dips the voice (13-21 %).
- **The user's real takes** are fairly clean: the AI leaves them almost untouched; Voxology now lowers the quiet parts
  2-5 dB.
- **Cost is fine**: the graph alone ran at 0.028 x real time on one core (batch; streaming a bit more): about 3-5 % of
  a core. Delay about 30 ms (10 ms STFT + 20 ms look-ahead), against Voxology's 21 ms. Model 8.6 MB (fp32).
  Voxology already runs a neural net without a framework (CREPE tiny, `Crepe.h`), so shipping one is possible.

## Verdict

Off-the-shelf AI is **not good enough for singing**. Engineering cost (CPU, delay, size, licence) is not the
blocker; the training data is (speech only). Listening page for the user (the user's songs are private: sent as a file,
not published): `tools/ai_cleanup/page.py`.

## Options for step 2 (the user decides)

1. **Teach the model singing** (recommended): fine-tune DeepFilterNet3 (its training code is MIT / Apache) on sung and
   rapped vocals with noise and simulated rooms (clean target = the dry vocal; room target = the dry + early
   reflections). Needs a GPU (Kaggle, connected to this environment, gives free GPU time) and vocals we may train on:
   VocalSet (CC BY 4.0, ~10 h of singing); the user's Cymatics / Looperman packs only if their terms allow AI
   training (check first). Noise: our own synthetic + CC0 / CC BY recordings. Pass mark: the step 1 set with under 5 %
   dropouts on every voice and more noise removed than Voxology now.
2. Improve the classic Room instead (the bedroom echo is barely touched): smaller and safe, but no AI.
3. AI only on speech-like takes: not reliable (the male trap chant failed too).

## Reproduce

`tools/ai_cleanup/`: `build_set.py` (needs the test-audio files decoded to 48 kHz f32 in the scratch `src/`),
`classic.cpp` (link with `libvox_dsp.a`), `df3.py` (needs `deepfilter.onnx`), `run_all.py`, `page.py`. Paths in the
scripts point at the session scratch (`/tmp/claude-0/ai`).
