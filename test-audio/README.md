# Test audio (the user's own songs: private, for testing only)

Saved at the user's request for testing Voxology, Honey Tune, Polisher and any future plug-in, in any thread.
Do not share or publish. All MP3, 44.1 kHz stereo.

| File | What | Notes |
|---|---|---|
| Don_Birthday_2024_Vox_only.mp3 | dry vocal, 105 s | sung / melodic rap; song in C major (user's key app); voice alone leans G; f0 ~246 Hz; sings ~28 cents off note centres; heavy rumble |
| Gallas_2026_Clip_Acapella.mp3 | vocal, 38 s | rap + singing; peaks +8 dB over full scale when decoded to float (hot export) |
| Gallas_2026_Clip_Instrumental.mp3 | beat, 38 s | trap beat; BeatKey: B minor (notes of D major), tuned -3 cents; C / C# barely played |
| Gallas_2026_Clip_with_music.mp3 | vocal + beat | the user's mix |
| Gallas_2026_Clip_Mastered.mp3 | mastered | the user's own master (reference for Polisher / Voxology) |
| Don_Lysette_Clip_Acapella.mp3 | vocal, 49 s | male lead + female backing vocal on the same track (not one voice: hard for a monophonic tuner) |
| Don_Lysette_Clip_Instrumental.mp3 | acoustic guitar | BeatKey: E mixolydian (notes of A major), tuned +4 cents |
| Don_Lysette_Clip_Music_Vox.mp3 | vocal + guitar | the user's mix |

| Schaf_2026_Clip_Acapella.mp3 | vocal, 43 s | peaks +2 dB over full scale (float) |
| Schaf_2026_Clip_Instrumental.mp3 | beat, 43 s | BeatKey: F# (dorian / minor: plays neither D nor D#, so that note is left open), in tune |
| Schaf_2026_Clip_Music_Vox_Unmastered.mp3 | vocal + beat | the user's raw mix, not mastered |
| Schaf_2026_Clip_Mastered.mp3 | mastered | the user's own master |

| Gallas_2026_Note26_Raw.mp3 | rap vocal, 8.7 s, no plug-ins | note 26 (3.88 - 4.56 s) sung G3 +46 cents: the Honey Tune whole-step test |
| Gallas_2026_Note26_HoneyTune_A3_v0.15.mp3 | Honey Tune (v0.15) + Voxology Auto-Edit, note 26 moved to A3 | the bug: top resonances moved with the pitch (4-6 kHz +11 dB); fixed in v0.16.2 |

| DMinor_Test_2026_Clip_Music_Vox.mp3 | vocal + beat, 28 s (user's "Test for Claude" WAV, 48 kHz float) | the v0.18 key case: user's Key Compass says D minor; Voxology (vocal only) fell back to A Chromatic. BeatKey on this mix: notes of C major (Bb open), home D (dorian / minor), conf 1, tune -4 c. Auto-Edit's voice key on it: D minor but only 40 % sure (ambiguous with D major) |

Decode for the engine: `ffmpeg -i X.mp3 -ac 1 -ar 48000 -f f64le x.f64` (raw doubles, mono 48 kHz).

Levels: the acapellas peak above 0 dBFS when decoded to float (Gallas +8 dB, Don & Lysette +3, Schaf +2, Don_Birthday +2). The
peaks are intact, not clipped (only Schaf has a few flat tops): turn them down (e.g. -8 dB) before mixing or encoding renders.
