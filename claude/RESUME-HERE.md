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
- CI green: Actions run 37470452372 (v0.10.1 with the Schaf fix). Test clips' peaks over 0 dB are intact (float in MP3, not
  flattened): scale them down when mixing / exporting renders.

## v0.11.0 MIDI + note control (2026-10-06)
- UI fix from the user's Cubase screenshot: Scale list sat on the window frame -> Pitch row 1 172 px (was 178), scale buttons
  19 px, margin 3 (rows ~15 px higher).
- PitchParams: onlyNotes (bitmask; when set ONLY these pitch classes, any octave), removedNotes (never these), transpose (-12..12
  st, added after tuning, not glided; harmony voices ignore it). targetNote(..., extra, only, removed) searches +-6 st;
  allowedMask() (all removed -> all 12). Two-band split only for |transpose| <= 2. Reading.targetMidi includes transpose.
- Plug-in: NEEDS_MIDI_INPUT TRUE, acceptsMidi true, AU type MusicEffect (Mac: AU identity changes - fine, no Mac users yet).
  Params ptMidi {Off, Notes, Learn}, ptTranspose, ptRm0..11 (Pitch Remove C..B). Notes mode: onlyNotes = pitch classes held
  now (none held = key as usual). Learn: notes played after letting go of everything start a new learned set (atomic, saved in
  state XML "midiLearned"). Meters midiNotes (held / learned), notesUsed (allowedMask). BEAT mode ignores MIDI.
- UI: Notes cell (row 2, right): 12 note buttons (lit = may be used, struck red = off, blue glow = MIDI), MIDI OFF | NOTES |
  LEARN, transpose - / +. Learn text + tip LEARN IS WAITING.
- Tests: MIDI-only target (A3 -> C4 with a C held), removed notes, all-off fallback, transpose +3 after Robot, -12 at Amount 0
  (an octave down, untuned). 64 cases.
- CI green: Actions run 37477442784.
- Cubase how-to for the user: MIDI track -> output = Voxology (insert on the vocal track); "Notes" = play / draw the melody.
- v0.11.1: user tested v0.11.0 in Cubase (BEAT on "Acoustic- Main" read G mixolydian 100 %, vocal followed "beat: G mix";
  Classic 5 ms). UI fix: Beat Key name was too big for "G mixolydian" -> font 22 / 18 / 15 px by length, nowrap, centred.

## v0.12.0 Record mode (low latency) + Honey Tune playhead fix (2026-10-06)
- User: Honey Tune playhead missing in Cubase (ruler click seeks fine, no playhead / auto-scroll while playing). The editor relied
  only on the playback renderer's stamp (SourceState.playPosition / playStamp). Now HoneyProcessor::processBlock records the host
  song position (songSeconds / songStamp atomics) and the editor maps it into the clip (song - timeline.songOffset), falling
  back to the renderer stamp; stale after 400 ms. NOT verified in Cubase yet (no ARA host here) - ask the user.
- Record mode: PitchCorrector::prepare(sr, ch, lowLatency). Low: latency 5 ms (240 @ 48k); delay-line shifter (processLow):
  read point lowD behind the newest input moves by 1 - ratio per sample; kept in [lowMin 2.5 ms, lowMin + P]; splices +-P at a
  matching waveform point (spliceTarget: NCC over one past period, +-15 % P) with a Hann crossfade over P/2; unvoiced / off ->
  back to the reported latency (exact delayed pass-through when off). Detection unchanged (uses the newest frame). Formant and
  harmony voices need normal mode (VocalChain skips voices when pitch.isLowLatency()). Tests: 3 pitches within 0.2 cents,
  latency 240, no clicks, level kept, off = exact delay. On the user's Don vocal (Natural 25 ms): note centres 12.2 cents off
  (normal 13.5), Robot 4.3; no buzz windows.
- Plug-in: toggle "recMode" (Record Mode); two VocalChains (chain + recChain, both prepared); switching resets the new one and
  sets latency via AsyncUpdater (wantedLatency). BEAT mode always normal. Meters recMode, latencyMs10. UI: header REC chip (red
  dot), status "REC 6.1 ms", Pitch tip RECORD MODE, Learn line. Header tightened (gap 11, Auto-Edit 226 px) to fit.

## v0.12.1 Honey Tune: Undo / Redo + fast edits (2026-10-06)
Honey Tune upgrade plan (user: "move forward with Honey Tune upgrades"): 1 fast edits + undo / redo (DONE), 2 note timing &
length (time-stretch: move notes, stretch ends), 3 per-note formant. Later: key from Cubase (ARA key signatures?), Capture mode.
- Undo / Redo: SourceState.undoStack / redoStack (whole edit lists, 100 deep); pushUndo on setEdit (one step per gesture: same
  note within 0.7 s = one step), forced on Reset all; cleared on re-analysis (attach). Model::undo() / redo() (defaults false).
  Panel buttons Undo / Redo (left of Snap note; pair sliders min 230 px), Ctrl + Z / Ctrl + Y / Ctrl + Shift + Z in the roll.
