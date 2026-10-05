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

## Status: v0.1.0 (2026-10-05) - first build (v0.2.0 adds Pitch, below)
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

## Pitch correction (v0.2.0, 2026-10-05): module 01, first in the chain
dsp/src/PitchCorrector.cpp. Detection: YIN on a 12 kHz copy every ~2.7 ms (80 - 1000 Hz), period refined
at full rate by normalised cross-correlation + parabolic interpolation; voicing = YIN aperiodicity < 0.25
and level > -55 dBFS. Decision: nearest note of key / scale (6 scales) with 0.3-semitone hysteresis;
correction (semitones) glides with Retune Speed (0 = hard); Humanize slows it on sustained notes (x4 at
0.6 s). Shifter: streaming TD-PSOLA (2-period Hann grains, analysis marks one period apart, synthesis
marks period/ratio apart, normalised by window sum) -> formants kept; unvoiced = fixed 5 ms grains.
Each analysis is time-stamped; grains use the reading for their own moment (fixed vibrato lag).
Latency 32 ms (1536 @ 48k) + saturation 55, constant; chain.latencySamples() reported to the host.
Measured: lands within 0.1 cent; hard tune cuts 77 cents of vibrato to 9; slow (400 ms) keeps it all;
G3->C4 glide becomes steps (55/58 windows on a note); no clicks; noise passes (< -40 dB error);
block-size invariant; CPU whole chain ~5 % of a core. 7 tests (test_pitch.cpp); 27 total, ASan clean.
Auto-Edit: key from sung notes (Krumhansl profiles; < 45 % sure -> Chromatic + KEY UNSURE note);
off-cents reported; per-style retune (Trap Lead 10 ms / Rap 60 ms 70 % / Melodic 5 / Ad-libs 0 /
R&B 80 ms + humanize 50); Pitch stays off when < 15 % of the vocal is pitched (rap). User can change
Key / Scale (Pitch page: 12-key grid, scale list, live "you sing -> you get" readout with cents).
Bug fixed on the way: grains written before the start point came back one ring-length later.
NEXT for pitch: test on the user's real vocals; maybe formant control, MIDI note input, a note graph.

## Crackle fix (v0.2.1, 2026-10-05) - from the user's first real test (Will_BDay_2024 vocal, Rap)
User heard a slight crackle on the soloed Voxology vocal (not on A / off). Found with the user's vocal-only
exports (A dry, B Voxology) + leave-one-out renders, measuring >9 kHz energy rising more than 1-4 kHz per
5 ms window: Pitch was the source (re-stitching the voice at word edges / note-breath flicker, even when the
correction was ~0). Fixes: (1) blend grains with the untouched voice by how much correction is needed
(0 cents = dry, >= 10 cents = all grains); (2) voicing needs 2 readings in a row to switch; (3) analysis
offset eases back in breaths instead of snapping. Rap: worst blip 13.5 -> 9.8 dB (floor without pitch 4.6).
Also (not the cause, cleaner anyway): compressor detectors no longer ride each cycle (peak env held 15 ms,
level RMS 30 ms, attacks 1 / 10 ms); De-Esser shelf always runs (no switching). The user's vocal peaks +4 dBFS
(562 samples over 0 dBFS in the dry export): fine in float, but suggest clip gain -6 dB in Cubase.
Sent the user old vs new renders of their vocal to judge by ear. Remaining: ~3 (Rap) / 10 (hard tune)
5-ms blips per 28 s at 8-10 dB; next step if still audible: epoch (waveform-peak) aligned pitch marks.

## Honey Tune (decided 2026-10-05; build AFTER the user confirms v0.2 works in Cubase)
User's name for our Melodyne-style note editor: "Honey Tune". Each note = a gold-rimmed blue-marble
honeycomb cell on a piano-roll grid (off-key notes glow blue; pulled into key the cell "fills" gold).
Audio access: ARA2 (Cubase Pro/Artist, Nuendo, Studio One, Logic, Reaper, Cakewalk/Sonar, Pro Tools
2022.12+) via JUCE's ARA support; Capture mode (play the part once) for non-ARA hosts (FL Studio,
Ableton Live, Cubase Elements: verify FL). Features: drag notes, pitch drift / vibrato / slide
smoothing, note length, snap-all-to-key start, Auto-Edit suggests fixes + Learn explains. Offline-quality
shifting (no live latency limit). Live Pitch module stays. Ask user's Cubase edition (Elements = no ARA).
Housekeeping: trademark check "Voxology" / "Honey Tune" before selling (VOX amps).

## Honest gaps vs Nectar 4 Advanced (the plan)
1. Pitch correction: DONE in v0.2.0 (see above).
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
