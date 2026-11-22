# Honey Tune: features and workflow vs the best vocal editors

Audit of the read-only copy `/tmp/claude-0/audit-src` (git HEAD b833cc0). Findings only: no repository file was changed.
Code references are `file:line` in that copy. Competitor facts are cited in the Sources section; [M5] means the
Melodyne 5 assistant manual (Celemony Help Center PDF, updated 10/15/2026), read in full for this audit.

Legend. **Impact** = how much it matters for a novice who wants a pro-sounding vocal fast: High / Medium / Low.
**Size** = rough build effort: S (about a day), M (2 to 5 days), L (1 to 3 weeks), counting tests and the manual.

Out of scope because it is already being built: notes drawn as amplitude blobs (gold rim, light blue fill, gold when
tuned into key, red glow when off-key), no drips on the title, the cream marble background, and the playhead staying
visible after playback stops. Where a gap depends on these, it says so.

---

## 1. Verdict

Honey Tune's engine and its "brains" are already competitive. It has automatic key from the beat or project, an AI octave
check, live non-destructive Snap / Keep Drift / Keep Vibrato, per-note pitch, time, length and formant, a one-click A/B,
fast partial re-renders, and exports that wait for the render. A few of these beat Melodyne outright.

What separates it from Melodyne, VariAudio and Auto-Tune Graph mode is the **editing workflow**, not the DSP:

1. You can select only **one note at a time**. There is no rubber band, Shift/Ctrl-click or Select All, and every edit is per note.
2. There is no **split or merge**. A note the analysis cut wrong cannot be fixed, and the manual admits "a very wobbly note
   can show as two cells".
3. You **can't hear a note while editing**. There is no audition when you click, drag or arrow to a note: you have to run the DAW.
4. There is **no context menu**, few shortcuts, no vertical zoom, and "Fit" does not fit the whole clip.
5. There is **no note volume or mute**, and no level evening.
6. There is **no control over transitions and scoops**. Steadying a note also flattens its natural scoop.
7. There is **no time grid or quantize**.
8. The editor shows **one recording at a time**. That recording is the first selected event's source, shown in full even
   when the event is trimmed. Edits belong to the recording, so copied events (doubles, harmonies) can't be tuned differently.

Fix items 1 to 6, add a novice-first "problem finder", and use the ARA transport powers Melodyne doesn't use. A fair
reviewer would then call Honey Tune at least Melodyne-Assistant-level for vocal tuning and easier for novices.

---

## 2. Checklist: what the best monophonic vocal editors offer

### 2.1 Celemony Melodyne 5 (Essential / Assistant / Editor / Studio)

Editions ([Celemony editions page]):

- **All editions, Essential included:**
  - Main Tool: pitch, position, duration and note separations.
  - Macros for pitch, timing and leveling.
  - Edit note assignments.
  - Chord Track and chord recognition.
  - ARA / VST3 / AU / AAX.
- **Assistant and up:**
  - All tools: pitch (with modulation and drift), formants, volume, timing, sibilants.
  - Inspectors.
  - Audio-to-MIDI.
- **Editor and up:** DNA polyphonic editing, extended scale and tempo functions.
- **Studio only:** multitrack windows, multitrack note editing, track-spanning macros, quantize to a reference track.

**Main Tool** ([Main Tool page], [M5] "Main Tool"):

- Drag a note vertically for pitch or horizontally for time. The first movement decides which, and you release the mouse
  to switch axis.
- Pitch moves in semitone steps; Alt gives free (cent) moves and ignores the grids.
- Double-click snaps the note to the nearest allowed pitch with 0 cents offset. Alt+double-click moves it to the nearest
  chord tone and keeps its offset.
- Drag a note's front or back edge to stretch or compress it. A neighbour with a soft separation stretches along, so no
  gap or overlap appears.
- Hover over the upper part of a note to get the separation tool: double-click there to split, double-click a separation
  to remove it, drag a separation sideways to move it. Separation edits apply to every selected note.
- While you drag pitch, Melodyne plays the "frozen" sound at the point you clicked; moving sideways scrubs through the
  note ("Monitor When Editing Blobs", on by default).

**Pitch Tool and its sub-tools** ([M5] "Pitch Tool", "Pitch modulation and drift", [Modulation/Drift page]):

- **Pitch center:** typed values in the Inspector, either absolute (C3) or relative (+2), plus cents and Hz.
- **Pitch transitions:** drag the back part of a note to make the slide into the next note steeper or gentler (soft
  separations only).
- **Pitch Modulation and Pitch Drift tools:** drag down to reduce to 0, further to invert, and up to exaggerate. Values run
  from -100 % (inverted) through 0 % (flat) to 100 % (original) and beyond. Double-click toggles between the original and
  none. Both work on multiple selections.
- **Edit > Add Random Deviations** (slight / moderate / drastic) for pitch and time: makes doubles sound like two singers.
- **Edit > Reset Individual Edits / Restore Original**, per aspect: pitch, pitch center, modulation, drift, transitions,
  formant, amplitude, time handles, and so on.

**Formant Tool:** shifts formants per note, with formant transitions between connected notes.

**Amplitude Tool:**

- Drag for level, or type it in the Inspector (Alt gives finer steps).
- Amplitude transitions between connected notes.
- Double-click mutes a note (or "Note Off" in the Inspector); muted notes still show as outlines.

**Fade Tool:** fade-in and fade-out per note; double-click to add, arrows to move and shape.

**Sibilant Balance Tool:** sets the level of sibilants and breaths against the pitched part. Double-click means -100 %
(sibilants muted). Sibilants are shown hatched and are never pitch-shifted by the macros. In Note Assignment Mode the
Sibilant Range tool adjusts where they start and end.

**Timing Tool:**

- Move and stretch notes.
- Double-click quantizes the note to its "intended" beat (grey frames, "Show Intended Notes").
- **Time Handles** speed up or slow down phases inside a note.
- **Attack Speed** makes the attack harder or softer without moving the musical start.

**Note separation** ([M5] "Note separation tools"):

- Soft separations keep the notes connected (they share transitions); hard separations disconnect them. The Separation
  Type Tool switches between the two.
- Context menu: "Separate Note", "Separate Notes as Trills" (splits a vibrato into its hills and valleys), "Merge notes".

**Macros**, which act on the selection or on all notes ([M5] "Correct Pitch Macro", "Quantize Time Macro",
"The Leveling Macro"):

- **Correct Pitch:** a pitch-center slider from 0 to 100 % and a drift slider.
  - The center slider is *musically non-linear*: low settings fix only badly-off notes, higher settings reach the
    nearly-right ones too.
  - Optionally snaps to the scale or chord ("Snap to Chord Scale").
  - Notes you hand-tuned are left alone unless "Include notes fine-tuned manually" is ticked.
  - Remembers its values per note, previews live during playback, and ends with OK or Cancel.
- **Quantize Time:** "Auto" moves notes to their intended beats; otherwise to a 1/4, 1/8 ... or triplet grid; Intensity
  sets how far. It moves the rhythmic emphasis points, not every onset, and also has an "include hand-edited" option.