- Partial re-render: honey::changedRegion(before, after, clip, sr, from, to) = middle of the gap before the first changed note
  to the middle of the gap after the last (neighbours < 0.15 s apart join in); renderPart() re-runs the shifter from 0.3 s
  before (Plan got a start offset: the shifter's time 0 = clip sample `from`) and S-crossfades 10 ms into the old render.
  renderNow uses it when the track is the same, note count equal and the region < 60 % of the clip (SourceState.renderedNotes /
  renderedTrack). Don vocal (105 s, 363 notes): full render 1.84 s per channel, one-note edit 64 ms (region 2.9 s).
- Test: edit note 1 of a 4-phrase line -> region covers it, < 60 % of the clip, outside bit-identical to the old render, notes'
  pitches = full render within 3 cents, level within 1.5 dB (PSOLA level varies +-1 dB with the shifter's start), no clicks.
- CI green: Actions run 37499282269 (v0.12.1, includes v0.12.0).
- Syntax-check Honey Tune locally: ARA SDK cloned at /tmp/claude-0/sc/ara (releases/2.3.0) + JucePlugin_* stub defines.

## v0.13.0 Honey Tune: move notes in time, stretch / shorten them, per-note formant (2026-10-06)
- Repo: user made DK7602/Voxology PUBLIC (chose to leave test-audio public). Reminder scheduled: send_later trigger
  trig_01KYSkur9ifims1G57npJ9Qw fires 2026-11-01 15:00 UTC into this session -> tell them to make it private again.
- Engine (dsp HoneyTune): Note.outStart / outEnd (samples; < 0 = as sung), soundStart() / soundEnd() / timeMoved(), Note.formant.
  TimeMap (knots: each note's start / end input -> output; gaps stretch / squeeze; clip keeps its length; min gap 5 ms, min
  note 30 ms; order kept) with inAt / outAt. warp(): pitch-synchronous OLA, grains one period apart in the output read where the
  map says plus a smooth drift (a period repeated / skipped at a matching point past P/2, like the shifter); unvoiced: 5 ms
  grains at the mapped spot. render(): no timing edits -> as before; else retime (warped audio + readings at their new times +
  notes at their new places) then the usual shifter pass. Per-note formant: Guide::formantAt (Plan from planFormant: the note's
  value faded over 20 ms), Frame.formant (live frames carry params.formant), shifter reads fr.formant (split off when non-zero).
  changedRegion compares timing / formant too; a timing edit takes its neighbours along; renderPart retimes just its segment.
- Tests: move +100 ms -> sound starts / ends +0.09 / +0.10 s, pitch kept, others untouched; stretch 1.5x -> 1.46x; formant +4
  on one note -> its centroid +10 %, the others unchanged; timing edit partial == full (edges within 11 ms). 67 cases.
- UI: NoteEdit.shift (s), length (ratio), formant (st); applyEdits(..., sampleRate). Roll: drag decides by the first 6 px:
  up / down = pitch, sideways = time; grabbing a note's end (7 px) stretches from that end; cursor shows it; cells, links and
  the pitch line are drawn where the note sounds (Original: as sung). Panel: NOTE DRIFT / NOTE VIBRATO / NOTE FORMANT (-6..+6
  st, double-click 0); note info shows "+80 ms, 120 % long". Archive HNY3 adds shift / length / formant per edit (HNY2 / v1 load).
- CI green: Actions run 37510226963. Sent the user a demo (Don vocal 8-16 s: note #6 stretched 1.4x, #8 moved +80 ms; no clicks, level same).
- Limits: big stretches of noisy / breathy parts can sound smeary; a note can't be moved past its neighbours (clamped).

## v0.14.0 Big shifts: octave down fixed (2026-10-06)
User picked "4, big-shift engine". Measured first (scratch /tmp/claude-0/sc/big/eval.cpp: source-filter vowels ah / ee / oo at
130 / 220 / 330 Hz, shifts -12 -7 -3 +4 +7 +12 and formant +-4, vs an ideal reference = the same vowel resynthesised at the new
pitch (natural pulse) or with the same pulse length (same-pulse ref); metrics: harmonic-envelope error dB, inharmonic energy,
pitch). Baseline mean envelope error 5.2 dB; octave down 11-23 dB and 2 of 9 cases kept the ORIGINAL pitch.
- Cause: downward shifts lay grains further apart than they are long; (1) where the window sum fell under 0.05 the output fell
  back to the raw delayed input -> fragments of the voice at its old pitch spliced in (the "artificial" octave down);
  (2) dividing by the window sum squared the grains off. Fix: the delayed input only fills in before the first grain
  (synthStart); after that a gap is silence; grains with ratio < 0.95 mark wfloor = 1 / sqrt(ratio) and those samples are
  acc / max(ws, 1) x gain (not normalised; the gain keeps the level: half the pulses = half the power).
- Result: octave down 16.6 -> 3.7 dB, 5th down 4.1 -> 2.9, 3rd down 3.5 -> 2.9; up-shifts and formant +4 unchanged (2-3.7);
  wrong octave 2 -> 0; mean 5.2 -> 3.4 dB. User's Gallas acapella, transpose -12: notes landing an octave down 68 -> 86 %,
  staying at the original pitch 22 -> 0 %; level -1 dB. All 67 old tests pass (untouched = unchanged, voices, Honey Tune).
- Tried and reverted: longer grains for formant down (no average gain). Left: formant -4 and high "oo" vowels (F1 below f0)
  ~6-15 dB on the synthetic test - edge cases (singers change the vowel there).
- Test added: octave down of a source-filter vowel within 4 dB of the ideal (got 1.0), pitch within 10 cents, level within 2 dB.
- Sent renders: Gallas acapella + its octave-down double, BEFORE (v0.13) and AFTER. CI green: Actions run 37514662285.

## v0.15.0 AI pitch check in Honey Tune (CREPE tiny) (2026-10-06)
- Model: CREPE "tiny" (marl/crepe, MIT; weights from raw.githubusercontent.com/marl/crepe/models/model-tiny.h5.bz2 - the
  github.com/.../raw URL is 403 behind the proxy; old PyPI sdists only hold the 88 MB full model). 487,096 weights stored as
  float16 bits in dsp/src/CrepeWeights.cpp (generated: per conv layer kernel (w x in x out), bias, BN gamma / beta / mean / var;
  then dense 256x360 + bias; float16 = identical results to float32 on the checks). License: dsp/third_party/CREPE-LICENSE.txt,
  README "Third-party".
- dsp Crepe.h / .cpp: vox::crepe::Tiny (6 x [conv 'same' -> ReLU -> BN (eps 1e-3, folded to scale / shift) -> maxpool 2], time-
  major flatten 4x64, dense sigmoid 360; decode = salience-weighted mean of +-4 bins around the peak, cents = 1997.38 + 20 x bin;
  Hz = 10 x 2^(c/1200)); per-frame mean / std normalisation. Matches a numpy reference exactly (110 / 220 / 440 / 330 Hz ->
  109.89 / 220.24 / 440.61 / 329.67). ~7 ms per estimate at -O3 (Crepe.cpp gets -O3 in Release for GCC / Clang).
  resampleTo16k: Blackman windowed sinc.
- honey::aiCheck (also public) run by analyse(..., ai = true): every 30 ms where the 16 kHz frame is within 40 dB of the clip's
  loudest, on up to 8 threads; where detector and AI (conf > 0.6) are an octave (or two) apart, whichever is closer to the
  median of the agreeing moments within +-120 ms wins (no context: AI if conf > 0.8) -> readings within +-15 ms shifted by
  octaves; unvoiced readings where the AI is > 0.75 sure and the level is within 20 dB of the loudest become notes (clarity =
  0.5 x conf). Track.aiChecked / aiFixed / aiFound; Honey Tune status line shows them.
- Evidence (user's vocals): Don, octave disagreements settled by context: AI right 62, detector right 10; Gallas 3 cases,
  detector right 3 (the context rule handles both). Results: Don 65 slips fixed + 42 missed notes found; Gallas 0 + 36; Schaf
  4 + 32; Don & Lysette 66 + 55 (two singers on one track: some may follow the backing voice). Time here (4 cores): +19 s per
  105 s of audio.
- Tests: network accuracy 100 - 523 Hz within 12 cents, conf > 0.7; resampler keeps pitch; a planted octave slip is repaired
  (30 readings back), a clean track is untouched. Partial-render click check now vs the input's own steepest step. 69 cases.
- Live Voxology chain does NOT use it (too heavy for real time).
- CI green: Actions run 37520640704. All six "top tier" items done; open: blind A / B vs Auto-Tune / Melodyne if the user has one.

## v0.16.0 De-clip (Cleanup) (2026-10-06)
- Why: user's Gallas punch-in crackled after Voxology. Their raw AND processed exports both hard-clip at exactly
  +-0.8861352 (-1.05 dB), so something AFTER Voxology (Stereo Out limiter / clipper at ~-1 dB, or the export) clips both.
  The raw even has MORE clipped samples (1018+131 per ch) than the processed (124+320), yet the user hears no crackle on
  it -> clipping is probably NOT the crackle. My render's crackle candidates (1.851 / 5.391 / 7.182 / 8.277 s raw time,
  "PITCH-ONLY bursts") have 0-3 clipped samples nearby, and De-clip doesn't change them. OPEN: ask the user the exact second
  of the crackle in their file + what's on their Stereo Out; then re-check Pitch there.
- dsp DeClip.h / .cpp: runs of >= 3 equal loud samples (within 0.01 %, > 0.01 and > 25 % of the recent peak, run <= 64)
  are redrawn by Janssen least squares: order-32 all-pole model from 512 samples each side (Hann-tapered autocorrelation,
  Levinson), Toeplitz Rc normal equations, Cholesky; result held to [clip, 4 x clip] with the clip's sign. kLookahead 640
  (13 ms at 48 kHz), constant even when off; bit-exact delayed pass-through on clean audio. 128-sample context made it WORSE
  (-8 dB): shorter than one voice cycle. Tried in Python and dropped: Hermite, constrained active set (tiny gain), AR + pitch
  predictor, a frame-wise sparse (IHT) approach (worse, and far too heavy for real time).
- Measured: synthetic vowel clipped at 70 % of peak: error in the clipped stretches 7.1 dB smaller, added top end 4.0 dB less.
  Real vocal (Don) in Python: ~3 dB / ~3.5 dB. CPU: 20 s clipped hard at -6 dB, 2796 runs, 0.8 % of one core.
- Chain: first (before Pitch; the pops / breaths side-chain now hears the de-clipped input); off in Record mode (no added
  latency there). CleanupParams.declip (default on); ChainMeters.declipRuns. Plug-in: "clDeclip" toggle (Auto-Edit sets it on),
  meters.declipRuns (running total) -> editor declipNow (decaying ~1 s) / declipTotal. UI: Cleanup page 7 cells (100 px, nowrap
  subs), De-clip ON / OFF first, header stat "de-clip N", Learn text + tip CLIPPED RECORDING. 71 test cases.

## v0.16.1 De-clip repair REMOVED -> Clip watch (warnings only) (2026-10-06)
- Why: user's "master chain off" export of the Honey-Tune-only track: no flat tops at all (crest 22 dB) but the vocal peaks
  +3..+7 dBFS everywhere and +14 dB at 9-11 s (the punch-in). Their Stereo Out limiter (ceiling -1.05 dB) was squashing
  4-8 dB, ~15 dB at the punch-in -> THAT is the crackle. De-clip on the master-on file: 550 redraws, no measurable HF
  change, peaks to +2 dBFS. Told the user: lower punch-in clip gain 6-7 dB, vocal ~8-10 dB, limiter should shave 1-3 dB.
  User agreed to remove the repair and keep detection.
- dsp ClipWatch.h / .cpp (replaces DeClip): read-only, no latency. runs = 3+ equal loud samples (> 0.01, > 25 % of
  recent peak, within 1e-6) at ANY level; overs = samples |x| > 1. VocalChain runs it on the input (also in Record
  mode and bypass); ChainMeters.input {runs, overs}. Latency back to Pitch + Saturation. CleanupParams.declip gone.
- Auto-Edit: clippedRuns now from ClipWatch (old rule only caught >= 0.989, missed the -1.05 dB limiter); new TOO HOT note
  when the clip peaks over 0 dBFS.
- Plug-in: clDeclip param gone. meters.clipRuns / overs (running totals); editor sends clipNow / overNow (~1 s decay),
  clipTotal (since editor opened), hotPeak (input peak, held 10 s then falls 1 dB / s). UI: Cleanup back to 6 cells;
  hex stat "clipped in!" / "too hot in!"; inputTips() (CLIPPED RECORDING, TOO HOT) on Cleanup and Output Learn pages.
- On the user's files: master-on 1070 clipped stretches / 0 overs; master-off 0 / 58,214 overs; raw punch 277 / 0.
  71 test cases.
- CI green: Actions run 37529268173 (v0.16.0 run 37526831096 was green too, superseded).

## v0.16.2 Whole-step moves keep the voice's tone (Honey Tune + Pitch) (2026-10-07)
- User: Honey Tune note 26 (Gallas, sung G3 +46 c) moved to A3 / F3 "not in the right octave or something". Their
  vocal-only exports (Error*.wav, saved as test-audio/Gallas_2026_Note26_*): pitch and octave were RIGHT (A3 = 220 Hz
  fundamental), but the tone changed: on A3 the 3.7 kHz resonance moved to 4.2 kHz, 4-6 kHz +11..13 dB (thin); on F3
  1-1.6 kHz +7.6 dB. (The first look, at a screen recording with the beat mixed in, wrongly suggested a weak fundamental.)
- Cause: PitchCorrector's two-band mode (> 2 kHz read at the grains' sliding offset = resampled) moves the high band's
  formants by the pitch ratio. Fine for nudges, wrong for whole steps. Fix: split only when |corr| <= kSplitMaxSemis
  (1.0 st); bigger moves put the whole voice in the grains (PSOLA keeps formants). Marks already crossfade split on/off.
- Measured: synthetic voice, shifter vs ideal per band: before up to +-13 dB, after within ~2 dB (one 600 Hz band -5..-7
  both before and after). User's note: A3 4-6 kHz +13.2 -> -0.3 dB; F3 1-1.6 kHz +7.6 -> +0.2. HF pitch-pulse ("buzz")
  metric 32-34 -> 36-37 dB on the moved note (raw 32): slightly more, watch for it.
- Test: "moving a note a whole step keeps the voice's tone": error above 2 kHz vs ideal 6.6 / 7.1 dB before, 1.3 / 1.1
  after. Sent the user BEFORE / AFTER renders of note 26 on A3 and F3. 72 test cases.

## v0.17.0 Pitch audit: long notes no longer warp (Natural) (2026-10-07)
- User: "G3 sounds warped too ... Voxology made that note sound weird"; "warping long notes in other songs, supposed to
  sound natural"; "audit the pitch correction in Voxology and Honey Tune"; "getting way better results with Waves Tune
  Real-Time". Audit tools now in tools/audit/ (README there). Reference = Honey Tune's offline notes of each vocal; all
  five test vocals (Don, Gallas, Gallas note 26, Schaf, Don & Lysette), Natural 25 / 60 ms and Classic 10 ms.
- ROOT CAUSE of the warp: Natural restarted a note (centre = the instantaneous pitch, vibrato band reset, onset clamp
  re-armed) whenever the pitch was 0.8 st off the centre for 2 readings (5 ms). A wide wobble (note 26: +-0.8 st at ~6 Hz)
  hit that at its peaks; the new centre sat on a peak, so the next swing restarted it again: the target flip-flopped
  G3 <-> G#3 (18 flips in 1.5 s on a replica), correction steps up to 40 - 66 cents in one reading.
- Fix (PitchCorrector::analyse): for a note's first 0.25 s the centre is the average of everything sung since it began;
  a mid-note restart needs the pitch off the centre by 0.8 AND nearer another note AND (>= 1.5 st, or out for 45 ms);
  then 0.15 s cooldown (unless >= 2 st). Replica: biggest step 0.9 c, 0 flips.
- Also: clarity smoothing 0.5 per reading -> 40 ms one-pole (its flicker scaled the correction: a wobble) - jumps -27 %.
  Two-band hysteresis: off above 1.0 st, back on below 0.8 (splitBig).
- Tried and dropped (scored worse on the fair test): note "persistence" (a new note must win 12 - 40 ms: delayed real
  changes, and a buggy first version stuck between two notes); a vibrato-depth-raised restart threshold (notes landed
  late); a centre-only Natural (output = note + sung - slow centre: no jumps but notes 25 - 40 c off on p90, and
  overshoots after dips) - code kept in the audit history only.
- Fair score (released v0.16.2 -> v0.17.0), Natural 25 ms: centre error p90 69 -> 58 c, wrong-note time 20.6 -> 18.7 %,
  jumps -21 %; Natural 60 ms: p90 57 -> 49, 19.2 -> 16.8 %, jumps -23 %; median centre +0.8 c (noise); Classic: same.
  Shape change (movement above 1 Hz vs sung) stays ~13 - 19 c median: Natural still evens out non-vibrato movement.
- Honey Tune audit: notes land within 0.2 - 0.4 c median (p90 0.9 - 2 c); shape kept ~4 - 7 c rms; pitch "glitches" are
  mostly the measuring tracker's own noise (32 - 106 / min on untouched audio). Guide path unaffected by the Natural fix.
- Test: "Natural doesn't restart a long note with a wide vibrato (no warp)" (+-0.9 st, 40 c sharp): step < 5 c.
  Sent the user BEFORE / AFTER: note-26 clip through Auto-Edit Rap, Don's long notes (Natural 25 ms). 73 test cases.
- Open: Natural's evening-out of non-vibrato movement (shape change) is a design trade-off; compare with Waves Tune RT
  by ear once the user has v0.17.0.
- CI green: Actions run 37570962193 (v0.16.2 run 37568620976 green too).

## v0.18.0 Auto key (beat, side-chain or voice) + header + Pop / Folk / Natural Singer (2026-10-07, "key detection" thread)
User video: vocal-only session, Key Compass says D minor, Voxology fell back to A Chromatic (no beat linked) and sounded
off; D minor / D mixolydian sounded great. Asked: 1) key detection with OR without Voxology on the beat ("ideally I'm
playing with music on so it can detect key from the beat during play"); 2) header re-order + round power button;
3) styles Pop, Folk, Natural Singer. Their "Test for Claude" WAV (vocal + beat, D minor) saved as
test-audio/DMinor_Test_2026_Clip_Music_Vox.mp3: BeatKey reads it C-major notes, Bb open, home D = D minor, conf 1.
- Header (commit 2d33f86, CI green run 37629922384): UNDO, COMPARE, MATCH, REC, wordmark (margin-left auto; 0 in BEAT),
  power (flex-shrink 0: round 46 x 46). Checked in Chromium with mock.js at 1600 px.
- Styles appended (indices 5 Pop, 6 Folk, 7 Natural Singer: saved projects keep theirs): kSpecs, pitch tunes (all
  Natural: 15 / 50 / 40 ms; amount 100 / 70 / 80), breathBase 7 / 3 / 4, isSungStyle(); Folk / Natural Singer no doubler,
  no delay (delay "Off" reason, keep(Module::delay)); presence 3.5 kHz for R&B / Folk / Natural. Picker: long names 13 px.
- Key source param ptKeySrc renamed {Auto, Manual} (same index). VOCAL + Auto picks the surest of, in order:
  2 = the beat on Voxology's new SIDE-CHAIN input (bus "Beat (side-chain)", stereo, off by default; a second BeatKey,
  sideKey, read first in processBlock since its channels alias the outputs), 1 = a Voxology on the beat (link, as
  before), 3 = the voice (VoiceKey). Hysteresis per source: start 0.5, keep 0.3. Meter bkSource (0 none / 1 link /
  2 side-chain / 3 voice); bkState 1 following, 3 listening, 2 nothing heard. numIn is now getMainBusNumInputChannels().
