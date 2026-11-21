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

## Round 2 (2026-11-21)

The user picked option 1. Round 2 started from v1 (`train_sing.py` settings in the kernel header): cutting the voice
costs extra (`UNDER_W` 2: output quieter than the target; `LEVEL_W` 2: dB a voiced moment is down beyond 1 dB), more
female singers and long tones, more pitched-down (to -7 semitones) and saturated voices, less speech (20 %).
PRIVATE kernel `sonicology-sing-cleanup-r2`, 23:23-04:54 UTC (5.5 GPU hours, 29,171 steps, no bad steps; the check
clips' dropouts 4.9 % -> 0.6 %). Kept as `sing_r2.pt` / `sing_r2.onnx` (best check, step 26000) in the same private
dataset. New in the test: the **clean take alone**, which should come out untouched.

| Voice | Case | Voxology now | Round 1 | **Round 2** |
| --- | --- | --- | --- | --- |
| Female sung | home noise | 16.5 (1 %) | 13.5 (8 %) | **16.4 (0 %)** |
| | real noise | **9.6 (0 %)** | 8.2 (15 %) | 8.5 (14 %) |
| | clean take | **26.1 (0 %)** | 12.7 (20 %) | 12.2 (22 %) |
| Male sung | home noise | 11.5 (3 %) | 21.4 (0 %) | **20.6 (0 %)** |
| | real noise | 7.0 (3 %) | 15.8 (0 %) | **14.7 (0 %)** |
| | clean take | 12.7 (3 %) | 42.5 (0 %) | **40.8 (0 %)** |
| Female rap | home noise | 13.2 (5 %) | 18.5 (1 %) | **18.0 (0 %)** |
| | real noise | 8.9 (2 %) | 13.5 (0 %) | **13.7 (0 %)** |
| Male trap chant | home noise | **13.0** (2 %) | 6.7 (12 %) | 10.9 (1 %) |
| | real noise | 9.2 (1 %) | 6.1 (7 %) | **9.0 (0 %)** |
| | clean take | **17.8** (1 %) | 8.5 (6 %) | 12.6 (0 %) |

- **Fixed**: the chant (12 % -> 1 %), the female singer in steady home noise (8 % -> 0 %), the bedroom dropouts
  (2 % on three voices; the female singer 19 %). The male singer and the rapper stay far ahead of Voxology now.
- **Still wrong**: the female singer's quieter held notes on a **clean** take (22 %, 20-40 dB down at 2.1-2.4 s,
  4.6-5.4 s, 6.7-7.5 s) and after the real noise drops away. The model's own local-SNR estimate reads those moments
  as noise only (-15 dB, its floor), so DeepFilterNet's per-frame stage rule (`gated.py`) makes it worse (35 %),
  not better. Her pitch is not unusually steady (off the semitone grid ~9 cents, like the male singer), so it is
  not simply a tuned vocal. With steady noise around, the same notes are kept.
- **Gentler**: round 2 leaves more noise between phrases than round 1 (female sung -8 dB, male -13 dB; round 1 -20 /
  -39 dB). Still at or above Voxology now (-4 / -14 dB).
- Listening: six clips, 1-4 beeps = before, Voxology now, round 1, round 2 (`clips2.py <dir> sing r2`).

Verdict: much closer. It passes on three of four voices; the last failure is the worst kind for a vocal plug-in (a
clean held note turned down), so not shippable yet.
