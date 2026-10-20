// Turning a prepared YouTube song (audio analysis from the cloud or a PC helper) into a playable
// SongAlong song: find the lyrics, put them on the video's clock, build the song, save it to the
// shared catalog so the next person gets it instantly.

import type { PrepAnalysis } from "../shared/prep";
import { cleanTitle, guessSongName, nameCandidates, tidyTitle, type SongName } from "../shared/songName";
import { Timeline } from "../shared/timeline";
import type { LyricLine, SongDoc } from "../shared/types";
import { distinctChords, validateSongDoc } from "../shared/validate";
import { alignPlain, alignSynced, alignVersion, listen, oneOffsetFits, timeWords, transcribeAll, type AlignedLyrics, type Segment, type SpeechAi } from "./align";
import { findLyrics } from "./lyrics";
import { encodeChords, newSongId } from "./songData";

export const CATALOG_OWNER = "system";

export function buildSongDoc(a: PrepAnalysis, name: SongName, lyrics: AlignedLyrics, credits: string): SongDoc {
  const d = a.detect;
  const doc: SongDoc = {
    v: 1,
    title: name.title || a.meta.title || "Untitled",
    artist: name.artist,
    genre: "",
    key: d.key,
    bpm: d.bpm,
    beatsPerBar: d.beatsPerBar,
    capo: 0,
    tuning: "standard",
    media: { kind: "youtube", videoId: a.videoId },
    offset: d.beatTimes[0] ?? 0,
    beatTimes: d.beatTimes,
    chords: d.chords,
    lines: [],
    length: d.length,
  };
  if (credits) doc.credits = credits;
  const tl = new Timeline(doc);
  const lines: LyricLine[] = [];
  const toBeat = (t: number) => Math.round(tl.timeToBeat(t) * 100) / 100;
  for (const l of lyrics.lines) {
    const beat = Math.round(tl.timeToBeat(l.time) * 4) / 4;
    const prev = lines.at(-1);
    if (!l.text.trim()) {
      // an empty timed line is an instrumental break: end the previous line there
      if (prev && beat > prev.beat) prev.end = beat;
      continue;
    }
    if (beat < -8 || beat > d.length + 8) continue;
    if (prev && Math.abs(prev.beat - beat) < 0.25) {
      prev.text = `${prev.text} / ${l.text}`.slice(0, 500);
      delete prev.words;
      delete prev.wordsEnd;
      continue;
    }
    const line: LyricLine = { beat, text: l.text.slice(0, 500) };
    if (l.words?.length && line.text === l.text) {
      line.words = l.words.map(toBeat);
      if (l.wordsEnd !== undefined) line.wordsEnd = toBeat(l.wordsEnd);
    }
    lines.push(line);
  }
  doc.lines = lines;
  return doc;
}

export type Progress = (stage: string, fraction: number) => void;

/** Video titles that say there is no singing. */
const INSTRUMENTAL_VIDEO = /\b(instrumental|karaoke|backing track|minus one|no vocals?|piano (cover|version)|guitar instrumental)\b/i;

