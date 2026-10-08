# Pitch audit (Voxology live Pitch + Honey Tune)

Not part of the build. Run on the vocals in `test-audio/` (decode: `ffmpeg -i X.mp3 -ac 1 -ar 48000 -f f64le X.f64`).
Compile each against the engine: `g++ -std=c++20 -O2 -Idsp/include tools/audit/<file>.cpp build-t/dsp/libvox_dsp.a -lpthread -o <name>`.

| Tool | What it measures |
|---|---|
| `reference_notes.cpp in.f64 out.notes` | Honey Tune's offline notes of a vocal (the fixed reference) |
| `dump_readings.cpp in.f64 mode retuneMs humanize out.d` | the live Pitch's readings every 2.7 ms: time, voiced, sung, applied correction, target |
| `score_vs_reference.py` | for each reference note >= 0.3 s: centre error (cents), shape change (movement above 1 Hz vs as sung), correction jumps (> 6 cents in one reading), wrong-note time (> 0.5 semitone from the note) |
| `live_pitch_audit.cpp in.f64 mode retune humanize amount` | per-config summary, re-measuring the actual output audio (shifter accuracy, vibrato kept) |
| `cleanup_audit.cpp in.f64 style [intensity]` | Cleanup: Auto-Edit's settings, then low cut / gate / pops / breaths as the chain runs them: words turned down by the gate, how late it opens on phrase starts, each pop cut (depth, time > 6 dB, low band vs normal), breath ducks over voiced sound |
| `eq_audit.cpp in.f64 style [intensity]` | Tone EQ: the vocal's third-octave tone vs the style target, the EQ Auto-Edit chose, predicted vs measured after (low cut + EQ rendered), mean distance from target before / after |
| `tone_profile.cpp a.f64 ...` | Auto-Edit's analysis of many vocals as CSV (third-octave tone, pitch, etc.): used to measure 143 MUSDB18 pro vocal stems for the style targets |
| `eq_batch.cpp a.f64 ...` | Auto-Edit's Tone EQ gains on many vocals ("do no harm" check on finished vocals) |
| `render_autoedit.cpp in.f64 style out.f32` | the whole Auto-Edit chain rendered, for before / after clips |
| `dyneq_audit.cpp in.f64 style [all]` | Dynamic EQ: Auto-Edit's settings, then per band how much of the singing it cuts, how deep, and whether the cuts follow the melody |
| `deess_audit.cpp in.f64 style` | De-Esser: Auto-Edit's settings, then how much of the "s" frames it cuts, whether it touches vowels, and whether "s" onsets slip through |
| `honey_tune_audit.cpp in.f64` | Honey Tune: snap every note, keep drift + vibrato, re-analyse: lands within, shape kept, pitch glitches |

Results and history: claude/RESUME-HERE.md (v0.17.0).
