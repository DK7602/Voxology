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
  and the beat's key reaches Honey Tune and every Voxology through it.
- `tests/` Catch2 tests; `tools/report.cpp` prints Auto-Edit's report for a test vocal or a raw file.

## Third-party
- CREPE "tiny" pitch model weights (`dsp/src/CrepeWeights.cpp`, from github.com/marl/crepe), MIT License,
  (c) 2018 Jong Wook Kim: see `dsp/third_party/CREPE-LICENSE.txt`. Used by Honey Tune's AI pitch check.
- JUCE (AGPLv3 / commercial), ARA SDK (Apache 2.0), Catch2 (BSL-1.0): fetched at build time.