- **Leveling:** two converging sliders, one lifting quiet notes and one lowering loud notes, toward the mean amplitude.
  Short, quiet "noise" blobs are ignored.

**Grids, scales and tuning** ([M5] "The Pitch Grid", "The Time Grid"):

- **Pitch Grid:** No Snap, Chromatic Snap, or Key / Chord Snap.
- **Backgrounds:** Keyboard, Key, Chord, Chord Scale, or Pitch Lines (lines for exact intonation).
- **Pitch labels:** note names or scale degrees.
- **Scale Ruler:** pick the tonic and the mode, "Analyzed" (closest major/minor, or an exact microtonal scale), "Notes
  Reflect Scale Changes", and Play Scale.
- **Scale Window:** a library of scales and Scala .scl files.
- **Reference Pitch Ruler (master tuning):** Concert (A4 = 440), Default, **Detected** (the recording's own tuning), or
  a typed A4 in Hz.
- **Key Track:** key changes over time.
- **Chord Track:** can be taken from the DAW over ARA ([M5], [Melodyne in Cubase]).
- **Time Grid:** note values, triplets or seconds. Moved notes keep their offset from the beat; Alt places them freely.

**Selection** ([M5] "Selecting notes"):

- Click; Cmd/Ctrl-click to add or remove; rubber band (Cmd adds); Shift-click for a range.
- **Snake selection:** Shift and paint over notes.
- **Pitch Ruler:** click a note name to select every note of that pitch, double-click for all octaves, drag for a range.
- **Select All** and the **Select Special** menu: Restore Last Selection, Invert, all following notes, same pitch, same
  pitch in all octaves, fifths above and below, Same Beat in All Bars, Notes Between Locators. These respect the cycle.

**Copy / paste** notes ([M5] "Copying notes"), with grid-aware placement.

**Playback, navigation and zoom** ([M5] "Playback, navigation, zooming", "Cycle mode", "Display and other options"):

- In the ARA plug-in, Melodyne follows the DAW. *"It is not possible to start, stop or reposition the DAW's playback
  cursor from within Melodyne."*
- When the DAW is stopped, Melodyne plays on its own:
  - Double-click the ruler to play from there (Alt plays only the selection).
  - Drag in the ruler to scrub; drag up or down in the ruler to zoom.
  - **Alt+Space plays the selected notes in ARA mode** (5.2 release notes).
  - The arrow keys step through the notes, and **the selected note sounds when playback is stopped**.
- **Auto-scroll pauses** while notes are selected or after you scroll the playhead out of view. It resumes when you
  deselect, then stop and restart playback.
- Cycle range: drag in the ruler, or **set it from the selected notes** with Shift+double-click.
- Zoom:
  - Cmd+Alt+drag or Cmd+Alt+wheel zooms both axes.
  - Zoom to the selected notes, and go back to the previous zoom.
  - Double-click a scroller to fit all notes.
  - The scroller shows a mini overview.
  - A slider sets the blob height.
- Options: Highlight Notes During Playback, Show Intended Notes, Show Pitch Curve, Show Note Separations.

**Other features:**

- Note Inspector: shows and types pitch, cents, Hz, amplitude, formant and so on, with "---" when the selected notes differ.
- Undo 25 levels by default, up to 100.
- Every command can be given a shortcut; there are tooltips.
- Audio-to-MIDI via "Save as MIDI…": velocity comes from amplitude and breaths are left out.
- Note Assignment Mode (fixing the detection) and a choice of algorithm.

### 2.2 Cubase VariAudio 3 (included in the owner's Cubase Artist 14)

Cubase Artist 14 includes VariAudio with the Scale Assistant ([Thomann Artist 14], [SOS VariAudio 3], [MusicRadar VA3],
[Steinberg Scale Assistant pages]).

- **Segments:** split with the scissors at a segment's bottom edge, glue with Shift; warp the segment start and end
  (timing); select several segments.
- **Smart Controls** on a segment (about 11 or 12 when set to "All"):
  - Quantize Pitch toward the nearest scale note, gradually.
  - Straighten Pitch, reduce or **expand**, with **range markers** so the onset and ending are left alone.
  - Tilt and Tilt Anchor (fix drift).
  - Shift Formant.
  - **Volume** per segment.
  - Pitch Curve range.
- **Pitch Snap Mode:** Absolute or Relative; Shift turns snap off.
- **Scale Assistant:**
  - A scale picked by hand, or Follow Chord Track (chord events, scale events, or both).
  - **Snap Pitch Editing** (only in-key notes allowed while you drag).
  - **Segment colours by scale or chord** (in key or out of key).
  - Quantize segment pitches to the chord track.
- **MIDI Reference:** shows a MIDI track as target lines. **Extract MIDI** from a tuned vocal.

### 2.3 Antares Auto-Tune Pro 11, Graph mode

From [SOS Auto-Tune Pro 11], the [Antares product page], the [Antares Graph Mode blog] and the [Auto-Tune Pro user guide];
the guide is the 2018 edition, and Pro 11 keeps the same Graph-mode toolset.

- **Getting audio in:**
  - In ARA hosts, Graph mode works without "tracking" (playing the clip in first).
  - Track Pitch, or Track Pitch + Time.
  - Make Notes or Make Curves.
  - **Number of Notes** sets how sensitive the note splitting is.
  - **Make Notes From MIDI:** the target melody comes from MIDI.
  - Import Auto, Learn Scale.
- **Correction objects:** Line, Curve and Note.
- **Tools:**
  - Line, Curve, Note, Arrow.
  - Scissors (split).
  - Zoom, I-Beam, Hand.
  - Time correction: Move Point and Move Region.
- **Per-object Retune Speed, Adjust Vibrato and Formant (throat):**
  - Pro 11 adds modifier-drag shortcuts for them: hold R, V or F while dragging or scrolling.
  - Retune speed sets how fast each note reaches pitch: transitions and scoops.
- **Editing and options:**
  - Cut, Copy, Paste, Select All, Nudge, Undo / Redo.
  - Snap to Note.
  - "Play Pitch When Moving Notes" (audition).
  - Auto Scroll.
- **Pro 11:** a Zoom Outline strip for navigation, toolbar items you can hide, and a first-use helper screen.
- **Auto mode extras:** Humanize (a slower retune only on long sustains), Flex-Tune, Auto-Key.

### 2.4 Waves Tune (the full version, not Real-Time) ([Waves Tune manual], [Waves Tune explained])

- Note segmentation with a correction **Speed** inside a note, a **Note Transition** time between target notes
  (0.1 to 800 ms), and a Ratio.
- Natural vibrato detection and editing, and synthetic vibrato.
- Formant correction, Tolerance and Range, scale editing, global shift.
- Graphic tools include **Merge** (join a note with the next one) and Zoom.

### 2.5 Synchro Arts RePitch ([SOS RePitch])

- **Pointer tool:** drag for pitch; side handles change length; a top handle sets **vibrato depth**; Option-drag on an end
  **tilts** the note.
- **Draw** a pitch line freehand.
- **Split** a note.
- **Warp** markers for timing.
- **Center Notes:** amount and drift.
- **Level view:** notes *and unvoiced breaths and sibilants* (shown as green blocks) can be raised or lowered.
- **Heat map** of the processed areas.
- **Macros run automatically after detection:** snap to the detected scale, formant.
- **Local playback / audition** of a selection.
- **SynchroLink to VocAlign:** the tuned lead becomes the guide for its doubles.

---

## 3. What Honey Tune has today (verified in the code, not the manual)

**Notes and analysis**

- **Analysis:** the live detector's readings, cleaned with look-ahead: octave slips fixed against ±12 readings, gaps of
  16 ms or less bridged, a median of 5. Then the **AI check** (CREPE tiny): octave slips fixed by context and loud missed
  notes found (`dsp/src/HoneyTune.cpp:75-228`).
- **Note splitting:**
  - A new note starts when the pitch, averaged over 180 ms, stays more than 0.6 semitone away for 30 ms.
  - Notes are at least 50 ms long.
  - A note's pitch is the median of its middle two thirds.
  - None of these thresholds can be changed by the user (`dsp/src/HoneyTune.cpp:230-302`, `:236-237`, `:283`, `:247-250`).

**Settings for the whole project** (`honeytune/HoneyView.h:20-32`)

- Key: Auto or a picked key. Auto takes the project's key, else the key of the Voxology on this track, else Voxology's
  beat key, else the voice (`HoneyView.h:121-156`).
- Scale: 10 scales (`dsp/include/vox/PitchCorrector.h:14-16`).
- Snap amount: **linear**, every note pulled the same fraction of its way (`HoneyView.h:179`).
- Keep Drift and Keep Vibrato.
- These apply live to every note you haven't changed with its own controls.

**Per-note edits** (`HoneyView.h:35-45`): pitch target (moved), drift 0..1, vibrato 0..1, time shift, length, and formant
±6 st. Nothing else: no level, mute, transition, split or merge.

**Mouse** (`honeytune/HoneyRoll.cpp:615-710`)

- Click selects **one** note (`HoneyRoll.h:72`, `HoneyRoll.cpp:625-637`).
- Drag:
  - The first 6 px decide between pitch (vertical) and time (horizontal).
  - Pitch rounds to **whole semitones, chromatic even when a scale is set**; Alt gives free moves (`:651-657`).
  - Time moves are free, with no grid (`:659`).
  - Grabbing within 7 px of an end stretches the note.
- Double-click snaps the note to the key (`:700-710`).
- Clicking the ruler seeks the **host's** playhead (`:619-624`, `HoneyTunePlugin.cpp:1117-1122`).
- Right-click behaves like left-click: there is no context menu (`:615`).

**Keyboard** (`HoneyRoll.cpp:745-789`)

- Ctrl+Z undoes; Ctrl+Y and Ctrl+Shift+Z redo.
- + / - zoom in time.
- Delete resets the note.
- Left / right go to the previous or next note.
- Up / down move the note ±1 semitone, chromatic.

**View**

- Mouse wheel scrolls pitch, Shift+wheel scrolls time, and Ctrl+wheel zooms **time only**. There is **no vertical zoom**:
  the row height is set only by fitAll, between 16 and 30 px (`HoneyRoll.cpp:724-743`, `:79`).
- Fit shows at most about 6 s from the first note (`HoneyRoll.cpp:58-95`, `:136-140`), although the manual says it fits the
  whole clip (`docs/MANUAL.md:394`).
- The view always follows playback (`HoneyRoll.cpp:105-113`).
- The ruler shows bars and beats from the host's tempo map (`HoneyTunePlugin.cpp:1073-1095`).

**Panels** (`HoneyPanel.cpp`)

- Original | Tuned A/B for the whole document (`:85-102`, `HoneyTunePlugin.cpp:585-593`).
- Bottom bar: NOTE DRIFT, NOTE VIBRATO and NOTE FORMANT sliders for the selected note, with click-to-type values.
- Buttons: Snap note to key, Reset note, Reset all notes, Undo, Redo.
- A note line showing sung → plays, cents, timing and wobble hints (`:266-300`).
- A one-line status line (`:391-394`).
- The built-in manual.

**Undo** (`HoneyTunePlugin.cpp:115-140`)

- Snapshots of each recording's whole edit list, at most 100. Edits to the same note less than 0.7 s apart count as one step.
- **Changes to Settings are not undoable** (`setSettings` at `:522-531` pushes no undo step; `setEdit` at `:553-562` does).
- The stacks are **cleared on re-analysis** (`:144-145`).

**ARA**

- State is kept **per audio source**, not per audio modification (`HoneyTunePlugin.cpp:171-177`).
- The editor shows the first selected region's source, and all of that recording (`:1129-1144`).
- The project key comes from the **first** key-signature event only (`:513-514`).
- The playback renderer plays only while the host plays (`:212-213`). There is **no editor renderer**, so no audition.
- Offline exports wait for analysis or render (`:230-237`).
- Partial re-render: a one-note edit takes about 64 ms (`:883-906`, `dsp/src/HoneyTune.cpp:648-710`).
- Edits are re-attached after re-analysis by note start, within 30 ms (`:142-156`).
- Saved in the project as archive HNY3 (`:656-744`).

**Engine facts that matter below**

- The move to a new pitch is smoothed with one fixed 25 ms transition (`dsp/include/vox/HoneyTune.h:91`,
  `HoneyTune.cpp:394-405`).
- Changes to drift and vibrato fade in and out over only **20 ms** at a note's edges (`HoneyTune.cpp:366-371`, `:381`).
  Steadying a note therefore also removes its scoop in and its fall out.
- A note moved in time can't pass its neighbours (`HoneyTune.cpp:492-512`).
- A4 = 440 is hard-coded in the plan and the warp (`HoneyTune.cpp:55`, `:61`, `:542`).
- The engine already supports a reference tuning (`PitchParams.tuneCents`) and custom notes (`extraNotes`, `removedNotes`)
  (`PitchCorrector.h:51-54`), but Honey Tune passes neither (`HoneyView.h:166-169`).
- Voxology already measures the beat's tuning (`BeatKey.h:47`), but `BeatKeyShare` carries no tuning to Honey Tune
  (`KeyShare.h:16-22`).

---

## 4. Gap list

Format for each item: what the best tools do, what Honey Tune does now (code reference), then **impact**, **size**, and
how it could fit the UI without a redesign.

### A. Selection and batch editing (the biggest single gap)

**A1. Multi-select**
- *Best tools:* Melodyne has Ctrl-click, rubber band, Shift-click ranges, snake selection, Select All, Invert, Select
  following, Same pitch (all octaves) and Between locators. VariAudio and Auto-Tune also select several notes.
- *Honey Tune:* one note only; clicking the background deselects (`HoneyRoll.h:72`, `HoneyRoll.cpp:625-627`).
- **High**, **M.**
- *UI:* rubber band on the empty grid; Ctrl-click toggles a note; Shift-click selects a range; Ctrl+A selects all; Esc
  clears. The note line reads "7 notes selected". Clicking a key on the left keyboard selects every note of that pitch.

**A2. Batch edit API**
- *Best tools:* any tool or macro acts on all selected notes as one undo step.
- *Honey Tune:* `Model::setEdit(index)` handles one note at a time and is undone per note (`HoneyView.h:409-425`,
  `HoneyTunePlugin.cpp:553-562`).
- **High** (it is the base for A1, C and F), **S.**
- *UI:* none. Add `setEdits(vector<pair<int,NoteEdit>>)` that pushes one undo step and makes one render request.

**A3. Bottom-bar sliders and buttons act on the selection**
- *Best tools:* Melodyne's Inspector shows "---" for mixed values and drags change them relatively.
- *Honey Tune:* the controls cover the selected note only (`HoneyPanel.cpp:266-312`).
- **High**, **S** once A1 and A2 exist.
- *UI:* the same sliders. A mixed value shows "—"; dragging sets every selected note. "Snap note to key" becomes
  "Snap to key" for the selection.

**A4. Copy / paste notes**
- *Best tools:* Melodyne copies and pastes notes; Auto-Tune has Cut, Copy and Paste.
- *Honey Tune:* none.
- **Low** for vocals, **M.**
- *UI:* later; Ctrl+C / Ctrl+V of edits only ("paste this note's tuning onto those").

### B. Fixing how the clip was cut into notes

**B1. Split a note**
- *Best tools:* Melodyne double-clicks the upper part of a note; VariAudio uses the scissors at a segment's bottom edge;
  Auto-Tune has the Scissors; RePitch has Split.
- *Honey Tune:* none. The manual says a wobbly note can show as two cells, and nothing can fix a missed boundary
  (`HoneyTune.cpp:230-302`).
- **High**, **M+.** Splits and joins have to be saved and re-applied after `findNotes`. Undo must also snapshot the note
  boundaries, because `step()` refuses lists of a different size (`HoneyTunePlugin.cpp:134`).
- *UI:* double-click the top quarter of a cell (scissors cursor, as Melodyne does), or right-click > "Split here".

**B2. Merge two notes**
- *Best tools:* Melodyne double-clicks the separation or uses "Merge notes"; VariAudio glues with Shift; Waves Tune has
  Merge.
- *Honey Tune:* none.
- **High**, **S** with B1.
- *UI:* double-click the black phrase connector between two notes (it already exists, `HoneyRoll.cpp:257-279`), or
  right-click > "Merge with next".

**B3. Split by vibrato ("Separate Notes as Trills")**
- *Best tools:* Melodyne.
- *Honey Tune:* none.
- **Low**, **S** after B1.
- *UI:* context menu.

**B4. Note-splitting sensitivity**
- *Best tools:* Auto-Tune's "Number of Notes"; Melodyne's Note Assignment Mode.
- *Honey Tune:* fixed thresholds (`HoneyTune.cpp:236-237`, `:283`).
- **Low** once B1 and B2 exist, **S.**
- *UI:* the context menu on empty grid: "Find notes: fewer / normal / more".

**B5. Correct a detection error without moving the audio**
- *Best tools:* Melodyne's Note Assignment Mode can, for example, mark a note as detected an octave off.
- *Honey Tune:* none. The AI check handles most cases automatically (`HoneyTune.cpp:75-162`).
- **Low**, **M.**
- *UI:* context menu "This note is an octave lower/higher (fix the reading)".

### C. Hearing what you do: audition and transport

**C1. Audition while editing**
- *Best tools:* Melodyne plays the frozen sound during a pitch drag and scrubs sideways; the arrow keys sound the selected
  note when stopped; Alt+Space plays the selection in ARA. Auto-Tune has "Play Pitch When Moving Notes". RePitch has local
  playback.
- *Honey Tune:* nothing sounds unless the DAW plays. The renderer outputs only while the host plays
  (`HoneyTunePlugin.cpp:212-213`), and there is no `ARAEditorRenderer`.
- **High**, **M.** JUCE's own ARA demo previews a region through the editor renderer (`examples/Plugins/ARAPluginDemo.h`,
  `PreviewState`).
