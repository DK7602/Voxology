# Honey Tune audit 3 (2026-11-22): the plan and where it stands

The user (2026-11-22): "thoroughly audit honey tune. lets make sure its as good as the best or better. i dont want
melodyne touching it!" Plus look changes: no drips on the title, cream marble instead of the blue / white, notes drawn
as waveforms like a normal vocal editor (24k gold and light blue, red glow off-key), more defined waveforms, quieter
rows outside the key, the pitch line black and running on between notes (instead of the connectors), the playhead
staying in view when the music stops.

Three independent reviews (subagents, git b833cc0):
- AUDIT-CODE.md: the bug hunt (14 findings + small ones, two reproduced high-severity audio bugs).
- AUDIT-FEATURES.md: features and workflow vs Melodyne 5, VariAudio 3, Auto-Tune Pro 11, Waves Tune, RePitch.
- AUDIT-ENGINE.md: sound quality and analysis accuracy, measured (tools in its last section; scratch work dir, not kept).

## Beta 38 (Voxology 0.74.0)

Look (a1a1e34): waveform notes (Track::level / power, dB), one black pitch line through the notes and the slides between
them, cream marble, sand-gold lanes, no drips, top bar 60 px, playhead held where it stopped.

Bugs fixed (AUDIT-CODE.md numbers):
- 1 partial re-render after a time move: changedRegion covers before + after positions; any overlap -> full render.
- 2 notes pushed into neighbours / past the clip: the roll stops a drag at its neighbours and the clip's ends;
  applyEdits keeps timed notes between neighbours and inside the clip; timeMap never builds falling knots.
- 3 silence until analysed: the take plays as recorded from the moment it's read (listen()); a render of older
  samples stops playing.
