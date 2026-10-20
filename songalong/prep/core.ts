// The audio half of song preparation, shared by the cloud container (server.ts) and the PC
// helper (helper.ts): fetch a YouTube video's audio with yt-dlp, decode it with ffmpeg, find the
// beat / key / chords, and cut short pieces for the Worker's speech recognition (lyrics timing).
// The audio itself never leaves this machine and is deleted when done.

import { spawn } from "node:child_process";
import { mkdtemp, readdir, readFile, rm } from "node:fs/promises";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { detectChords } from "../src/analysis/detect";
import { keyPrefersFlats, parseKey } from "../shared/music/notes";
import { leadPart } from "./melody";
import { loadNoteModel } from "./noteModel";
import { transcribe } from "./notes";
import {
  CHUNK_SECONDS,
  MAX_SONG_SECONDS,
  type PrepAnalysis,
  PREP_ENGINE,
  type PrepChunk,
  type PrepErrorCode,
  type PrepMeta,
} from "../shared/prep";

export interface Tools {
  ytDlp: string;
  ffmpeg: string;
  /** extra directories for PATH (e.g. where deno lives: yt-dlp needs a JS runtime for YouTube) */
  extraPath?: string[];
}

export class PrepError extends Error {
  code: PrepErrorCode;
  constructor(code: PrepErrorCode, message: string) {
    super(message);
    this.code = code;
  }
}

export type Progress = (stage: string, fraction: number) => void;

const RATE = 22050;

function run(cmd: string, args: string[], tools: Tools): Promise<{ out: Buffer; err: string }> {
  return new Promise((resolve, reject) => {
    const sep = process.platform === "win32" ? ";" : ":";
    const env = { ...process.env, PATH: [...(tools.extraPath ?? []), process.env.PATH ?? ""].join(sep) };
    const p = spawn(cmd, args, { env, windowsHide: true });
    const out: Buffer[] = [];
    let err = "";
    p.stdout.on("data", (d: Buffer) => out.push(d));
    p.stderr.on("data", (d: Buffer) => {
      err += d.toString();
      if (err.length > 20000) err = err.slice(-20000);
    });
    p.on("error", (e) => reject(new PrepError("failed", `Couldn't start ${cmd}: ${e.message}`)));
    p.on("close", (code) => {
      if (code === 0) resolve({ out: Buffer.concat(out), err });
      else reject(Object.assign(new Error(err.trim().split("\n").slice(-3).join(" ") || `${cmd} exited with ${code}`), { stderr: err }));
    });
  });
}

/** Turns yt-dlp's complaint into a code the Worker can act on. */
export function classifyYtDlpError(stderr: string): { code: PrepErrorCode; message: string } {
  if (/confirm you.?re not a bot|sign in to confirm|HTTP Error 429|too many requests|blocked|captcha/i.test(stderr)) {
    return { code: "blocked", message: "YouTube refused this computer for now." };
  }
  if (/private video|video unavailable|has been removed|not available in your country|members-only|age-restricted|premieres in|live event will begin/i.test(stderr)) {
    return { code: "unavailable", message: "This video can't be used (it's private, removed, age-restricted or not available here)." };
  }
  return { code: "failed", message: "Couldn't get this song's audio. " + (stderr.trim().split("\n").filter((l) => /ERROR/.test(l)).slice(-1)[0] ?? "").slice(0, 200) };
}

/** Downloads the best audio-only stream into `dir`; returns the file path and the video's details. */
export async function fetchYouTubeAudio(videoId: string, dir: string, tools: Tools): Promise<{ file: string; meta: PrepMeta }> {
  const url = `https://www.youtube.com/watch?v=${videoId}`;
  let res;
  try {
    res = await run(
      tools.ytDlp,
      [
        "-f", "bestaudio[ext=m4a]/bestaudio/best",
        "--no-playlist", "--no-progress", "--no-warnings",
        "--match-filter", `duration <= ${MAX_SONG_SECONDS}`,
        "-o", join(dir, "audio.%(ext)s"),
        "-j", "--no-simulate",
        url,
      ],
      tools,
    );
  } catch (e) {
    const { code, message } = classifyYtDlpError(String((e as { stderr?: string }).stderr ?? (e as Error).message));
    throw new PrepError(code, message);
  }
  let info: Record<string, unknown> = {};
  try {
    info = JSON.parse(res.out.toString().trim().split("\n").filter(Boolean).at(-1) ?? "{}");
  } catch {
    /* keep going with what we know */
  }
  const files = (await readdir(dir)).filter((f) => f.startsWith("audio."));
  if (!files.length) {
    if (Number(info.duration) > MAX_SONG_SECONDS) throw new PrepError("unavailable", "This video is longer than 15 minutes.");
    throw new PrepError("failed", "The audio download didn't produce a file.");
  }
  const str = (k: string) => (typeof info[k] === "string" ? (info[k] as string) : undefined);
  const meta: PrepMeta = {
    title: str("title") ?? "",
    channel: str("channel") ?? str("uploader") ?? "",
    duration: Number(info.duration) || 0,
    artist: str("artist") ?? (Array.isArray(info.artists) ? String(info.artists[0]) : undefined),
    track: str("track"),
    album: str("album"),
  };
  return { file: join(dir, files[0]), meta };
}

