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

## Beta 40 (Voxology 0.76.0; the user: "go ahead with Beta 40")

- Split / join notes (AUDIT-FEATURES.md: Melodyne's note separation): vox::honey::Layout (splits / joins in seconds,
  outliving a re-analysis), reshape() on findNotes()'s notes (each part's pitch from its own singing), addSplit /
  addJoin (each undoes the other where they meet), carryEdits (a part keeps the move as a step from its own pitch and
  its timing), changedRegion for a different note count (only around them), applyEdits lets notes sung touching stay
  touching. Plug-in: layout per clip, in undo steps and archive HNY5; Alt + double-click splits (a line shows where
  while Alt is held), J joins (the selected neighbours, or with the next), both in the right-click menu.
- Hear a note while stopped: HoneyEditorRenderer (ARA editor renderer) plays a stretch of the clip on its own track,
  the host's playhead left alone; the document controller picks the renderer serving the event shown; after an edit it
  waits for the render (SourceState done >= wanted). Click a note = hear it; drags, arrows, Snap / Steady / resets =
  hear the result; Esc stops; the right-click menu turns it off. Not testable here (no ARA host): check in Cubase.
- AUDIT-ENGINE.md #5 consonants: timeMap (with the track) maps the sound next to each moving note 1:1 (its consonant
  before / ending after: within 32 dB of the singing, up to 150 ms, closures inside), only silence / breath between
  stretches; meeting in a gap: split at its quietest 10 ms; no room: only the 20 ms next to each note stays whole. warp:
  no drift carried out of unvoiced grains (a squeeze over a breath made the next note's attack late). Measured: +80 ms
  s 124 -> 82 ms (as sung), t one 4.5 ms burst (was 2 peaks, 9 ms); +150 ms B on time (was 40 ms late), s 159 -> 82,
  the next note identical (was -30 dB off); -60 ms t whole, s 42 ms (was 50: no silence to squeeze there).
- AUDIT-ENGINE.md #13 octave errors (high / breathy / raspy voices; details in its "Fixed in Beta 40"): analyse() checks
  each reading at the full rate (checkPeriods: P / 2 ... P / 16 up to 1600 Hz, then the loudness-weighted choice within
  130 ms; readings stamped in silence dropped), holdVoicing (breathy gaps up to 80 ms), findNotes' average not across a
  3-semitone jump, aiCheck's stretch vote with the waveform's last word. 784 / 988 / 1175 Hz: 30 / 32 / 100 % wrong
  octave -> 0, one note each; breathy HNR 14 dB 24 - 54 % -> 0, 6 notes for 6. Not fixed: rasp at 50 %. Side effect:
  notes now keep the attack that used to be a separate fragment, so a moved note's first 40 ms measures 7 c further off.

## Beta 41 (Voxology 0.77.0; the user: "go ahead with Beta 41")

- F1 note level and mute: NoteEdit.gainDb (-30 .. +12 dB) / muted -> Note.gainDb / muted; render / renderPart apply them
  after matchLoudness (applyLevels: one gain per note, a 10 ms raised-cosine ease around it, centred on the gap's middle
  when a neighbour is nearer; 0 dB notes untouched sample for sample); sameNote compares them (a level edit re-renders
  only around the note). UI: Ctrl + drag up / down (0.2 dB / px, Shift 0.05; a Ctrl-click still toggles the
  selection), M mutes / unmutes, the menu's Louder / Quieter (3 dB), Mute, Reset level; the blob is drawn as loud as it
  will be (up to 1.25 x full height), a muted one as a dashed outline. Archive HNY6.
- M1 / M2 notes to check (findChecks, HoneyView.h): split (a < 80 ms piece touching a neighbour at its pitch, or two
  notes within 35 c under 15 ms apart: D joins), outOfKey (sung within 30 c of a note outside the key that Snap moves a
  semitone or more: D keeps it), between (40 - 60 c from the key's note, the other one not much further: D takes the
  other), wobbly (0.3 s +, its slow wander alone 15 c + as it will sound: D sets drift 0, vibrato kept). Only notes you
  haven't changed; NoteEdit.checked ("It's fine") stops it (saved, isBlank vs isDefault). Amber dot on the blob, the
  status line counts them, N selects the next (scrolls to it, plays it while stopped), D fixes (any other note: steady
  as before), the note line says what and what D does.
- G1 beat grid: time drags put the note's start (or the dragged end) on the nearest beat (halves / quarters at >= 48 /
  96 px a beat: Timeline::nearestLine), a line shows where; Alt = free.
- H1 key notes: Settings.added / removed (pitch classes), KeyGrid (key, scale, added, removed, tune) replaces keyNote
  in Snap, red / gold, drags, arrows and the rows; Alt + click a key toggles it, right-click a key for the menu (and
  Back to the key's own notes); another KEY / SCALE clears them.
- H2 tuning: Settings.a4 (0 = Auto); Auto = the beat's tuning from the Voxology on this track (honeylink layout 2:
  VoxInfo.tuneCents, published from meters.bkTune when its key follows the beat; Honey Tune ignores < 2 c changes).
  KeyGrid.tune moves every key note. Right-click a key > Tuning (Auto, 432 - 446 Hz). The status line shows it.
- Attack of moved notes (Beta 40's side effect): see AUDIT-ENGINE.md.
- Tests: tests/test_honey_edit.cpp ([level], [keygrid], [checks], [beat], [link]).

## Next (in order)
1. Later: several clips at once and copies tuned independently (ARA audio modifications), MIDI export, tighten timing
   (G2), even out levels (F2), vertical zoom / fit fixes (I1, I2).
