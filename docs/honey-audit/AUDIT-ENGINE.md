# Honey Tune engine audit: sound quality and analysis accuracy vs a top tool (Melodyne 5)

Source audited: read-only copy `/tmp/claude-0/audit-src` at git HEAD `b833cc0`. No repository files were changed.
Engine built in `/tmp/claude-0/audit-eng` (Release, `vox_dsp`). All tools, outputs and patched experiment copies are in
`/tmp/claude-0/audit-eng-work` (listed at the end). Private audio (owner's songs, sample packs) was only decoded and
read locally in `/tmp/claude-0/audit-eng-work/audio`. Nothing was uploaded.

How it was measured, in short:
- **Synthetic vocals with exact ground truth** (`synth.h`). This is a source-filter voice: additive glottal source with jitter
  and shimmer, pulsed aspiration noise, 5 Klatt formants per vowel, smooth vowel moves, vibrato, drift, scoops, glides,
  legato joins, nasal dips, "s", "sh", plosive bursts, breaths, and a noise floor 74 dB under the peak. Every sample
  knows its true pitch, and every note knows its true start and end.
- **IDEAL reference for every render.** The same synthetic voice is re-synthesised at the target pitch with the same
  formants and the same noise. Each output is compared with this reference, not just with the input.
- **Independent measurement** (`meas.h`). None of the engine's detector code is used:
  - full-rate FFT-YIN with guided search (pitch)
  - third-octave LTAS envelope difference (formants and tone)
  - harmonic-to-noise ratio
  - 5 ms RMS level flutter
  - pitch roughness
  - noise periodicity ("buzz") above 5 kHz
  - 2nd-difference transient counting
  - best-lag difference in dB
- **Real vocals.** 13 clips (6 pro dry leads and acapellas, 3 rap clips, 4 of the owner's songs), 16.8 minutes in total.
- **Fixes were validated by experiment where possible.** A patched copy of `HoneyTune.cpp` (scratch dir only) is linked
  ahead of the library, and the same measurements are re-run.

"Melodyne 5" numbers below come from my knowledge of the product, not from measurements here. Each one is marked
*(believed)* or *(unsure)*.

---

## Summary table

| Area | Honey Tune, measured | Best in class (Melodyne 5) | Verdict |
|---|---|---|---|
| **Reliability: analysis hang on deep voices** | `findNotes` never returns on a 65 Hz synthetic clip and on a real rap vocal dropped one octave: one NaN reading out of 72,639 | never hangs | **Critical bug** (one-line fixes) |
| Pitch track accuracy, clean voice 82-659 Hz | median 0.7-1.6 c, p95 2.8-8 c; note pitch within 0.1-1.4 c | about 1 c *(believed)* | **Good, keep** |
| False notes on breath / "s" / "sh" / plosives alone | 0 notes, 0.3 % of readings | about 0 | **Good** |
| Vibrato (±30..150 c, 3.5-7 Hz) splitting a note's body | never | never | **Good** |
| **Start of a moved note** (first 50 ms) | first ~30 ms at the OLD pitch, then a scoop. Mean error 65-148 c for 1-12 st moves; on target after 60-122 ms. Real vocals, +5 st: first 40 ms off by 154-167 c | whole note moves, on target from its first cycle *(believed)* | **Major flaw** |
| **End of a moved note** (last 40 ms) | slides back toward the old pitch: 5-33 c on synthetic notes, 65-85 c on real +5 st moves | whole note moves | **Major flaw** |
| Short syllables (100-160 ms) moved +2 st | only 24-27 % of voiced time within 20 c of target (ideal 98 %) | about 95 %+ *(believed)* | **Major flaw** (pitch moves on rap syllables mostly don't land) |
| **Level of a moved note** | +5 st: -5 dB synthetic, -2.7..-4.7 dB real; +2: -0.6..-1.4; -5: -1.2..-1.7; +12: -4.2 | level kept *(believed)* | **Major flaw** |
| **Stereo clip** (per-channel render) | side/mid -7.0 → -1.8 dB at +3 st; L-R timing changes by up to ~3.7 ms | image kept *(believed)* | **Major flaw** (validated fix) |
| **Note segmentation** (legato 1-2 st) | 20-21 notes for 12 sung. A 2 st step splits the next note in two. Boundary error median 25-30 ms, p90 44-68 ms | boundaries on the transition, no same-pitch splits *(believed)* | **Weak** (validated fix) |
| Note fragmentation, real vocals | 99-303 notes/min, median length 82-155 ms, 32-73 % shorter than 120 ms, 11-36 % "tail fragments" | far fewer, word or syllable sized blobs *(unsure, estimate)* | **Weak** |
| Same-pitch syllables joined by a voiced consonant (level dip, no unvoiced gap) | merged: a -20 dB dip gives 3 notes for 6; at -10 dB, 3 of 5 joins are missed | split at syllable onsets (amplitude / attack cue) *(believed)* | **Weak** |
| Note start placement | 20-35 ms after the voice starts (worst on low voices) | onset including the consonant *(believed)* | **Weak** (validated fix) |
| **Time move**: consonants before the note | "s" stretched 82 → 123 / 160 ms (+80 / +150 ms moves), squeezed to 49 ms (-60 ms); "t" burst smeared or doubled | sibilant and plosive travel with the note *(believed)* | **Major flaw** |
| Time stretch of the note itself (0.5-2x) | duration within 3 ms, pitch within 1-5 c, HNR / flutter as input, tone within 1-3 dB, level ±0.1 dB | similar | **Good** |
| Stretching a note into its neighbour | next note squeezed 0.5 s → 0.08 s (6x) at 2x | neighbour shortened, but under the user's control *(unsure)* | **Hazard** |
| Formant / timbre after a move (third-octave error vs ideal) | ±0.3 st: 1 dB; ±1..±12 st: 2.0-4.0 dB mean (max 6-16 dB). 110 Hz voice 1-3 dB, 440 Hz voice 3-9 dB | formants kept, about 1-2 dB *(unsure)* | **Fair** |
| Small corrections (< 1 st, the default Snap) | 4-5 kHz band +3.5 / +6.8 / +11 dB at +0.3 / +0.5 / +0.9 st, -3..-8 dB downward (synthetic voice) | no HF timbre shift *(believed)* | **Weak** (real voices likely 1-3 dB) |
| Pitch landing in the note middle | median ≤ 1 c, p95 2.5-17 c (30 c at -12 st) | about 1-2 c *(believed)* | **Good** |
| Added roughness / flutter / HNR loss on steady notes | +0-0.5 c rough, flutter like the ideal, HNR within 1-2 dB | similar | **Good** |
| Octave-down of a vibrato note | pitch roughness 22 c rms (ideal 0.8) | smooth *(believed)* | **Weak** |
| Downward moves: impulsive artefacts | 16-68 transient spikes per 3 s phrase at -5 / -7 st (220 Hz voice) where the ideal has 0-4 | none *(believed)* | **Weak** (needs listening) |
| Breath noise inside moved notes | 4-10 kHz noise -1..-8 dB vs ideal on up-moves; periodicity 0.21-0.24 vs 0.09 at +12 | breathiness kept *(believed)* | **Fair** |
| Untouched audio after one edit | later notes shifted 0.3-1.9 ms (up to one period), -26..-44 dB different; "s" delayed up to 2.5 ms and 0.35-0.49 dB quieter | untouched stays in place *(believed)* | **Weak** |
| Untouched clip, nothing edited | bit-exact (-300 dB) | bit-exact | **Good** |
| Octave errors, high female "ee"/"eh" ≥ 784 Hz | whole notes read 1-2.5 octaves low (30 % of readings without AI; 4-22 % with AI; a 1319 Hz "ee" still a whole octave low with AI) | rare *(believed)* | **Weak** |
| Breathy voice (HNR 14 / 10 dB) | octave errors 24 / 33 %; 6 notes become 26-33 fragments | robust *(believed)* | **Weak** |
| Very low voices below 75 Hz (C2-D2) | 32-70 % of voiced readings found; notes missed or fragmented | tracks bass voices *(believed)* | **Weak** |
| Quiet recordings (peak -40 / -50 dBFS) | 16 / 26 % of voiced readings lost; 2-3 of 12 notes missed | level independent *(believed)* | **Minor** |
| renderPart vs full render | no join clicks; output differs from a full render (5-21 sample offsets) | n/a | **OK / minor** |
| Speed | analyse 12-14 s/min on 4 cores (20-24 s/min single core), about 95 % of it the AI check (1 s/min without). Render 1.2-2.1 s/min per channel | analysis a few s/min; edits play back immediately *(unsure)* | **Fair** (AI dominates) |

---

## Weaknesses, ranked by how much a listener would hear them

### 0. (Reliability, before any sound issue) A single NaN pitch reading makes `findNotes` loop forever

**Measured.**
- Synthetic C2 clip (65 Hz, `dbg_low2 36`): `analyse` returns in 0.7 s with one NaN reading (at 1.239 s, AI on or off).
  `findNotes` then never returns (killed after 100 s).
- Real material: `looperman/...die-slow-evil-type-rap` dropped an octave with ffmpeg (a deep male voice, about 50-70 Hz).
  It gives one NaN out of 72,639 readings, and `findNotes` was still running when killed after 60 s (`dbg_nan`,
  `dbg_hang`).
- The unaltered 13 clips and the octave-down Don and "Light" clips gave no NaN.

**Consequence in the plug-in.** The analysis job never finishes and spins a core at 100 %. The job pool has ONE thread
(`honeytune/HoneyTunePlugin.cpp:948`), so every later analysis and render in the project queues behind it: Honey Tune
stops working for the whole session. On close, the controller waits up to 60 s (`:303`) for a job that never checks
`closing`. That most likely means a long hang on close, or a forced thread kill (not tested in a host).

**Cause.**
1. The YIN parabolic step is not clamped (`dsp/src/PitchCorrector.cpp:196-198`; the NCC step at `:228` is clamped). When
   the CMND around the pick is nearly flat (a period beyond `tauMax`, below 75 Hz), `0.5 · (a - c) / den` is huge. The
   instrumented copy (`patched/PC_dbg.cpp`) shows `pEst` about -1500: then `span < 0`, the NCC loop does not run,
   `bestL = lround (pEst) = -1501`, `period = -1501`, and `midi = 69 + 12 · log2 (negative)` = NaN.
2. `findNotes` (`HoneyTune.cpp:270-300`): for a NaN, `t.midi[i] <= 0.0` and `t.midi[j] > 0.0` are both false, so
   `j == i` and `i = j` never advances.

**Fix (trivial).**
- Clamp the parabolic offset to ±0.5 and require `pick + 1 < tauMax`.
- Reject `period <= 0` or non-finite as unvoiced.
- In `analyse`, map non-finite readings to 0.
- In `findNotes`, make the outer loop always advance (`i = std::max (j, i + 1)`) and test `!(midi > 0)`.
- Let long jobs check `closing`.

The same NaN reaches the live Voxology Pitch state (`centreA`, `corr`). A NaN `corr` silently disables correction until
reset (`std::abs (NaN) >= 0.03` is false). That is outside this audit and not verified.

### 1. A moved note starts at the old pitch and scoops in; its end slides back (every edited note)

**Measured.** I moved two 1 s notes per voice (110 / 220 / 440 Hz, steady and with vibrato) by -12..+12 st. I then read the
output pitch with the independent YIN, against the ideal resynthesis (`audit_render shift`, mean over 12 notes per row):

| move (st) | onset error, 8-50 ms (c) | time until within 15 c (ms) | end error, last 40 ms (c) |
|---|---|---|---|
| ±0.3 (default Snap) | 18-20 | 32-34 | 0.6 |
| ±1 | 61-65 | 60-61 | 2.4-2.6 |
| ±2 | 129-133 | 77-78 | 5 |
| ±5 | 105-148 | 99 | 14 |
| ±7 | 112-120 | 107-110 | 19 |
| ±12 | 75-142 | 121-122 | 33 |

A raw trace of a 220 Hz note moved +5 (`dbg_trace 5 57`) shows the pattern:
- +14 to +32 ms: exactly -500 c, the unshifted original pitch.
- +38 ms: -166 c (half the move).
- Then a glide, reaching -10 c only at about 115 ms.

On real vocals (all 13 clips), every 4th note moved +5 st gives a first-40-ms mean error of 154-167 c and a last-40-ms
error of 65-85 c. The middle of the note is fine (median 3-11 c). Moving +2 st gives 62-74 c at the start and 27-31 c
at the end.

Two cases are worse:
- Two notes 50 ms apart (a plosive gap), both +4 st: the first 30 ms of each note is at the original pitch (-389..-421 c),
  and the last 40 ms drops back by about 3 st.
- 12 short syllables moved +2 st: only 24-27 % of the voiced time reaches the target.

**Cause** (two parts, both confirmed by experiment):
1. `planShift` sets `base = 0` for every reading outside a note (`dsp/src/HoneyTune.cpp:379-393`). It then smooths with a
   forward + backward 25 ms one-pole (`:394-405`). At an isolated note's edge the smoothed shift is therefore only about
   50 % of the move, and it needs about 3 τ (~75 ms) to reach 90 %.
2. The note's first voiced reading comes 20-35 ms after the voice starts (see #6). The Plan returns period 0 there
   (`HoneyTune.cpp:49-58`), and the shifter treats period 0 as unvoiced with ratio 1
   (`dsp/src/PitchCorrector.cpp:458-461`). So the attack is not shifted at all.

**Fix (validated** in `patched/HoneyTune_fix.cpp`, flags `FIX_HOLD` + `FIX_ONSET`):
1. `FIX_HOLD`: between notes, use the nearer note's move instead of 0. Smoothing then only acts between notes that move
   differently.
2. `FIX_ONSET`: after analysis, extend each voiced run back to the audible onset (up to 50 ms) and into the release (up to
   40 ms), where the level is within 30 dB of the note and the zero-crossing rate is below 3.5 kHz (that keeps the "s"
   out). The run takes the first or last reading's pitch.

Results with both fixes:
- onset error 1.4-9 c, settled in 5-13 ms, end error 0.7-2.5 c (`render_shift_fixed.txt`)
- the two-notes-50-ms case: 0-26 c
- short syllables: 93 % on target (ideal 98 %)
- note starts come out at +5..+7 ms instead of +21..+35 ms

A production version should track the onset pitch backward properly (the AI or YIN run backwards from the first sure
reading) rather than copy one value.

*Melodyne:* the whole blob, including its attack, moves to the new pitch. Transitions are only smoothed between connected
notes (Pitch Transition tool) *(believed)*.

### 2. Moved notes get quieter, mostly upward (2-5 dB)

**Measured.**
- Synthetic notes, level vs the INPUT: +2: -2.2 dB, +5: -5.2 dB, +7: -5.0 dB, +12: -4.2 dB, -2..-7: -1.4..-1.6 dB.
  Versus the ideal at +12 it is -6.0 dB on every voice.
- Real vocals, median level change of the moved notes: +5 st: -2.7..-4.7 dB (-3.3 typical), +2: -0.6..-1.4 dB, -5:
  -1.2..-1.7 dB. A note moved up a fourth simply sounds weaker.

**Likely cause (not validated by experiment).**
- The overlap-add is normalised by the window sum: `acc / ws` (`PitchCorrector.cpp:674`). On up-shifts the neighbouring
  grains' windows overlap each glottal pulse (Σw ≈ 1.3 at +5, 2 at +12), so pulses are divided down.
- The grain marks follow a smooth drift and are not centred on glottal pulses. Overlapping grains therefore carry the same
  pulses at different positions and partly cancel, which is also behind #8's envelope errors.
- Classic, epoch-centred PSOLA keeps the pulse amplitude.

The compensation fix below works whatever the exact split between these causes.

**Fix.** Per-note loudness compensation in `render()`: compare each moved note's short-term RMS (input vs output, 20 ms
windows), smooth the ratio and apply it, then clamp to ±6 dB. Or use energy-preserving weights (√Σw² instead of Σw) for
the voiced grains. Measure again with `audit_render shift` (the "level vs input" column).

### 3. Stereo clips: left and right are pitch-shifted with different grain joins

**Measured** (`audit_render2 stereo`; L and R = the same voice plus different early reflections): side/mid energy in the
notes is -7.0 dB in, -1.8 dB out at +3 st, and -6.2 dB at +0.3 st. The L-R cross-correlation lag of single notes moves by
up to 178 samples (3.7 ms). The image smears and the mono fold-down combs.

**Cause.** The plug-in renders each channel on its own (`honeytune/HoneyTunePlugin.cpp:899-905`). Each call makes its own
1-channel shifter (`HoneyTune.cpp:438 prepare (sr, 1)`). Its join decisions (`alignMark`, `grainEnergy`: quiet-moment
timing, `PitchCorrector.cpp:476-483`) read that channel's own waveform.

**Fix (validated, `FIX_STEREO`).** Render both channels through one 2-channel `PitchCorrector`; it already shares the mono
mix for marks. Side/mid then stays at -6.2 dB (+3 st) and -6.9 dB (-5 st). This needs a 2-channel `render` /
`renderPart` / `warp` API. Most of the per-channel cost (detection, marks) would also be shared; the render time was not
measured.

### 4. Note segmentation: legato notes split in two, release tails split off, boundaries placed late

**Measured** (synthetic, `audit_analysis`):

| Case | Sung | Engine | Detail |
|---|---|---|---|
| Legato melody, 1-2 st steps, 40 ms glides, vibrato 0 / 25 / 60 c | 12 notes, 11 boundaries | 20 / 21 / 16 notes | 4-8 extra boundaries; boundary error median 25-30 ms, p90 44-53 ms |
| 180 ms legato notes | | | boundaries median 24 ms late, p90 68 ms |
| Wide vibrato at 220 Hz | 7 notes | 5 of 7 lose their last ~90 ms | release becomes a separate note 0.4-0.8 st off |

The pattern: after every 2-semitone step, the next note is split into two same-pitch notes about 100 ms in.

**Real vocals** (`real_out.txt`): 99-303 notes/min, median note length 82-155 ms, 32-73 % of notes shorter than 120 ms.
11-36 % are tail fragments (shorter than 120 ms, touching a longer neighbour less than 1.5 st away). There are 5-88
touching pairs less than 1 st apart per clip. A sung pro lead ("No Good Reason", dry) has 157 notes/min, median 102 ms
and 35.5 % fragments.

**Why a listener hears it.** A user drags the blob they see; the fragment stays behind, so the end or start of the note
plays at the old pitch. Or the user has to hunt down the fragments. Late boundaries also put the next note's first
20-70 ms on the previous note's move.

**Cause.**
- `findNotes` restarts its reference from the transitional, 180-ms-averaged value right after a boundary (`ref = sm[j]`,
  `HoneyTune.cpp:284-290`). When the average settles on the new note, it again exceeds 0.6 st → a second boundary.
- The boundary is put where the 180 ms average departs (`:283`), not where the sung pitch changes.
- The end of a phrase truncates the averaging window, so the release follows the vibrato.
- Short tails only join the previous note when they are shorter than 50 ms (`:293-298`).

**Fix (validated, `FIX_LEGATO`).** After a boundary, take the new note's level from the average one window later (offline
look-ahead), and do not feed transitional values into the reference. Move the boundary to the first reading where the raw
pitch is nearer the new level than the old. Result: 12-13 notes for 12, boundary error median 7-9 ms (p90 14-35 ms),
and the vibrato clip drops to 8 notes for 7 (from 13). Also merge tail fragments shorter than about 120 ms into the
neighbour when it is within 1 st and touching.

### 5. Time moves: the consonant before a moved note does not travel with it

**Measured** (`audit_render2 time`; clip: A | "s" | closure + "t" burst | B | breath | C; edits on B):

| Edit on B | "s" length (82 ms as sung) | "t" burst (4.5 ms, 1 peak as sung) | Other |
|---|---|---|---|
| +80 ms | 123 ms | 8.5 ms wide | |
| +150 ms | 160 ms | 3 peaks | breath after B squeezed about 4x |
| -60 ms | 49 ms | 4 peaks | |

"sing" becomes "sss-ing", and a "t" flams.

**Cause.** Notes cover only voiced readings. `timeMap` places its knots at the note's voiced start and end
(`HoneyTune.cpp:492-512`), so the consonant lies in the gap, and the gap is stretched linearly. `warp` reads unvoiced
5 ms grains at the mapped spot (`:577-578`). The late note start (#6) leaves even the first 15 ms of the voice in the gap.

**Fix.** For time edits, give each note a "sound start" that includes its leading unvoiced consonant: an onset detector on
HF energy / spectral flux back to the previous silence or breath. Map that consonant rigidly with the note (1:1). Take the
stretch or squeeze only from silence and breath, at a 1:1 rate where possible, or with breath-friendly crossfades
instead of 4x squeezes.

*Melodyne:* the blob includes its sibilant (drawn hatched) and moves with it. Melodyne 5 also detects sibilants separately
*(believed)*.

**Fixed in Beta 40** (timeMap with the track; warp's drift): the sound next to each moving note goes with it 1:1, only
silence / breath stretches. Same measure: +80 ms s 82 ms (as sung), t 1 peak 4.5 ms; +150 ms s 82 ms, t 1 peak, B on time,
C identical; -60 ms t 1 peak, s 42 ms (no silence to take the squeeze; was 49). #7 (a stretch crushing the next note)
was already closed in Beta 38: the editor keeps a stretched note before its neighbour.

### 6. Note starts are 20-35 ms late (and the cause of half of #1 and #5)

**Measured.** Engine note start minus the true voice onset: +35 ms at 82 Hz, +31 at 97-110 Hz, +27 at 131, +23 at 220,
+19-21 at 329-659 Hz. Rap syllables: +23-27 ms (p90 46 ms). In real vocals, the independent tracker hears a voice
matching the note pitch for a median 9-13 ms (p90 13-33 ms) before the note starts.

**Cause.**
- `analyse` keeps a reading only if its stamp is later than the previous one (`HoneyTune.cpp:180`). Voiced readings are
  stamped 0.5 x (len + P) back (`PitchCorrector.cpp:256`), up to about 18 ms on low voices; unvoiced ones only
  0.25 x sr / 75 back (`:369`). So the first voiced readings after an unvoiced stretch are thrown away.
- Voicing needs 2 voiced readings (`:189-190`), and the YIN window (27 ms) must fill.

**Fix.** Validated as `FIX_ONSET` in #1. Better still, offline: after the first sure reading, insert the dropped readings
by re-analysing backwards.

### 7. Stretching a note can crush the next note

**Measured.** B (0.6 s) stretched 2x before a 0.2 s gap: C (0.5 s) is mapped into 81 ms (6.3x faster). At 1.5x, C becomes
0.39 s. C's audio diff is +1.2 dB, i.e. garbled.

**Cause.** `timeMap` clamps the next note's start after the previous end but keeps its end (`HoneyTune.cpp:504-505`).

**Fix.** Either ripple (push later notes and their gaps), or cap the neighbour squeeze (e.g. ≤ 1.3x) and stop the drag
there, showing it in the editor.

### 8. Formant and timbre: PSOLA envelope error grows with pitch and move size

**Measured** (third-octave envelope vs ideal, `render_shift2.txt`):

| Move | Mean error | Notes |
|---|---|---|
| ±0.3 st | 0.9-1.0 dB | |
| ±1 st | 2.2-3.8 dB | |
| ±2..±7 st | 2.3-4.0 dB | |
| ±12 st | 2.0-2.3 dB | |
| 110 Hz voice | 1-3 dB | |
| 440 Hz voice | 3-9 dB, single bands to 16 dB | e.g. a 440 Hz "a" at +5: the 1.26 kHz band 17 dB low, the 3.2 kHz band 15 dB low |

**Cause.** Two-period Hann grains sample the envelope only at the original harmonics (`PitchCorrector.cpp:525-555`). On
high voices the new harmonics fall between them. This is the textbook PSOLA limit.

**Fix.** For voices above about 250 Hz, or moves of more than 2 st, add an envelope-correction filter after the PSOLA
output. Estimate the true or cepstral envelope of input and output per 10-20 ms and apply the ratio with a short
minimum-phase FIR. Or render voiced frames with a harmonic + noise model with an explicit envelope.

### 9. Default small corrections (two-band mode) shift the "air" band with the pitch

**Measured** (`dbg_level 57 <shift> 0 a`, 220 Hz "a"; 4-5 kHz bands vs ideal):

| Shift (st) | +0.3 | +0.5 | +0.9 | -0.5 | -0.9 | +1.1 (split off) |
|---|---|---|---|---|---|---|
| 4-5 kHz vs ideal (dB) | +2.8 / +3.5 | +4.5 / +6.8 | +7.2 / +11.2 | -2.8 / -5.7 | -4.2 / -7.6 | -0.7 / -0.1 |

In the full matrix, the >3 kHz band error at ±1 st is +5.4..+7.5 dB (up) and -1.5..-4.8 dB (down).

My synthetic F5 is sharp, so real voices will likely show 1-3 dB. But this is the DEFAULT state (Snap 100 % moves every
note up to ±50 c), so neighbouring notes corrected in opposite directions get brighter and duller "air".

**Cause.** In split mode (|corr| ≤ 1 st) the band above 2 kHz is read at the grains' sliding offset, i.e. resampled
(`PitchCorrector.h:207-212`, `.cpp:491-494`, `580-601`). Its resonances move by the pitch ratio.

**Fix ideas.**
- Keep the resampling (it avoids the buzz) but undo its formant move with a short envelope-ratio correction on the high
  band (its resonances should not move). A spectral-tilt correction per ratio might be enough; that is untested.
- Or use PSOLA for the high band too, with longer grains and randomised grain positions for the noise.

### 10. Octave-down and big downward moves: jitter and impulsive grain artefacts

**Measured.**
- Tenor 220 Hz with vibrato at -12 st: pitch roughness 21.7 / 21.9 c rms vs 0.8 c ideal (steady note: 13.9 vs 0.4 on
  the "e").
- Downward moves on a 3 s phrase (`dbg_clicks`), 2nd-difference transient spikes:

| Voice | -5 st | -7 st | -12 st |
|---|---|---|---|
| 220 Hz | 16-23 (ideal 0) | 32-68 (ideal 4) | 257-266 (ideal 80) |
| 440 Hz | 4 (ideal 0) | 4 (ideal 0) | 39-55 (ideal 0) |

Real vocals at -5 st: transient counts rose on two owner clips (178 → 232, 95 → 165). Listening is needed to confirm
how audible this is.

**Cause.** For ratio < 0.6 the grain is centred on "the waveform maximum within ±P/2" (`PitchCorrector.cpp:500-510`). That
peak flips between cycles under vibrato. Sparse, unnormalised grains on downward moves (`:520-524`, `:554`, `:673`) make
the output more pulse-like than a real lower voice.

**Fix.** Proper epoch (GCI) marking with continuity (LPC-residual peaks tracked with a pitch-consistent DP), done once
offline for the whole clip. Fill the gaps on large down-shifts with the grain's own decaying tail (longer grains, 3-4
periods, with a window that does not truncate the vocal-tract ringing).

### 11. Untouched audio is not left alone after an edit

**Measured** (`audit_render untouched`):
- Nothing edited: bit-exact.
- One note moved by +3 / -3 / +0.5 st: every later note is delayed by +14 / +91 / -73 samples (0.3 / 1.9 / -1.5 ms) and
  differs at -26..-44 dB even after aligning.
- Unvoiced "s" / breath / "sh" between moved notes: same level (±0.5 dB; the "s" is 0.35-0.49 dB quieter), but shifted by up to
  ±119 samples. They differ at -6..-18 dB after alignment (fractional cubic reads).
- The offset can reach a full period (about 12 ms on an 82 Hz voice). It also makes partial renders differ from full ones
  (#15), and it matters for stereo or multi-mic takes and for comping against other takes.

**Cause.** In unvoiced stretches the shifter's drift "just holds" (`PitchCorrector.cpp:485-487`). The header still says it
is "eased back to 0 in breaths" (`PitchCorrector.h:236`), which is stale.

**Fix.** In silence or quiet breath (level < -50 dBFS for > 20 ms) or before an untouched note, glide the drift back to 0,
or jump it at a near-silent sample. With drift 0 and ratio 1, untouched notes come out bit-exact again (the existing
"untouched = exact" property).

### 12. Neighbouring legato notes moved in opposite directions: a V-shaped dip

**Measured.** 57 → 59 legato with a 50 ms glide, moved +2 / -2. The output dips to 56.0 at -20 ms (-294 c below the path)
before the transition, because the engine boundary sits 41 ms early and the shift step is smoothed symmetrically. With
+2 / 0, the error is -142 c at -20 ms.

**Cause.** `planShift` smooths the step of the move around the detected boundary (`HoneyTune.cpp:394-405`), ignoring where
the sung glide actually is.

**Fix.** In a transition, shape the shift by the sung pitch's progress between the two notes:
`shift = mA + (mB - mA) · clamp((sung - pitchA)/(pitchB - pitchA), 0, 1)`. The output then glides from targetA to
targetB with the singer's own glide shape, which is what Melodyne's transitions keep *(believed)*. Fixing #4's boundary
placement helps too.

### 13. High notes, breathy and raspy voices: octave errors (and then a broken render)

**Measured.**

| Case | Octave errors (share of readings) | Other effects |
|---|---|---|
| High female "ee"/"eh", detector alone (`dbg_high`) | 659 Hz: 3.9 % (an 83 ms run of the "ee" note); 783 Hz: 29 %; 987 Hz: 32 % (1, 1.6 and 2.6 octaves low); 1174 Hz: 100 % | |
| Same, with the AI check | 659 Hz: 0.8 %; 783 Hz: 4.3 %; 987 Hz: 21.7 %; 1174 Hz: 33.7 % | 1319 Hz "ee": whole note still an octave low; 1175 Hz "a" notes split in two; 987 Hz: 12 notes for 3 |
| Breathy voice, HNR 23 / 17 / 14 / 10 dB | 0 / 0 / 24 / 33 % | 6 notes → 26-33 fragments at HNR ≤ 14 dB |
| Subharmonic (rasp) 15 / 30 / 50 % | 0 / 17 / 61 % | |

A mis-detected soprano "e" then renders badly: at -5 / -7 st the tone error is 17-19 dB and the level drops 14-15 dB.

**Cause.**
- YIN runs on a 12 kHz copy with integer lags (`PitchCorrector.cpp:52`). At 880 Hz the period is 13.6 samples, so the
  CMND at the true period is raised. The first-dip-under-0.12 rule (`:171-177`) then picks 2P or 3P, whichever falls near
  an integer lag.
- `kMaxHz = 1000` (`PitchCorrector.h:94`).
- The octave guard (`:232-250`) keeps a wrong octave because the waveform also repeats at 2P.
- The AI only overrides with agreeing context or confidence > 0.8 (`HoneyTune.cpp:139-149`), so a note that is wrong
  from start to end stays wrong.

**Very low voices.** Below the detector's 75 Hz floor (`kMinHz`, `PitchCorrector.h:94`), notes are mostly missed:
- C2 (65 Hz), 3-note clip: 32 % of voiced readings found (46 % with AI), 1-2 of 3 notes.
- C#2-D2 (73 Hz): 55-70 % found, the notes fragmented (5-6 notes for 3).
- E2 (82 Hz) and up: fine.

Bass singers, deep rap and vocal fry reach these notes. Melodyne tracks bass voices *(believed)*. Fix: offline, lower the
floor to about 50 Hz (longer YIN window).

**Fix (offline only, the live path can stay).**
- Re-run the period search at the full rate (or 24 kHz) with interpolated CMND minima.
- Score all candidates (P, 2P, 3P …) rather than taking the first dip.
- Raise the offline maximum to about 1600 Hz.
- Take a note-level octave vote (median over the note of CREPE and the detector) instead of 30 ms windows.

**Fixed in Beta 40 (offline analysis only: the live Pitch detector and its tests are unchanged).** All in
`dsp/src/HoneyTune.cpp`:
- `checkPeriods` (new, right after the live detector's pass in `analyse`): every pitch reading is checked again at the
  full sample rate. How well the waveform repeats (normalised difference, dips interpolated between lags) is measured at
  the period read and at P/2 ... P/16, up to 1600 Hz. The shortest candidate that repeats about as well is taken: at most
  2x the error at P plus 0.03, never above 0.2, and its 2nd and 3rd multiples must repeat too. A low voice with a towering
  2nd harmonic repeats nearly, but not quite, as well at P/2, and keeps P. Then each reading takes the candidate nearest
  to the loudness-weighted median of the readings within 130 ms. Readings stamped in silence (50 dB under the singing)
  are dropped: after a dead stop the live detector went on reading a made-up pitch for 20-60 ms.
- `holdVoicing` (new): a gap of up to 80 ms between two readings at the same pitch is filled in where the waveform still
  repeats at that pitch and the level holds (median within 5 dB). This is for breathy voices; a consonant or a level dip
  stays a gap.
- `findNotes`: the 180 ms average no longer reaches across a jump of more than 3 semitones between neighbouring readings.
  A resonance ringing on after the voice stops used to pull the average and split the note.
- `aiCheck`: a vote over each stretch of the line. Where 60 % of CREPE's estimates hear another octave, the whole stretch
  moves. The waveform has the last word, here and for the old per-check moves: down only if P doesn't repeat cleanly and
  2P clearly does, up only if P/2 repeats too (each cycle at its own level, so rasp still counts).

Before -> after (same tools, rebuilt against the branch; the audit's numbers above are the older baseline):

| Case | Before | After |
|---|---|---|
| High notes (`dbg_high`), detector alone: octave errors / notes for 3 | 659 Hz 3.8 % / 6, 784 Hz 29.6 % / 5, 988 Hz 32.2 % / 8, 1175 Hz 100 % / 8, 1319 Hz 100 % / 10 | 0 / 3, 0 / 3, 0 / 3, 0 / 3, 34.2 % / 3 |
| Same, with the AI check | 0.8 % / 3, 4.2 % / 5, 22.3 % / 9, 34.2 % / 12, 66.2 % / 13 | 0 / 3, 0 / 3, 0 / 3, 0 / 3, 34.2 % / 3 |
| Breathy, HNR 23 / 17 / 14 / 10 dB (`audit_analysis`, AI on): octave errors | 0 / 0 / 24.1 / 32.5 % | 0 / 0 / 0 / 1.4 % |
| Same, notes for 6 | 11 / 8 / 21 / 18 | 6 / 6 / 6 / 9 |
| Same, detector alone: octave errors, notes | 0, 0, 54.3, 57.0 %; 11, 8, 20, 17 | 0, 0, 0, 1.5 %; 6, 6, 6, 8 |
| Rasp (subharmonic) 15 / 30 / 50 %: octave errors, AI on | 0 / 17.2 / 61.2 % | 0 / 0 / 61.2 % |
| Same, detector alone | 0 / 17.2 / 61.2 % | 0 / 17.2 / 61.2 % |
| Rasp / rough clips, notes for 6 (sub 15 / 30 / 50, rough) | 8 / 7 / 7 / 11 | 6 / 6 / 5 / 6 |
| Rap syllables @48 / @55: notes for 40, one-to-one | 44 / 47, 33 / 34 | 40 / 42, 37 / 38 |
| Legato 1-2 st (vib 0 / 25 / 60 c): notes for 12 | 13 / 12 / 11 (5.1 / 2.1 / 0 % octave errors) | 11 / 11 / 11 (0 %) |
| Vibrato wide/slow @57 / @76: notes for 7 | 10 / 11 | 8 / 7 |
| Quiet -20 / -50 dBFS: notes for 12, missed | 15, 0 / 14, 3 | 12, 0 / 13, 2 |
| False voicing (unsung moments read as pitch), median over the clips | 5.4 % (p90 12.8 %) | 0.6 % (p90 3.5 %) |
| New clips: high legato line 659-1175 Hz (8 notes) | 14 notes, 15.4 % octave errors (detector alone 39.7 %) | 8 notes, 0 % |
| New clips: octave leaps; "u" / "i" with the 2nd harmonic on F1 | 12 / 12; 0 % | unchanged |
| Low voices (73-130 Hz), melisma, repeated syllables, legato boundaries | | unchanged (or fewer extra boundaries) |
| Real vocals, 13 clips: notes in all | 2693 | 2576 (-4 %; 12 clips fewer or equal, one acapella 171 -> 175) |
| Real vocals: notes under 120 ms | 21-49 % | 15-46 % (that acapella 37.4 -> 41.1 %) |
| Real vocals: tail fragments | 0-6.9 % | 0-5.6 % (one rap clip 2.8 -> 3.2 %, one pro lead 0.7 -> 1.0 %) |
| Real vocals: AI octave fixes needed | 0-112 per clip | 0-63 (the detector is right on its own) |
| analyse() speed, AI on (the 2.5 min pro lead / the owner's 1.75 min song, 4 cores) | 13.6 / 16.3 s/min | 13.3 / 16.3 s/min |
| analyse() speed, AI off | 1.25 / 0.96 s/min | 1.39 / 1.04 s/min (+11 / +9 %) |

Real vocals have no ground truth. An octave changed on 0-4 % of the sung readings (6.2 s in 16.8 min). In 10 of the 12
clips with a change the waveform repeats as well or better at the new period. The exceptions are one rap clip's
octave-ambiguous passage and one acapella, which is slightly worse. CREPE, where it is sure, hears the new octave in
87 % of that time (3.9 of 4.5 s) and the old one in 11 %. Renders, with the same moments moved in both: the middle and
end of moved notes are as before or better, and Snap 100 % starts and ends improved (5.9 -> 4.7 c, 5.0 -> 4.1 c). The first 40 ms of a moved note is worse on average at +5 / -5 st (mean 42 -> 49 c, 36 -> 43 c), and a -5
render has 4 % more clicks. Most of this comes from notes that now include their attack (a fragment before the note used
to stay unmoved); the attack is the hardest part to shift (#1, #6, #10).

**Still open.**
- A synthetic 1319 Hz "o" whose F1 / F2 lie far below the pitch (noise-dominated, not how sopranos sing) is read an octave
  low.
- Rasp at 50 % is read at its waveform's period (2P). CREPE hears it there too.
- An extreme 2nd harmonic (18 dB over the 1st, a sharp F1) goes an octave up where CREPE says so. That was so before too
  (19-21 % of the readings).
- Breathy HNR 10 dB: 1.4 % octave errors and 8-9 notes for 6.

### 14. Breath noise inside moved notes is thinned

**Measured** (`audit_render buzz`; breathy voice, harmonics to 3.5 kHz, so the band above 5 kHz is noise only):
- 4-10 kHz level vs ideal: -1.3..-1.6 dB at +5 st on the 147 / 220 Hz voices (-8.1 dB on the 330 Hz voice), and -7.1 dB
  at +2 st on the 220 Hz voice.
- At +12 st: -3.6..-6.1 dB.
- Noise periodicity at +12: 0.21-0.24 (ideal 0.09-0.10); at +5: 0.14-0.17 (ideal 0.09). This is mild buzz.

An airy up-moved note sounds cleaner and thinner than the singer.

**Cause.** Uncorrelated noise averaged with Σw-normalised weights loses power (Σw² / (Σw)²), and repeated cycles repeat
the noise.

**Fix.** Separate the noise and process it apart (harmonic + noise split: comb or harmonic mask), or at least use
power-preserving weights for the >4 kHz part.

### 15. Smaller items

- **Partial re-render ≠ full render.** The partial render starts a fresh shifter (drift 0) 0.3 s early
  (`HoneyTune.cpp:691-709`). The region then differs from a full render by 5-21 samples of offset and different joins:
  -1.6..-21 dB after alignment. There are no clicks at the joins (0 extra transients; 2nd-difference peaks the same as the
  full render). The export therefore depends on edit history. Fixing #11 makes it converge.
- **Per-note formant.** The effective move is within 0-1.5 st of the request for ±3 / ±6. ±1 st requests had no measurable
  effect on the 131 / 440 Hz voices (0.5 st search grid). Level changes are ±1-4.6 dB. 440 Hz: -3 st acts like -1, and
  ±6 like ±7.5. Envelope error vs the requested ideal is 1.1-6.2 dB. OK, but loudness should be compensated (#2).
- **Fast melisma.** 80-110 ms notes: 3 of 15 boundaries are lost (4 notes merged with a neighbour), because the 180 ms averaging is longer
  than the notes. A 300 ms slide becomes 2 extra notes.
- **Same-pitch syllables** separated only by a voiced consonant or a level dip are merged (-20 dB dip: 3 notes for 6).
  "s", breath and plosive gaps split correctly. Melodyne also uses amplitude / attack cues to separate notes *(believed)*.
  Fix: add an energy-onset split (a rise > 6-8 dB within 30 ms after a dip, inside a voiced run).
- **Quiet recordings.** The voicing gate is absolute: -55 dB (`PitchCorrector.cpp:20`). A clip peaking at -40 dBFS loses
  16 % of its voiced readings and 2 of 12 notes; at -50 dBFS, 26 % and 3 of 12. The AI "found" step recovers part of
  this. Fix: offline, gate relative to the clip's level (e.g. its 95th-percentile frame level minus 50 dB).
- **Clean digital-silence tails.** Voicing hangs 60-80 ms into a decay to digital silence and makes junk notes at
  -80..-190 dBFS. The cause is the 10 ms one-pole level gate on power, which falls only about 4.3 dB per 10 ms
  (`PitchCorrector.cpp:63, 185-186, 623`). It vanishes with a -74 dB noise floor, so it only matters for gated or comped
  clips.
- **Plan ignores clarity.** Live, the detector applies less correction to noisy or raspy frames (clarity); the Guide path
  applies the full move to every voiced frame (`PitchCorrector.cpp:357-358`, `375-382`). This may contribute to the
  breathy-voice issues.

---

## What is already good (do not break it)

- **Pitch accuracy of the analysis on clean voices.**
  - 82-659 Hz: median 0.7-1.6 c, p95 2.8-8 c.
  - Note pitch (median of the middle) within 0.1-1.4 c.
  - Readings are stamped within about 2 ms of the audio.
  - No octave errors from 82 to 440 Hz, even with the AI check off.
  - The AI check repairs most octave slips at 659-783 Hz (3.9 % → 0.8 %, 29 % → 4.3 % of readings).
- **No false notes** from breaths (-14..-34 dB), "s", "sh" or plosives on their own.
- **Vibrato never splits a note's body** (±30-150 c, 3.5-7 Hz). Only the release is affected (#4).
- **The middle of a moved note lands accurately:** median ≤ 1 c, p95 2.5-17 c, with no added warble on steady notes
  (roughness +0-0.5 c). Flutter matches the ideal; HNR is within 1-2 dB.
- **Low male voices keep their formants well:** 1-3 dB envelope error even at ±12 st.
- **Untouched clip = bit-exact output.** renderPart joins are click-free. The cancel flag works (existing test).
- **Time stretching of held notes is clean** (0.5-2x): duration within 3 ms, pitch within 1-5 c, HNR and flutter as the
  input, tone within 1-3 dB, level ±0.1 dB.
- **Unvoiced sounds between moved notes keep their level** (±0.5 dB) and are not pitch-shifted.
- **Breath noise is not turned into strong buzz** at moderate moves (periodicity 0.11-0.17 vs 0.09-0.10 ideal).
- **Analysis without the AI is fast** (about 1 s per minute of audio).

---

## Speed

Measured with `audit_real <file> <name> time` on this 4-core container, which had other load (load average about
1.8). "Single core" = `taskset -c 3`. Audio was 48 kHz mono. Render is per channel, so a stereo clip costs twice the
render time (and would cost the same as mono with the #3 fix).

| | analyse, AI on | analyse, AI off | full render (one channel) |
|---|---|---|---|
| Don (owner, 1.75 min), 4 cores | 13.6 s/min | 0.99 s/min | 1.18 s/min |
| Don, single core | 24.2 s/min | 0.94 s/min | 1.30 s/min |
| "Light" pro lead (2.53 min), 4 cores | 11.9 s/min | 1.34 s/min | 2.11 s/min |
| "Light", single core | 19.5 s/min | 1.32 s/min | 2.11 s/min |
| All 13 clips, 4 cores, while other audits ran | 11.5-17.7 s/min | 0.95-1.36 s/min | 1.2-2.3 s/min |

- **The AI check is about 93-96 % of the analysis time.** CREPE tiny runs every 30 ms over every moment within 40 dB of
  the peak (`HoneyTune.cpp:80-109`). A 3-minute stereo vocal takes about 36-42 s to analyse on 4 cores (60-75 s on one
  core). A full render then takes about 7-13 s.
- **Edits are cheap.** renderPart re-renders only around the edit: a one-note edit costs well under 0.2 s.
- *Melodyne 5 (unsure):* analysis of a 3-minute vocal takes a few seconds on a modern machine, and edits play back
  immediately (rendered on the fly). Honey Tune's analysis is roughly 5-10x slower. Its first full render is a
  noticeable wait; edits after that are fast.
- **Speed-up ideas.**
  - Run CREPE only where it can change something: low clarity, octave-ambiguous moments, near the 659 Hz+ region, and
    unvoiced but loud moments. A note-level vote needs 3-5 estimates per note, not 33 per second.
  - Batch the convolutions (im2col + SIMD GEMM).
  - Use a 2-channel render (#3).

---

## Suggested order of work (largest audible gain first)

0. #0: the NaN clamp and the `findNotes` loop guard (minutes of work; a session-wide lock-up otherwise).
1. #1 + #6: the onset / hold fix (validated: onset 65-148 c → 1-9 c) and the backward onset tracking.
2. #2: loudness compensation of moved notes.
3. #3: render stereo through one shifter.
4. #4: segmentation (validated look-ahead fix) and merging of tail fragments.
5. #5 + #7: notes own their leading consonant for time moves, and the neighbour-squeeze cap or ripple.
6. #11: drift back to 0 in silence (untouched audio exact again; partial = full).
7. #13: offline high-resolution period search with a note-level octave vote.
8. #8 / #9 / #14 / #10: envelope correction, the split-mode HF fix, the noise path, epoch marking.

After each step, re-run `audit_render shift`, `audit_analysis` and `audit_real` (below). The numbers in this report are
the baseline.

---

## Tools and outputs (all in `/tmp/claude-0/audit-eng-work`)

Build the engine:
`cmake -S /tmp/claude-0/audit-src -B /tmp/claude-0/audit-eng -DCMAKE_BUILD_TYPE=Release -DVOX_BUILD_PLUGIN=OFF -DVOX_BUILD_TESTS=OFF && cmake --build /tmp/claude-0/audit-eng --target vox_dsp -j3`

Build each tool:
`g++ -std=c++20 -O2 -I/tmp/claude-0/audit-src/dsp/include <tool>.cpp /tmp/claude-0/audit-eng/dsp/libvox_dsp.a -lpthread -o <tool>`

| File | What it does |
|---|---|
| `synth.h` | Synthetic vocal generator with exact truth (pitch per sample, notes, unvoiced events), with an IDEAL re-synthesis at any pitch or formant |
| `meas.h` | Independent measurements: FFT-YIN (free / guided), LTAS third-octave envelope difference, HNR, flutter, roughness, noise buzz, transient count, best-lag diff, WAV / f32 I/O |
| `audit_analysis.cpp` | Analysis accuracy on 40+ synthetic clips: per-reading recall / false voicing / cents error / octave errors / lag; notes missed / split / merged / false; start / end / pitch error; legato boundary error. `./audit_analysis` (all), `./audit_analysis -v "<name>"` (one clip, note lists). Output: `analysis_final.txt` (all clips, AI on, then AI off for range / breathy / rough / subharmonic) |
| `audit_render.cpp` | Render quality. Sections: `shift` (R1 matrix: landing, onset / settle / end, envelope, HNR, flutter, roughness, level vs ideal and input), `buzz` (R2), `unvoiced` (R3: "s" / breath, short gaps, legato pairs, short syllables), `untouched` (R4), `formant` (R5), `time` (R6 move / stretch), `part` (R7 renderPart vs full), `stereo` (R8). Binaries: `audit_render` (shift / unvoiced / untouched), `audit_render2` (all sections). Outputs: `render_shift2.txt`, `render_unv2.txt`, `render_buzz_unv.txt`, `render_part.txt`, `render_time.txt`, `render_part_stereo_formant.txt` (stereo and formant parts; its renderPart part is an older, too-dense clip) |
| `audit_real.cpp` | Real vocals: note counts / lengths / fragments, voiced attack outside notes, default render (Snap 100 %) and every-4th-note moves (+2 / +5 / -5) re-measured, moved-note level, transients, timing. `./audit_real audio/X.f32 name [time]`. Output: `real_out.txt` |
| `patched/HoneyTune_fix.cpp` | Copy of `HoneyTune.cpp` with experimental fixes behind flags: `FIX_HOLD`, `FIX_ONSET`, `FIX_LEGATO`, `FIX_STEREO` (`renderStereo`). Compile with `-c` and link the `.o` before `libvox_dsp.a`. Binaries `audit_render_fixed` (HOLD + ONSET), `audit_analysis_fixed` (all three), `audit_render_stereofix`; outputs `render_shift_fixed.txt`, `render_unv_fixed.txt` |
| `dbg_trace.cpp` | Output pitch trace at a moved note's onset and release (`./dbg_trace <shift> <midi>`); `patched/dbg_trace_{HOLD,ONSET,BOTH}` for the fixed variants |
| `dbg_level.cpp` | Band-by-band spectrum out vs input vs ideal for one moved note (`./dbg_level <midi> <shift> <vibCents> <vowel>`) |
| `dbg_high.cpp` | High-note octave errors (`./dbg_high <midi> <ai 0/1> [vib]`) |
| `dbg_clicks.cpp` | Transient counts after up / down moves vs ideal |
| `dbg_low.cpp` | Very low voices (65-73 Hz): recall and notes (`./dbg_low <midi> <ai 0/1>`) |
| `speed_out.txt` | Speed runs (multi-core and `taskset -c 3`) |
| `dbg_low2.cpp`, `dbg_nan.cpp`, `dbg_hang.cpp`, `patched/PC_dbg.cpp` | The NaN / `findNotes` hang: `STOP=1 ./dbg_low2 36` (finds the NaN reading), `./dbg_hang audio/low_<clip>.f32` (hangs; run with `timeout`). `PC_dbg.cpp` is an instrumented `PitchCorrector.cpp` copy that prints the bad period |
| `dbg_tail.cpp`, `dbg_raw.cpp` | Raw detector readings around a note release (the level-gate hang) |
| `audio/*.f32` | Decoded local copies of the vocals, plus `low_*.f32` octave-down copies (private; delete when done) |
| `time_in.wav`, `time_stretch2.wav` | Synthetic before / after for the 2x stretch case (listening) |
