# AI voice / room / noise cleanup: step 3 (in Voxology), 2026-11-21

The user: "Yes, build it into Voxology" (round 3 + the safety net, with Auto-Edit using it). Voxology 0.73.0.

## What it is

- **Cleanup's Mode: Classic or AI** (`clMode`, default Classic: projects from before keep their sound). In AI mode the
  network takes noise and the room's echo out together; **Noise** = how far it may turn the sound down (24 dB, the top,
  shows "AI full" = no limit, how it was tested), **Room** is the AI's (dimmed, "by the AI"). The classic reducer is off
  in AI mode; the Dynamic EQ's Smooth still rides on its spectra.
- **Engine**: `dsp/include/vox/AiCleanup.h`, `dsp/src/AiCleanup.cpp`, no framework (like Honey Tune's CREPE). The round-3
  model (`sing_r3.pt`) exported by `tools/ai_cleanup/export_cpp.py` to `dsp/src/AiCleanupWeights.cpp` (2.13 M float16
  weights, 15 MB of source, 4.3 MB in the binary; unpacked once per process to 8.5 MB of floats, shared by every
  instance). License: `dsp/third_party/DeepFilterNet-LICENSE.txt` (MIT).
- **Any sample rate with 50 Hz bins**: 20 ms frames, 10 ms hop at the host's rate (960 points at 48 kHz, 882 at 44.1,
  1920 at 96), a mixed-radix FFT; 32 / 44.1 / 48 / 88.2 / 96 kHz (`AiCleanup::supports`). Below 48 kHz the missing top
  bins (44.1: 22 - 24 kHz) are mirrored in what the network hears (never in the sound): left empty, it took out about
  half as much noise in the gaps (male singer: -12 dB at 44.1 against -20 at 48; mirrored: -17).
- **Latency**: 2 frames + a hop, 50 ms, constant (on or off, either Mode): the chain's total is about 106 ms at 48 kHz
  (was 56). The hop: the network's work for a frame is spread over the next hop's callbacks in 6 pieces (`Net::stage`),
  so no audio callback carries a whole frame (the review measured 1.64 ms in one 32-sample callback, budget 0.67 ms).
  Record mode leaves it out. Off: bit-exact delay, the network and FFTs idle. On / off / strength glide over ~0.1 s
  (6 pass-through frames, then 10 frames of glide).
- **Safety net, as tested, plus one fix**: the network hears a Gaussian hiss above 8 kHz (6th-order Butterworth twice,
  as the tested filtfilt) added to the take; mask and filter go on the real take. The hiss is **28 dB under the
  voice's running level** (frames within 30 dB of the recent peak, ~2 s average), not a fixed -50 dBFS: with a fixed
  level a take 12 - 24 dB quieter lost 1 - 6 dB of air (top end); relative, within 1 dB at -24 / -12 / 0 / +8 dB, notes
  kept the same. A fixed power floor instead of the random hiss does NOT work (`featfloor.py`: 9 % cut on the clean take).
- **CPU**: ~0.7 ms per 10 ms frame on this 2.1 GHz cloud Xeon (7 - 9 % of a core), most of it the GRUs and reading the
  weights; a desktop should be about half. Auto-Edit runs the AI once per run on its capture (`AiPass` in AutoEdit.cpp:
  renders after that start from the cleaned take with the AI off, the same sound): +1.4 s on a 14 s take (14.4 -> 15.8 s).
- **Auto-Edit**: when the take needs Noise or Room (the classic rules), Mode AI, Noise 24 (full) on Balanced / Strong;
  Light: noise need + 6 dB, 12 - 24. Reasons: Mode "AI", Noise "AI, full", Room "By the AI". `AutoEditSettings::allowAi`
  = false keeps the classic reducer. The Coach, PREVIEW and Auto Analyze carry clMode like any setting (forEachValue).
