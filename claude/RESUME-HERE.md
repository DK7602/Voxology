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

Round 2 (user: still crackles at 0:02, 0:04, 0:14, 0:24): grain logging showed the read point jumping
erratically at word edges (re-derived from scratch whenever the period changed), drift halving at
note->breath, and the v0.2.1 dry/wet blend (comb filter: wet drifts up to ~0.7 ms vs dry). Redesign:
drift (analysis - synthesis offset) only changes smoothly (+= P - P/ratio per grain), one aligned cycle
repeat / skip when |drift| > P/2 (alignMark: best-matching waveform within +-15 % P), held constant in
breaths; corrections < 3 cents not applied; blend removed; median-of-3 period readings. Joins on the user's
28 s vocal: 148 -> 38. NOTE: short-window band-energy "fizz" metrics are fooled by the drift (phase) and
long windows can't see ticks: the user's ears are the judge. Sent Will_vocal_Voxology_fix2.mp3.
If still crackly: (a) check 44.1k-specific issues, (b) grain-boundary epoch alignment (glottal pulses),
(c) for rap, Auto-Edit should keep Pitch off unless clearly sung (raise the 15 % pitched-share rule).

Round 3 (user: 0:02 and 0:24 fixed; small crackle left at 0:03-04 and 0:14): octave slips in the raspy
voice (period halving 142 -> 76) made the grain size jump and joins flip repeat/skip 5x in 40 ms. Fixes:
octave guard (half / double of the last period -> keep the old octave if correlation >= 0.85 of best)
and join hysteresis (joins at |drift| > 0.6 P). Joins on the clip: 38 -> 19, no bursts. Sent fix3 render.

Round 4 (user was on MELODIC, not Rap: 100 %, 5 ms; crackle left at 0:02; user A/B'd in Cubase: Pitch is the
source, Reverb / Doubler clean): no waveform kinks; spectrogram shows PSOLA chopping the noisy / raspy part
of the voice into a buzz at the pitch rate (2.06-2.18 s "s"/"sh" in a note, 2.28-2.45 s raspy onset).
With no correction the engine is transparent (-320 dB error), so this is the technique's known weakness on
rough voices. Done: clarity-weighted correction (YIN aperiodicity + period match; clean vowel = full tune,
rasp / breath less), 4-point cubic interpolation, join timing for quieter moments. Buzz windows 21 -> 16 of
402 (metric /tmp buzz.py: 3-8 kHz envelope modulation 60-600 Hz vs 30 Hz envelope). Sent renders at 5 ms
and 20 ms retune. Next if needed: two-band tuning (shift < ~4 kHz only, drift-aligned high band with
crossfaded joins), or accept for live tuning and do the clean version in Honey Tune (offline).

## Honey Tune build plan (started 2026-10-05; user: Cubase ARTIST 14 = ARA OK; wants all big DAWs)
Separate plug-in target "Honey Tune" (like Melodyne): ARA extension on the clip; Voxology stays the insert
chain; shared engine. ARA SDK: github.com/Celemony/ARA_SDK tag releases/2.2.0 (Apache 2.0; JUCE 8 supports
it: juce_set_ara_sdk_path + IS_ARA_EFFECT; JUCE example examples/Plugins/ARAPluginDemo.h). Hosts: ARA =
Cubase Artist/Pro, Nuendo, Studio One, Logic, Reaper, Cakewalk/Sonar; Capture mode = FL Studio, Ableton,
others; Pro Tools = AAX + Avid PACE signing (separate signup, last).
Steps: 1 note engine (offline note segmentation + offline shifter) -> 2 ARA + Capture plug-in -> 3 honeycomb
piano-roll editor + Auto-Edit suggestions + Learn -> 4 tests + user test in Cubase Artist 14.
Pitch live module: user accepted the remaining minimal buzz (technique limit, not a bug).

Step 1 DONE (2026-10-05): dsp/src/HoneyTune.cpp (vox::honey): analyse (live detector readings + offline
clean-up: octave slips vs +-12 readings, gaps <= 16 ms bridged, median-5), findNotes (boundaries on a 180 ms
averaged pitch, > 0.6 semitone for 30 ms, min 50 ms), snapToKey, centsOff, render (PitchCorrector driven by a
Guide plan: shift = smoothed note move + unsmoothed drift / vibrato detail, faded at note edges).
PitchCorrector got Guide support + Reading.period / clarity / time. Tests test_honey.cpp (4): notes found
within 8 cents, untouched = unchanged (< -60 dB), snap within 4 cents, drag +2 st, vibrato 76 -> 14 cents,
drift 37 -> 0. User's vocal: 132 notes, analysis 0.34 s + render 0.36 s for 28 s. Key guess (B major,
65 %) left 79/132 notes > 25 cents off: likely wrong key -> ask the user's song key; demo render sent
chromatic. NEXT: step 2 (ARA: ARA_SDK releases/2.2.0 + Capture mode), separate plug-in target "Honey Tune".

User's test song (Will_BDay_2024) is in B MAJOR (their Key Compass plug-in, 98 %). Voxology Auto-Edit
guessed B major (Rap run, 65 %) / B minor (Melodic run, unsure -> Chromatic). Sent a B major Honey Tune render.
Idea for later: ARA hosts can pass the song key to the plug-in (ARA content: key signatures) -> Honey Tune /
Pitch could use the DAW's key; Key Compass is the user's own earlier plug-in (separate project).