- *UI:*
  - With the host stopped, clicking, arrowing or double-clicking a note plays it, following the Original | Tuned switch.
  - During a pitch drag, the note's middle loops at the new pitch.
  - A speaker toggle next to Original | Tuned (or a context-menu option) turns this off.

**C2. Drive the host transport from the editor**
- *Best tools:* Melodyne's plug-in *cannot* start, stop or reposition the DAW (quoted in [M5]); it only plays its own
  preview.
- *Honey Tune:* it can already seek the host (`HoneyTunePlugin.cpp:1117-1122`), but it doesn't use the rest of ARA's
  playback controller: `requestStartPlayback`, `requestStopPlayback`, `requestSetCycleRange` and `requestEnableCycle`
  (ARA_SDK `ARAInterface.h:2179-2194`).
- **High.** This is where Honey Tune can **beat** Melodyne. **S.**
- *UI:*
  - Double-click the ruler, or press Enter, to play from there.
  - **L** loops the selected notes in the host's cycle and plays them.
  - Space passes through to the host.
  - Works with the playhead that now stays visible after a stop.

**C3. Follow playback without fighting the user**
- *Best tools:* Melodyne pauses auto-scroll while notes are selected, or after you scroll the playhead out of view, until
  the next play.
- *Honey Tune:* while playing, any scroll that hides the playhead is undone within one timer tick
  (`HoneyRoll.cpp:105-113`). You can't look at another part while the host plays.