- **LISTEN** (Cleanup) hears what the AI takes out too (delayed by the classic reducer's frame to line up).
- **Rack**: Cleanup shows Noise "AI full" / "AI -N dB", Room "by the AI", and Mode (clMode, 2 steps): the Rack draws 8
  values at most, so the gate is one value there ("Gate": its range on the knob, "-12 dB under -45 dB").
- **Fresh-eyes review fixes** (same day): the staged network (above); Mode in the Rack (was cut off as a 9th value); AI
  mode at an unsupported rate falls back to Classic; LISTEN keeps the AI's line moving when it switches off (stale audio
  before); a channel coming back starts clean; Classic -> AI: the classic reducer keeps its settings until the AI is
  fully on (no 0.2 s gap of noise). UI: switching Mode by hand moves Noise to fit (to AI from nearly off: full; to
  Classic from over 15 dB: 12). Vocal Fit's "your vocal is in the beat" now counts the raw-vocal check too.
- **Found on the way, an older bug in the classic reducer** (`Denoise.cpp`): after digital silence its smoothed level
  crept up from zero, those frames read as the quietest of the last 5 s, so nothing was quiet and no noise was learned
  for ~5 s. It hit every playback start after a stop in the DAW, and every render with the AI's delay line in front
  (Classic Noise did nothing for the first 5 s). Now digital silence (32+ exact zeros in a row) and any frame still
  holding some of it are left out of the quiet-moment trackers, a frame that's all silence restarts the smoothing, and
  a quiet moment also needs something 6 dB louder in the same ~5 s (so a held note after silence still isn't learned as
  noise: the silence used to protect it). Normal audio unchanged (identical numbers). Test: "Denoise: digital silence
  first"; the held-note test still passes.

## Checked

- C++ vs PyTorch (the same float16 weights): 1e-8 abs (-135 dB) frame for frame; `tests/data/ai_ref_48k.f32`.
- The C++ engine on the whole test set, live (warm-up, glide, voice-relative hiss), 48 kHz: clean takes 0 % notes lost,
  real noise 6 % (Python + net 5 %), every case within ~1 dB SI-SDR of Python; 44.1 kHz: 0 % / 4 %. The user's three
  takes: 0 % at both rates.
- `tests/test_aicleanup.cpp`: weights load, FFT vs a plain DFT, off = bit-exact delay at 5 rates, dark held notes kept
  (2 rates x 4 notes x 2 levels), noise -15 dB+ in the gaps with the voice kept, strength limit, block-size invariance,
  glide on / off back to bit-exact, stereo linked, chain latency constant and Record mode without it, Auto-Edit picks
  it (and not on a studio-quiet take). `test_realtime.cpp`: Auto-Edit's plan now includes the AI: no allocation.

## Beta 36 test (2026-11-21): "sounds good", but the audio cut out twice while Mixology re-analysed the mix

Not reproducible here (no DAW). Mixology is unchanged since Beta 35; the new load is the AI, so Voxology 0.73.1
makes it lighter (Beta 37):
- Digital silence (between clips, the DAW stopped) costs nothing: no network (paused, not reset) and, after 10 hops
  of it, no FFTs either (exactly the zeros they'd give). AI stage alone, 48 kHz stereo, this cloud Xeon: on sound 7.5 %
  of a core, on silence 1.1 % -> 0.06 %, off 0.03 %. Whole Voxology: Classic ~4.5 %, AI ~12 % (on sound).
- Background work: the AI pass in Auto-Edit (and any chain render with the AI) rests half the time while paced
  (pace::Gentler 4 ms work / 4 ms rest inside the usual 20 / 8): it reads 8.5 MB of weights a frame, sharing the
  memory bus with the AI on the audio thread.
- The hiss level is the filters' impulse-response energy (exact), not a second of noise made on every prepare
  (Auto-Edit prepares a chain for each of its dozens of renders).
Asked the user: did Beta 35 cut out on the same re-analysis, and how many tracks run Mode AI.
