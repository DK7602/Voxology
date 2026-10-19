// "Songs with the same chords", independent of key: each chord becomes its root's distance from
// the key's tonic plus its quality, so G-C-D in G matches A-D-E in A.

import { mod12, parseKey, parseNoteAt } from "../shared/music/notes";

const SKIP = /^(N\.?C\.?$|notes?:)/i;

/** Spelling variants of the same quality collapse to one form ("min7" = "m7", "M7" = "maj7"). */
function normaliseQuality(q: string): string {
  const s = q.trim();
  if (s === "maj" || s === "M") return "";
  if (s === "Δ") return "maj7";
  return s
    .replace(/^(min|mi|-)/, "m")
    .replace(/^(M|Δ|ma)(?=\d)/, "maj")
    .replace(/^(°|o)/, "dim")
    .replace(/^\+/, "aug")
    .replace(/^sus$/, "sus4");
}

/** Chord names (absolute) -> relative chord set. Slash basses are ignored. */
export function relativeChordSet(key: string, chords: string[]): Set<string> {
  const parsedKey = parseKey(key);
  // Minor keys are measured from their relative major so Am-F-C-G in Am matches C-G-Am-F in C.
  let tonic = parsedKey ? (parsedKey.minor ? mod12(parsedKey.tonic + 3) : parsedKey.tonic) : null;
  const out = new Set<string>();
  for (const name of chords) {
    if (SKIP.test(name)) continue;
    const root = parseNoteAt(name);
    if (!root) continue;
    if (tonic === null) tonic = root.pc;
    const quality = normaliseQuality(name.slice(root.length).split("/")[0]);
    out.add(`${mod12(root.pc - tonic)}${quality}`);
  }
  return out;
}

export function jaccard(a: Set<string>, b: Set<string>): number {
  if (a.size === 0 || b.size === 0) return 0;
  let shared = 0;
  for (const x of a) if (b.has(x)) shared++;
  return shared / (a.size + b.size - shared);
}