- 4 Original view: notes hit-tested where drawn; a drag switches to Tuned and edits the tuned note.
- 5 one per-note slider no longer pins the others (each writes only its own value).
- 6 source properties (rate / length / channels) re-read; new samples with access on read at didEndEditing.
- 7 renderedTrack is a shared_ptr (and the render's length is checked).
- 9 archive HNY4: note starts in seconds (HNY2 / HNY3 read as samples, converted).
- 10 bars, seek and playhead use the event you selected (else the first).
- 11 another clip: the view fits it afresh.
- 12 AI check: half the cores, a thread that can't start leaves its share to this one; jobs catch everything.
- 14 an edit carries the analysis it was made on (a newer one drops it); notes changing tells the editor at once.
- small: no undo step when nothing changes; arrows limited to the roll's range; zoom / scroll limits never invert;
  an older analysis gives way to newer samples; Original says so in red (an export gets the untuned take).
- Not done: 8 (edits per audio modification: with multi-clip editing, later), 13 (realtime try-lock: rare).
- AUDIT-ENGINE.md #0 (critical, every earlier version): one NaN reading (a voice below ~75 Hz: the YIN parabolic step
  wasn't clamped, a negative period) made findNotes loop forever, and Honey Tune's single job thread with it. Fixed:
  the step stays within half a lag (PitchCorrector.cpp, also Voxology's live Pitch), analyse() never keeps a non-finite
  reading, findNotes always moves on. Test.

Quick wins built (AUDIT-FEATURES.md items):
- D1 SNAP TO NOTE pulls notes that are further off harder (honeyui::snapPull: 1 - (1 - snap)^(|d| / 0.25)).
- D3 starting points under the SNAP TO NOTE label: Natural / Polished / Tight / Hard.
- D4 drags and arrows step through the key's notes (Shift: semitones, Alt: free / 5 cents).
- E1 steadying spares each note's scoop and fall (steadyEdgeSeconds: a fifth of the note, 20 - 60 ms, eased).
- L1 undo covers KEY / SCALE / SNAP / DRIFT / VIBRATO (undo steps hold the settings).
- I4 / L3 / L4 right-click menu per note (snap, steady, loop + play, play from here, reset pitch / timing / tone /
  steadying / all); right-click never drags. Keys S, D, L, Enter, F, B, Esc.
- C2 drive the host: Enter plays / stops, double-click the ruler plays from there, L loops the note (ARA host
  playback controller: start / stop / cycle range).
- C3 following pauses when you scroll or zoom while it plays (until the next play, a ruler click or F).
- I1 vertical zoom (Alt + wheel, Alt + / -); I2 Fit = the whole clip, again = the close-up view.

## Beta 39 (Voxology 0.75.0): the sound (AUDIT-ENGINE.md), then several notes at once

Measured with the engine review's own tools (scratch: /tmp/claude-0/audit-eng-work, `mine/` builds; synthetic
110 / 220 / 440 Hz voices, 12 notes per row; real: the owner's Gallas acapella and a pro dry lead):
- #1 / #6 A moved note's attack and release move with it. analyse() fills each sung run back to the voice's onset (50 ms)
  and on into its release (40 ms) where it's voice (level within 30 dB; repeats at about the note's period, or few zero
  crossings), marked Track::filled; findNotes decides on the detected readings and then gives each note its filled edges.
  planShift: exactly the note's move inside a note, an S-curve across a gap, the singer's own glide between legato notes
  (no global smoothing any more). Synthetic: first 50 ms 61 - 148 c -> 1.4 - 9 c; settled 60 - 122 ms -> 5 - 13 ms; last
  40 ms 2 - 33 c -> 0.7 - 1.7 c; note starts +20 - 35 ms late -> +5 ms. Real (every 4th note moved): Gallas first 40 ms
  52 / 147 / 150 c (+2 / +5 / -5) -> 32 / 30 / 36 c; pro lead 61 / 156 / 159 -> 29 / 69 / 70 c (much of what's left is
  the glide-shaped legato change, by design).
- #2 Loudness: each changed note's energy matched to how it was sung (one gain per note, +-6 dB, eased over 12 ms):
  +5 st -5.2 dB -> 0.0 dB (synthetic), real -1 .. -4.7 dB -> 0.0 dB.
- #3 Stereo: every channel through one shifter, time edits with the same grains for every channel (render / renderPart /
  warp take Channels; the plug-in renders that way). Test: side / mid kept after a +3 move.
- #4 Segmentation: the new note's level from what's sung just after a change (not the 180 ms average still moving),
  the boundary where the sung pitch crosses over; scraps under 100 ms sung on at the same pitch join their note. Legato
  12-note lines: 16 - 21 notes found -> 11 - 13, boundaries 25 - 30 ms off -> 4 - 5 ms; real tail fragments 20 / 36 % ->
  4 %. Rap syllables as before (33 - 35 of 40 one-to-one), melisma 18 -> 16 of 19 (the one trade-off).
- #11 Following a guide, the shifter's offset goes back to 0 in silence: untouched notes after it come out as recorded.
- Tests: attack / release / loudness, stereo image, legato line, untouched notes exact, NaN hang. All Honey Tune tests pass.
- Several notes at once: Ctrl / Shift click, rubber band, Ctrl+A, a key on the keyboard = that pitch, Esc; drags (pitch:
  the same step; time: as far as the nearest unselected note lets them), keys, the menu and the bottom bar act on all,
  one undo step (Model::setEdits, HoneyDocumentController::setEdits).

## Next (in order; proposed to the user on 2026-11-22, no objection yet)
1. Beta 40: split / merge notes (saved, undoable); hear a note while stopped (ARA editor renderer); a consonant moves
   with its note in time (AUDIT-ENGINE.md #5); high / breathy voices' octave errors (#13).
2. Beta 41: note level and mute (+ blob height), "notes to check" finder (amber dot, N next, D fix), time edits snap
   to the beat (Alt free), key extras (click a key to add / remove a scale note; the beat's A4 tuning from Voxology).
3. Later: several clips at once and copies tuned independently (ARA audio modifications), MIDI export, tighten timing.
