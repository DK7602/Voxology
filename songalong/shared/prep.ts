// Song preparation ("search a song, get chords + lyrics in time"): what the prep step (the cloud
// container or the user's own PC helper) sends back to the Worker, and the progress events in
// between. The prep step only does the audio work; the Worker adds lyrics and saves the song.

export interface PrepMeta {
  title: string;
  channel: string;
  /** seconds */
  duration: number;
  /** from YouTube Music metadata, when the video has it */
  artist?: string;
  track?: string;
  album?: string;
}

export interface PrepDetect {
  bpm: number;
  beatsPerBar: number;
  /** seconds; beatTimes[0] is a downbeat */
  beatTimes: number[];
  key: string;
  chords: { beat: number; name: string }[];
  length: number;
  duration: number;
  confidence: number;
}

/** A short piece of the song (mono 16 kHz MP3, base64) for speech recognition of the vocals. */
export interface PrepChunk {
  /** seconds into the song */
  start: number;
  mp3: string;
}

/** Version of the beat / chord detection. Bump it when detection changes, so songs made by an
 *  older version can be told apart (and redone). 1: first release; 2: bar-level tempo scoring;
 *  3: rival tempos settled by where the chords change; 4: power chords. */
export const PREP_ENGINE = 4;

export interface PrepAnalysis {
  videoId: string;
  meta: PrepMeta;
  detect: PrepDetect;
  chunks: PrepChunk[];
  /** PREP_ENGINE of the code that made it (absent from version 1) */
  engine?: number;
}

export type PrepErrorCode =
  /** YouTube refused this computer (bot check): try another prep location */
  | "blocked"
  /** the video itself can't be used (private, removed, region-locked, too long) */
  | "unavailable"
  /** anything else */
  | "failed";

/** One line of the NDJSON stream from the prep step (and from /api/youtube/prepare). */
export type PrepEvent =
  | { type: "progress"; stage: string; fraction: number }
  | { type: "result"; analysis: PrepAnalysis }
  | { type: "error"; code: PrepErrorCode; message: string };

/** What /api/youtube/prepare streams to the app. */
export type PrepareEvent =
  | { type: "progress"; stage: string; fraction: number }
  | { type: "ready"; songId: string }
  | { type: "waiting_pc"; message: string }
  | { type: "error"; message: string };

export const MAX_SONG_SECONDS = 15 * 60;
export const CHUNK_SECONDS = 30;

export function isVideoId(s: unknown): s is string {
  return typeof s === "string" && /^[A-Za-z0-9_-]{11}$/.test(s);
}

export interface YouTubeResult {
  videoId: string;
  title: string;
  channel: string;
  /** seconds, 0 when unknown */
  duration: number;
  thumbnail: string;
  /** set when the song has been prepared before */
  songId?: string;
  status?: "ready" | "working" | "waiting_pc" | "failed";
}

export interface PrepStatus {
  videoId: string;
  status: "none" | "working" | "waiting_pc" | "ready" | "failed";
  stage: string;
  progress: number;
  songId: string | null;
  error: string;
  /** a PC helper of this user checked in within the last minute */
  helperOnline: boolean;
}
