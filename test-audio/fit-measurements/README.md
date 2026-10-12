# Vocal Fit measurements (numbers only, no audio)

How finished vocals sit against their own beats, measured for Vocal Fit (v0.32.0) with `tools/audit/fit_study.cpp`
(Auto-Edit's `analyseVocal` on the vocal + `analyseBeat` on the beat under it; mono, 48 kHz).

- `musdb18_fit.csv`: the 144 songs of MUSDB18-7 (SiSEC18-MUS 7 s excerpts, zenodo 3270814): vocal = stem 4, beat =
  stems 1 + 2 + 3 (drums, bass, other) summed, as mixed. 143 used (beat under the voice >= 3 s). Mostly rock / pop /
  indie, a few rap songs. Research data: only these numbers are kept, the audio is not in the repo. Credit MUSDB18
  (Rafii et al. 2017) if derived numbers ship; add to the legal check before selling.
- `user_songs_fit.csv`: the user's Gallas, Schaf and Don & Lysette clips (acapella vs instrumental from test-audio).
  Only Don & Lysette's stems add up exactly to its mix (Gallas / Schaf mixes were processed further), so only its
  level reading is the mix's own.

Columns: `voiced` s of voice, `held` % held notes (sung vs rapped), `f0` Hz, `sibDb` / `sibHz` "s" level vs the
voice and where, `microDyn` word-to-word punch (dB), `rangeDb` loud vs quiet lines, `vocalOverBeat` vocal LUFS minus
beat LUFS, `beatLufs`, `bright` beat 5 - 12 kHz vs 500 Hz - 2 kHz, `low` beat under 100 Hz vs all, `busy` P90 - P10 of
the beat's 50 ms levels, `under` s of beat under the voice; then per third-octave band (100 Hz - 16 kHz) `v<f>` the
vocal's balance and `b<f>` the beat's (each vs its own 500 Hz - 2 kHz average), and `o<f>` = v - b.

Findings (143 songs; r = correlation across songs):

| What | Result | Used for |
|---|---|---|
| Vocal vs beat loudness | median -2.1 dB (P25 -4.1, P75 -0.6; P10 -5.6, P90 +0.6); rapped (held < 30 %) -2.0, sung -2.4 | "Vocal vs beat" line, the fader advice |
| Vocal top end vs beat brightness | r +0.16 (air), +0.19 ("s"): brighter beats go with slightly BRIGHTER vocals, not darker | "less air on bright hats" dropped |
| Vocal tone vs beat tone, per band | r -0.15 .. +0.32, slopes 0 .. +0.43: weak, same direction | step 2: at most a small nudge |
| Vocal punch vs beat movement | r +0.42, slope +0.25 dB per dB: steady beats go with more compressed vocals | step 2: Compressor |
| Vocal level vs beat movement / low / brightness | r +0.03 / -0.14 / +0.01: no link | - |
| Space (reverb tail) | can't be measured on 7 s clips | step 2 needs full songs |
