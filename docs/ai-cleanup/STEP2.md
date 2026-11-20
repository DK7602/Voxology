# AI voice / room / noise cleanup: step 2 (teach it singing), 2026-11-20

The user's go: "teach the AI to handle singing and then lets retest", then "We are good to go on the cymatics and
looperman i checked and they do not forbid ai training". No Voxology code changed.

## What was done

- **Training**: DeepFilterNet3 (MIT / Apache-2.0) fine-tuned from its own checkpoint (`tools/ai_cleanup/train_sing.py`)
  on the user's PRIVATE Kaggle GPU (kernel `sonicology-sing-cleanup-v3`, Tesla T4, 17:31-23:02 UTC, about
  5.5 GPU hours, 32,237 steps of 32 x 3 s). Material: VocalSet 1.1 singing (CC BY 4.0), VCTK speech (CC BY 4.0), the
  user's licensed Cymatics / Looperman dry stems (70 files, 88 min; the retest's songs and all wet stems left out),
  MUSAN noise (CC BY 4.0) + generated home noise, 2,500 simulated rooms (target = the dry voice + 20 ms of early
  reflections). Mix: 30 % speech, 32 % packs, 38 % VocalSet. Held out for checking: two VocalSet singers, four VCTK
  speakers, one pack song, one MUSAN noise source.
- **A bug, found and fixed**: about 20 training clips landed on pure digital silence in a vocal stem with noise added;
  a level calculation then divided by nothing and produced NaN. Training skipped those steps, but the model's
  BatchNorm running averages had already taken the NaN (they update during the forward pass). Training mode does
  not use them, so the training loss looked healthy (23.6 -> about 7); normal use does, so every check after step
  1000 read NaN and no "best" model was saved. The weights themselves were fine. Fixed for the model trained:
  `calib.py` recomputes the running averages from 1,920 fresh training-style clips (VocalSet training singers +
  the packs' training songs + generated noise and rooms; no VCTK / MUSAN here). Fixed for future runs: the level
  falls back to a fixed value on silent crops, the clip maker never returns NaN, and the training loop puts the
  averages back on any skipped step.
- **Model kept** in the PRIVATE Kaggle dataset `sonicology-ai-models`: `sing_v1.pt` (PyTorch) and
  `sing_v1.onnx` (same contract as the official export; checked against PyTorch: max error 3e-6).

## Results (`step2-results.json`, `tools/ai_cleanup/compare.py`)

Same step 1 test set and measures, plus a recorded noise the AI never heard (the DeepFilterNet repo's
`noise_freesound_573577.wav`, 10 dB SNR). SI-SDR in dB (higher = closer to the clean vocal); in brackets the
dropouts (voiced 50 ms moments turned down more than 6 dB).

| Voice + noise | Before | Voxology now | Original AI | **Retrained AI** |
| --- | --- | --- | --- | --- |
| Female sung, home noise | 14.7 | **16.5** (1 %) | 3.3 (44 %) | 13.5 (8 %) |
| Female sung, real noise | 9.7 | **9.6** (0 %) | 3.1 (40 %) | 8.2 (15 %) |
| Male sung, home noise | 14.1 | 11.5 (3 %) | 6.9 (15 %) | **21.4 (0 %)** |
| Male sung, real noise | 9.1 | 7.0 (3 %) | 4.6 (18 %) | **15.8 (0 %)** |
| Female rap, home noise | 15.0 | 13.2 (5 %) | 18.6 (1 %) | **18.5 (1 %)** |
| Female rap, real noise | 10.0 | 8.9 (2 %) | 12.7 (1 %) | **13.5 (0 %)** |
| Male trap chant, home noise | 15.0 | **13.0** (2 %) | -3.5 (57 %) | 6.7 (12 %) |
| Male trap chant, real noise | 10.0 | **9.2** (1 %) | -1.9 (42 %) | 6.1 (7 %) |

- **Noise between phrases**: the retrained AI takes out -20 to -39 dB (sung, home noise) against -4 to -14 dB for
  Voxology now. On the recorded noise Voxology now does almost nothing (0 dB); the AI -3 to -4 dB.
- **Bedroom echo**: now a little better than the input for three of four voices (SI-SDR +0.2 to +2.7 dB; Voxology now
  -0.8 to -0.2), clearly so on the male singer (7 % dropouts, echo tail -34 dB against -11.5 dB). The female singer
  (26 %) and the chant (16 %) still lose some voice; Voxology's Room at 100 % loses 13-21 %.
- **The user's own takes**: untouched (0 % dropouts against the take itself).
- **Strength limit** (the would-be fader; the AI may turn nothing down more than N dB): at 12 dB the chant passes
  (home 10.2 / 4 %, real 7.9 / 1 %), the female singer still not (home 15.5 / 6 %, real 9.7 / 14 %).
- **Pass mark** (step 1: under 5 % dropouts on every voice and more noise removed than Voxology now): **male singer
  and female rapper pass; female singer and male chant not yet.**
- Listening files for the user (their songs are private: sent as files, not published): `tools/ai_cleanup/clips2.py`,
  five clips, four versions each marked by 1-4 beeps (before, Voxology now, original AI, retrained AI).

## Verdict

Teaching it singing worked: from badly worse than Voxology on three of four voices to clearly better on two (male
singer +10 dB, female rapper +5 dB) and close on the others, with far more noise removed and some bedroom echo too.
Still not good enough to ship: it turns down some held female sung notes and parts of the deep processed chant.

## Options for step 3 (the user decides)

1. **One more training round** (recommended, about 5.5 more Kaggle GPU hours): start from this model; add a penalty for
   turning the voice down below the target (cuts cost more than leftover noise); more weight on the weak material
   (female held notes, deep / processed male voices: heavier pitch-down and saturation); the NaN fix is in. Pass mark
   unchanged.
2. Build it into Voxology now as an AI mode of Cleanup with a strength control (12 dB default): already better for
   male singers and rappers, but the female singer would still lose some notes.
3. Stop here and keep the classic Denoiser.

## Reproduce

`train_sing.py` (Kaggle; `SMOKE=1` locally), `calib.py <latest.pt> <out.pt>` (only needed for the v1 model trained
before the fix), `retest.py <pt> sing`, `retest.py - orig`, `compare.py`, `export_onnx.py <pt> <out.onnx>`,
`clips2.py <dir>`. Paths point at the session scratch (`/tmp/claude-0/ai`).
