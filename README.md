# Voxology

An all-in-one vocal chain plug-in (VST3 / AU / Standalone) for trap and hip-hop vocals, with
**Auto-Edit**: it listens to your vocal, sets every module for the style you pick, and explains
each decision (plus a plan for every warning) in the Learn panel. **Auto Analyze** keeps listening after
that and offers new settings (APPLY / UNDO) when a part of the song needs something new.

## Chain
01 Pitch (Natural / Classic / Robot) -> 02 Cleanup (noise, room, low cut, pops, clicks, breaths, gate) ->
03 Tone EQ (5 bands) -> 04 Dynamic EQ (+ Smooth) -> 05 De-Esser -> 06 Rider (auto level) ->
07 Compressor (peak catcher + leveler, makeup, mix) -> 08 Saturation (Tape / Tube / Clip, oversampled) ->
09 Voices (double, unison, harmonies, MIDI chords) -> 10 Dimension (chorus / flanger / phaser) ->
11 Delay (tempo-synced, ducked, ping-pong, throws) -> 12 Reverb (vocal plate, ducked, throws) -> 13 Output.
A / B (original vs Voxology, latency-aligned) with MATCH (level-matched comparison). BEAT mode on the instrumental
finds its key (shared with Honey Tune and every Voxology in the song) and makes room for the vocal (Unmask).

**Honey Tune** (same download): an ARA note editor on the vocal clip (pitch, timing, length, formant per note).

Full manual: `docs/MANUAL.md` (also inside both plug-ins, the ? button). Install by hand: `docs/INSTALL.txt`, or use
the Sonicology installer (built in the Mixology repo).

## Build
```
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
ctest --test-dir build -C Release
```
Engine tests only (fast, no JUCE): `cmake -S . -B build-tests -DVOX_BUILD_PLUGIN=OFF && cmake --build build-tests && ./build-tests/tests/vox_tests`

UI preview in a browser (no DAW): copy JUCE's `modules/juce_gui_extra/native/javascript/*.js` into
`plugin/ui/juce/`, then serve `plugin/ui` (e.g. `python3 -m http.server`) and open index.html;
`mock.js` simulates the plug-in.

## Layout
- `dsp/` framework-free engine (double precision): `Modules` (inserts), `Space` (doubler / delay /
  reverb), `VocalChain`, `AutoEdit` (analysis + decisions + report). Shared with the tests.
- `plugin/` JUCE wrapper: `Params.h` (all parameters), processor, `AutoEditController`, web editor.
- `plugin/ui/` HTML / CSS / JS interface (embedded in the binary).
- `dsp/include/suite/SuiteLink.h` the suite link: Voxology, Mixology and Masterology tell each other what they do (the
  same file in all three repos; Voxology publishes each vocal it sits on), and since v0.49 the song's style, shared by
  all three (`plugin/ui/mixstyles.js` is the same list in each; `dsp/include/vox/SongStyle.h` turns it into a
  suggested vocal STYLE; since v0.49.2 it's picked in Mixology or Masterology, Voxology only follows it). Since
  v0.52 (link layout 4) Mixology answers each vocal (MIXOLOGY in the bottom bar says whether it found this track),
  and the beat's key reaches Honey Tune and every Voxology through it. Since v0.53, when Mixology answers that it sets a
  vocal's level (it rides the fader, or Analyze Mix heard the vocal), FIT LEVEL and Auto Analyze leave that level to
  it (`vox::findNewNeeds (..., levelByMixology)`), so the two never answer each other's moves.
- `dsp/include/suite/RackLink.h` + `plugin/ui/rack.js` / `rack.css` / `rack/` the Sonicology Rack (since v0.55): every
  Sonicology plug-in in the project in one view (the RACK pill beside MY CHAINS). The same files in all three repos;
  `dsp/include/vox/RackUnit.h` is what Voxology publishes (its 13 modules, the Beat Listener), Honey Tune publishes
  each track it tunes (`honeytune/HoneyView.h`: `rackModule`).
- Since v0.56 (Rack stage 2) the Rack changes things too: Voxology serves the Rack's requests (`applyRack`, through the
  shared `plugin/RackHost.h`: a knob drag is one host gesture), locked while Auto-Edit listens or works; every knob
  names its parameter (`vox::rackunit::fromKnob` / `switchOf`). Honey Tune takes Snap / Drift / Vibrato
  (`honeyui::rackSet`), applied once where a drag stops.
