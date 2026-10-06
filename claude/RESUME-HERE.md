# Voxology: resume here (updated 2026-10-05, v0.3.0 Dynamic EQ)

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
refresh every 1.5 s for tempo changes. Preview: HONEY_BPM fakes a tempo map. Voxology VST3 category now "Fx|Vocals" (Cubase Vocals folder; user may need a Plug-in Manager rescan). Note names: bright gold with a dark edge on blue notes, black on gold (fixed). A/B is now a two-part switch Original | Tuned (radio pair, lit side = what you see and hear; logo hidden under 1320 px). Phrase connectors: black line (white edge) note end -> next start when gap < 0.2 s. Click/drag the ruler -> HostPlaybackController::requestSetPlaybackPosition (song = clip + songOffset). Chrome blocks the unsigned zip as dangerous: user must Keep (code signing later). drips de-haloed and drawn BEFORE the frame with tops tucked under it.

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

## Thread handoff (2026-10-05)
Honey Tune step 3 is DONE and user-tested in Cubase Artist 14 ("works perfect"): honeycomb editor in the mockup look,
red/cream glows, Original | Tuned switch, host bars/beats ruler + time readout, click-to-seek, phrase connectors,
gold note names. Last good build: Actions run 37368105904. Voxology VST3 category is now Fx|Vocals.
A NEW THREAD was started for "Voxology add-ons" (user's request). Still open from here: Capture mode for non-ARA hosts
(FL Studio, Ableton), Auto-Edit / Learn inside Honey Tune, render only changed notes (speed), Pro Tools AAX + PACE,
code signing (Chrome flags the unsigned zip), trademark check (Voxology / Honey Tune).

## Add-ons thread (2026-10-05): v0.3.0 Dynamic EQ
User showed ChatGPT's Nectar-gap list; agreed order: 1 Dynamic EQ (DONE, v0.3.0), 2 Breath control + plosive
remover (one build), 3 Reference Match (reuses analyseVocal), 4 Unmask (needs 2nd instance on the beat +
sidechain; give Cubase routing steps), 5 Harmony / Voices + formant (biggest, CPU heavy, last).
ChatGPT's "Gate / Expander" and "2-stage compression" are already covered (Cleanup gate; Compressor = Peak +
Level stages, plus Rider); a gentle expander mode can ride along in a later build.
Dynamic EQ (dsp DynamicEq.h/.cpp, module 04, between Tone EQ and De-Esser; Module enum + UI MODULES shifted by one):
- 4 bands, bell cuts: Boom 80-300 (Q1.0), Mud 200-800 (1.4), Nasal 600-2500 (1.6), Harsh 2-8 kHz (1.4). Params dqOn,
  dqSens, dqCut1-4 (Max Cut 0-12 dB, 0 = band off), dqFreq1-4. Neutral (all cuts 0 / off) = bit-exact.