- **Medium-High**, **S.**
- *UI:* no new control is required. Pause following when the user scrolls, zooms or selects; resume when playback
  restarts. Optionally, an F key or a small "follow" toggle in the ruler.

**C4. Highlight the note that is playing**
- *Best tools:* Melodyne's "Highlight Notes During Playback".
- *Honey Tune:* none.
- **Low**, **S.**
- *UI:* the playing blob's rim brightens (fits the new blob drawing).

### D. Pitch correction quality controls

**D1. Musically non-linear "Correct Pitch"**
- *Best tools:* Melodyne's macro fixes badly-off notes first at low settings and leaves nearly-right notes alone. Waves
  Tune and Auto-Tune have tolerance.
- *Honey Tune:* Snap is linear (`HoneyView.h:179`). At 60 %, a note 50 c off is still 20 c off (audible), while every
  nearly-right note is flattened toward the grid too.
- **High.** This is the default sound of every clip. **S.**
- *UI:* the same SNAP TO NOTE slider with a smarter curve: the pull grows with distance, so "70 %" already gets 50 c
  notes to under 5 c and leaves 5 c notes almost untouched. Add a test.

**D2. Apply the top bar to the selection only**
- *Best tools:* Melodyne macros act on the selection; VariAudio smart controls on the selected segments.
- *Honey Tune:* the top bar always covers every note in every clip (`HoneyView.h:20-32`); per-note overrides exist only
  for drift and vibrato.
- **Medium**, **S** after A2.
- *UI:* with notes selected, a "Tune selected" button (bottom bar) writes the top-bar values into those notes' own edits.

**D3. Style presets for one-click results**
- *Best tools:* none of them offer this (it is the owner's planned "Auto-Edit suggestions" for Honey Tune,
  `claude/RESUME-HERE.md:132`).
