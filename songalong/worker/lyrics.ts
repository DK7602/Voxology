// Lyrics for prepared songs, from LRCLIB (free community database of timed lyrics). Musixmatch can
// replace it later behind the same findLyrics() call.

import type { SongName } from "../shared/songName";

export interface TimedLine {
  /** seconds from the start of the recording the lyrics were timed to */
  time: number;
  text: string;
}

export interface FoundLyrics {
  source: "lrclib";
  artist: string;
  title: string;
  /** seconds; the length of the recording the timing belongs to */
  duration: number;
  /** timed lines (empty when only plain lyrics exist) */
  synced: TimedLine[];
  /** plain lines (always filled when lyrics were found) */
  plain: string[];
  instrumental: boolean;
}

interface LrclibTrack {
  id: number;
  trackName: string;
  artistName: string;
  albumName?: string;
  duration: number;
  instrumental: boolean;
  plainLyrics: string | null;
  syncedLyrics: string | null;
}

const BASE = "https://lrclib.net/api";
const HEADERS = { "User-Agent": "SongAlong (https://github.com/DK7602/voxology)" };

/** Parses LRC: "[01:02.34] words". Lines with several stamps repeat; metadata tags are skipped. */
export function parseLrc(lrc: string): TimedLine[] {
  const out: TimedLine[] = [];
  for (const raw of lrc.split(/\r?\n/)) {
    const stamps = [...raw.matchAll(/\[(\d{1,3}):(\d{1,2}(?:[.:]\d{1,3})?)\]/g)];
    if (!stamps.length) continue;
    const text = raw.replace(/\[[^\]]*\]/g, "").trim();
    for (const s of stamps) {
      const sec = parseInt(s[1], 10) * 60 + parseFloat(s[2].replace(":", "."));
      if (Number.isFinite(sec)) out.push({ time: sec, text });
    }
  }
  return out.sort((a, b) => a.time - b.time);
}

function toFound(t: LrclibTrack): FoundLyrics {
  const synced = t.syncedLyrics ? parseLrc(t.syncedLyrics) : [];
  const plain = (t.plainLyrics ?? synced.map((l) => l.text).join("\n"))
    .split(/\r?\n/)
    .map((l) => l.trim());
  return {
    source: "lrclib",
    artist: t.artistName,
    title: t.trackName,
    duration: t.duration,
    synced,
    plain: trimBlank(plain),
    instrumental: t.instrumental,
  };
}

function trimBlank(lines: string[]): string[] {
  let a = 0;
  let b = lines.length;
  while (a < b && !lines[a]) a++;
  while (b > a && !lines[b - 1]) b--;
  return lines.slice(a, b);
}

async function getJson<T>(url: string, fetcher: typeof fetch): Promise<T | null> {
  try {
    const res = await fetcher(url, { headers: HEADERS });
    if (!res.ok) return null;
    return (await res.json()) as T;
  } catch {
    return null;
  }
}

/** Best match for a song: exact lookup by artist + title + duration, then searches. Prefers
 *  timed lyrics and the recording closest in length to the video. */
export async function findLyrics(name: SongName, videoDuration: number, fetcher: typeof fetch = fetch): Promise<FoundLyrics | null> {
  const q = (p: Record<string, string>) => new URLSearchParams(p).toString();
  if (name.artist && name.title && videoDuration > 0) {
    const exact = await getJson<LrclibTrack>(
      `${BASE}/get?${q({ artist_name: name.artist, track_name: name.title, duration: String(Math.round(videoDuration)) })}`,
      fetcher,
    );
    if (exact && (exact.syncedLyrics || exact.plainLyrics || exact.instrumental)) return toFound(exact);
  }
  const searches = [
    name.artist ? `${BASE}/search?${q({ track_name: name.title, artist_name: name.artist })}` : null,
    `${BASE}/search?${q({ q: `${name.artist} ${name.title}`.trim() })}`,
  ].filter((u): u is string => !!u);
  for (const url of searches) {
    const list = await getJson<LrclibTrack[]>(url, fetcher);
    if (!list?.length) continue;
    const best = pickBest(list, videoDuration);
    if (best) return toFound(best);
  }
  return null;
}

function pickBest(list: LrclibTrack[], videoDuration: number): LrclibTrack | null {
  const scored = list
    .filter((t) => t.syncedLyrics || t.plainLyrics)
    .map((t) => {
      const lenDiff = videoDuration > 0 ? Math.abs(t.duration - videoDuration) : 0;
      // music videos often run longer than the record (intros, skits): allow some slack
      const score = (t.syncedLyrics ? 0 : 30) + Math.min(lenDiff, 120);
      return { t, score, lenDiff };
    })
    .filter((x) => videoDuration <= 0 || x.lenDiff < 90)
    .sort((a, b) => a.score - b.score);
  return scored[0]?.t ?? null;
}
