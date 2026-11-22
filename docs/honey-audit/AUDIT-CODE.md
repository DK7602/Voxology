# Honey Tune correctness audit (source b833cc0, read-only copy /tmp/claude-0/audit-src)

Scope: honeytune/HoneyTunePlugin.cpp, HoneyView.h, HoneyRoll.*, HoneyPanel.*, dsp HoneyTune.*, HoneyLink, KeyShare.
No repository file was edited. Reproducers (synthetic signals only, no test-audio used):
tools/audit/honey-repro/ (common.h, repro_*.cpp; built against vox_dsp like the other audit tools: see
tools/audit/README.md). After the Beta 38 fixes changedRegion refuses the overlapping cases (full render).

Severity labels: crash / wrong audio / wrong edit / UI. Status: CONFIRMED (reproduced, or certain from the code) / PLAUSIBLE.

---------------------------------------------------------------------------------------------------------------------

## 1. [wrong audio, HIGH, CONFIRMED by reproducer] A partial re-render misses the notes a time move really affects: the moved note vanishes, and undo / reset leaves stale audio

- dsp/src/HoneyTune.cpp:648-685 (changedRegion), used by honeytune/HoneyTunePlugin.cpp:893-902 (renderNow, partial path).
- changedRegion works only from the `after` positions and takes one neighbour on each side when timing changed. It
  ignores (a) where the changed notes sounded BEFORE the edit and (b) timeMap's clamping cascade (HoneyTune.cpp:504-505):
  a note moved past its neighbour pushes / crushes the following notes, which can lie past `to`.
- The roll lets you do this: HoneyRoll.cpp:203-205 (span) clamps a time drag only at 0 s, and a right-edge drag has no
  upper limit. There is no limit against the neighbours or the clip end. applyEdits (HoneyView.h:183-188) does not clamp
  either.
- Scenario A (repro_partial.cpp): 8 notes 0.4 s long with 0.3 s breaths. Drag note 2 (D4) +1.5 s, so it now sits on note 4.
  changedRegion returns [1.063, 3.165) s, which is less than 60 % of the clip, so the partial render runs. At the note's
  new place (3.42-3.81 s) the partial render plays note 4 untouched (65.04 MIDI), not note 2 (62). The full render plays
  62.02 there. Max |full - partial| = 0.35. So the note you moved is not heard at all. A later full render (any settings
  or key change) suddenly changes the audio.
- Scenario B (repro_undo.cpp): after that move, Ctrl+Z (or Reset note / Reset all) runs changedRegion(moved -> as sung).
  That is again partial, [1.063, 3.165) s. After the undo, note 4 still plays note 2's pitch (62.02 instead of 65.04). The
  stale doubled note stays until some full render happens.
- Fix: build the region from both time maps. Take the union of the before and after sound spans of the changed notes,
  extended to wherever timeMap(before).inAt and timeMap(after).inAt differ. Simpler: fall back to a full render when any
  changed note's before or after sound span overlaps a neighbour or runs past the clip. Also clamp the drag in the UI
  (see 2).

## 2. [wrong audio, HIGH, CONFIRMED by reproducer] Dragging or stretching a note past its neighbours or the clip end erases other notes, while the editor still draws them

- dsp/src/HoneyTune.cpp:492-512 (timeMap). It keeps the order by clamping every later note to `lastOut + 5 ms` and to a
  minimum length of 30 ms. A note dragged onto or over the next note squeezes that note to 30 ms. A note pushed near the
  clip end makes `std::clamp(v, lo, hi)` run with lo > hi (lines 504 and 505): that is undefined (an assert in MSVC Debug
  and in hardened libc++ / _GLIBCXX_ASSERTIONS builds). In libstdc++ release it returns lo, so the `out` knots stop
  rising and mapThrough's upper_bound runs on an unsorted range.
- repro_timemap.cpp (note 1 of 5 dragged +20 s): the knots go 1.4 -> 1.4, 1.5 -> 9.965, 1.9 -> 9.995, 2.0 -> 9.965 ...
  ("out rising: NO"). Output 1.4-9.96 s becomes the 0.1 s breath stretched over 8.5 s, and every later note is crammed
  into the last ~35 ms.
- repro_pastend.cpp (8 notes, 6.1 s clip):
  - Note 1 dragged +6 s: notes 1-6 render at -200 dB (silence) where they were sung, note 7 at -52 dB.
  - Note 1's right end stretched by 10 s: notes 2-7 are all replaced by note 1's pitch.
  - repro_partial's full render: notes 3 and 4 disappear when note 2 is dragged onto them.