- *Honey Tune:* the defaults are Snap 100 %, Keep Drift 100 % and Keep Vibrato 100 %.
- **High** for novices, **S.**
- *UI:* clicking the gold "SNAP TO NOTE" label opens Natural / Polished / Tight / Hard (effect), which set the three
  top-bar sliders. No new widget.

**D4. Drag and arrows follow the scale**
- *Best tools:* Melodyne's Key Snap; VariAudio's Snap Pitch Editing.
- *Honey Tune:* drag and up/down are chromatic (`HoneyRoll.cpp:654-655`, `:778-786`).
- **Medium**, **S.**
- *UI:* when a scale is set, drag and arrows land on scale notes; Shift gives chromatic and Alt stays free.

**D5. Type pitch and cents, nudge by cents**
- *Best tools:* Melodyne's Inspector takes C4 / +2 / cents / Hz; Auto-Tune has Nudge.
- *Honey Tune:* the note line is read-only (`HoneyPanel.cpp:291-298`).
- **Low-Medium**, **S.**
- *UI:* Alt+up/down nudges ±5 cents; double-click the note line's "plays G3 +12 cents" to type a value.

**D6. Vibrato above 100 % (exaggerate) and drift inversion**
- *Best tools:* Melodyne goes from -100 % to above 100 %; VariAudio's Straighten can expand.
- *Honey Tune:* 0..1, clamped (`HoneyView.h:63`), although the engine would scale above 1 (`HoneyTune.cpp:370`).
- **Low**, **S.**
- *UI:* let NOTE VIBRATO go to 150 %.

**D7. Draw a pitch line freehand**
- *Best tools:* Auto-Tune's Line and Curve tools; RePitch's Draw.
- *Honey Tune:* none.
- **Low** for novices, **L.**
- *UI:* later, an Alt+Shift drag mode.

**D8. Random deviations for doubles**
- *Best tools:* Melodyne's Add Random Deviations (pitch and time).
- *Honey Tune:* none.
- **Low**, **S.**
- *UI:* context menu "Make it a double (loosen)".

### E. Movement inside a note, and transitions

**E1. Steady the sustain but keep the scoop and fall**
- *Best tools:* VariAudio's Straighten Pitch has range markers that spare the onset and ending; Auto-Tune's Humanize slows
  correction only on sustains; Melodyne separations can isolate the scoop.
- *Honey Tune:* drift and vibrato changes fade over only 20 ms at the edges (`HoneyTune.cpp:366-371`), so NOTE DRIFT 0
  also flattens a natural scoop and fall. This is the classic "robotic" tell.
- **High.** NOTE DRIFT / VIBRATO at 0 is the advice for every wobble. **S-M.**
- *UI:* none needed. Spare the first and last ~60-80 ms by default; the context menu offers "Steady the whole note".

**E2. Transition and retune speed per note**
- *Best tools:* Melodyne's Pitch Transitions tool; Auto-Tune's per-object Retune Speed (R + drag in Pro 11); Waves Tune's
  Note Transition (0.1-800 ms).
- *Honey Tune:* one fixed 25 ms transition for everything (`HoneyTune.h:91`, `HoneyTune.cpp:394-405`).
- **Medium-High**, **M.**
- *UI:* drag the black connector between two notes vertically (steeper or softer slide), and show it in the note line.
  Or hold R while dragging a note, as Auto-Tune does.

**E3. Tilt (drift correction by angle)**
- *Best tools:* VariAudio's Tilt and anchor; RePitch's Option-drag on an end.
- *Honey Tune:* KEEP DRIFT only.
- **Low**, **M.**
- *UI:* later; Alt-drag on a note's end vertically.

**E4. Time handles and attack speed**
- *Best tools:* Melodyne Assistant and up.
- *Honey Tune:* none.
- **Low**, **M.**
- *UI:* later.

### F. Level, mute, sibilants

**F1. Note volume and mute**
- *Best tools:* Melodyne's Amplitude Tool (double-click mutes, Note Off); VariAudio's Volume handle; RePitch's Level view.
- *Honey Tune:* none (`HoneyView.h:35-45`).
- **Medium-High.** Novices often need "that word is too loud/quiet" and "kill that noise". **M:** `NoteEdit.gain` in dB
  plus mute, applied in `render` with 10 ms fades, archive v4.
- *UI:*
  - Ctrl+vertical drag on a note sets its level (cursor and note line show dB).
  - **M** mutes, and the context menu has it too.
  - The new blob height follows the gain.

**F2. Leveling (even out notes)**
- *Best tools:* Melodyne's Leveling macro (two sliders toward the mean; noise blobs ignored).
- *Honey Tune:* none. Voxology's Rider and Compressor do this in the chain (`docs/MANUAL.md` module list).
- **Medium**, **S** after F1.
- *UI:* context menu "Even out levels (selected / all)" with an amount popup.

**F3. Breath and sibilant level**
- *Best tools:* Melodyne's Sibilant Balance; RePitch shows unvoiced parts and lets you set their level.
- *Honey Tune:* unvoiced parts are not shown at all. Voxology's Cleanup (Breaths) and De-Esser cover the common need
  (`docs/MANUAL.md:97-100`).
- **Low** inside Honey Tune, **M.**
- *UI:* later; unvoiced stretches drawn as faint hatched blobs (fits the blob work) that take F1's gain drag.

**F4. Per-note fades**
- *Best tools:* Melodyne's Fade Tool.
- *Honey Tune:* none.
- **Low**, **S** after F1.
- *UI:* context menu.

### G. Timing

**G1. Snap time moves and stretches to the beat grid**
- *Best tools:* Melodyne's Time Grid (keeps the offset from the beat; Alt places freely); VariAudio warp; Auto-Tune Move
  Point.
- *Honey Tune:* time moves are free (`HoneyRoll.cpp:659`), even though the bars and beats are known (`HoneyView.h:205-227`).
- **Medium**, **S.**
- *UI:* snap to beat when the host shares a tempo map; Alt = free (the same meaning Alt has for pitch).

**G2. Quantize / tighten timing**
- *Best tools:* Melodyne's Quantize Time ("Auto" intended beats or a grid, Intensity %).
- *Honey Tune:* none.
- **Medium** (rap and pop), **M.**
- *UI:* context menu "Tighten timing to 1/8 / 1/16 …" with %, acting on the selection.

**G3. Move several notes together**
- *Best tools:* all of them.
- *Honey Tune:* one note only, and it can't pass its neighbours (`HoneyTune.cpp:492-512`).
- **Medium**, **S** after A1.
- *UI:* drag moves the whole selection.

### H. Key, scale, tuning

**H1. Custom notes in the scale (add or remove one)**
- *Best tools:* the Melodyne Scale Ruler and Scale Window; Auto-Tune's scale editing.
- *Honey Tune:* 10 fixed scales. The engine already has `extraNotes` and `removedNotes` (`PitchCorrector.h:52-54`), but
  Honey Tune doesn't pass them (`HoneyView.h:166-169`).
- **Medium**, **S.**
- *UI:* click a key on the left keyboard to switch that note in or out of the key; the existing gold in-scale marker
  shows it (`HoneyRoll.cpp:550-554`).

