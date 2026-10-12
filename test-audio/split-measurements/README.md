# Split vocals vs real vocal stems (numbers only)

How a vocal **split out of a full song** measures against the real vocal stem, for the split-reference check (v0.41.0).

- Songs: MUSDB18-7 (144 seven-second excerpts; 142 with 3 s or more of voice). Research / non-commercial dataset:
  the audio is NOT in this repo, only these measurements.
- Split with UVR's MDX-Net vocal model `UVR-MDX-NET-Voc_FT` (what the free UVR app uses), via the `audio-separator`
  Python package on CPU, for measuring only. Nothing from it ships in Voxology.
- `musdb18_real.csv` / `musdb18_split.csv`: Auto-Edit's analysis (`tools/audit/split_study.cpp`) of each song's real
  vocal stem and of the vocal split from its mix (both mono 48 kHz). Compare with `tools/audit/split_compare.py`.

Result (split minus real, median / 90 % of songs within):

| | median | 90 % within |
| --- | --- | --- |
| "s" brightness | +0.06 dB | 1.5 dB |
| punch (word-to-word) | +0.10 dB | 0.8 dB |
| tone 250 Hz - 10 kHz (third octaves) | -0.5 .. +0.02 dB | 1 - 3.6 dB |
| tone 100 Hz / 125 Hz | -9.1 / -7.0 dB | 22 / 20 dB |
| space after phrases | measurable on 7 of 142 only | |

So a split vocal is a good reference for tone (200 Hz up), "s" and punch, not for its low end or its space.
Auto-Edit uses it that way (`ReferenceProfile::split`, `kSplitLowHz`).

Re-run: decode each `.stem.mp4` (mixture = stream 0, vocals = stream 4), split the mixtures with
`tools/audit/split_separate.py <dir>` (needs `pip install "audio-separator[cpu]"`), decode both to mono f64 48 kHz,
`split_study` each set, then `split_compare.py`.
