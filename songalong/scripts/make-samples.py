#!/usr/bin/env python3
"""Builds the instrument samples the player strums with (public/samples, src/player/sampleMap.ts).

Sources (see public/samples/CREDITS.txt):
  guitar   steel-string acoustic guitar from the Musyng Kite soundfont, as rendered note by note
           in gleitz/midi-js-soundfonts (CC BY-SA 3.0); the mandolin plays these too
  ukulele  FreePats tenor ukulele (CC0)
  bass     FreePats Finger Bass YR (CC0)

Each note is trimmed to start at its attack, cut to a few seconds with a fade, brought to one
loudness (so a strum is even across the strings) and saved as mono MP3.

Usage: python3 scripts/make-samples.py <download-cache-dir>   (needs ffmpeg, numpy, py7zr)
"""

import json
import re
import subprocess
import sys
import urllib.request
from pathlib import Path

import numpy as np

ROOT = Path(__file__).resolve().parent.parent
OUT = ROOT / "public" / "samples"
MAP = ROOT / "src" / "player" / "sampleMap.ts"
RATE = 44100

GLEITZ = "https://raw.githubusercontent.com/gleitz/midi-js-soundfonts/gh-pages/MusyngKite/acoustic_guitar_steel-mp3/"
UKULELE = "https://github.com/freepats/ukulele1/releases/download/2026-08-11/Ukulele-SFZ+FLAC-20260811.7z"
BASS = "https://github.com/freepats/electric-bass-YR/releases/download/2019-09-30/FingerBassYR-SFZ+FLAC-20190930.7z"

FLATS = ["C", "Db", "D", "Eb", "E", "F", "Gb", "G", "Ab", "A", "Bb", "B"]
SHARPS = ["C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"]

# seconds kept per note, and the loudness each note is brought to (RMS of its first 0.4 s)
KEEP = {"guitar": 3.5, "ukulele": 2.5, "bass": 3.0}
FADE = 0.25
TARGET_RMS = 10 ** (-20 / 20)
PEAK = 10 ** (-1 / 20)


def name(midi: int, names=FLATS) -> str:
    return f"{names[midi % 12]}{midi // 12 - 1}"


def fetch(url: str, dest: Path) -> Path:
    if not dest.exists():
        dest.parent.mkdir(parents=True, exist_ok=True)
        with urllib.request.urlopen(url) as r:
            dest.write_bytes(r.read())
    return dest


def unpack(archive: Path) -> Path:
    import py7zr

    out = archive.with_suffix("")
    if not out.exists():
        with py7zr.SevenZipFile(archive) as z:
            try:
                z.extractall(out)
            except Exception as e:  # an odd file (a document) py7zr can't unpack: the audio is out
                print(f"{archive.name}: {e}")
    return out


def sfz_regions(sfz: Path):
    """(key centre, tune in cents, sample path) for each pitched region of a simple SFZ."""
    text = sfz.read_text()
    out = []
    for block in text.split("<region>")[1:]:
        f = dict(re.findall(r"(\w+)=(\S+)", block))
        centre = int(f.get("pitch_keycenter", f.get("key", -1)))
        # notes only: not the ukulele's body tap and chuck
        if centre < 0 or not re.fullmatch(r"[A-G]#?\d?\.(flac|wav)", Path(f.get("sample", "")).name):
            continue
        out.append((centre, int(f.get("tune", 0)), sfz.parent / f["sample"]))
    return out


def decode(path: Path) -> np.ndarray:
    raw = subprocess.run(
        ["ffmpeg", "-v", "error", "-i", str(path), "-ac", "1", "-ar", str(RATE), "-f", "f32le", "-"],
        check=True,
        capture_output=True,
    ).stdout
    return np.frombuffer(raw, dtype=np.float32).copy()


def shape(x: np.ndarray, keep: float) -> np.ndarray:
    peak = np.abs(x).max() or 1.0
    onset = int(np.argmax(np.abs(x) > 0.02 * peak))
    x = x[max(0, onset - int(0.003 * RATE)) :]
    x = x[: int(keep * RATE)]
    fade = min(len(x), int(FADE * RATE))
    x[-fade:] *= np.linspace(1, 0, fade) ** 2
    rms = np.sqrt(np.mean(x[: int(0.4 * RATE)] ** 2)) or 1.0
    x = x * (TARGET_RMS / rms)
    return x * min(1.0, PEAK / (np.abs(x).max() or 1.0))


