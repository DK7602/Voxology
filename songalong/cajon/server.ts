// The cajon service inside its Cloudflare container (4 cores). Only the SongAlong Worker can reach it.
//   POST /cajon { videoId } -> NDJSON CajonEvent stream (progress..., then result or error)
//   GET  /health            -> ok
// It fetches the song's audio, pulls the drums out (separate.py, Demucs) and finds the kick,
// snare and hi-hat hits (drums.ts). The audio never leaves the container and is deleted after.

import { spawn } from "node:child_process";
import { mkdtemp, readFile, rm } from "node:fs/promises";
import { createServer, type ServerResponse } from "node:http";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { isVideoId, type CajonEvent } from "../shared/prep";
import { decode, fetchYouTubeAudio, PrepError, type Tools } from "../prep/core";
import { drumOnsets } from "./drums";

const tools: Tools = {
  ytDlp: process.env.YTDLP ?? "yt-dlp",
  ffmpeg: process.env.FFMPEG ?? "ffmpeg",
  extraPath: ["/usr/local/bin"],
};
const PYTHON = process.env.PYTHON ?? "python";
const SEPARATE = process.env.SEPARATE ?? "/app/separate.py";
const PORT = Number(process.env.PORT ?? 8080);
/** separating a 15-minute song takes about 6 minutes on 4 cores; anything far past that is stuck */
const SEPARATE_LIMIT_MS = 12 * 60_000;

// YouTube changes often and yt-dlp follows within days: update it when the container starts.
spawn(tools.ytDlp, ["-U"], { stdio: "ignore" }).on("error", () => {});

function run(cmd: string, args: string[], limitMs: number, signal: AbortSignal): Promise<void> {
  return new Promise((resolve, reject) => {
    if (signal.aborted) return reject(signal.reason);
    const p = spawn(cmd, args, { stdio: ["ignore", "ignore", "pipe"] });
    let err = "";
    p.stderr.on("data", (d: Buffer) => {
      err = (err + d.toString()).slice(-4000);
    });
    const stop = () => p.kill("SIGKILL");
    const timer = setTimeout(stop, limitMs);
    signal.addEventListener("abort", stop);
    const done = () => {
      clearTimeout(timer);
      signal.removeEventListener("abort", stop);
    };
    p.on("error", (e) => {
      done();
      reject(new Error(`Couldn't start ${cmd}: ${e.message}`));
    });
    p.on("close", (code, killedBy) => {
      done();
      if (code === 0) resolve();
      else if (signal.aborted) reject(signal.reason);
      else reject(new Error(err.trim().split("\n").slice(-2).join(" ") || `${cmd} stopped (${code ?? killedBy})`));
    });
  });
}

async function cajon(videoId: string, send: (e: CajonEvent) => void, signal: AbortSignal) {
  const dir = await mkdtemp(join(tmpdir(), "cajon-"));
  try {
    send({ type: "progress", stage: "Getting the song" });
    const { file } = await fetchYouTubeAudio(videoId, dir, tools);
    const song = join(dir, "song.wav");
    const drums = join(dir, "drums.wav");
    await run(tools.ffmpeg, ["-v", "error", "-y", "-i", file, "-ac", "2", "-ar", "44100", song], 5 * 60_000, signal);
    send({ type: "progress", stage: "Listening for the drums" });
    await run(PYTHON, [SEPARATE, song, drums], SEPARATE_LIMIT_MS, signal);
    send({ type: "progress", stage: "Finding the hits" });
    const { level } = JSON.parse(await readFile(drums + ".json", "utf8")) as { level: number };
    send({ type: "result", onsets: drumOnsets(await decode(drums, tools)), level });
  } finally {
    await rm(dir, { recursive: true, force: true }).catch(() => {});
  }
}

/**
 * The job running now. One at a time: the cajon queue sends one job at a time, so a new one
 * means whoever asked for the last is gone (the Worker restarted mid-job, in a deploy), and the
 * last one stops (two separations at once are slow and can run out of memory). A job also stops
 * when its caller hangs up.
 */
let current: AbortController | null = null;

const server = createServer(async (req, res: ServerResponse) => {
  const url = new URL(req.url ?? "/", "http://container");
  if (req.method === "GET" && url.pathname === "/health") {
    res.writeHead(200, { "Content-Type": "text/plain" }).end(`ok ${current ? 1 : 0}`);
    return;
  }
  if (req.method !== "POST" || url.pathname !== "/cajon") {
    res.writeHead(404).end();
    return;
  }
  let body = "";
  for await (const chunk of req) body += chunk;
  let videoId: unknown;
  try {
    videoId = (JSON.parse(body || "{}") as { videoId?: unknown }).videoId;
  } catch {
    /* answered below */
  }
  res.writeHead(200, { "Content-Type": "application/x-ndjson", "Cache-Control": "no-store" });
  const send = (e: CajonEvent) => res.write(JSON.stringify(e) + "\n");
  if (!isVideoId(videoId)) {
    send({ type: "error", code: "failed", message: "Bad video id" });
    res.end();
    return;
  }
  current?.abort(new Error("Stopped: another song started"));
  const job = new AbortController();
  current = job;
  res.on("close", () => {
    if (!res.writableFinished) job.abort(new Error("Stopped: the caller went away"));
  });
  // a line every 20 s, so the Worker knows the work is still going
  const beat = setInterval(() => send({ type: "progress", stage: "Working" }), 20_000);
  try {
    await cajon(videoId, send, job.signal);
  } catch (e) {
    const code = e instanceof PrepError ? e.code : "failed";
    send({ type: "error", code, message: (e as Error).message.slice(0, 300) });
  } finally {
    clearInterval(beat);
    if (current === job) current = null;
    res.end();
  }
});

server.listen(PORT, () => console.log(`cajon service listening on ${PORT}`));
