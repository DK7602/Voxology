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

Decode for the engine: `ffmpeg -i X.mp3 -ac 1 -ar 48000 -f f64le x.f64` (raw doubles, mono 48 kHz).