/** Decodes any audio file to mono float samples at 22.05 kHz. */
export async function decode(file: string, tools: Tools): Promise<Float32Array> {
  try {
    const { out } = await run(tools.ffmpeg, ["-v", "error", "-i", file, "-f", "f32le", "-ac", "1", "-ar", String(RATE), "pipe:1"], tools);
    // copy into an aligned buffer (Buffer views may start at an odd offset)
    const aligned = new ArrayBuffer(out.byteLength - (out.byteLength % 4));
    new Uint8Array(aligned).set(out.subarray(0, aligned.byteLength));
    return new Float32Array(aligned);
  } catch (e) {
    throw new PrepError("failed", "Couldn't read the audio: " + (e as Error).message.slice(0, 200));
  }
}

/** Cuts the song into 30-second mono 16 kHz MP3 pieces (small enough for speech recognition). */
export async function speechChunks(file: string, dir: string, tools: Tools): Promise<PrepChunk[]> {
  const pattern = join(dir, "chunk_%03d.mp3");
  await run(
    tools.ffmpeg,
    ["-v", "error", "-i", file, "-vn", "-ac", "1", "-ar", "16000", "-b:a", "32k", "-f", "segment", "-segment_time", String(CHUNK_SECONDS), "-reset_timestamps", "1", pattern],
    tools,
  );
  const names = (await readdir(dir)).filter((f) => /^chunk_\d+\.mp3$/.test(f)).sort();
  const chunks: PrepChunk[] = [];
  for (const [i, name] of names.entries()) {
    const bytes = await readFile(join(dir, name));
    chunks.push({ start: i * CHUNK_SECONDS, mp3: bytes.toString("base64") });
  }
  return chunks;
}

/** Everything the Worker needs from a local audio file. */
export async function analyseFile(file: string, meta: PrepMeta, videoId: string, tools: Tools, progress: Progress, dir: string): Promise<PrepAnalysis> {
  progress("Reading the audio", 0.35);
  const samples = await decode(file, tools);
  if (samples.length < RATE * 5) throw new PrepError("unavailable", "This audio is too short to play along with.");
  const d = detectChords(samples, RATE, {
    onProgress: (f, stage) => progress(stage === "Done" ? "Working out the chords" : stage, 0.4 + 0.3 * f),
  });
  // the lead part: the notes heard (RATE is the note model's rate), their top line on the beats
  let lead: { beat: number; name: string }[] | undefined;
  const model = await loadNoteModel();
  if (model) {
    try {
      const notes = await transcribe(samples, model, (f) => progress("Listening for the lead", 0.7 + 0.17 * f));
      lead = leadPart(notes, d.beatTimes, keyPrefersFlats(parseKey(d.key)));
    } catch (e) {
      console.warn("lead part failed:", (e as Error).message);
    }
  }
  progress("Preparing the lyrics timing", 0.88);
  const chunks = await speechChunks(file, dir, tools);
  return {
    videoId,
    meta: { ...meta, duration: meta.duration || d.duration },
    detect: {
      bpm: d.bpm,
      beatsPerBar: d.beatsPerBar,
      beatTimes: d.beatTimes,
      key: d.key,
      chords: d.chords,
      ...(lead?.length ? { lead } : {}),
      length: d.length,
      duration: d.duration,
      confidence: d.confidence,
    },
    chunks,
    engine: PREP_ENGINE,
  };
}

/** The whole audio side for one YouTube video. */
export async function prepareVideo(videoId: string, tools: Tools, progress: Progress): Promise<PrepAnalysis> {
  const dir = await mkdtemp(join(tmpdir(), "songalong-"));
  try {
    progress("Getting the song", 0.05);
    const { file, meta } = await fetchYouTubeAudio(videoId, dir, tools);
    return await analyseFile(file, meta, videoId, tools, progress, dir);
  } finally {
    await rm(dir, { recursive: true, force: true }).catch(() => {});
  }
}

/** Same as prepareVideo but from a local file (tests, and songs you have on disk). */
export async function prepareLocalFile(path: string, meta: PrepMeta, tools: Tools, progress: Progress = () => {}): Promise<PrepAnalysis> {
  const dir = await mkdtemp(join(tmpdir(), "songalong-"));
  try {
    return await analyseFile(path, meta, "local", tools, progress, dir);
  } finally {
    await rm(dir, { recursive: true, force: true }).catch(() => {});
  }
}

/** Search YouTube with yt-dlp (used when no YouTube API key is set). */
export async function searchYouTube(query: string, tools: Tools, limit = 12) {
  const { out } = await run(tools.ytDlp, ["--flat-playlist", "-J", "--no-warnings", `ytsearch${limit}:${query}`], tools).catch((e) => {
    const { code, message } = classifyYtDlpError(String((e as { stderr?: string }).stderr ?? e));
    throw new PrepError(code, message);
  });
  const data = JSON.parse(out.toString()) as { entries?: Record<string, unknown>[] };
  return (data.entries ?? [])
    .filter((e) => typeof e.id === "string")
    .map((e) => ({
      videoId: e.id as string,
      title: String(e.title ?? ""),
      channel: String(e.channel ?? e.uploader ?? ""),
      duration: Number(e.duration) || 0,
      thumbnail: `https://i.ytimg.com/vi/${e.id}/hqdefault.jpg`,
    }));
}