- dsp: pickNoteSet() factored out of BeatKey::analyse (returns ties: sets within 3 %). VoiceKey: fed one Pitch reading
  per host block (voiced, sungMidi, clarity >= 0.5); counts readings within 0.4 st of a 60 ms smoothed pitch (held notes,
  vibrato ok, glides no); 10-cent histogram, the singer's own sharp / flat (circular mean) taken out before folding
  into notes; ~60 s memory (fades only while singing); ready after 6 s held; confidence share 0.68 -> 0, 0.88 -> 1,
  capped 0.25 if 2+ ties or < 5 notes at >= 3 %. tuneCents 0.
- Voice-only results (probe, block 128 / 512 / 2048 all alike): Don & Lysette A major notes (E mixolydian) sure at
  ~25 s = the beat's; Don: unsure 45 s (holds G 70 %), then G major notes, F open later (song C major per user's app:
  one note apart); Gallas, Schaf (rap): max conf 0.41, never followed -> Chromatic. First tries without the fixes were
  sure of wrong keys (D# major on Don, F# major on Schaf): the 5-note rule, offset removal and 0.68-0.88 fixed those.
- Auto-Edit: AutoEditController.liveKeySource (set by the processor from the meters) fills settings.beatKey*; new
  beatKeyFromVoice -> Key reason "From your voice: ..."; KEY UNSURE note steps point to side-chain / Auto.
- UI: Key switch AUTO | MANUAL; status "beat: Dm" / "voice: Dm" / "voice: listening..." / "sing or play"; tip NO BEAT
  HEARD (side-chain steps for Cubase) replaces NO BEAT LINKED; Learn text updated. Version 0.18.0.
- Tests: "Auto-Edit: Pop, Folk and Natural Singer styles", "Voice key: a sung D minor melody, one held note, rap
  glides". 75 cases. Plug-in compiled on Linux (apt: X11 / GTK / WebKit dev libs) before pushing.
- Open: user to try side-chain routing in Cubase (Artist 14: side-chain button in the plug-in window, then a Send from
  the beat track to "Voxology - Side-Chain").
- CI green: Actions run 37634314512 (v0.18.0, commit 8de7309: engine ASan tests, Windows build + tests, pluginval
  strictness 10 + VST3 validator, with the new side-chain bus).

## v0.18.1 Beat key home from the 808 + side-chain status + bigger Pitch titles (2026-10-07)
- User test (video, "Gal Bears" = the Gallas clip): side-chain on, but the beat track's send was off -> Voxology only
  heard the voice ("voice: A mix", flip-flopping). Send on, side-chained to the RHYTHM KEYS track only: "beat: Bm";
  Key Compass: Em. User's chord list (F#m E D C#7) does NOT match the audio (G, not G#, in the beat; numpy chroma).
- Root cause: BeatKey's home note came from the chords only (B 25 %); the 808 sits on E (E1 = 41 Hz, under the old
  50 Hz floor; its 3rd harmonic B2 fooled a first bass try). Fix: per analysis the LOWEST prominent peak 28 - 160 Hz is
  the bass note, weighted by its level vs the frame's loudest peak (an 808 counts fully, an acoustic guitar's low strings
  little), faded like the chroma; home score = chroma + 1/4 fifth + bass (pickNoteSet(..., &bass)). Also: when two sets
  tie on one barely played note, name the one where home is minor / major (E minor, not E dorian; same notes allowed).
  Real beats: Gallas E minor (was B minor), Schaf F# minor (A major notes, D# open), D-minor test D minor, Don & Lysette
  notes A major, home now B dorian (was E mixolydian; never confirmed by the user; its lowest notes are mostly B).
