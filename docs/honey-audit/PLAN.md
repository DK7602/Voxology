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

## Next (in order; proposed to the user on 2026-11-22, no objection yet)
1. Beta 39: the sound first (AUDIT-ENGINE.md, validated fixes in its scratch copy): a moved note's whole attack and
   release move with it (no scoop in / slide back: hold the note's move between notes, extend voiced runs to the
   audible onset / release), both channels through one shifter (stereo image), legato boundaries (no split notes),
   moved notes' loudness kept; then multi-select (rubber band, Ctrl / Shift click, Ctrl+A, click a key = that pitch) with batch edits as one
   undo step; split / merge notes (saved, undoable); hear a note while stopped (ARA editor renderer); the engine
   audit's sound fixes.
2. Beta 40: note level and mute (+ blob height), "notes to check" finder (amber dot, N next, D fix), time edits snap
   to the beat (Alt free), key extras (click a key to add / remove a scale note; the beat's A4 tuning from Voxology).
3. Later: several clips at once and copies tuned independently (ARA audio modifications), MIDI export, tighten timing.
