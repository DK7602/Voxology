# Cymatics Infinity vocals (royalty-free sample packs the user owns: private, for testing only)

124 vocal stems from Cymatics' Infinity vocal collection, sent by the user on 2026-10-12 (male: 10 songs, 87 stems;
female: 5 songs, 37 stems; "supposed to be trap"). Royalty-free means the user may use them in songs; don't share or
publish the audio itself (keep this repo private). The plug-ins store only measurements of them, never audio.

Saved as mono MP3 (LAME V5, the original sample rate), full length (39 - 405 s; none needed looping). The originals were
stereo WAVs. Each song comes as a full acapella (dry) plus its parts (lead / lead 1 - 3, ad-libs, background,
harmonies, underlays), most parts both **dry** (as recorded) and **wet** (the pack's finished mix: tuned, EQ'd,
compressed, effects). A few songs repeat earlier uploads (test-audio/cymatics: Deliberation, In My Life, Ja Ja, Make
Love, No Good Reason, Reminisce, and the female Make Me Crazy, Not Mine, Wake Up, What I Do); this set is complete.

File names: `<Song>_<part>-<dry|wet>_<bpm>bpm_sung_<key>.mp3`, the key being the song's as actually sung (from its full
acapella). This time the pack's key names were right for 14 of 15 songs; Make Love ("A Maj") is sung with G natural:
A mixolydian. Method (scratch sungkey.py, independent of Voxology): harmonic-sum pitch on 128 ms FFT frames, level-
weighted note histogram, the 7-note major-scale sets holding the most of it.

`labels.csv`: `song_key` (the song's), `stem_key` / `sung_note_sets` / `notes_in_set_pct` (that stem alone: a few
stems with few notes read differently), `voiced_s`, `f0_hz`, `held_pct`, `s_db` (empty = no "s" heard), `punch_db`:
Auto-Edit's analysis of the original WAV (tools/audit/split_study.cpp).