- The roll keeps drawing the other notes at their sung places (span() uses each note's own soundStart / End), so what you
  see is not what plays. RESUME-HERE notes "a note can't be moved past its neighbours (clamped)", but nothing in the UI
  enforces or shows that.
- Fix: clamp the drag in HoneyRoll::span / mouseUp between the neighbours' sound positions (prev.soundEnd + 5 ms,
  next.soundStart - 5 ms) and the clip end. Clamp outStart / outEnd to the clip in applyEdits. In timeMap, guard both
  clamps (hi = max(lo, hi)), or skip notes that no longer fit.

## 3. [wrong audio, MEDIUM-HIGH, CONFIRMED from code] A clip plays silence until its analysis job starts; "couldn't read" and "too long" clips play silence forever; an export can bake that silence in

- HoneyTunePlugin.cpp:244-249: when `rendered` and `recorded` are both null, the region is skipped, so it is silent.
  JUCE ignores the return value: processBlockForARA always returns true when bound (juce_AudioProcessor_ARAExtensions.cpp
  101-114), and the buffer was cleared at line 211.
- `recorded` is set only inside analyseClip (line 809) on the single pool thread (line 948, `ThreadPool pool {1,...}`).
  listen() (line 748) only queues the job.
- Scenario: open a project, or apply Honey Tune to several takes. Clip 1's analysis takes tens of seconds (AI check). Clips
  2..N stay silent in realtime playback until their own job starts. All render jobs are queued after all analyses (FIFO),
  so no clip plays tuned until every analysis is done.
- Offline: an export started early waits at most kOfflineWaitMs = 180 s per clip (line 234). After that `waitedOut` is
  set and a clip whose job hasn't started exports as silence, because recorded is null. A clip whose render is still
  queued exports untuned.
- Status 3 (reader failed) and status 4 from listen (count > INT_MAX, or bad_alloc while reading: lines 770, 778-789)
  return before any job, so `recorded` is never set and the region is silent for good. The comments at lines 4-5 and 233
  ("the host plays the clip untouched", "plays as it is") are not true for ARA.
- Fix: publish `recorded` in listen(), under the spin lock, as soon as the samples are read (the old one can go to a
  mutex-guarded retired list, or be handed to the job). Consider running analyses ahead of renders, or prioritising the
  clip the editor shows.

## 4. [wrong edit, MEDIUM, CONFIRMED from code] In the Original view a sideways or edge drag erases the note's existing timing edit; hit testing uses the tuned pitch while cells are drawn at the sung pitch

- HoneyRoll.cpp:198: span() returns the sung {start, end} in Original view, before the drag offsets are applied.
- mouseUp (lines 667, 679-686) then computes `edit.shift = a - n.start/sr = 0` and `edit.length = 1` and calls setEdit.
  setEdit switches to Tuned (toTuned).
- Scenario: note 5 moved +200 ms earlier. Switch to Original and drag note 5 sideways by any amount. The +200 ms is
  removed and the drag is ignored. The drag is never visible in Original view either: drawCell (line 409) draws at
  `pitch`, span ignores dragging.
- noteAt (line 240) and mouseMove (line 718) hit-test the cell at `note.target`, but drawCell draws it at `note.pitch` in
  Original view. A note moved by a semitone or more can't be clicked where it is drawn. Clicks, double-clicks and
  Delete hit the wrong or no note. A pitch drag then applies relative to the hidden tuned target.
- Fix: in Original view either disable editing (or switch to Tuned on mouseDown), or hit-test and draw with the same
  pitch, and apply the drag on top of the note's existing soundStart / End.

## 5. [wrong edit, MEDIUM, CONFIRMED from code] Touching one per-note slider pins the note's other values to today's global settings

- HoneyPanel.cpp:302-311 (applyNoteEdit) writes `e.drift = noteDrift/100` and `e.vibrato = noteVibrato/100`. Those
  sliders show the EFFECTIVE value, which is the global KEEP DRIFT / KEEP VIBRATO when the note has no override
  (updateNoteControls 280-281).
- Scenario: set NOTE FORMANT on note 7. Its edit now holds drift = 1.0 and vibrato = 1.0 as explicit overrides. Later,
  KEEP VIBRATO 0 % for the clip no longer steadies note 7. The note also counts as "changed by hand", and the Rack's
  "steadied" count is off.
- Changing NOTE DRIFT likewise freezes NOTE VIBRATO. The 1 % slider step also rounds a Rack-set 0.555 to 0.56.
- Fix: only write the field whose slider changed. Keep -1 ("follow the settings") for the others.

## 6. [wrong audio, MEDIUM, PLAUSIBLE] Audio-source property changes are ignored, and content updates are only picked up when the host re-enables access