- Detector: band-pass energy vs the REST of the voice (80 Hz HP minus band), true mean-square (2 one-poles, 4-10 ms)
  -> ratio dB; "normal" = learned per band while singing (fast 0.4 s for the first 1.5 s, then rises 4 s / falls 1 s, so
  frequent jumps don't become normal); gaps (> 24 dB under the recent voice peak, or < -60 dBFS) don't count; 50 ms
  warm-up. Cut = 1.5 x (rise - threshold) with 3 dB knee, threshold = 5.5 - 0.05 x Sensitivity dB (50 % = 3 dB),
  clamped to Max Cut; per-band attack/release (2-6 ms / 60-120 ms). Harsh holds while energy above 6 kHz exceeds it
  (an "s" = De-Esser's job). Level-independent (whisper vs shout within 0.01 dB in tests). CPU ~0.7 % of a core.
- Auto-Edit (after Tone EQ): per band, runs the module at each third-octave in a zone (Boom 100-200, Mud 250-630,
  Nasal 800-2000, Harsh 2.5-4 kHz), ignoring sibilant frames; keeps the spot whose P95 cut is biggest; off if < 1.5 dB,
  else Max Cut = P95 x intensity (2-8 dB). Adds ~2 s to Auto-Edit on a 30 s capture. Report module key "dyneq".
- UI: page uses the Tone EQ two-row layout (Max Cut row + live "Cutting" bars B/M/N/H; Frequency row + Sensitivity).
  Spectrum: orange filled = live cut (always), dashed = Max Cut curve and draggable B/M/N/H points on its page (Tone EQ
  points hide there). Learn text + tips (NO BANDS SET, AT THE LIMIT).
- Tests: tests/test_dyneq.cpp (pass-through, cuts only during the bloom, level independence, steady tone untouched,
  Max Cut / Sensitivity, band selectivity, block-size invariance, Auto-Edit finds a bloom / leaves a steady voice).
- Honest gap: tuned only on synthetic vocals. User test: run Auto-Edit on a real verse, open 04 DYNAMIC EQ, watch the
  orange cuts land on boomy / muddy / harsh words; A/B with MATCH. Retune thresholds from what they hear.

## v0.3.1 fixes from the user's first Cubase test (2026-10-05)
- Learn panel flickered: live tips (Pitch BIG JUMP, Dynamic EQ AT THE LIMIT) appeared / vanished many times a second and
  each change rebuilt the panel. Now each tip stays >= 4 s and updates in place (app.js liveTips / stickyTips).
- Dynamic EQ on a REAL vocal: Auto-Edit set all 4 bands to -8 dB, cutting ~72 % of the time. Cause: "normal" tracked the
  low side of the band (rise 4 s / fall 1 s); real voices swing much more than the synthetic ones. Now "normal" is a running
  60th percentile (fixed-step up/down, 8 dB/s, 40 dB/s for the first 1.5 s); threshold 6.5 - 0.05 x Sens (50 % = 4 dB).
  Auto-Edit: a band only counts if P95 cut >= 2.5 dB AND it cuts > 1 dB at most 20 % of the time; Max Cut 2-6 dB.
  On the user's dry vocal (Don_Birthday_2024, 105 s, f0 ~246 Hz, heavy rumble): Mud -4.5 @ 315 Hz, Nasal -4.5 @ 1.2 kHz,
  Boom / Harsh off. Test audio kept only in the session scratchpad (not in the repo).
- Key: user's key app says C major; Voxology / Honey Tune said G major. Whole song = C major; the first ~50 s are sung
  mostly on G (11 of 19 s) with almost no C, so from the voice alone that part is genuinely G-leaning. detectKey now: among
  close runners-up (within 0.15) with different notes, the notes only one key has decide; if those are barely sung
  (< 4 % apart) the guess is "ambiguous" (KeyGuess.ambiguous / altKey / altMinor; confidence capped at 0.4). Auto-Edit only
  trusts a key at >= 60 % and not ambiguous (else Chromatic + "set your beat's key"); Honey Tune shows "G major or C major".
  Lesson: the beat decides the key; tell the user to set Key / Scale from the beat when unsure.

## v0.4.0 Breath control + plosive remover (2026-10-06), inside module 02 CLEANUP
User's effort plan: high for breath/plosive, medium for Reference Match, high for Unmask and Harmony/formant.
- dsp PopBreath.h/.cpp: PopRemover (clPops 0-100 %, 100 % = up to 24 dB) and BreathControl (clBreath 0-24 dB). Run after
  Cleanup's low cut + gate. Params live in CleanupParams (popAmount, breathDb); clOn switches all of Cleanup.
- FREE LOOK-AHEAD: both listen to the chain INPUT (mono "side" buffer captured before Pitch), which is Pitch's latency
  (~32 ms) ahead of the audio they process; decisions are delayed to land 3 ms (pops) / 25 ms (breaths) early. No added
  latency. Standalone (sc == nullptr, Auto-Edit's measuring runs) = no look-ahead.
- Pops: <150 Hz vs >150 Hz peak followers (0.3 ms / 30 ms), ratio vs a learned normal (running 60th percentile, prior 0 dB =
  cautious, learns only when not cutting); threshold 9 dB, knee 4, slope 1.5; must be loud (low band within 18 dB of the
  recent voice level). Cut = two low shelves at 160 Hz (each half the cut), attack 0.5 ms, release 50 ms.
- Breaths: 10 ms mean-square of body (100-800 Hz), air (1.5-6 kHz), top (>6 kHz), full (>80 Hz); breath = audible, 8+ dB
  under the recent voice peak, air beats body by 2 dB, not an "s" (top < 1.5 x air), for >= 100 ms (so "sh"/"f"/"h" end
  first). A word (loud, voiced or "s") ends it at once with a 4 ms release (25 ms ahead in the chain); quiet = 40 ms hold.
- Auto-Edit (Cleanup): counts pops (cut > 6 dB) -> 60/80/100 % by intensity, POPPY MIC note if > 6 / min; counts breaths and
  their level vs the voice -> base by style (Trap 9, Rap 10, Melodic 6, Ad-libs 12, R&B 5 dB) x intensity, off if < 2
  breaths or already 36 dB+ under the voice.
- User's dry vocal: 9 pops (all right before a sung word), 39 breaths (~1 per 2.7 s), 0.00 s of flagged breath had a pitch.
  Auto-Edit (Melodic): Pops 80 %, Breaths -6 dB.
- UI: Cleanup page = Low Cut, Pops, Breaths, Gate, Gate Range + "Working" meter (P / B / G bars; multiMeter()). Learn text
  + NO AIR tip. Meters "pops", "breath" in the 30 Hz frame.
- Tests tests/test_popbreath.cpp (pass-through, pop cut / vowels untouched, breaths -10 dB / words and "s" untouched,
  block-size invariance, Auto-Edit finds them / clean take off, full chain look-ahead: pop -12 dB from its first moment,
  next word at full level). 44 test cases pass; pluginval 10 SUCCESS.
- To verify by ear: run Auto-Edit, solo the vocal, listen to a "p" word and the gaps; Breaths 0 vs 10 dB with A/B.

## v0.5.0 Reference Match (2026-10-06) + Pops safety net
- Pops: Auto-Edit always sets Pops >= 60 % (it only acts on a real pop; Auto-Edit hears 12-30 s, the song may pop later).
- Test data: MUSDB18-7 (SiSEC18-MUS 7 s excerpts, zenodo 3270814, CC-BY 4.0): 144 pro songs, each with the finished
  vocal stem + full mix. Downloaded to the session scratch only (/tmp, not in the repo). Credit it if derived numbers ship.
- Findings: (1) reading a vocal's tone from a FULL SONG is ~7.6 dB RMS off the true vocal (no better than our built-in target,
  8.0), so the reference must be an acapella / vocal stem; full songs get a warning (lowBassDb > -14 dB: vocals <= -20 dB at
  P95, mixes >= -10.5 dB at P5; 3 / 143 vocals and 1 mix misread). (2) Pro vocals average much less low end (-12.7 dB at
  200 Hz, -20 at 160) and less presence (-7 to -15 dB at 2.5-5 kHz) than styleTarget() asks for. Most MUSDB songs aren't
  trap, so the styles were NOT changed; revisit with hip-hop references.
- dsp: VocalAnalysis gains microDynDb (punch: P95 - P50 of 50 ms levels while singing), tailDb (space: 100-300 ms after a
  phrase end vs its last 200 ms; -120 unknown), lowBassDb (<100 Hz vs all). ReferenceProfile + analyseReference() (ok /
  problem / warning; < 3 s of voice = problem). AutoEditSettings.reference.
- Auto-Edit with a reference: Tone EQ + Low Cut are FITTED (coordinate descent over each band's frequency grid and gain,
  Mud / Nasal mostly cuts, low cut may rise to 0.95 x lowest notes, max 250 Hz) to the reference's SMOOTHED third-octave shape
  (energy average with neighbours, so harmonics landing in a band don't steer it); Light = 70 %. De-esser target = the
  reference's "s" level (clamped -10..0). Compressor Level threshold searched so the output's punch = the reference's (0.5 -
  10 dB average squeeze). Reverb mix searched so the finished chain's tail = the reference's (0 if the reference is drier;
  delay off if the delay alone is too wet). Pitch, cleanup, saturation, doubler stay per STYLE. Reasons + "Tone match X -> Y dB".
- Validation (20 random pairs of pro vocals, one as "you", one as reference): tone distance 8.2 dB (style) -> 5.5 dB
  (reference); "s" level diff 5.1 -> 2.5 dB; punch diff 2.0 -> 1.7 dB. Remaining tone gap = the voices themselves.
- Plug-in: AutoEditController::loadReference (background thread, AudioFormatManager basic formats + JUCE_USE_MP3AUDIOFORMAT,
  up to 120 s from the middle), clearReference, reference JSON for the UI; measurements saved in the state ("aeReference").
  Native fns chooseReference (FileChooser), clearReference, getReference; frame field refVersion. UI: "+ REFERENCE" chip under
  the STYLE picker (name + X when loaded, warn colour for full-song / problem); Learn > Auto-Edit Report starts with a
  REFERENCE MATCH section (how-to, problems, full-song warning); report meta shows "matched to ...".
- Tests: tests/test_reference.cpp (brighter / thinner reference pulls EQ that way and lands >= 20 % closer; full song warns +
  report note; too-short refused). 46 test cases pass; pluginval 10 SUCCESS locally.
- User plan: find an acapella of a vocal they love ("<song> acapella"), load it with + REFERENCE, run Auto-Edit, A/B with MATCH.

## v0.6.0 Unmask (2026-10-06): BEAT mode
- New param "mode" (Vocal / Beat), "umAmount" (0-100 %, default 50), "umFocus" (Centre / Full). Same latency in both modes
  (BEAT runs the chain bypassed = its delayed dry path), so a Voxology on the beat lines up with the one on the vocal.
- Link, no routing: dsp Unmask.h/.cpp. UnmaskLink = process-wide registry (static in the binary; every Voxology instance in
  the host shares it): 16 slots, each a ring of 4096 frames {song position, 6 band levels}, seqlock per frame, lock-free on
  the audio side; names (track name via updateTrackProperties) behind a mutex. Every instance claims a slot; VOCAL mode
  publishes its PROCESSED output's band levels (BandAnalyser: octave band-passes at 200/400/800/1.6k/3.15k/6.3k, Q 1.4,
  ~20 ms) every 128 samples, tagged songPos + i - latency (kNoPosition when stopped). BEAT reads, per 128-sample hop, the frame
  at or before its own songPos + s - latency (within 0.25 s; newest when no position; nothing if stale -> dips let go).
  Several vocals: per-band max ("All vocals"), or one chosen by name (saved as "umSource", resolved by a 1 Hz timer as
  instances appear). Live = published in the last second (wall clock).
- Unmask (beat side): bells at the six bands (dip Q 1.9), each up to Amount x 6 dB x weight (0.35, 0.6, 0.85, 1, 1, 0.7) x
  vocal presence (vs its own recent per-band peak: from 24 dB under it, full at 6 dB under; peak falls 6 dB/s) x beat present
  (> -70 dBFS). 15 ms in, 100 ms out. Centre focus = mid / side, only the mid dipped. A (listen original) and bypass = no dips.
  Fader-independent by design (follows when / where the vocal sings, not level vs the beat).
- UI: VOCAL / BEAT switch in the chain card head. BEAT: hive hidden, big UNMASK hex with link status, header shows BEAT
  MODE (Style / Intensity / Auto-Edit / Undo hidden), Unmask page (Amount, Focus, "Make room for" list from
  getUnmaskSources / setUnmaskSource, 6-bar Dipping meter), BEAT SPECTRUM with live dip curve + vocal band dots, Learn text +
  NO VOCAL HEARD (after 2 s) / DEEP DIPS tips. Frame fields umDip, umVocal, umLink (BEAT only).
- Tests: tests/test_unmask.cpp (link by position / stale / release; off = untouched, no vocal = no dip; middle dips 2-6.6 dB
  at 1.6 kHz while singing, < 0.5 dB in gaps, sides untouched; through the link with the vocal a block late).
  tools/linkcheck (cmake -DVOX_BUILD_LINKCHECK=ON, local only): two REAL processors, beat processed first each block:
  dips 4.6 - 5.3 dB while singing, <= 0.3 dB in gaps, PASS. Found + fixed: BEAT used getSampleRate() (0 if a host never
  calls setRateAndBufferSizeDetails) -> now the prepared rate.
- Caveats for the user: both in the same project / process (plug-in sandboxing breaks the link); Cubase "suspend VST3
  processing when no audio" just means no dips while the vocal is silent (correct). Cubase ASIO-Guard prefetch is why frames
  are matched by song position, not by arrival.

## v0.7.0 Harmony voices + formant (2026-10-06)
- PitchCorrector: PitchParams.formant (semitones, +-kMaxFormant 6) = each PSOLA grain read at 2^(st/12) speed (resonances
  move, pitch doesn't; reads clamped to the newest input). PitchParams.harmony (index into kHarmonyIntervals: Off, 3rd/5th/
  octave up, 3rd/4th/5th/octave down; scale steps in a key/scale, semitones in Chromatic, octaves always 12): full shift to
  harmonyNote(snapped note), corr jumps to target at note starts (no scoop), no clarity scaling. Big down shifts (ratio < 0.6,
  octave down) read each grain centred on the pulse peak within +-P/2 (else 2-period grains at 2P spacing keep the original
  pitch). Measured on a sung A3 in C major: 3rd up C4 261.8, 5th up E4 329.8, 3rd down F3, 4th down E3, 5th down D3 146.8,
  octave down 110 with odd harmonics present (true octave), octave up 440.5; formant +-4 st moves the centroid 707 -> 914 /
  484 Hz with the pitch unchanged. Existing pitch / Honey Tune tests unchanged.
- dsp Voices.h/.cpp HarmonyVoices: 2 PitchCorrectors (harmony mode, lead's key/scale, retune <= 40 ms, formant = hvFormant)
  fed from the chain's input "side" buffer (Pitch's latency ahead, which they add back) -> per-voice low cut (cleanup's, >= 60
  Hz) -> pan (voice 1 left, 2 right by dbWidth; one voice = centre) -> lead's VocalEQ + DeEsser (stereo bus) -> Saturation's
  latency (55) -> level follows the finished lead's (30 ms RMS ratio, max +24 dB, HELD in gaps 40 dB under the lead's recent
  peak) x hvLevel -> added to ch and to mono (so doubler / delay / reverb carry them). In the chain after Saturation, before
  the Doubler. doubler.enabled (dbOn) switches the whole VOICES module.