- Side-chain status: meter scState (0 off, 1 on but under -80 dBFS for 2 s, 2 sound); Key status "beat: no sound" and
  warn tip SIDE-CHAIN SILENT (Cubase steps) when the vocal plays but the side-chain is silent.
- VoiceKey: once sure (>= 0.5), the few-notes / ties cap no longer applies (no flip-flopping between "listening" and a key).
- UI: Key / Scale / Tune / Notes title and status on one pill line, status 12.5 px ("KEY · beat: Em"); Tune's status
  "live". Checked in Chromium (serve plugin/ui over http with JUCE's javascript/ as juce/: app.js is a module).
- Tools (local, -DVOX_BUILD_LINKCHECK=ON): vox_sidecheck (the processor with a beat on its side-chain: prints side-chain
  state / source / key); vox_vst3sidecheck (loads the real Voxology.vst3 like a host, side-chain on, F4 sine vocal:
  "moved" = the beat's key followed; MANUAL_BMINOR=1 control). Linux plug-in build needs apt X11 / GTK / WebKit dev
  libs; build with --parallel 2 - 3 (8 ran out of memory and restarted the worker).
- Advice given: side-chain the whole beat (a Group of all music tracks, or the 2-track beat), not one instrument and not
  Stereo Out (it has the vocal in it). Key changes mid-song are followed slowly (15 - 40 s). Guitar-only: notes right,
  home name less sure.
- Test "Beat key: the 808 names the home note" (Bm - G - Bm - D over an 808 on E1 -> E minor, C# open). 76 cases.
- CI green: Actions run 37671076436 (v0.18.1, commit 972bc23).

## Thread handoff (2026-10-07): "key detection" thread -> "Voxology audit" thread
- Key detection state: v0.18.1 (CI green). User's setup advice given: one Voxology in BEAT mode on a Group of all music
  tracks ("Beat"; Cubase 14: right-click > Add Track > Group Track to Selected Channels), Voxology on each vocal track
  with Key on AUTO (no side-chains needed); side-chain only for a single vocal. Never Pitch on a vocal group (monophonic).
- Open: user to confirm "beat: Em" on Gallas with the full beat; voice-only key can land one note off (Gal Bears vocal
  read A mixolydian); Don & Lysette home now B dorian (unconfirmed).

## Cleanup audit (2026-10-07, "Voxology audit" thread) - findings only, no code changed yet
Tool: tools/audit/cleanup_audit.cpp (4 acapellas, Auto-Edit settings: Gallas / Schaf Rap, Don Melodic, Don & Lysette R&B).
- Low cut: fine (0.72 x lowest notes: 90 - 135 Hz; Don's rumble handled).
- Breath control: fine. 0 ducks over loud voiced sound on all four (Don: 37 breaths, 5.6 s turned down).
- Gate: inside words fine (0.5 - 2.5 % of word frames touched, 0.00 dB lost). PROBLEM: phrase starts. It has no
  look-ahead, so soft starts are cut until they cross the threshold: Don 12 of 20 phrase starts lose > 3 dB (median
  -8.5 dB), fully open a median 40 ms after the word starts (worst 373 ms). Others mild (1 start each).
- Pops: the big cuts (15 - 19 dB) hit real thumps (under-100 Hz 10 - 28 dB over the voice). PROBLEMS: (1) the cut
  hangs on 50 - 140 ms > 6 dB (30 ms detector release + 50 ms cut release), into the vowel after the "p"; (2) ~half
  the events are light 3 - 6 dB cuts on ordinary voiced words (low band only ~10 dB over its norm): small thinning.
- Proposed plan (awaiting the user's OK): 1) gate listens to the side-chain (free look-ahead, like pops / breaths) and
  opens ~10 ms before the word; 2) pops let go within ~20 - 30 ms of the thump, and ignore the small 3 - 6 dB cases;
  re-run the audit + tests, then build.

## v0.18.2 Cleanup fixes (2026-10-07): gate look-ahead, pops let go sooner
- CORRECTION to the audit above: most of Don's "chopped phrase starts" were BREATHS before phrases (unvoiced,
  ~-35 dB, 200 - 370 ms), which the gate is meant to turn down. Real voiced starts lost 0 - 1 dB on Don. The real
  chopped starts were soft sung ones: Don & Lysette 16.04 s (-6.6 dB) and 24.17 s (-2.5), Gallas 30.75 s (-4.2).
  Pop tail also smaller than first said: cut > 6 dB on sound with no thump was 0.1 - 0.64 s per clip (~15 - 20 ms
  per pop), not 50 - 140 ms (most of that time the thump is still there).
- Gate: Cleanup::listen() hears the chain's input (before Pitch, its own copy of the low cut) and the gate's peak
  detector reads it through a delay line so it leads the audio by 10 ms (kLookAheadSeconds); hold + 10 ms so tails
  aren't cut earlier. Record mode: ~5 ms lead (all there is). Standalone (no listen) unchanged. Results: soft sung
  starts -6.6 -> -2.1, -2.5 -> -0.3, Gallas -4.2 -> -1.4 dB; voiced frames touched roughly halved (Don 17 -> 8).
- Pops: cut release 50 -> 20 ms; threshold 9 -> 10 dB (11 failed the synthetic 6 dB pop test by 0.01 dB; 12 missed
  real thumps). Cut on no-thump sound: Gallas 0.46 -> 0.16 s, Schaf 0.64 -> 0.10, Don 0.10 -> 0.01; small (< 8 dB)
  catches 12 -> 10, 11 -> 8, 12 -> 7; deep cuts on real thumps kept (13 / 20 / 16).
- Tests: "In the chain the gate opens before a soft word start", plus the vowel after each pop gets its low end back
  (within 1 dB, 100 - 200 ms after the pop starts). Both fail on v0.18.1, pass now. 77 cases.
- Low cut, breath control: no change (fine).
- CI green: Actions run 37702101577 (v0.18.2, commit 0555a36).

## Tone EQ audit (2026-10-07) - findings only, no code changed yet
Tool: tools/audit/eq_audit.cpp. Same 4 vocals / styles as the Cleanup audit.
- Filters: fine. Measured response = what the UI / Auto-Edit predict (within ~0.1 dB; glides, exact bypass at 0 dB).
- Auto-Edit gets only part of the way: mean distance from the style target 3.4 -> 2.5 (Gallas), 3.5 -> 3.0 (Schaf),
  2.3 -> 1.8 (Don), 4.1 -> 2.9 dB (Don & Lysette). Three causes:
  1) Mud aims at the wrong spot: all four vocals are 4 - 7 dB hot at 315 - 630 Hz (boxy), but Mud lands at 200 Hz on
     Gallas / Schaf (already Body's job) and cuts only ~2 dB; 315 - 400 Hz stays +4.5 to +6 dB after.
  2) Presence boosts at a fixed 4 kHz; the real dip is 2.5 - 3.15 kHz (-4 to -7.6 dB) and stays -3.5 to -4.7 after.
  3) Body judges bands under the singer's lowest note (Don & Lysette: 160 Hz, which the low cut is removing anyway)
     and boosts +1.9 dB there.
- Caveat: the target curve is built in, not measured from real finished vocals; 4 different singers all reading
  "boxy" could partly be the target. Any fix should be A/B'd by ear.
- Proposed plan: Mud searches 250 - 630 Hz and cuts a bit more of the excess; Presence boosts where the dip actually
  is; Body only judges bands at / above the lowest notes. Re-run the audit + tests, send before / after clips.

## v0.19.0 Tone EQ from pro vocals (2026-10-08)
- Pro data: MUSDB18 7-second preview set (github.com/sigsep/sigsep-mus-db releases v0.4.0, MUSDB18-7-STEMS.zip, 144
  released songs, vocals = stream 4 of each .stem.mp4; research / non-commercial: measurements only in the code, audio
  NOT committed). 143 usable. Mostly rock / indie (few rap): their top end is darker than modern rap / trap.
- Finding: above 630 Hz pro vocals agree (spread +-1 dB at 500 Hz - 1.6 kHz); below it the tone follows the voice's
  pitch. So styleTarget (style, f0) = pro median above 630 Hz + pitch-matched medians below (voices at 148 / 193 /
  255 / 335 Hz, blended by the singer's f0Median) + the style's body / presence / air offsets.
- Auto-Edit Tone EQ (no reference) now uses fitTone (Reference Match's fitter) for all 5 bands, toward a partial
  target: only the part of the gap beyond the normal pro spread (half the IQR per band, after pitch matching:
  5.9 dB at 100 Hz .. 0.8 at 800 Hz .. 4.5 at 16 kHz) is corrected, 75 % of the way at medium Intensity. Limits:
  Body -4..+3 and only from the voice's lowest note up; Mud (>= 200 Hz) / Nasal cut only; Presence 2 - 5 kHz, -2..+5;
  Air >= 8 kHz, -1.5..+6 (+2 on a hissy take); 5.5 - 7.5 kHz not judged (the De-Esser's "s" zone); each dB^2 of gain
  costs 0.02 in the fit (stops bands cancelling each other to chase tiny errors). Reference Match path unchanged.
- "Do no harm" on the 143 finished pro vocals (style Rap if mostly rapped, else Pop): total EQ move median 7.5 dB
  (old rules 8.1); first tries without the tolerance / cost moved them MORE (13 dB) - kept as the reason for both.
- The user's vocals mostly sit inside the pro range: the moves are now gentler and better aimed (Gallas Body -1.5 @
  325, Schaf Body -1.5 @ 400, Don Mud -1.5 @ 548, Don & Lysette Mud -2 @ 483). Before / after clips (Auto-Edit whole
  chain, 20 s, loudness within 0.1 LU) sent to the user.
- Cambridge-MT (user's link) is behind a Cloudflare check (no automated download) and is RAW multitracks: good for
  raw-vocal robustness tests if the user downloads some; not finished vocals. Asked the user for their own finished
  vocal stems (best genre-matched target data).
- Test "Auto-Edit Tone EQ: target from pro vocals follows the voice's pitch; moves stay in their jobs". 78 cases.
- CI green: Actions run 37711674326 (v0.19.0, commit c199333).

## Dynamic EQ audit (2026-10-08) - findings only, no code changed yet
Tool: tools/audit/dyneq_audit.cpp (+ a scratch batch over the 143 MUSDB18 pro vocals).
- DSP: fine. Detector = the bell's own slice vs the rest of the voice, learned 60th-percentile normal, "s" hold for
  Harsh; envelopes smooth enough down to 80 Hz; exact pass-through. Cuts do NOT follow the melody (a low harmonic in
  the bell: 70 - 85 % of singing vs 78 - 87 % of cut moments: barely above chance).
- PROBLEM 1 (Auto-Edit): it switches bands on for normal vowel-to-vowel variation. On the 143 finished pro vocals it
  turns on ~2 bands each (Boom 22 %, Mud 64 %, Nasal 66 %, Harsh 46 %; only 19 get none). The user's jumps (Auto-Edit's
  p95 cut at 50 %: Mud 4.4 - 7.8, Nasal 4.7, Harsh 4.0 - 5.6) are about the pros' (median 4.2 - 4.5, p75 4.8 - 5.8);
  only Schaf's Mud (7.8, about the pros' p90) stands out. (Pros are 7 s clips, the user's 35 - 105 s: longer audio
  reads a little higher, so the user's are even less unusual.)
- PROBLEM 2 (behaviour once on): each band cuts 13 - 19 % of the singing, usually all the way to its max (p90 = max).
- Text bug: the reason says "rise about X dB past your normal" but X is the p95 CUT at 50 % sensitivity (the rise is
  ~4 dB + X / 1.5).
- Proposed plan: switch a band on only when its jumps are beyond the pros' upper quarter (measured per 7 s so long
  takes compare fairly); Max Cut = the excess over a typical pro; fix the text; re-run on pros ("do no harm") + the
  user's songs; clips.

## v0.19.1 Dynamic EQ: only jumps beyond finished pro vocals (2026-10-08)
- Auto-Edit's measure (p95 of the band's cut at 50 % Sensitivity, best third-octave in the zone, share <= 20 %) is now
  taken per 7 s piece and the median used (fair vs the 7 s pro clips; a long take otherwise reads higher). A band is
  switched on only above the pros' top quarter (Boom 4.8, Mud 5.8, Nasal 5.2, Harsh 4.9; pros' typical 3.8 / 4.2 /
  4.5 / 4.3). Max Cut = 1.5 x (jump - pro typical), 2 - 6 dB. Reason text now gives the real rise (thr + cut / 1.5).