Step 2 BUILT (2026-10-05, user said "B Major render is great"): honeytune/HoneyTunePlugin.cpp, target HoneyTune
(VST3 + AU, IS_ARA_EFFECT, code Hny1). ARA_SDK must be releases/2.3.0 (JUCE 8.0.15 needs ARAChannelFormat.cpp).
Doc controller: on sample access -> reads whole clip (message thread), ThreadPool job analyse + findNotes + detectKey,
render per channel with document Settings (key Auto/0-11, scale, snap, drift, vibrato; saved in ARA archive).
Playback renderer plays rendered audio (spin try-lock), returns false while not ready. Plain JUCE editor (sliders +
status). CI: pluginval 10 on Honey Tune as insert (pass-through). USER TESTED in Cubase Artist 14: shows in Extensions,
335 notes, detected B major 72 %, "sounds good!". Editor labels white-on-cream (fix). NEXT: step 3 honeycomb note editor, Capture mode for non-ARA hosts.

Step 3 BUILT (2026-10-05): honeycomb editor. honeytune/HoneyView.h (JUCE-free: Settings, NoteEdit, applyEdits,
Snapshot, Model), HoneyRoll (piano roll: hex cells, marble fill, gold rim, blue glow off-key, gold fill fixed,
dashed ghost at sung pitch, pitch line inside; drag = whole notes, Alt = cents, dbl-click snap, Delete reset,
arrows; wheel/shift/ctrl), HoneyPanel (top: key/scale/snap/drift/vibrato/Fit, bottom: per-note drift/vibrato,
Snap note, Reset note, Reset all; cream LookAndFeel, sendLookAndFeelChange fixes white slider text).
Plugin: per-source NoteEdits, coalesced re-render (whole clip per edit: ~0.4 s per 30 s per channel), ARA archive
v2 ("HNY2": settings + per-source persistentID edits keyed by note start, 30 ms match; v1 still loads), editor
follows host selection. Preview tool: -DVOX_HONEY_PREVIEW=ON, HoneyPreview raw sr out.png key scale snap
(raw = /tmp/claude-0/A_mono.raw 44.1k, may be gone). NEXT: user test; later: render only changed notes,
playhead, Capture mode, Auto-Edit/Learn in Honey Tune.

Step 3b (2026-10-05): user's mockup look (blue/white liquid marble, gold frame, honey drips). honeytune/HoneyTheme
(marble/gold/logo via juce_add_binary_data HoneyAssets: assets/marble_blue_white.jpg [procedural domain-warped],
gold.jpg [mirrored = seamless], logo.png [from plugin/ui/assets/logo.webp]); Look = glass combos/buttons, gold
sliders with marble knobs, gold scrollbars. Gotcha: Graphics::drawImage uses the current colour's opacity -> setOpacity(1).
Round 2 (user: "more like mine, more realistic, cream glow in-key / blue glow off-key like the knobs"): agate marble
(assets/src/make_marble.py) with gold flecks, glitter gold (mirrored seamless), raised bevelled gold bars on rows
OUTSIDE the key (black keys when chromatic) = mockup's alternating bars, gold honeycomb lines, DropShadow glows
behind cells, embossed gold title / labels (drawGoldText). Round 3: no drips on the timeline; bottom drips = cut
from the user's mockup (assets/src/cutout.py removes fake checkerboards; drip_a/b/c.png); user's Honey Tune title
art (images/13.jpg -> assets/title.png, "by Voxology" trimmed and drawn as gold text); marble now 3D (height
field lighting + specular + sheen in make_marble.py). Top bar 104 px, editor 1200 x 720.
Round 4: top bar = Voxology header (marble_cream_panel + 160deg sheen + blue inner shadow, drawCreamGlass; labels
drawCaps gold-deep + white edge); Round 5 (user tested in Cubase lower zone): no bottom drips (kDripRoom 0), no "by Voxology", title bottom-aligned so
its drips end just above the gold trim, top bar 76 / bottom 78 px (more roll). BUG fixed: roll fitted at full size then
host shrank editor -> notes below view; resized() now keeps the centre pitch. Earlier: bottom bar also cream glass; Round 6: notes squished (fit at small size, host enlarged) -> defaultZoom ~6 s across, min 120 px/s, re-fit on
resize until the user zooms (userZoomed; Fit resets), cells min 12 px wide, + / - keys zoom. Playhead: renderer stores
playPosition/playStamp per source (audio thread atomics), editor timer 30 Hz -> roll.setPlayhead, view pages along.
Round 7: black note text; RED glow = will SOUND off-key (NoteView.off from target; fixed = wasOff && !off; dragged
notes judged where they land); Original A/B button (controller playOriginal -> SourceState.playOriginal, renderer
plays `recorded` copy; also plays the recording instead of silence while analysing); vertical fit centres on the
duration-weighted median pitch. Round 8: ruler shows the HOST's bars/beats (editor buildTimeline: region songOffset + ARA TempoConverter /
BarSignaturesConverter -> honeyui::Timeline lines in clip seconds; seconds fallback), bar lines raised gold, beat
lines fine; time readout card (bar . beat + song time, play/stop icon) in the bottom bar, synced via panel.setPlayhead;
refresh every 1.5 s for tempo changes. Preview: HONEY_BPM fakes a tempo map. Phrase connectors: black line (white edge) note end -> next start when gap < 0.2 s. Click/drag the ruler -> HostPlaybackController::requestSetPlaybackPosition (song = clip + songOffset). Chrome blocks the unsigned zip as dangerous: user must Keep (code signing later). drips de-haloed and drawn BEFORE the frame with tops tucked under it.

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
