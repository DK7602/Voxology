# Trap vocal measurements (no audio)

Measurements of the 33 wet male trap vocals behind the built-in references "Male trap, melodic" and "Male trap rap"
(v0.30.0). The audio is NOT kept: the user captured these clips with Sample Snagger (sample-site previews, not licensed
to the user), so only these numbers are stored. Enough to rebuild, regroup or re-tag the two references; testing new
analysis on real trap audio needs audio the user owns (e.g. bought trap vocal packs, which could then go in test-audio).
Get a legal check before selling Voxology (see claude/RESUME-HERE.md, licence notes).

`trap_measurements.csv`, one row per clip (Auto-Edit's analysis, mono 48 kHz):

| Column | Meaning |
|---|---|
| `group` | `melodic` (held notes >= 30 %, centred within 5 cents, 3+ notes), `rap` (held < 30 %), or `out: ...` (why it was left out) |
| `seconds`, `voiced_s`, `held_pct`, `pitched_pct`, `f0_hz` | length, singing time, held-note share (sung vs rapped), pitched share, median pitch |
| `s_db`, `punch_db`, `tail_db`, `low_bass_db` | "s" level vs the voice (empty = none heard), word-to-word punch, space after phrases (empty = unknown), energy under 100 Hz |
| `notes_measured`, `centre_off_cents`, `movement_in_note_cents` | tuning: notes >= 0.25 s; median distance of their centres from the nearest semitone; median pitch movement inside them. Voxology's modes on a dry lead: Natural ~10-14 c movement, Classic ~7-10, Robot ~3-6 |
| `tone_<f>hz_db` | third-octave balance vs the 500 Hz - 2 kHz average (the reference's tone = the group's median) |

`upload_name` is the file's name as uploaded (meaningless hashes / stream_N / demo_N), for tracing only.