- Do no harm on 143 pros: bands on per vocal 1.99 -> 0.49; none on 19 -> 92 of 143.
- User's songs: Gallas Mud 4.5 -> off; Schaf Mud 6 -> off (per 7 s its jumps are normal; the whole-clip 7.8 was the
  long-take effect); Don Mud / Nasal off, Harsh 5.5 -> 2 dB @ 2.5 kHz; Don & Lysette all off. Before / after clips sent.
- Test "Auto-Edit leaves the Dynamic EQ off for word-to-word changes a finished vocal also has" (12 dB bloom: off;
  v0.19.0 cut it 3 dB; 18 dB still caught). 79 cases.
- CI green: Actions run 37715221627 (v0.19.1, commit de9409a).

## v0.19.2 De-Esser: Auto-Edit checks the "s" again after the compressor (2026-10-08)
- Audit (tools/audit/deess_audit.cpp): the module itself is fine. On Don & Lysette (18 % at 6.8 kHz) it cut 97 % of
  "s" frames (avg 2.1 dB), touched only 5 % of vowel frames (avg 0.13 dB), and caught "s" onsets (first 5 ms -1.1 dB
  vs -1.2 later: no leak). Targets vs pros: pro "s" median -5.1 dB vs voice (rap-like -4.4); targets Rap / Trap -4,
  Melodic / Pop -5, R&B / Folk / Natural -6: sensible.
