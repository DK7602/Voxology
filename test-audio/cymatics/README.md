# Cymatics vocals (royalty-free sample packs the user owns: private, for testing only)

54 vocals from Cymatics sample packs, uploaded by the user on 2026-10-10 for building references and testing.
Royalty-free means the user may use them in songs; don't share or publish the audio itself (keep this repo private).
The plug-ins store only measurements of them (two built-in references), never audio.

Saved as mono MP3 (LAME V5, the original sample rate), full length. The original WAVs were stereo 44.1 / 48 kHz.

| Kind | Count | What |
|---|---|---|
| `acapella` | 13 | whole songs, 24 - 108 s, R&B / pop, sung (mostly female by formants) |
| `acapella-wet` + `stem-*` | 6 | "Deliberation": the processed lead plus its dry lead, adlib and background stems, dry and wet, all aligned (45.6 s) |
| `hook-wet` | 10 | finished sung hooks, effects baked in, 17 - 44 s |
| `loop` | 25 | 11 - 27 s melodic vocal loops for trap beats (mostly 140 BPM), heavily filtered (dark, thin), many pitched / chopped |

## Keys: the pack's file names are often wrong

Each file's name ends in the key it's actually sung in (`sung_Csharp_minor`). **24 of the 54 pack names were a
semitone off** (e.g. "Burial - 140 BPM C Min" is sung in C# minor), so the pack's own key is kept only in
`labels.csv` (`pack_key`). Labelled from the sung notes with a method independent of Voxology (harmonic-sum pitch on
FFT frames, level-weighted note histogram, the 7-note major-scale set holding the most of it), checked against
Voxology's YIN pitch track, and re-checked on these MP3s (same notes on all 54).

`labels.csv` columns:

| Column | Meaning |
|---|---|
| `sung_key` | the key sung, named from the pack's home note where it fits (a mode when the notes say so: `G dorian` = the notes of F major, home G). `unclear` = no trustworthy key (Held: mostly rapped; the two pitch methods disagree) |
| `sung_note_sets` | the 7-note sets (named by their major scale; the relative minor has the same notes) that fit within 3 %. Two = one note barely sung (e.g. D vs D#); `too few notes to tell` = only 2 - 4 different notes sung |
| `notes_in_set_pct` | how much of the singing those 7 notes hold |
| `voice_estimate` | male / female from formant spacing (vocal tract length; ~1000 Hz male, ~1150-1200 female). Unreliable on high or pitched-up voices: loops over 350 Hz say so |
| `f0_hz`, `held_pct`, `pitched_pct`, `s_db`, `punch_db` | Auto-Edit's analysis of the original WAV (`s_db` empty = no "s" heard) |

## What they were used for (v0.27.0)

- Built-in references: "Pro R&B singer (female), modern" (the 13 acapellas with a female / likely-female voice,
  Deliberation from its dry stem) and "Sung hooks, finished (mostly female)" (the 10 hooks). The loops aren't a
  reference: they're filtered for background use (-40 dB at 8 kHz).
- Key benchmark (`tools/audit/keybench.cpp`): Auto-Edit's voice key was sure and wrong on 11 of 45 labelled vocals
  (one note off); v0.27 cross-checks it with the note-set method: 22 right, 2 wrong (one barely sung note), 21 unsure.
  The live AUTO key (VoiceKey): 24 right, 1 wrong, 20 unsure.
- Deliberation dry -> wet: Auto-Edit on the dry lead with the wet lead as reference lands its space within 0.4 dB of the
  producer's (tail -24.5 vs -24.1 dB), tone unchanged; style only: 2.5 dB wetter.

Decode for the engine: `ffmpeg -i X.mp3 -ac 1 -ar 48000 -f f64le x.f64`.