- Params: ptFormant, hv1, hv2 (choices), hvLevel (50 %), hvFormant; Auto-Edit never writes them (musical choices). Meter
  hvNotes. UI: Pitch page gets Formant (7 cells, narrower); module 09 renamed VOICES: Double, Width, Voice 1 / Voice 2 (2-col
  interval grids), Level, Formant; hive stat shows the voices' notes; Learn + CHROMATIC KEY warning.
- Tests tests/test_voices.cpp (notes in key incl. octave down, level ~-7.5 dB in mono at 50 %, onset within 15 ms of the
  lead, < -70 dB in gaps, formant moves centroid > 15 % with pitch unchanged). 53 test cases; ASan clean; CPU: full chain 3.2 %
  -> 6.8 % of a core with 2 voices + formant (mono). On the user's dry vocal: voices -7.7 dB under the lead, finite.
- Honest limits: harmonies follow the sung melody note by note (a "smart" harmonizer, not chord-aware); fast rap gets choppy
  harmonies (meant for hooks / ad-libs); octave down is the most artificial-sounding.

## Thread handoff (2026-10-06): add-ons thread -> "Pitch Improvement" thread
Add-ons thread finished: v0.3 Dynamic EQ, v0.3.1 fixes, v0.4 Pops + Breaths, v0.5 Reference Match, v0.6 Unmask (BEAT mode),
v0.7 harmony voices + formant. Last green Windows build: Actions run 37408152948 (v0.7.0). The user hasn't reported Cubase
results for v0.4-v0.7 yet: ask them (harmonies on their voice, Unmask linking, Pops/Breaths, Reference Match).
New thread "Pitch Improvement" (user-requested): the Pitch module (dsp PitchCorrector, TD-PSOLA, YIN detection, ~32 ms latency)
and probably Honey Tune. Known facts to start from: the user sings quite out of tune (avg ~36 cents off on Don_Birthday_2024);
their key app says C major; the voice alone reads G in the first ~50 s (see v0.3.1 notes); key guess now flags ambiguity.
Real test audio (session scratch only, re-request if gone): the user's dry vocal Don_Birthday_2024_Vox_only.mp3 (105 s, f0 ~246 Hz).
PitchCorrector now also has formant (grain read speed) and harmony mode (pulse-aligned grains for ratio < 0.6): keep the
pitch / Honey Tune / voices tests green when changing it. Start by asking the user what to improve (sound quality, artefacts,
speed of tracking, key handling, Honey Tune editing?) and propose a short plan with warnings.