- PROBLEM: Auto-Edit judged the "s" before the rider and compressor, which bring the quieter "s" up 0.6 - 1.4 dB
  (saturation: 0). So it left the De-Esser off on Gallas, Schaf and Don, and the chain's output "s" ended 1 - 2 dB over
  target.
- Fix: "De-Esser, second look" after Saturation is decided: render the chain up to the De-Esser once, then run De-Esser
  -> Rider -> Compressor on it (modules directly, Saturation skipped: it doesn't move the "s"), and binary-search the
  amount until the "s" is on target. Auto-Edit time +27 % (Don 105 s: 46 -> 59 s; a full-chain version was +130 %).
- User's songs (Auto-Edit): Gallas 0 -> 10 %, Schaf 0 -> 22 %, Don 0 -> 28 %, Don & Lysette 18 -> 54 %. Pros: on for
  78 / 143 (was 53), median 55 % (Voxology's own compression lifts their "s" too).
- Test "Auto-Edit's De-Esser keeps the s on target at the end of the inserts (after the compressor)" (fails on
  v0.19.1: s ended at -0.2 / +0.5 dB vs the -4 target). 80 cases. Before / after clips sent.
- CI green: Actions run 37720067094 (v0.19.2, commit fbebd37).

## Rider audit (2026-10-08) - findings only, no code changed yet
Tool: tools/audit/rider_audit.cpp (optional range override to test the module itself).
- As Auto-Edit sets it: Off on Gallas / Schaf (lines only ~4 - 5 dB apart), +-2 dB slow on Don, +-1 dB on Don & Lysette:
  no measurable effect (line spread 8.0 -> 8.0, 7.7 -> 7.9). The compressor does all the evening.
- The module with real room (+-6 dB, Auto-Edit's target / speed): it makes lines LESS even (Don 8.0 -> 9.1 dB, Don &
  Lysette 7.7 -> 8.7) and lifts breaths / airy bits (p90 +1.9 .. +3.1 dB) and quiet voiced bits (p90 +3.1 .. +4.3).
  Causes: (1) it holds its gain through gaps, then glides at 0.4 - 0.8 s, so each new line starts with the LAST line's
  gain (lines last 1 - 3 s: the lag dominates); (2) anything within 24 dB of the target counts as voice, so breaths,
  word tails and quiet consonants are turned up toward the target.
- Proposed plan: (1) ride on voiced sound only (body vs air like Breath Control: breaths / "s" / tails hold the gain);
  (2) phrase-start catch-up: after a gap, set the gain from the new line's first ~150 ms quickly instead of carrying
  the last line's (use the 32 ms free look-ahead from Pitch's latency too); then re-run (+-6 dB must make lines more
  even, not less) and only then let Auto-Edit use bigger ranges where lines really are uneven.

## Overnight run plan (2026-10-08, user asleep: "continue with audits and edits until all are complete")
Order: Rider fix -> Compressor -> Saturation -> Voices (doubler / harmonies) -> Delay -> Reverb -> Output / loudness ->
Auto-Edit whole-chain check on the pros + user's songs. Each module: audit tool in tools/audit/, fix, tests, version
bump, commit + push, notes here. If usage runs low: commit what's done, note where to resume here.

## v0.19.3 Rider follows the line on voiced sound only + "Ad-libs" style renamed "Robot" (2026-10-08)
- Rider rebuilt: the gain follows the LINE's level (voiced sound only, averaged 0.8 / 0.4 / 0.25 s for Slow / Medium /
  Fast, fader glide 0.25 / 0.12 / 0.08 s) instead of a 150 ms RMS chased with a 0.8 - 0.2 s lag. Only sung / spoken
  sound teaches it: more energy under 800 Hz than over 1.5 kHz (breaths, "s" hold it) and within 15 dB of the recent
  loud words (fading tails hold it); first word primes the level. Tried: very fast (0.3 / 0.15 / 0.08 + 0.1 - 0.03
  glide) evened most but pumped (3 dB per 50 ms) and lifted breaths again; slow (1.6 s) kept the old lag problem.
- Result with +-6 dB (rider_audit, override): Don 400 ms spread 8.0 -> 7.4 (medium) / 6.9 (fast), phrase to phrase
  3.6 -> 1.7 / 1.4 dB (old module: 8.0 -> 9.1, i.e. worse). Don & Lysette (already very even, 6 phrases): 0.7 ->
  1.2 - 1.6 phrase (small). With Auto-Edit's own settings: Don phrases 3.6 -> 2.6, Don & Lysette 0.7 -> 0.4.
- Auto-Edit range: (spread - 6) x 0.75 (was x 0.5) x style scale; still off on Gallas / Schaf (lines 4 - 5 dB apart).
- Test "Rider evens loud and quiet lines and doesn't lift the breaths between them" (a behaviour lock: the old rider
  also passes it on long steady lines; the real-song numbers above are the proof). 81 cases.
- User request: style "Ad-libs" is now "Robot" (index 3 unchanged, so saved projects keep it): kStyleNames, app.js,
  mock.js, Learn text, Auto-Edit reasons / suggestion ("Try Robot style on your ad-lib track").
- CI green: Actions run 37724525642 (v0.19.3, commit 942c3d4).

## Compressor audit (2026-10-08): no change
- tools/audit/comp_audit.cpp. Distortion on a loud 110 Hz note: THD+N -66 .. -79 dB (inaudible). Auto-Edit's fixed
  squeeze per style leaves punch (microDyn) at 2.7 - 3.4 dB vs pros 3.5 typical (middle half 2.7 - 5.0): inside the pro
  range, slightly on the tight side, and evens lines well (Don 10.5 -> 5.6, Don & Lysette 8.9 -> 5.5 dB).
- Tried and reverted: aiming the leveler at the pros' punch (3.5 dB) like Reference Match does: the leveler then
  barely works (thresholds -0 .. -5.5 dB) because the peak stage alone brings the punch down, and lines got less even
  (Don 5.6 -> 8.1 dB). Pros' line-to-line range can't be measured on 7 s clips, so the evidence doesn't support it.