**H2. Reference pitch (A4) / detected tuning**
- *Best tools:* Melodyne's Reference Pitch: Concert, Default, **Detected**, or a typed Hz.
- *Honey Tune:* A4 = 440 is assumed (`HoneyTune.cpp:55`, `:542`; key logic in MIDI). Voxology already measures the beat's
  tuning (`BeatKey.h:47`) but doesn't share it (`KeyShare.h:16-22`).
- **Medium** (a beat tuned to 432 or 445 makes every "in tune" note clash). **S-M.**
- *UI:* the status line says "tuning A = 443 Hz (from the beat)"; a "Tuning" item at the bottom of the KEY combo.
  **Tuning from the beat beats Melodyne**, which only detects the vocal's own tuning.

**H3. Key changes**
- *Best tools:* Melodyne's Key Track; VariAudio's Scale Assistant following the chord track's scale events.
- *Honey Tune:* only the host's first key signature is read (`HoneyTunePlugin.cpp:513-514`); one key per project.
- **Low-Medium**, **M.**
- *UI:* read every key-signature event and draw the rows per section; the status line names the key at the playhead.

**H4. Follow the host chord track**
- *Best tools:* Melodyne over ARA; VariAudio's Follow Chord Track.
- *Honey Tune:* none (`kARAContentTypeSheetChords` is never read).
- **Low** for lead vocals; Medium for harmonies. **M.**
- *UI:* a "Chord track" item in SCALE.

### I. View and navigation

**I1. Vertical zoom**
- *Best tools:* Melodyne zooms both axes (Cmd+Alt drag or wheel) and has a blob-height slider.
- *Honey Tune:* none; the row height is set only by Fit (`HoneyRoll.cpp:724-743`, `:79`).
- **Medium**, **S.**
- *UI:* Ctrl+Shift+wheel, or + / - with Shift.

**I2. A Fit that fits the whole clip; zoom to the selection**
- *Best tools:* Melodyne fits by double-clicking a scroller, zooms to the selected notes, and goes back to the previous zoom.
- *Honey Tune:* Fit shows about 6 s from the first note (`HoneyRoll.cpp:67`, `:136-140`), contradicting
  `docs/MANUAL.md:394` and `HoneyRoll.h:42`.
- **Medium** (the manual mismatch is a bug), **S.**
- *UI:* Fit = the whole clip; a second click returns to the 6-second working zoom; Shift+F zooms to the selection.

**I3. Overview strip**
- *Best tools:* Melodyne's scroller has a mini image; Auto-Tune Pro 11 has the Zoom Outline.
- *Honey Tune:* plain scrollbars.
- **Low**, **S-M.**
- *UI:* draw the blobs faintly inside the horizontal scrollbar.

**I4. Context menu and shortcuts**
- *Best tools:* Melodyne offers context menus for every tool and lets you assign any shortcut.
- *Honey Tune:* no context menu; about 8 keys.
- **High.** It is the no-redesign home for most of this list. **S.**
- *UI:* right-click a note for Snap to key, Steady (drift + vibrato 0), Split here, Merge with next, Mute, Reset pitch /
  time / formant / level (each separately), Select same pitch, Loop and play this note. Keys: S snap, D steady, M mute,
  L loop, Enter play, Ctrl+A, Esc, F fit, Alt+up/down cents, B = Original/Tuned.

**I5. Status-line readability**
- *Best tools:* Melodyne's tooltips and Inspector.
- *Honey Tune:* one very long line squeezed by `drawFittedText(…, 1)` (`HoneyPanel.cpp:391-394`); KEY UNSURE competes with
  counts.
- **Medium**, **S.**
- *UI:* put warnings first, move the counts into a tooltip, and show a hover tooltip on notes ("G3 +38 c → G3, wobbles 30 c").

### J. Clips, events and ARA model

**J1. Several events at once, and edit across them**
- *Best tools:* Melodyne in Cubase opens the selected events together and edits across event boundaries; Studio adds
  several tracks ([Melodyne in Cubase]).
- *Honey Tune:* it shows only the first selected region's recording (`HoneyTunePlugin.cpp:1129-1144`). A vocal comped
  from several takes or files means re-opening for each one.
- **High** (the verse/chorus workflow in Cubase), **L.**
- *UI:* the roll draws every selected region on the song timeline. A thin clip strip under the ruler shows the event
  bounds; clicking a clip there selects it.

**J2. Respect the event's trim**
- *Best tools:* Melodyne shows the regions as the DAW plays them.
- *Honey Tune:* it shows and edits the whole recording, including notes outside the trimmed event
  (`HoneyView.h:262-263`). Auto key is detected from the whole recording (`HoneyTunePlugin.cpp:822-824`).
- **Medium** (confusing: notes you never hear), **M.**
- *UI:* dim and lock what is outside the event; Fit goes to the event.

**J3. Copies tuned independently (ARA audio-modification clones)**
- *Best tools:* ARA hosts create "independent variations of the audio edits" with `cloneAudioModification` (ARA_SDK
  `ARAInterface.h:2652-2661`), so a copied lead can become a double or harmony with its own edits.
- *Honey Tune:* edits live on the audio source (`HoneyTunePlugin.cpp:171-177`), so every copy of the take shares one set.
- **Medium** (harmonies and doubles), **L.** Move `edits` and `rendered` to `ARAAudioModification` and keep the analysis
  per source. Do it together with J1.
- *UI:* none.

**J4. Report when nothing is changed**
- *Best tools:* the ARA API's `isAudioModificationPreservingAudioSourceSignal`.
- *Honey Tune:* not implemented.
- **Low**, **S.**
- *UI:* none; it lets hosts skip work.

### K. Export and other

**K1. MIDI of the tuned notes**
- *Best tools:* Melodyne Assistant's "Save as MIDI…" (velocity from level); VariAudio's Extract MIDI.
- *Honey Tune:* none.
- **Low-Medium** (doubling with a synth, harmony sketches), **S-M.**
- *UI:* a small drag handle in the bottom bar or a context-menu item. Drag the .mid file into Cubase
  (`juce::MidiFile` + `performExternalDragDropOfFiles`).

**K2. Guide-based doubles (match a double to the lead)**
- *Best tools:* RePitch + VocAlign (SynchroLink); Melodyne Studio "Quantize to reference track".
- *Honey Tune:* none.
- **Medium** for stacked pop vocals, **L.** A later "beats Melodyne Assistant" item that uses J1 and J3.
- *UI:* context menu "Match these notes to track …".

**K3. MIDI reference melody**
- *Best tools:* VariAudio's MIDI Reference; Auto-Tune's Make Notes From MIDI.
- *Honey Tune:* none.
- **Low**, **M.**
- *UI:* later.

### L. Undo, reset, safety

**L1. Undo covers settings too**
- *Best tools:* all of them undo every action.
- *Honey Tune:* Key, Scale, Snap, Drift, Vibrato and Rack knob changes aren't undoable (`HoneyTunePlugin.cpp:522-531`).
- **Medium** (a novice drags Snap to 0 by mistake), **S.**
- *UI:* the same Ctrl+Z.