- HoneyTunePlugin.cpp:630-644: only `doUpdateAudioSourceContent(affectSamples)` sets `samplesChanged`, and a re-read
  happens only on the next didEnableAudioSourceSamplesAccess(true).
- ARA allows sampleCount, sampleRate and channelCount to change through updateAudioSourceProperties while access is
  disabled (ARAInterface.h:842-863), and says a plug-in "may restart with a fresh analysis". Honey has no
  didUpdateAudioSourceProperties hook. After a re-enable, status is 2 and samplesChanged is false, so nothing is re-read.
- processBlock (line 238) then uses the NEW source->getSampleRate() for `ratio` with buffers read at the OLD rate (wrong
  speed and pitch), or with the old length (the extra part is silent, or cut off).
- Likewise, a host that sends updateAudioSourceContent while access stays enabled (no toggle) gets the old samples'
  render indefinitely.
- Fix: override didUpdateAudioSourceProperties (set samplesChanged when count / rate / channels changed). On
  affectSamples with access enabled, schedule a re-listen (callAsync / didEndEditing).

## 7. [wrong audio, LOW-MEDIUM, PLAUSIBLE] `renderedTrack` is a raw pointer: address reuse (ABA) makes a stale render look current

- HoneyTunePlugin.cpp:52, 893, 909: the partial-render test is `st.renderedTrack == track.get()`.
- Scenario: render with track T0 (renderedTrack = T0). The samples change and analysis 1 runs; `state->track = T1` frees
  T0 (if no editor snapshot holds it). The samples change again during analysis 1, so analysis 2 is queued BEFORE the
  render job that analysis 1 requests. Analysis 2's make_shared<Track> can get T0's freed address (same size class;
  tcache is LIFO).
- The queued render then sees renderedTrack == track. If the note list happens to match, changedRegion says "nothing
  changed" (line 895), or renderPart silently no-ops because previous.size() != mono.size() (HoneyTune.cpp:690). The
  render of the old samples keeps playing and is stamped as current (renderedNotes updated).
- Fix: keep a shared_ptr<const Track> (or a generation counter) for the last render, and also compare channel lengths
  with the original.

## 8. [wrong edit, MEDIUM (design), CONFIRMED from code] Edits live on the audio source, not the audio modification: independent copies can't be tuned differently

- All state is in SourceState (per ARAAudioSource). The archive is keyed by the source's persistent ID (lines 719-744),
  and the plug-in never looks at ARAAudioModification.
- ARA uses separate modifications for "independent variations of the audio edits as opposed to aliases"
  (ARAInterface.h cloneAudioModification, 2653-2661).
- A duplicated chorus (same take, made independent by the host) always plays the same tuning. Editing one copy re-tunes
  every copy, and a partial archive of one modification carries the whole source's edits.
- Fix: per-modification edit lists (ARAAudioModification subclass), with the analysis shared per source.

## 9. [wrong edit, LOW-MEDIUM, PLAUSIBLE] Saved edits are matched by note start in SAMPLES

- SavedEdit.start (line 32), saved() line 164, attach() lines 147-154 (30 ms tolerance at the NEW track's rate).
- If the source's sample rate differs from when the edits were made (a converted project / file, or a properties
  change), old sample positions are read at the new rate. Edits after ~0.3 s miss their note and are silently dropped
  (attach discards unmatched entries, and later saves no longer contain them), or land on a different note that happens
  to start there.
- Fix: archive starts in seconds (HNY4), or store the rate and convert.

## 10. [UI, MEDIUM, CONFIRMED from code] Ruler, playhead and seek use the first playback region, not the one you selected

- HoneyTunePlugin.cpp:1061-1068: the timeline is built from the first modification's `getPlaybackRegions().front()`.
  The playhead (1160-1165) and seek (1117-1122) use its songOffset.
- For a take with two regions (a split clip, an alias copy later in the song), selecting the second region shows the
  first region's bars.
- Clicking the ruler seeks the host to the first region's song position.
- While region 2 plays, the playhead shows a wrong clip position whenever `songSeconds - offset1` falls inside the
  clip. In the gaps, or in a trimmed-out part, it moves even though nothing of the clip is playing.
- Fix: remember the selected region (pickSource) and build the timeline / offset from it. Map the song position through
  the region that actually covers it.

## 11. [UI, LOW, CONFIRMED from code] Switching to another clip keeps the old clip's view

- HoneyRoll::fitted is never reset (HoneyEditor::pickSource 1138-1143 only clears the selection).
- After selecting a different clip, viewStart, zoom and topMidi stay from the previous clip. If the old view was past the
  new clip's end, or at another pitch range, the roll shows nothing until Fit or a scroll.