## Saturation audit (2026-10-08): no change
- tools/audit/sat_audit.cpp. Auto-Edit's estimate = the real oversampled module within 0.4 dB (-34.2 .. -42.4 dB of
  harmonics at Mix 50 %). Aliasing with a loud 9 kHz tone: -127 dB (Tape), -71 dB (Tube, its 2nd harmonic at 18 kHz is
  -39 dB). Note: the curve is level-dependent (bends relative to full scale, Polisher's design); the user's float
  acapellas reach it peaking +5 .. +9 dBFS, so Auto-Edit picks low Drive (1 - 3 dB). Re-run Auto-Edit after changing
  the vocal's level before Voxology.

## Doubler / Delay / Reverb / Output audits (2026-10-08): no change
- Doubler (12 / 17.5 ms copies, slow drift): comb ripple vs the lead at Auto-Edit's amounts 20 - 50 %: std dev 0.5 -
  1.2 dB in one ear, 0.4 - 1.0 dB in mono (100 %: 2.2 / 1.8): normal double-track character. Delay / Reverb: tests
  cover beat grid, ducking, decay; space after phrases on the user's Auto-Edit outputs -22.4 .. -29.7 dB vs pros -25.6
  median (only 10 of 143 7 s clips measurable). Output: loudness-matched to the input (no limiter: a channel plug-in).

## v0.19.4 Whole-chain "do no harm": Compressor punch floor (2026-10-08)
- tools/audit/whole_chain.cpp: Auto-Edit's whole insert chain (space off) on the 143 pro vocals: tone change median
  1.9 dB, "s" kept, loudness matched, but punch 3.5 -> 2.2 dB (below the pros' bottom quarter 2.7): the style's fixed
  squeeze flattened already-compressed vocals.
- Fix (Auto-Edit compressor, no reference): after the style's squeeze, if the punch (Auto-Edit's punchDb) falls below
  min (2.7 +-0.4 by Intensity, the take's own punch), relax: raise the leveler threshold until it's back, or if the
  leveler alone can't, turn it off and ease the peak stage. Reason text says so ("Off: already as controlled as a
  finished vocal" / "Less than usual ...").
- Pros now: punch 3.5 -> 2.7 (p10 2.1), tone change 1.7 dB. User's songs unchanged (their squeeze stays above the
  floor: Gallas 2.7, Schaf 3.4, Don 2.7, Don & Lysette 2.9).
- Test "Auto-Edit doesn't flatten a vocal that's already compressed (a second pass keeps the punch)" (v0.19.3 took the
  test take to 1.7 dB on the first pass; now 2.65, second pass 2.65). 82 cases.
- CI green: Actions run 37727725597 (v0.19.4, commit ece62d5).

## v0.19.5 Reference Match: disciplined fit + reference low end moved to your pitch (2026-10-08)
- Check (scratch refcheck: MUSDB rap stems "Little Chicago's Finest - My Own", "PR - Oh No" as references on Gallas /
  Don): the reference fit slammed bands to +-8 dB (total 25 - 37 dB), stacked Presence +3 and Air +8 at 8 kHz, boosted
  Body +8 at 80 Hz under a 120 Hz low cut, and copied the reference SINGER's low end (pitch-dependent).
- Fix: ReferenceProfile.f0Median (saved in the plug-in state as "f0"; older saves = 0 = no pitch move). Under 630 Hz the
  wanted tone = reference + (pro target at your pitch - pro target at theirs). Fit limits like the style fit with a bit
  more room: Body -6..+4 from your lowest note up, Mud (>= 200 Hz) / Nasal -8..+1, Presence 2 - 5 kHz -4..+6, Air >= 8 kHz
  -3..+6 (+2 if hissy), 5.5 - 7.5 kHz not judged, cost 0.02 / dB^2. Low cut may rise only to 0.85 x your lowest notes
  (was 0.95: the fit leaned on it once EQ moves cost something).
- Result: totals 17 - 23 dB, no stacking, nothing under the low cut; tone match about as close (2.5 - 3.1 dB left vs
  2.0 - 3.4 before).
- Test "Reference Match keeps each band on its job (no stacking, nothing under the voice or in the s zone)". 83 cases.
- CI green: Actions run 37729920905 (v0.19.5, commit 3674eaf).

## Unmask quick check (2026-10-08): no change
- Gallas beat (stereo) + Gallas acapella, Amount 100 %, centre only (scratch unmask_check): average dip while singing
  200 Hz -2.0, 400 -3.4, 800 -4.6, 1.6k -5.5, 3.2k -5.6, 6.3k -3.3 dB (follows kUnmaskWeight); while silent -0.2 ..
  -0.5 dB (lets go); side (L - R) untouched (-300 dB). Works as designed; 100 % is deep, 30 - 60 % is the usual range.

## Thread handoff (2026-10-08): overnight audit COMPLETE (v0.19.5, CI green)
- Every module audited this thread: Cleanup (v0.18.2), Tone EQ (v0.19.0), Dynamic EQ (v0.19.1), De-Esser (v0.19.2),
  Rider + "Robot" rename (v0.19.3), Compressor (no change, then punch floor v0.19.4), Saturation / Doubler / Delay /
  Reverb / Output / Unmask (no change), Reference Match (v0.19.5). Pitch / Honey Tune / key: earlier threads.
- Clips for the user: tonight's START (v0.18.2) vs NOW (v0.19.4 = v0.19.5 without a reference), Auto-Edit whole chain.
- Open: user to listen and report; user's own finished vocal stems would sharpen the pro targets for trap / rap
  (MUSDB18 is mostly rock / indie). Cambridge-MT needs the user to download (Cloudflare blocks automation).

## v0.20.0 Reference library: built-in pro references + "Yours" (2026-10-08)
- User asked for a ready-made reference database (male / female, several genres) they can add to.
- Built-in (dsp/src/ReferenceLibrary.cpp, builtinReferences()): 6 profiles, medians of MUSDB18 groups (measurements
  only, no audio): Pro male singer (18 clips), Pro male rap / rhythmic (12), Pro female singer bright (26) / warm (25:
  split at the median 8 - 12.5 kHz), Pro female rap / rhythmic (35), Pro average (143). Male = f0 < 190 Hz, female
  > 230; singer = held >= 30 %. tailDb -120 (space can't be measured on 7 s clips: follows the style). MUSDB has few
  real rap tracks, so "rap / rhythmic" means rhythmic delivery; the user's own picks fill the trap gap.
- ReferenceProfile gained about / builtin. Plug-in: every vocal file loaded with REF is measured once and saved as
  JSON in Documents/Voxology/References (userReferenceFolder()); listReferences / selectReference / deleteReference
  native functions; the file picker starts in Downloads and remembers the last folder.
- UI: REF opens a menu: BUILT-IN (finished pro vocals), YOURS (with ✕ to remove), + Add a vocal file…, No reference.
  Checked in Chromium with mock.js (no errors); solid background. Plug-in compiled on Linux (VST3) before pushing.
- Test "Built-in references: six pro groups, usable by Auto-Edit". 84 cases.
- CI green: Actions run 37768426720 (v0.20.0, commit 05d5317).

## v0.20.1 Wet references kept sane + built-in "Trap rap, finished (male)" (2026-10-08)
- User link: slooply.com sample packs (royalty-free). Only page 1 of each pack's sample list is public (page 2+ = 403
  without login: respected). Public preview MP3s (cdn.slooply.com demo files). Rapper Vocals Vol. 1 (Trap Music, The
  Drum Bank) has the same 144 s song as Dry and Wet; Top Chart Vocals = dry male melodic only (raw: not a target).
  Audio kept in the scratchpad only, never committed.
- Wet trap stem vs pros: low end cut hard (-41 dB at 100 Hz), brighter (8 kHz -11.7 vs -13.5), s +1.7 dB (pros -5),
  much louder / compressed than its dry; its echoes made punch read 7.9 and tail -11 dB.
- Found: a wet reference (most online acapellas) made Auto-Edit set 60 % reverb, switch the compressor off and skip
  de-essing. Fix: refWet = tail > -18 dB or punch > 5.5 or s > -2 dB; then only tone + s (s target clamped -10 .. -2)
  are taken, space and compression follow the style (punch floor applies); reverb match capped at 40 %; a WET
  REFERENCE note explains it. Gallas / Schaf with the wet trap stem: EQ toward it, comp as the style, reverb 7 %.
- Built-in #7 "Trap rap, finished (male)": that stem's tone (f0 146), s stored -2, punch 0 (= style), no tail.
- Test "A wet reference (echoes baked in) gives its tone, not 60 % reverb and no compression". 85 cases.
- CI green: Actions run 37777844119 (v0.20.1, commit 28fb33f).

## Thread handoff (2026-10-08): "Voxology audit" thread -> "Honey Tune audit" thread
- State: v0.20.1 (CI green, run 37777844119). Voxology insert chain audited end to end this thread (see the sections
  above); reference library (7 built-ins + YOURS) shipped.
- Next: Honey Tune audit (the standalone note editor / ARA-style tuner in honeytune/). Earlier Honey Tune checks:
  v0.12.1 undo/redo, v0.13.0 move / stretch / per-note formant, v0.15.0 CREPE check, v0.16.2 whole-step tone fix,
  v0.17.0 audit (notes land within 0.2 - 0.4 c median, shape kept 4 - 7 c rms). tools/audit/honey_tune_audit.cpp exists.
- Method that worked here: an audit tool in tools/audit/ per area, real songs (test-audio/) + MUSDB18 pro stems
  (scratch only: github.com/sigsep/sigsep-mus-db releases v0.4.0 MUSDB18-7-STEMS.zip, vocals = stream 4), "do no
  harm" checks, a test that fails on the old code, before / after clips, version bump, commit + push, CI check.
- User prefs (unchanged): short replies, plain language, honest assessments, a plan with every warning; Actions link
  when green + Chrome Ctrl+J -> Keep; keep this file updated; push to claude/voxology; no PRs. Linux builds:
  --parallel 2 - 3. Repo is PUBLIC until the user makes it private (Nov 1).

## Honey Tune audit (2026-10-08, "Honey Tune audit" thread) - findings only, no code changed yet
User report: "off-key notes no longer highlight red". Tools: HoneyPreview (cmake -DVOX_HONEY_PREVIEW=ON; Linux needs
libx11 / xrandr / xinerama / xcursor / freetype / fontconfig / asound dev packages), tools/audit/honey_tune_audit.cpp,
scratch keyck.cpp (Auto key per vocal). 85 tests pass.
- RED GLOW: not a regression (logic unchanged since v0.11 Round 7): red = will SOUND off-key (NoteView.off from the target).
  Snap defaults to 100 %, so in the Tuned view every note lands on a note -> nothing is ever red (sung-off notes turn
  gold). Red only shows in Original or with Snap < ~75 %. Confirmed on Gallas: Snap 100 % = 0 red, Snap 0 % = 80 red.
- AUTO KEY falls back to Chromatic when < 45 % sure: Don (C major, 40 %, "or G") and Gallas (said D major or G
  major 40 %; real E minor = G major notes) both -> Chromatic. Then "off-key" only means "between two semitones" and
  wrong notes vs the song's key are never flagged or fixed. Schaf F# minor 87 %, Don & Lysette A major 100 % (Voxology's
  beat key said E mixolydian = same notes) are fine. Honey Tune doesn't use the host's key or Voxology's beat key.