def off_by(x: np.ndarray, midi: int) -> float:
    """How many cents the note sounds off its pitch once it settles (a plucked string starts a
    little sharp): autocorrelation at the expected period (several, for high notes), interpolated."""
    want = 440 * 2 ** ((midi - 69) / 12)
    seg = x[int(0.25 * RATE) : int(1.25 * RATE)].astype(np.float64)
    seg = (seg - seg.mean()) * np.hanning(len(seg))
    n = 1 << (2 * len(seg) - 1).bit_length()
    spec = np.fft.rfft(seg, n)
    ac = np.fft.irfft(spec * np.conj(spec))[: len(seg)]
    period = RATE / want
    k = max(1, int(np.ceil(400 / period)))
    lo, hi = int(k * period * 2 ** (-1 / 24)), int(k * period * 2 ** (1 / 24)) + 1
    lag = lo + int(np.argmax(ac[lo:hi]))
    a, b, c = ac[lag - 1], ac[lag], ac[lag + 1]
    return 1200 * np.log2((k * RATE / (lag + 0.5 * (a - c) / (a - 2 * b + c))) / want)


def encode(x: np.ndarray, dest: Path):
    dest.parent.mkdir(parents=True, exist_ok=True)
    subprocess.run(
        ["ffmpeg", "-v", "error", "-y", "-f", "f32le", "-ar", str(RATE), "-ac", "1", "-i", "-", "-c:a", "libmp3lame", "-q:a", "4", str(dest)],
        input=x.astype(np.float32).tobytes(),
        check=True,
    )


def main(cache: Path):
    bank = {}

    # guitar: every second semitone from the low E string up past the 17th fret of the high one
    notes = []
    for midi in range(40, 97, 2):
        src = fetch(GLEITZ + name(midi) + ".mp3", cache / "guitar" / f"{name(midi)}.mp3")
        x = shape(decode(src), KEEP["guitar"])
        file = f"{name(midi, SHARPS).replace('#', 's')}.mp3"
        encode(x, OUT / "guitar" / file)
        notes.append({"midi": midi, "file": file, "cents": round(-off_by(x, midi))})
    bank["guitar"] = notes

    for inst, url in (("ukulele", UKULELE), ("bass", BASS)):
        folder = unpack(fetch(url, cache / Path(url).name))
        sfz = next(folder.rglob("*.sfz"))
        notes = []
        for centre, _tune, sample in sfz_regions(sfz):
            if not sample.exists():
                continue
            x = shape(decode(sample), KEEP[inst])
            file = f"{name(centre, SHARPS).replace('#', 's')}.mp3"
            encode(x, OUT / inst / file)
            # measured like the guitar's (the sound bank's own tune= agrees within a few cents)
            notes.append({"midi": centre, "file": file, "cents": round(-off_by(x, centre))})
        bank[inst] = sorted(notes, key=lambda n: n["midi"])

    lines = [
        "// Generated by scripts/make-samples.py: the recorded notes in public/samples, per instrument.",
        "// `cents` corrects a recording that is slightly out of tune (measured; added when played).",
        "",
        "export interface SampleNote {",
        "  midi: number;",
        "  file: string;",
        "  cents: number;",
        "}",
        "",
        "export const SAMPLES: Record<\"guitar\" | \"ukulele\" | \"bass\", SampleNote[]> = "
        + json.dumps(bank, indent=2).replace('"midi"', "midi").replace('"file"', "file").replace('"cents"', "cents")
        + ";",
        "",
    ]
    MAP.write_text("\n".join(lines))
    total = sum(p.stat().st_size for p in OUT.rglob("*.mp3"))
    print({k: len(v) for k, v in bank.items()}, f"{total / 1024:.0f} KiB")


if __name__ == "__main__":
    main(Path(sys.argv[1]))
