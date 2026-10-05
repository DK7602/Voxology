# Voxology: resume here (updated 2026-10-05)

Read this first in a new chat. Code: github.com/DK7602/voxology, branch `claude/voxology`.
Sister project: Polisher (github.com/DK7602/polisher, mastering) - same user, same template.
CI: every push builds Windows (VST3 + Standalone), runs the engine tests, pluginval strictness 10 and
Steinberg's VST3 validator. Mac on request: Actions -> Build & Test -> Run workflow ("Also build macOS").
Download: Actions -> latest green run -> artifact "Voxology-Windows" -> copy Voxology.vst3 to
C:\Program Files\Common Files\VST3, rescan in Cubase.

## The user
Mixing / mastering novice, Cubase on Windows, headphones, makes trap / hip-hop. Wants plain language,
momentum ("you make the best choice"), honest assessments, and a plan with every warning. Watching
credits: keep replies short. Tests by screen recording; trust the plug-in's meters over recorded audio.
Goal they set: an all-in-one vocal chain better than iZotope Nectar 4 Advanced, with Polisher-style
Auto-Edit + Learn window that explains why edits were made and offers suggestions. They supplied the
logo (gold honeycomb waveform + V), the title wordmark and the honeycomb / blue marble UI art
(plugin/ui/assets: logo.webp, title.webp cut out from fake-checkerboard JPGs; honeycomb.webp).

## Quality bar (standing, from Polisher)
Measured (tests), clean internals (double precision, oversampled nonlinearities, no audio-thread
allocation, exact pass-through when neutral), fresh-eyes review before big releases, heard (user test
with A / B + MATCH), later blind loudness-matched shoot-outs vs Nectar 4 and others.
Learn rule: every warning carries "Do I need to fix it?" + numbered steps; "No / Optional" shows calm
(blue). Auto-Edit notes use "TITLE: text" + "\nNEED: ..." + "\nSTEP: ..." lines; a test enforces it.

## Status: v0.1.0 (2026-10-05) - first build
Chain: Cleanup (low cut 24 dB/oct + gate w/ hysteresis + hold) -> Tone EQ (5 bands) -> De-Esser
(ratio detector: sibilance band vs whole voice, so level-independent; dynamic high shelf) -> Rider
(auto level, holds in gaps) -> Compressor (peak 6:1 fast + opto-style leveler with program-dependent
release; 80 Hz sidechain HP; makeup; parallel mix) -> Saturation (Polisher's, 4x oversampled) ->
Doubler (2 modulated voices L/R) -> Delay (host tempo, duck, ping-pong, tone) -> Reverb (8-line FDN
plate, pre-delay, 200 Hz HP, duck) -> Output. Latency 55 samples (saturation), constant.
Buses: mono->stereo (default), mono->mono, stereo->stereo. Defaults are neutral (insert = no change).
A / B: A = original, latency-aligned; MATCH = Polisher's LevelMatch (turns the louder side down).
Auto-Edit: captures until 12 s of voice (or 30 s total; gaps kept for the noise floor), then
analyseVocal (levels, noise floor, pitch via autocorrelation, third-octave balance, sibilance, range,
clipping, rumble, one-sided stereo) -> decisions per Style (Trap Lead / Rap / Melodic / Ad-libs / R&B)
and Intensity (Light / Balanced / Strong): low cut from lowest pitch, gate between noise and quiet
words, EQ vs a style target curve (cuts aimed at the worst third-octave), de-esser / compressor /
saturation amounts found by RUNNING the modules on the captured vocal (binary searches to targets),
space by style, output gain matched to input loudness. Report: summary, NEED/STEP notes, reasons per
module, "checked - kept", suggestions. Undo restores everything.
Tests: 20 cases (`cmake -S . -B build-tests -DVOX_BUILD_PLUGIN=OFF && cmake --build build-tests &&
./build-tests/tests/vox_tests`): neutral = bit-exact, block-size invariance, every module measured,
Auto-Edit on synthetic vocals (noisy/sibilant, clean, boomy+clipped, too short).
Local Linux checks: plug-in builds (VST3 + Standalone), pluginval strictness 10 SUCCESS. CPU: full
chain ~6 % of one core (48 kHz stereo). UI checked in a browser with mock.js (screenshots).

## UI and fixes (2026-10-05, later)
- Look (user-directed): honeycomb panel art (assets/honeycomb.webp, panel crop of the user's image) is the
  stage background AND the surface inside Signal Chain + module panels; header, footer and Learn use the
  cream hive-tile marble (marble_cream_panel.webp + 160deg white sheen + inset blue shadow). Hive tiles
  alternate blue / cream marble cut from the art. Knobs = the user's knob art (knob_db / knob_hz cut from a
  fake-checkerboard JPG; knob_blank = Q knob with a generated marble disc, engraved %, ms, s, :1); art stays
  still, glowing value arc + pointer move. Geometry per face in app.js FACES. knob_q / knob_switch unused
  (for future Q / filter-shape controls). User rejected generic glass looks: match their art exactly.
- Crash fix: sibilance scan read 1025 bins from a 513-bin spectrum (intermittent SIGSEGV on Windows CI).
  CI now also runs the engine tests under ASan + UBSan on Linux (~2.5 min) on every push.
- Run 37251336431: all green (Windows build + tests + pluginval 10 + VST3 validator; sanitizers).

## Honest gaps vs Nectar 4 Advanced (the plan)
1. Pitch correction (Nectar has it; trap needs it). Plan: real-time YIN pitch detection + PSOLA
   shifter, key / scale, retune speed, humanize; Auto-Edit sets key from the vocal.
2. Unmask vs the beat (Nectar's "Audio Lens" / unmasking): sidechain the beat, dip the beat's
   competing band only where the vocal sits (needs a 2nd plug-in instance on the beat or sidechain bus).
3. Breath control, plosive (p/b pop) remover, harmony / backer voices, de-reverb / noise reduction.
4. Real-voice tuning of Auto-Edit (only synthetic vocals so far): user should run it on their own
   vocals; then compare targets with reference vocals; blind A/B vs Nectar's Vocal Assistant.
5. Presets / user preset save, resizable layout tweaks, per-module A/B.

## Next session quick start
- Read this file; branch claude/voxology. Pushes build Windows only; Mac on request.
- First: user's real-vocal test (Auto-Edit on a verse; read the report; A / B with MATCH). Fix what
  they hear. Then the plan above in order (pitch correction first) unless the user says otherwise.