- SOUND (snap every note, keep drift + vibrato): unchanged from v0.17 - lands within 0.2 - 0.4 c median (p90 0.9 - 2 c),
  shape kept 4 - 7 c rms median; glitch counts = the measuring tracker's own noise.
- Code read: the playback renderer outputs SILENCE if the clip's sample rate differs from the project's (comment says
  the host plays it untouched - JUCE ignores the false return; buffer is cleared). Cubase usually converts on import, so
  rare; unverified. Minor: Ctrl+wheel zoom max 1500 px/s vs + / - keys 2000.
- Proposed plan (awaiting the user's OK): 1) red glow = SUNG off-key in both views (gold body = now tuned onto the key,
  blue = plays as sung), legend "sung off-key" / "tuned to key"; 2) key: read the host's key signature (ARA musical
  context; Cubase sends it when the project has one) for Auto; when unsure, keep Chromatic snapping but say so plainly
  in red ("Key unsure: pick your song's key") with the two candidates; 3) sample-rate mismatch: play the recording
  untouched (or resample) instead of silence; 4) test for the red rule.

## v0.21.0 Honey Tune: red = sung off-key, project key for Auto, "key unsure" warning, sample-rate fix (2026-10-08)
User OK'd all four audit items.
- Red glow: honeyui::glowsRed (wasOff, offWhereItLands, original) = wasOff || (Tuned && off). Gold body = fixed (unchanged).
  Legend: in key / sung off-key (red ring) / tuned into key (gold + red ring); legend card 122 px.
- Auto key: HostKey from ARA kARAContentTypeKeySignatures (first key signature of the first musical context;
  root on the circle of fifths -> pitch class (root x 7) mod 12; intervals -> closest of our scales, never Chromatic).
  Controller refreshHostKey() on listen(), didEndEditing, doUpdateMusicalContextContent (affectHarmonies; we create
  the musical contexts and listen to them); change -> re-render all + editor refresh. resolveKey(s, guess, key, scale,
  host): Auto + host key wins (a picked Key wins over it). Status says "(from the project)". NOT verified in Cubase:
  unknown whether Cubase sends a key signature (Melodyne docs say Cubase / Studio One share chord / key info).
- keyUnsure (Auto, no host key, voice < 45 %): status line turns red: "KEY UNSURE: pick your song's key in KEY (heard
  X or Y); until then notes snap to the nearest note, any note".
- Playback renderer: reads the clip at its own sample rate (cubic) when it differs from the song's instead of silence;
  same rate = identical to before (srcStart = mod start + offset).
- Test file tests/test_honey_view.cpp (2 cases; vox_tests now includes honeytune/ for HoneyView.h). 87 cases pass.
  HoneyTune_VST3 + HoneyPreview built on Linux. Preview: Gallas Auto = red warning + red glows; E minor = 125 red-glow gold.
- User question: does Voxology re-tune after Honey Tune? Yes - no link between them; Voxology (insert) processes Honey
  Tune's output. Advice: Pitch off / Amount 0 in Voxology on a track tuned with Honey Tune.
- CI green: Actions run 37790294525 (v0.21.0, commit 5f94307).

## v0.22.0 Honey Tune takes Voxology's beat key (2026-10-08)
- User: "Voxology can determine key by beat, isn't it more accurate?" Yes (Gallas: beat E minor = right; voice D major
  40 % = wrong). Honey Tune only gets its clip, and the two plug-in files share no memory (UnmaskLink is a static inside
  each binary), so: vox::keyshare (dsp KeyShare.h / .cpp) puts "VOXOLOGY_BEAT_KEY" = "v1 key scale conf steadyMs" in the
  HOST PROCESS's environment (Windows: Set/GetEnvironmentVariableA - the static CRT gives each DLL its own getenv
  table; POSIX setenv / getenv). Voxology's 1 Hz processor timer publishes the surest ready BEAT-mode key (conf >= 0.5,
  followBeatKey(..., 0) = the beat's own mode). Honey Tune polls once a second (TimedCallback started in listen()),
  ignores it when > 5 s old. Same process only: another program never sees it.
- Order for Auto: Key you pick > project key signature (ARA) > Voxology's beat key > voice (red KEY UNSURE when < 45 %).
  honeyui::outsideKey(project, beat); HostKey.fromBeat; status "(from Voxology's beat)". A change re-tunes every clip.
- Limits: the beat key appears once Voxology (BEAT) has heard ~6 s of the song playing (after a project reload too);
  until then Honey Tune uses the voice, then re-tunes. The beat's open note (extraNotes) isn't used by Honey Tune.
  Two projects open in one Cubase could share a beat key (status line shows where it came from).
- Test "Voxology's beat key reaches Honey Tune (fresh only); the project's key comes first". 88 cases.
  Voxology_VST3 + HoneyTune_VST3 compiled on Linux (needed libgtk-3-dev / libwebkit2gtk-4.1-dev here).
- CI green: Actions run 37794074158 (v0.22.0, commit afd843d).

## v0.22.1 Red = will PLAY off-key only (2026-10-08)
- User (Cubase screenshot, v0.22.0: "key D Major (from Voxology's beat)" works): "Notes should only glow red if they are
  off key. If I move a note to an off-key note it should turn red, but no red glow when in key." So the red rule goes
  back to v0.11's meaning: glowsRed = Original ? wasOff : off (where it lands). Snap's fixes = gold, no glow. Legend
  "in key / off-key / tuned into key" (red ring only on off-key). The original "no red" report was this working as
  designed with Snap 100 %. Test updated (Snap 100 % = no red; Snap 0 % = sung-off red; dragged off-key = red). 88 cases.

## Thread handoff (2026-10-08): "Honey Tune audit" thread -> "Voxology UI easter egg" thread
- State: v0.22.1 pushed (commit 243a1b3; CI check pending when this was written - the old thread has a reminder
  for it). Honey Tune audit done this thread: v0.21.0 (project key via ARA, KEY UNSURE warning, sample-rate fix),
  v0.22.0 (Voxology's beat key shared to Honey Tune via the host process environment, vox::keyshare), v0.22.1 (red =
  will play off-key only). Not verified in Cubase yet: project key signature from Cubase; user confirmed the beat key works.
- Advice given: on a vocal tuned in Honey Tune, switch Voxology's Pitch off (Amount 0) - Voxology re-tunes its input.
  Offered later: Voxology could notice an already-tuned vocal and leave pitch alone.
- Next thread: "Voxology UI easter egg" - the user hasn't described it yet: ask what they want before changing code.
  UI lives in plugin/ui (index.html, app.js; WebView editor), mock.js for checking in Chromium.
- Linux build here needed: libx11 / xrandr / xinerama / xcursor / freetype / fontconfig / asound / gtk-3 /
  webkit2gtk-4.1 dev packages; build with --parallel 3.

## v0.23.0 Easter egg: click the logo, blue turns red (2026-10-08, "Voxology UI easter egg" thread)
- User: "click the logo: all the blue parts turn red (background, buttons, glows...), other colours stay; click again =
  blue." Session only (opening the plug-in again starts blue); not saved in the project.
- CSS: every blue now lives in :root vars (--blue*, --glow*, --midi-rgb, --knob-arc / --knob-track, --art-* image urls);
  :root.red overrides them (same lightness, hue -> red; --blue-deep red is darker, #7d1520, so button text reads).
- Art: tools/ui/make_red_assets.py makes *_red.webp twins (honeycomb, logo, title, marble_blue / blue2 / cream, 5 knobs):
  only blue-ish pixels change (hue 75 - 280, any colour at all), gold / ivory / white untouched. Re-run it if the art changes.
- app.js: logo click toggles html.red, swaps <img> art (logo, title, knobs) via art(), re-reads PAL (spectrum canvas
  colours) and redraws. Hive tiles use var(--art-marble-*).
- Honest limits: in red mode the calm (blue) Learn tips turn red too, so they look closer to the warnings; the red
  "REC" text sits on a red button. Checked in Chromium (mock.js + JUCE's index.js served over http): no errors,
  blue -> red -> blue. Plug-in not compiled locally (UI-only; assets come in through the existing glob): CI builds it.