- Since v0.66 (Beta 28): shared RackLink.cpp (viewJson keeps a report about the shown track: Mixology's Coach) and
  rack.js (the COACH page on Mixology's tracks). No Voxology code changed.
- Since v0.67 (Beta 29): shared RackLink (the Coach's steps pass a unit's lock) and rack.js (the COACH page on the
  master, through Masterology; STOP while the Coach listens). Voxology: the Coach's steps refuse while Auto-Edit runs
  from Voxology's own window, and "cancel" stops the Coach's own listen.
- Since v0.68 (Beta 30): the Coach after APPLY. Auto-Edit decides from the take as it comes in, so the same take gave
  the same plan again; the Coach now renders the take through the settings Voxology has now (what stands out, "before")
  and compares the plan with them (vox::coach::Now: "same" per module, "changes", "upToDate"). RACK is a word (no
  emblem).
- Since v0.65 (Beta 27): the analysis rests on purpose while the song plays (`vox/Pace.h`: a thread's `Scope` turns it
  on; `tick()` in the long loops sleeps 8 ms after every 20 ms of work; off by default, so tests and results are the
  same). Every AutoEditController job (Auto-Edit, the Coach, Auto Analyze, a reference) runs paced: the user heard the
  song cut out a couple of times while the Coach analysed. `tests/test_realtime.cpp`: the chain never allocates while
  it plays (a replaced operator new counts), and a paced Auto-Edit decides exactly the same.
- Since v0.64 (Beta 26): the Rack's COACH page for a vocal (stage 3). `vox/CoachPlan.h`: what stands out in the take
  against Auto-Edit's aim (`toneTarget` / `sibilanceTargetDb` in `vox/AutoEdit.h`), the take played through the plan
  (`VocalChain::render`) and measured again, and the plan as JSON for the Rack (`coach::planJson`, at most 7.6 KB).
  `AutoEditController::startPlan` / `applyPlan` / `discardPlan`: Auto-Edit listens and decides as usual but keeps the
  plan; APPLY sets it (one UNDO step). `applyRack` serves `rack::Change::coach` (plan / apply / undo); `coachReport`
  publishes it in `Unit::report` (RackLink layout 4). The Rack's STYLE / INTENSITY turn `aeStyle` / `aeIntensity`.
  Tests in `tests/test_coach.cpp`.
- Since v0.63 (Beta 25): the Output's PEAKS (`vox/OutputPeaks.h`: Off, Clip (SoftClipper's curve, knee 3 dB under the
  Ceiling), Limit (instant on, 80 ms back, stereo-linked); no lookahead, no latency) after the Output level on the
  processed side; params `outPeaks` / `outCeil` (Auto-Edit leaves them). Tests in `tests/test_output_peaks.cpp`. Shared
  rack.js: the insert-order check.
- Since v0.62 (Beta 24): the Rack's SAVE on Voxology's row (`rack::Change::save` in `applyRack`): saves this setup in My
  Chains, named after the track and the time ("Lead Vox 17 Nov 22.41"). Shared RackLink / rack.js: the track's fader in the
  Rack (from Mixology), the Link's RACK button.
- Since v0.61 (Beta 23): the shared Rack page has a SESSION page (every track and its modules), gain staging in the
  Rack Coach (each plug-in's peaks in and out) and TURN ON HERE on the MODULES page. No Voxology code changed.
- Since v0.60 (Beta 21): the Reverb's Type (`ReverbParams::type`: Plate, Room, Hall, Chamber: line sizes, diffusion,
  early reflections; a type change fades the tail out first) and the Delay's Character (`DelayParams::character`:
  Clean, Tape with wow / flutter and warm saturation in the loop, Analog darker and softer). Auto-Edit picks both per
  style. Tests in `tests/test_character.cpp`.
- Since v0.59 (Beta 20): the Rack's BYPASS reaches Voxology's own bypass (`rack::Change::power` in `applyRack`); Honey
  Tune refuses it (no bypass of its own). UNDO / REDO and A / B are the Rack page's own (rack.js).
- Since v0.58 (Beta 19, the sound gaps from Mixology's docs/rack/AUDIT-2026-11-17.md): LISTEN (`vox/Listen.h`:
  hear only what Cleanup, the Dynamic EQ or the De-Esser takes out; the Denoiser forms its own take-out from the same
  spectra, the later stages' is their input minus output; the same ListenMix design as Masterology's; monitoring only,
  `setListen` native, never offline), the Compressor's Style (`CompStyle`: Clean, FET, Opto; Auto-Edit picks it per
  style) and Saturation's Air (even harmonics of the vocal's top, kept above 8 kHz, in the 4x oversampled path).
  Tests: `tests/test_listen.cpp`, `tests/test_character.cpp`.
- Since v0.57 (Beta 18): details only for the track a Rack shows (`rack::Link::shows`, a 25 Hz timer while shown),
  reset to default (`Change::reset`; Honey Tune `honeyui::rackReset`), the window grows while the Rack is open
  (`rack::host::RackWindow`), and report natives return objects (`rack::host::jsonObject`, `jsonText` in app.js): JUCE
  escapes long text from a native function in time that grows with its square.
- `tests/` Catch2 tests; `tools/report.cpp` prints Auto-Edit's report for a test vocal or a raw file.

## Third-party
- CREPE "tiny" pitch model weights (`dsp/src/CrepeWeights.cpp`, from github.com/marl/crepe), MIT License,
  (c) 2018 Jong Wook Kim: see `dsp/third_party/CREPE-LICENSE.txt`. Used by Honey Tune's AI pitch check.
- JUCE (AGPLv3 / commercial), ARA SDK (Apache 2.0), Catch2 (BSL-1.0): fetched at build time.