**L2. Undo survives re-analysis**
- *Best tools:* not applicable.
- *Honey Tune:* the stacks are cleared when the clip is re-listened (`HoneyTunePlugin.cpp:144-145`).
- **Low**, **S.**
- *UI:* keep the stacks and re-map them by note start.

**L3. Reset one aspect at a time**
- *Best tools:* Melodyne's Restore Original and Reset Individual Edits (pitch, drift, modulation, transitions, formant,
  amplitude, time).
- *Honey Tune:* Reset note wipes everything (`HoneyPanel.cpp:125-130`, `HoneyRoll.cpp:766-772`).
- **Medium**, **S.**
- *UI:* the context menu (I4).

**L4. Right-click is harmless**
- *Best tools:* not applicable.
- *Honey Tune:* right-click drags a note like left-click (`HoneyRoll.cpp:615-638`).
- **Medium** (accidental moves), **S.**
- *UI:* right-click opens the I4 menu.

### M. Novice-first features none of the competitors have

**M1. Problem finder**
- *Best tools:* none. Melodyne's "intended notes" are passive.
- *Honey Tune:* partial. Red glow, the wobble hint (`HoneyView.h:304-312`) and the counts in the status line exist.
- **High**, **M.**
- *What it does:* after analysis, flag the notes that need a human:
  - snapped more than ~40 c (the intent is ambiguous: maybe the wrong target);
  - long notes still wobbling more than 15 c;
  - fragments under 80 ms;
  - neighbours that look mis-split;
  - octave-suspicious notes;
  - notes far off the key that Snap moved a whole semitone.
- *UI:* an amber dot on flagged blobs. The status line reads "5 notes to check ▸" (click to jump). **N** goes to the next
  one: it selects it, loops it with C2 and auditions it with C1.

**M2. "Fix this note" one-key action**
- *Best tools:* none.
- *Honey Tune:* none.
- **Medium**, **S.**
- *UI:* **D** on a flagged note applies what the hint suggests (steady, snap, or merge with neighbour).

---

## 5. Where Honey Tune is already better or unique: keep and highlight

| Strength | Where | Why it matters / how to show it off |
|---|---|---|
| **Key from outside the clip**: project key, the key Voxology's Pitch uses on this track, Voxology's beat key, then the voice; "KEY UNSURE" in red with both candidates | `HoneyView.h:121-156`, `HoneyPanel.cpp:186-199`, `HoneyTunePlugin.cpp:330-357` | Melodyne guesses from the vocal alone and warns about short phrases [M5 "Selecting the tonic"]. Show the source next to SCALE ("Auto: F Minor · from the beat") and keep it first in the manual. |
| **AI octave check** (CREPE) with counts: "N octave slips fixed, N missed notes found" | `HoneyTune.cpp:75-162`, `HoneyPanel.cpp:200-202` | Fewer detection fixes by hand. No competitor reports this. |
| **Live, non-destructive clip-wide Snap / Keep Drift / Keep Vibrato** that apply to every note not hand-edited, and hand edits stay put | `HoneyView.h:172-192` | Melodyne's macros are one-shot OK/Cancel dialogs. Honey Tune's are always adjustable. Pair with D1/D3 to make the first sound pro. |
| **One-click Original \| Tuned A/B of the sound *and* the view**; an edit made on Original switches to Tuned | `HoneyPanel.cpp:85-102`, `HoneyTunePlugin.cpp:585-593`, `:1108-1114` | Melodyne has no A/B switch in its window (Restore Original and undo only). Add a key (B). |
| **Moves the host's playhead** from the ruler | `HoneyRoll.cpp:619-624`, `HoneyTunePlugin.cpp:1117-1122` | Melodyne's plug-in cannot reposition the DAW [M5]. Extend with play, stop and loop (C2). |
| **Bars and beats from the host** on the ruler and in the time readout | `HoneyTunePlugin.cpp:1073-1095`, `HoneyView.h:205-227` | It is the base for G1 and G2. |
| **One tuner at a time with Voxology**: Pitch rests on the track Honey Tune tunes, with a red warning if it doesn't | `HoneyTunePlugin.cpp:362-409`, `HoneyView.h:285-300` | Stops the classic double-tuning mistake. No competitor talks to the vocal chain. |
| **Plain-English guidance**: the wobble hint ("NOTE DRIFT + NOTE VIBRATO at 0 make it steady"), "fixed / still off-key" counts, DAW-specific "how to open", a built-in manual | `HoneyView.h:304-312`, `HoneyPanel.cpp:224-235`, `:550-633` | This is the novice advantage. M1 builds on it. |
| **Fast partial re-render** (about 64 ms per note edit) and **exports wait for the render** | `HoneyTunePlugin.cpp:883-906`, `:230-237` | Instant feedback; no stale tuning in bounces. |
| **Edits survive re-analysis** (re-attached by note start) and are saved with the project | `HoneyTunePlugin.cpp:142-156`, `:656-744` | Robust projects. |
| **Sonicology Rack** cell and knobs | `HoneyView.h:316-406`, `HoneyTunePlugin.cpp:413-492` | A suite overview no competitor has. |
| **Red / gold at a glance** and the count "sung off-key N, fixed N, still off-key N" | `HoneyView.h:158-163`, `HoneyPanel.cpp:195-196` | Comparable to VariAudio's colours by scale. Keep it with the new blobs. |

---

## 6. Ordered build plan

The order favours what a novice hears and feels first, and builds foundations before the features that need them. Sizes
are rough.

1. **Multi-select and batch edits (A1, A2, A3, G3).** Rubber band, Ctrl/Shift-click, Ctrl+A, Esc, and selecting by pitch
   from the keyboard strip. Drag, arrows, double-click, the sliders and Reset act on the whole selection as one undo step.
   *M.* Everything below gets easier.
2. **Right-click menu and shortcuts, plus selective reset and harmless right-click (I4, L3, L4).** One menu holds Snap,
   Steady, Split, Merge, Mute, Reset pitch / time / formant / level, Select same pitch, and Loop. Keys: S, D, M, L,
   Enter, F, B, Alt+arrows. *S.*
3. **Split and merge notes (B1, B2).** Double-click the top quarter of a cell to split; double-click the black connector
   to merge. Splits and joins are saved (archive v4), and undo snapshots the note boundaries. *M+.*
4. **Audition (C1).** An ARA editor renderer: with the host stopped, clicking or arrowing to a note plays it, and a pitch
   drag loops the note at its new pitch, following Original | Tuned. Add a speaker toggle. *M.*
5. **Host transport and polite follow (C2, C3).** Double-click the ruler or press Enter to play; L loops the selected notes
   in the host's cycle. Following pauses while you scroll or select. Melodyne's plug-in can't do this. *S.*
6. **First result sounds pro (D1, D3, D4, L1).** A non-linear Snap curve (Melodyne-style), presets Natural / Polished /
   Tight / Hard under the SNAP TO NOTE label, scale-aware dragging and arrows, and undo for settings. *S-M.*
7. **Natural steadying and transitions (E1, E2).** NOTE DRIFT / VIBRATO spare the scoop and fall (~60-80 ms) by default.
   A per-note transition speed is set by dragging the connector, or R + drag. *M.*