- Fix: reset `fitted` (and userZoomed) when the source changes.

## 12. [crash / CPU, LOW, PLAUSIBLE] The AI check's own threads

- dsp/src/HoneyTune.cpp:96-109 starts up to 8 std::threads per analysis.
- (a) They bypass the pool's Thread::Priority::low (HoneyTunePlugin.cpp:948). On Windows, std::thread is normal priority
  regardless of the creator, so an analysis loads up to 8 cores at normal priority (UI stalls, more dropout risk).
- (b) If thread creation fails, emplace_back throws std::system_error. The plug-in catches only bad_alloc
  (lines 797-801), and unwinding destroys already-joinable threads: std::terminate, which takes the host down.
- Fix: a lower-priority (or JUCE) worker set, catch (...) around the job, and join on the error path.

## 13. [wrong audio, LOW, PLAUSIBLE] Realtime try-lock gives up after 32 immediate tries

- HoneyTunePlugin.cpp:89-95. The lock holder is the LOW-priority pool thread (swapIn line 104; renderNow 887-889). If it
  is preempted inside that critical section, the audio thread gets nullptr and the whole block is silent for that
  region.
- Rare, but avoidable: hold a lock-free atomic<shared_ptr> or an index swap, or fall back to the last pointer the audio
  thread used (kept alive by `retired`).

## 14. [wrong edit, LOW, PLAUSIBLE] Edits by index can hit the wrong note after a re-analysis

- setEdit takes the index from the editor's snapshot (HoneyTunePlugin.cpp:553-559). analyseClip replaces `notes` (lines
  826-833) without sending a change message. The editor sees it only on the next 0.5 s timer refresh, or after the render.
- A refresh from changeListenerCallback can also swap the snapshot during a drag. Up to then, a click, Delete, arrow or
  drag-release applies to whatever note now has that index.
- Fix: send a change message right after the notes change, and tag edits with the snapshot's track (reject on mismatch).

## 15. Smaller issues (CONFIRMED from code unless noted)

- [wrong edit / undo, LOW] pushUndo (lines 118-129) runs even when the edit changes nothing. Delete on an untouched
  note, or "Snap note" on an already-snapped note, pushes a duplicate step and clears the redo stack. Same-note edits
  within 0.7 s also merge across different actions (move, then Delete). Settings changes are not undoable, so Ctrl+Z
  undoes the last note edit instead.
- [wrong edit, LOW] The Up/Down arrows (HoneyRoll.cpp:778-786) have no range limit. Drags are clamped to 24..108
  (line 656), and archives to 0..127 (HoneyView.h:62), so repeated presses can push a note out of the drawable range and
  it changes on reload.
- [UI, LOW] Inverted jlimit bounds trigger a jassert and an odd view:
  - Ctrl+wheel zoom (HoneyRoll.cpp:731): the lower bound width/(1.2 x seconds) can exceed 1500 px/s for short clips in
    wide windows. The '+'/'-' keys use different limits (20..2000, line 758).
  - topMidi (lines 187, 740): the lower bound 24 + rows can exceed 108 in tall windows (up to 2400 px allowed), so the
    view sits far above the top note.
- [perf / wrong audio, LOW] A stale analysis is never superseded. If the samples change twice before the first job runs,
  the first job analyses the old take for minutes and swaps the OLD samples back into `recorded` (line 809) until the
  second job runs.
- [race, benign] timerCallback reads `state->sampleRate` (line 1167) on the message thread. It is a "background thread
  only" plain double written in analyseClip.
- [UX] Leaving the switch on Original makes an offline export bake the untuned recording (getPlayback line 87). Nothing
  warns about it.

## Checked and found OK

- shared_ptr lifetime on the audio thread: swapped-out buffers are only freed by the pool thread once
  use_count()==1, so the audio thread never drops the last reference.
- Controller destruction: `closing` plus removeAllJobs before members die, and callAsync guarded by controllerAlive /
  alive.
- Lock order: dataLock -> settingsLock only; no inversion found.
- Coalesced render loop: no lost request.
- Archive parsing: bounded by failed() and sanitised.
- Key signature -> key/scale (circle of fifths) and the tempo / bar conversion.
- Short, empty and silent clips: no crash paths found.
- warp is close to transparent where the map is locally identity (sub-period offset only: repro_warpid.cpp).
- L/R stay aligned after a move or stretch on a stereo pair (repro_stereo.cpp; a one-period slip can't be ruled out on
  real voices).
- HoneyLink / KeyShare seqlock reads: bounded retries, values clamped.