## v0.8.0 Pitch modes (2026-10-06, "Pitch Improvement" thread)
User asked for a "more robust, in-depth" tuner: mostly a human voice with corrected notes, but a robotic sound on demand.
Plan agreed (my call): step 1 modes + vibrato + scales (this); step 2 sound quality on raspy / breathy notes (two-band or
epoch marks, see v0.2.1 notes); step 3 MIDI note control + custom note on/off + Honey Tune upgrades.
- PitchParams.mode (kPitchNatural 0 / kPitchClassic 1 / kPitchRobot 2; struct default Classic so Voices / Honey Tune / old
  tests are untouched; the plug-in param ptMode defaults to Natural) and PitchParams.vibrato (% -100 flat .. +100 double).
- Natural: note centre = two 80 ms one-poles over the sung pitch (reset at a new note: start of a phrase or 0.8 st off the
  centre two readings running); the note is picked from the centre (a wide vibrato can't flip it); correction glides to
  note - centre at Retune speed (Humanize as before); no-overshoot clamp for the first 0.12-0.3 s of a note (a scoop never
  goes past the note); Vibrato adds (vib/100) x (midi - centre). Classic = the old code path exactly. Robot = corr = note -
  midi every reading, ignores Retune / Humanize, no clarity scaling (grit gets tuned too).
- Scales: + Dorian, Phrygian, Mixolydian, Blues (kScales 10; Honey Tune's list picks them up).
- Measured (tests/test_pitch.cpp, 4 new): G3 35 cents sharp +-40 vibrato -> Natural mean -0.6 cents, vibrato spread 77 -> 83
  (kept); Vibrato -100 -> spread 7, +100 -> 165; drift -30..+30 cents held within 10; scoop overshoot 0.06 cents; Robot with
  Retune 400 / Humanize 100 -> mean -0.2, spread 58 -> 10. 57 test cases pass.
- UI: Pitch page now two rows: Mode (Natural / Classic / Robot), Amount, Retune ("how fast"), Vibrato (bipolar, "as sung"),
  Humanize, Formant; Key, Scale (2 columns), Tune readout. Knobs the mode ignores are dimmed (.cell.inactive). Learn text
  rewritten; new tip ROBOT + CHROMATIC. Browser preview: copy plugin/ui + JUCE's native/javascript/{index,check_native_interop}.js
  into ui/juce, serve, screenshot with Playwright (chromium at /opt/pw-browsers).
- Auto-Edit: Trap Lead Classic 10 ms, Rap Natural 60 ms 70 %, Melodic Classic 5 ms, Ad-libs Robot, R&B Natural 40 ms
  humanize 50; new "Mode" reason line; Pitch-off (rap) leaves Mode on Natural.
v0.8.0 CI green: Actions run 37456434434.

## v0.9.0 Pitch step 2: no buzz on breath / rasp + Natural reworked (2026-10-06)
User's dry vocal re-sent in this thread: Don_Birthday_2024_Vox_only.mp3 (105 s; session upload, scratch only, not in repo).
Test harness (scratch, rebuild if gone): render.cpp (old engine = previous commit's PitchCorrector compiled into namespace
voxold vs new; prints buzz windows), offkey.cpp (per-note median cents off in C major), dump.cpp (pitch readings).
- Two bands (PitchCorrector kSplitHz 2 kHz, LR4 low band ring `lo`; high = in - lo): grains carry only the low band; the high
  band is read at the grains' offset, interpolated between grain Marks (pos, off, preOff, join, split), so it is resampled by
  the same ratio; at a join (cycle repeat / skip) it crossfades old -> new offset over that grain gap (Hann, like the grains).
  Only when correcting a single voice (harmony == 0 and formant == 0: those need the whole voice in the grains). No correction
  = exact low + high = input. Synthetic breathy voice: high-band pulse at the output pitch 0.14-0.17 -> ~0.01 (= input);
  user's vocal: buzzy 40 ms windows 21-24 -> 0-1 of 2624 (all modes). CPU ~ +0.3 % of a core.
- Natural reworked after the real vocal: the user's pitch wanders (0.3-0.5 st swings at 2-4 Hz) and the centre-tracking design
  kept it (note centres 28 -> 20 cents off only). Now: corr glides to (note - midi + kept vibrato) at Retune speed; kept vibrato =
  band-pass 5.5 Hz Q 1.5 over (midi - centre) per reading, faded in over noteAge 0.1-0.25 s, reset at a new note; Vibrato knob
  adds (vib/100) x kept. Centre (2 x 80 ms) still picks the note and spots new notes; no-overshoot clamp kept. On the vocal (C
  major): note centres off 27.9 -> Natural 25 ms 13.5 (10 ms 9.6), Classic 10 ms 10.6, Robot 5.6. Synthetic tests unchanged.
- Auto-Edit R&B Natural 25 ms. Test "breath and rasp in a note don't turn into a buzz" added (58 cases).
- Sent the user renders (C major): Natural 25 ms, Classic 10 ms, Robot, Classic before the buzz fix.
Known: very raspy notes (clarity < 0.5, ~10 % of the user's readings) still get less tuning in Natural / Classic (clarity
scaling); could relax now that the high band can't buzz - check by ear first.
NEXT: user listens; step 3 = MIDI note control, per-note on/off, Honey Tune gets the two-band engine automatically (check).

## v0.10.0 Key from the beat (2026-10-06)
User asked "is this the best pitch?" - honest no; ranked next levels: 1 key from the beat, 2 low-latency recording mode,
3 MIDI / note control, 4 better big-shift engine (harmonies, formant), 5 AI pitch detection (Honey Tune first), 6 Honey Tune
editing (timing / length, per-note formant, partial re-render). User chose 1 now; recommended order after: 3, 2, 4/5.
- dsp BeatKey (BeatKey.h/.cpp): 6 kHz copy, 4096 FFT every 0.25 s, peaks 50 Hz - 2 kHz that stand 12 dB over their +-40 Hz
  neighbourhood -> pitch-class histogram (sqrt level), tuning = weighted circular mean of peak offsets (> 100 Hz); fades ~40 s;
  ready after 6 s of tonal frames; key via keyFromHistogram (detectKey refactored: histogram part shared). KeyGuess gained
  notesConfidence (margin vs the best key with a DIFFERENT note set: relative major / minor don't count).
- PitchParams.tuneCents (-50..50): sung midi measured in the beat's tuning.
- UnmaskLink: per-slot beat key (seqlock atomics) publishKey / clearKey / readBeatKey (surest ready beat); release() clears.
  Kept while the instance lives (stopped transport keeps it).
- Plug-in: param ptKeySrc {From Beat (default), Manual}. BEAT mode feeds BeatKey from its input and publishes each block;
  leaving BEAT clears it. VOCAL + From Beat + a ready beat with notesConfidence >= 0.5: followBeatKey (beat gives the notes;
  the user's Scale keeps its flavour: minor-type scales on the minor tonic, major-type on the major tonic, Chromatic -> beat's
  Major / Minor) + tuneCents; else manual Key / Scale. Meters bkState (0 manual, 1 following, 2 no beat, 3 listening; BEAT 4/5),
  bkKey/bkMinor/bkConf/bkTune/bkHeard, keyUsed/scaleUsed. Auto-Edit: settings.beatKeyKnown -> Key reason "From beat", no KEY
  UNSURE note.
- UI: Key cell has BEAT | MANUAL switch, status line ("beat: C / Am", "listening...", "no beat linked", "you set it"); key and
  scale in use glow gold (.beat); clicking a key while following switches to Manual. BEAT page: Beat Key readout (name, % sure,
  tuning). Tips NO BEAT LINKED, BEAT DETUNED. Pitch hex stat shows the key in use.
- Tests tests/test_beatkey.cpp: Am-F-C-G loop with 808 + drums at 0 / -30 / +22 cents -> A minor notes, notes conf 1, tuning
  within 0.1 cent; C minor loop; drums only -> not sure; followBeatKey flavours; link end-to-end (detuned beat -> vocal E3
  lands 30 cents low; release clears). 62 test cases.
- CI green: Actions run 37461483269 (v0.10.0; includes v0.9.0, whose own run was cancelled).
- Limits to tell the user: needs a second Voxology on the beat in BEAT mode, same project (process); needs ~6 s of playback;
  key changes are followed slowly (~40 s memory); relative major / minor can't be told apart (same notes: doesn't matter for
  tuning). Honey Tune (separate plug-in binary) can't see the link - later: ARA key signatures from Cubase.

## v0.10.1 Beat key by NOTE SET + saved test songs (2026-10-06)
- User's test songs are now in the repo: test-audio/ (see its README: Don_Birthday vocal, Gallas rap / sing clip with beat, mix
  and master, Don & Lysette acoustic clip with backing vocal). Private repo; user asked to keep them for all future threads and
  plug-ins. Earlier notes saying "scratch only" are superseded.
- First real-beat run of v0.10.0 found flip-flopping: Gallas beat (B minor) went 100 % <-> 40 % sure (ambiguity cap: C vs C#
  barely played); Don & Lysette (E with a D chord) flipped E major <-> A major. Fix: BeatKey now picks the 7-note set holding
  most of the histogram (hysteresis: a new set must win by 1 % of all), then the home note = set note with the most level +
  1/4 of its fifth -> tonicOffset (mode). Result {setRoot, tonicOffset, confidence = in-set share mapped 0.70-0.85 -> 0-1,
  unclear}. followBeatKey(beat, userScale): 7-note scales keep the beat's exact notes (Major +0, Minor / Harm Minor / Minor
  Penta / Blues +9, Dorian +2, Phrygian +4, Mixolydian +7, Major Penta +0); Chromatic = the beat's own tonic + mode. KeyGuess
  .notesConfidence removed. Vocal side hysteresis: start following at 0.5, keep down to 0.3. UI names "B minor", "E mixolydian".
- Real beats now: Gallas B minor (D major notes) conf 1 for the whole clip, tune -3; Don & Lysette E mixolydian (A major
  notes) conf 1, tune +4. Natural 25 ms in those keys: Gallas note centres 28 -> 21 cents (lots of rap), Don & Lysette 23 -> 14.
  Sent the user both mixes (tuned vocal + beat).
- Test: synthetic E - D - A - E loop -> A major notes, home E (mixolydian); vi-IV-I-V accepts A or C home (same notes).
- Schaf clips added (test-audio). Its beat plays neither D nor D# (F# home): sets A major / E major are a toss-up and flipped
  at 35 s. Fix: set hysteresis 0 for the first 15 s of listening, then 3 % of all (was 1 %); Result.openNote = the runner-up
  set's note when unclear; followBeatKey(..., &extraNotes) allows it too with 7-note scales (PitchParams.extraNotes bitmask,
  targetNote(..., extraNotes)). All 3 real beats now hold one answer for the whole clip: Gallas B minor, Don & Lysette E
  mixolydian, Schaf F# dorian (D / D# open). UI Beat Key shows "D / D# open". Test added (63 cases).