8. **Problem finder (M1, M2).** Amber dots on notes that need a human, "5 notes to check ▸", N for the next note (selected,
   looped and auditioned), D to apply the suggested fix. No competitor has this. *M.*
9. **Note level and mute, then Even out (F1, F2).** Ctrl+drag sets a note's level, M mutes, the blob height follows, and
   the context menu has "Even out levels". *M.*
10. **View fixes (I1, I2, I5).** Vertical zoom, a Fit that really fits the clip (fixes the manual mismatch), zoom to the
    selection, warnings first in the status line, and hover tooltips on notes. *S.*
11. **Key and tuning extras (H1, H2).** Click a keyboard key to add or remove an allowed note. Share the beat's tuning from
    Voxology (`BeatKey.tuneCents`) and use it for snapping, with a manual A4 value as well. The tuning from the beat beats
    Melodyne. *S-M.*
12. **Time grid and tighten timing (G1, G2).** Moves and stretches snap to the host's beats (Alt = free), and a "Tighten
    timing" item takes a grid and a %. *S + M.*
13. **Event-aware, multi-clip editing with per-copy edits (J1, J2, J3).** Show every selected event on the song timeline,
    dim what lies outside an event's trim, and move edits to ARA audio modifications so copies can differ. *L.* The
    biggest architectural item; do it once the editing tools above are stable.
14. **MIDI export by drag (K1)**, NOTE VIBRATO up to 150 % (D6), key changes from the host (H3). *S each.*

Later, or only if asked: guide-based doubles (K2, L), chord track (H4), freehand pitch drawing (D7), tilt, time handles,
attack speed (E3, E4), sibilant and breath level inside Honey Tune (F3), copy / paste (A4).

**What would make a fair reviewer say "matches or beats Melodyne for vocals":**

- Items 1 to 9 close every gap a reviewer tests in the first 15 minutes: select a phrase, fix a mis-split, hear a note,
  correct pitch naturally, fix a scoop, lower a loud word, loop a line.
- Honey Tune's existing strengths go beyond Melodyne: the key from the beat or track, the AI octave check, live
  non-destructive settings, the A/B, host transport control, Voxology awareness, and the problem finder.
- Keep the sound audits (`tools/audit/`) in CI for every item that touches the render: E1, E2, F1, H2, and the split and
  merge re-render. A reviewer compares sound first.

---

## 7. Notes related to the work already under way

- **Amplitude blobs:** F1 (note gain) should change the blob height, and F3's unvoiced stretches could be drawn as faint
  hatched blobs. The split gesture (B1) needs a hit-zone at the top of the blob, and the connector used for merging and
  transitions (B2, E2) must stay visible between blobs.
- **Playhead visible after a stop:** this enables "Enter = play from the playhead" (C2), and gives audition (C1) a natural
  start point when no note is selected.
- **Gold = tuned into key, red glow = off-key:** with scale-aware dragging (D4) a dragged note can't go red by accident
  unless you hold Shift. The problem finder (M1) needs a third, distinct marker such as an amber dot, so it doesn't
  collide with red and gold.
- **Cream background:** check the contrast of the rubber band (A1), the cycle range (C2) and the flagged-note dot (M1)
  on cream.
- **Verify in Cubase:** the CMake file sets `EDITOR_WANTS_KEYBOARD_FOCUS FALSE` (`honeytune/CMakeLists.txt:20`), while the
  roll grabs focus on click. Check that the new shortcuts reach the plug-in in Cubase's ARA editor, and that Space still
  reaches Cubase.

---

## Sources

- [Celemony editions page]: https://services.celemony.com/en/melodyne/melodyne-5-editions
- [M5] Melodyne 5 assistant manual, Celemony Help Center PDF (201 pp., updated 10/15/2026): https://helpcenter.celemony.com/M5/pdf/melodyneAssistant5/en?env=live
  Sections read: Playback, navigation, zooming; Cycle mode; Display and other options; Preferences; The Time Grid; The Pitch Grid; Selecting notes; Correct Pitch Macro; Quantize Time Macro; The Leveling Macro; Pitch Tool; Pitch modulation and drift; Amplitude Tool; Tools for fades and sibilant balance; Timing Tool; Time Handles and Attack Speed; Note separation tools; Copying notes; The Edit menu; Audio to MIDI; release notes.
- [Main Tool page]: https://helpcenter.celemony.com/M5/doc/melodyneStudio5/en/M5tour_ToolMain
- [Modulation/Drift page]: https://helpcenter.celemony.com/M5/doc/melodyneStudio5/en/M5tour_ToolModulationDrift_2
- Melodyne 5 studio Quick Start (Cubase): https://helpcenter.celemony.com/M5/doc/melodyneStudio5/en/M5tour_QuickStart_studio?env=cubase
- [Melodyne in Cubase]: https://helpcenter.celemony.com/M5/doc/melodyneStudio5/en/M5tour_CubaseARA_IntroHub2?env=cubase
- [SOS VariAudio 3]: https://www.soundonsound.com/techniques/cubase-pro-using-variaudio-3-pitch-correction
- [MusicRadar VA3]: https://www.musicradar.com/how-to/how-to-get-your-head-around-cubase-10s-variaudio-3
- [Steinberg Scale Assistant pages]: https://steinberg.help/nuendo/v12/en/cubase_nuendo/topics/sample_editor_variaudio/variaudio_pitches_to_scales_snapping_while_editing_t.html and https://steinberg.help/nuendo/v12/en/cubase_nuendo/topics/sample_editor_variaudio/variaudio_to_chord_track_quantizing_t.html and https://steinberg.help/nuendo/v12/en/cubase_nuendo/topics/sample_editor_variaudio/variaudio_chord_track_as_musical_scale_base_using_t.html
- [Thomann Artist 14] (VariAudio with Scale Assistant in Artist 14): https://www.thomann.de/be/steinberg_cubase_artist_14_upgrade_ai.htm
- [SOS Auto-Tune Pro 11]: https://www.soundonsound.com/reviews/antares-auto-tune-pro-11
- [Antares product page]: https://www.antarestech.com/products/auto-tune/pro
- [Antares Graph Mode blog]: https://www.antarestech.com/blog/understanding-graph-mode-in-auto-tune-pro-x
- [Auto-Tune Pro user guide] (2018 edition): https://r2.gear4music.com/media/70/709346/download_709346.pdf
- [Waves Tune manual]: https://manualmachine.com/wavesaudio/wavestune/6484945-user-guide
- [Waves Tune explained]: https://josephnilo.com/blog/waves-tune-explained/
- [SOS RePitch]: https://www.soundonsound.com/reviews/synchro-arts-repitch
- ARA SDK (playback controller `requestStartPlayback` / `requestStopPlayback` / `requestSetCycleRange` / `requestEnableCycle`; `cloneAudioModification`; `isAudioModificationPreservingAudioSourceSignal`; content types notes / key signatures / sheet chords): https://github.com/Celemony/ARA_SDK (`ARA_API/ARAInterface.h`)
- JUCE ARA editor renderer: https://docs.juce.com/master/classjuce_1_1ARAEditorRenderer.html and the JUCE example `examples/Plugins/ARAPluginDemo.h` (PreviewState: preview through the editor renderer)
