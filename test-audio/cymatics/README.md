# Cymatics vocals (royalty-free sample packs the user owns: private, for testing only)

68 vocals from Cymatics sample packs, uploaded by the user on 2026-10-10 / 11 for building references and testing.
Royalty-free means the user may use them in songs; don't share or publish the audio itself (keep this repo private).
The plug-ins store only measurements of them (three built-in references), never audio.

Saved as mono MP3 (LAME V5, the original sample rate), full length. The original WAVs were stereo 44.1 / 48 kHz.

| Kind | Count | What |
|---|---|---|
| `acapella` | 13 | whole songs, 24 - 108 s, R&B / pop, sung (voices: see `labels.csv`) |
| `acapella-wet` + `stem-*` | 6 | "Deliberation": the processed lead plus its dry lead, adlib and background stems, dry and wet, all aligned (45.6 s) |
| `hook-wet` | 10 | finished sung hooks, effects baked in, 17 - 44 s |
| `loop` | 25 | 11 - 27 s melodic vocal loops for trap beats (mostly 140 BPM), heavily filtered (dark, thin), many pitched / chopped |
| `lead-*`, `lead1-*`, `acapella-dry`, `hook-dry` | 14 | the user's male sets (2026-10-11): dry and wet lead vocals of Deliberation, In My Life, Ja Ja, Make Love, No Good Reason, Light, Reminisce (wet only) and the hook Our Love |

## Keys: the pack's file names are often wrong

Each file's name ends in the key it's actually sung in (`sung_Csharp_minor`). **24 of the first 54 pack names were a
semitone off** (e.g. "Burial - 140 BPM C Min" is sung in C# minor), so the pack's own key is kept only in
`labels.csv` (`pack_key`). Labelled from the sung notes with a method independent of Voxology (harmonic-sum pitch on
FFT frames, level-weighted note histogram, the 7-note major-scale set holding the most of it), checked against
Voxology's YIN pitch track, and re-checked on these MP3s (same notes on all of them).

`labels.csv` columns:

| Column | Meaning |
|---|---|
| `sung_key` | the key sung, named from the pack's home note where it fits (a mode when the notes say so: `G dorian` = the notes of F major, home G). `unclear` = no trustworthy key (Held: mostly rapped; the two pitch methods disagree) |
| `sung_note_sets` | the 7-note sets (named by their major scale; the relative minor has the same notes) that fit within 3 %. Two = one note barely sung (e.g. D vs D#); `too few notes to tell` = only 2 - 4 different notes sung |
| `notes_in_set_pct` | how much of the singing those 7 notes hold |
| `voice` | male / female as labelled by the user; `unknown` otherwise. Don't guess it from `formant_spacing_hz`: that estimate (vocal tract length) put 5 male R&B singers in the "female" group and can't be trusted on these processed voices |
| `f0_hz`, `held_pct`, `pitched_pct`, `s_db`, `punch_db` | Auto-Edit's analysis of the original WAV (`s_db` empty = no "s" heard) |

## What they were used for

- Built-in references (v0.28.0): "Male R&B singer" (7 male leads: the dry stems of Deliberation, In My Life, Ja Ja, Make
  Love, No Good Reason, Light + Reminisce lead 1 wet), "Female R&B singer" (the 4 female acapellas: Wake Up, Make Me
  Crazy, Not Mine, What I Do) and "Sung hooks, wet" (the 10 hooks + Our Love wet). v0.27.0's "Pro R&B singer (female),
  modern" mixed in 5 male voices (formant guess); replaced. The loops aren't a reference: filtered for background use
  (-40 dB at 8 kHz).
- Key benchmark (`tools/audit/keybench.cpp`, 59 labelled vocals): Auto-Edit's voice key 27 right, 3 wrong (near-ties:
  the deciding note barely sung), 29 unsure; before v0.27 it was sure and wrong on 11 of the first 45. Live AUTO key
  (VoiceKey): 35 right, 1 wrong, 23 unsure.
- The producer's own dry -> wet: Auto-Edit on the dry lead with the wet lead as reference (style R&B). Space (tail):
  Deliberation -23.1 vs the producer's -23.0 dB, In My Life -24.3 vs -24.6, Ja Ja -20.9 vs -20.9, Light -21.9 vs -21.5;
  style alone 1.5 - 3.9 dB off. Tone stays within 0.4 - 1.5 dB (these dry stems are already EQ'd and compressed).
  Make Love's wet lead reads as wet (s +2.1 dB): Auto-Edit takes only its tone and s, space follows the style.
  No Good Reason / Our Love: their wet tails can't be measured (no clean phrase ends).

Decode for the engine: `ffmpeg -i X.mp3 -ac 1 -ar 48000 -f f64le x.f64`.