/** Lyrics + timing for an analysed video. */
export async function lyricsFor(a: PrepAnalysis, ai: SpeechAi | undefined, progress: Progress, fetcher: typeof fetch = fetch) {
  let name = guessSongName(a.meta);
  progress("Finding the lyrics", 0.9);
  const videoDuration = a.meta.duration || a.detect.duration;
  let found: Awaited<ReturnType<typeof findLyrics>> = null;
  for (const candidate of nameCandidates(a.meta)) {
    found = await findLyrics(candidate, videoDuration, fetcher);
    if (found) {
      name = candidate;
      break;
    }
  }
  let lyrics: AlignedLyrics = { lines: [], timing: "none" };
  let lyricSource = "";
  // what speech recognition hears over the whole song, once (not needed for instrumentals)
  let heard: Segment[] | null = null;
  const hear = async () => {
    if (!heard) {
      progress("Listening to the singing", 0.92);
      heard = await listen(a.chunks, ai);
    }
    return heard;
  };
  if (found) {
    const artist = found.artist || name.artist;
    name = { artist, title: tidyTitle(cleanTitle(found.title) || name.title, artist) };
    if (found.instrumental) lyricSource = "instrumental";
    else if (found.synced.length) {
      const segs = await hear();
      progress("Lining up the lyrics with the video", 0.96);
      // the same recording (as long, or one offset fits what is heard): one offset;
      // another version (a cover, a live take): follow the singing
      const sameLength = !found.duration || !videoDuration || Math.abs(found.duration - videoDuration) <= 12;
      lyrics =
        sameLength || oneOffsetFits(found.synced, segs)
          ? alignSynced(found.synced, segs)
          : alignVersion(found.synced, segs, found.duration, videoDuration);
      lyricSource = "LRCLIB";
    } else if (found.plain.length) {
      const segs = await hear();
      progress("Lining up the lyrics with the video", 0.96);
      lyrics = alignPlain(found.plain, segs);
      lyricSource = lyrics.lines.length ? "LRCLIB" : "";
    }
  }
  if (!lyrics.lines.length && lyricSource !== "instrumental" && ai) {
    lyrics = transcribeAll(await hear());
    if (lyrics.lines.length) lyricSource = "speech recognition (may have mistakes)";
  }
  // each word where it is sung (when the lines were found in what is heard: then it is singing)
  if (heard && lyrics.lines.length && lyrics.timing !== "synced") lyrics = { ...lyrics, lines: timeWords(lyrics.lines, heard) };
  // no singing to follow, so the lyric timing is a guess for this video, and the song says so:
  // an instrumental by its title (speech recognition can "hear" the words in a known tune), or
  // listened to without any line being heard
  const timedLyrics = lyricSource === "LRCLIB" && lyrics.lines.length > 0;
  const estimate = !timedLyrics
    ? ""
    : INSTRUMENTAL_VIDEO.test(a.meta.title)
      ? "timing estimated (instrumental version)"
      : heard !== null && lyrics.timing === "synced"
        ? "timing estimated (no singing heard)"
        : "";
  const credits = ["Chords found automatically", lyricSource && lyricSource !== "instrumental" ? `Lyrics: ${lyricSource}` : "", estimate]
    .filter(Boolean)
    .join(" · ");
  return { name, lyrics, credits };
}

/** Saves (or refreshes) the catalog song for a video and marks the job ready. Returns the song id. */
export async function saveCatalogSong(db: D1Database, videoId: string, doc: SongDoc, engine = 1): Promise<string> {
  const clean = validateSongDoc(doc);
  const existing = await db.prepare("SELECT song_id FROM yt_songs WHERE video_id = ?").bind(videoId).first<{ song_id: string | null }>();
  const id = existing?.song_id ?? newSongId();
  const now = Date.now();
  const chords = encodeChords(distinctChords(clean));
  await db.batch([
    db
      .prepare(
        `INSERT INTO songs (id, owner_id, title, artist, genre, song_key, bpm, chords, is_public, data, created_at, updated_at)
         VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, 1, ?9, ?10, ?10)
         ON CONFLICT(id) DO UPDATE SET title = ?3, artist = ?4, song_key = ?6, bpm = ?7, chords = ?8, data = ?9, updated_at = ?10`,
      )
      .bind(id, CATALOG_OWNER, clean.title, clean.artist, clean.genre, clean.key, clean.bpm, chords, JSON.stringify(clean), now),
    db
      .prepare("UPDATE yt_songs SET status = 'ready', song_id = ?, stage = 'Ready', progress = 1, error = '', engine = ?, updated_at = ? WHERE video_id = ?")
      .bind(id, engine, now, videoId),
  ]);
  return id;
}

/** The Worker's half of preparing a song, after the audio work is done. */
export async function finishPrep(
  db: D1Database,
  ai: SpeechAi | undefined,
  a: PrepAnalysis,
  progress: Progress,
  fetcher: typeof fetch = fetch,
): Promise<string> {
  const { name, lyrics, credits } = await lyricsFor(a, ai, progress, fetcher);
  progress("Saving", 0.98);
  return saveCatalogSong(db, a.videoId, buildSongDoc(a, name, lyrics, credits), a.engine ?? 1);
}
