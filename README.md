# Voxology

An all-in-one vocal chain plug-in (VST3 / AU / Standalone) for trap and hip-hop vocals, with
**Auto-Edit**: it listens to your vocal, sets every module for the style you pick, and explains
each decision (plus a plan for every warning) in the Learn panel.

## Chain
01 Cleanup (low cut + gate) -> 02 Tone EQ (Body / Mud / Nasal / Presence / Air) -> 03 De-Esser ->
04 Rider (auto level) -> 05 Compressor (peak catcher + opto-style leveler, makeup, mix) ->
06 Saturation (Tape / Tube / Clip, 4x oversampled) -> 07 Doubler -> 08 Delay (tempo-synced, ducked,
ping-pong) -> 09 Reverb (vocal plate, ducked) -> 10 Output.
A / B (original vs Voxology, latency-aligned) with MATCH (level-matched comparison).

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
- `tests/` Catch2 tests; `tools/report.cpp` prints Auto-Edit's report for a test vocal or a raw file.
