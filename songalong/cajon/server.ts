// The cajon service inside its Cloudflare container (4 cores). Only the SongAlong Worker can reach it.
//   POST /cajon { videoId } -> NDJSON CajonEvent stream (progress..., then result or error)
//   GET  /health            -> ok
// It fetches the song's audio, pulls the drums out (separate.py, Demucs) and finds the kick,
// snare and hi-hat hits (drums.ts). The audio never leaves the container and is deleted after.

import { spawn } from "node:child_process";
import { mkdtemp, rm } from "node:fs/promises";
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

function run(cmd: string, args: string[], limitMs: number): Promise<void> {
  return new Promise((resolve, reject) => {
    const p = spawn(cmd, args, { stdio: ["ignore", "ignore", "pipe"] });
    let err = "";
    p.stderr.on("data", (d: Buffer) => {
      err = (err + d.toString()).slice(-4000);
    });
    const timer = setTimeout(() => p.kill("SIGKILL"), limitMs);
    p.on("error", (e) => {
      clearTimeout(timer);
      reject(new Error(`Couldn't start ${cmd}: ${e.message}`));
    });
    p.on("close", (code) => {
      clearTimeout(timer);
      if (code === 0) resolve();
      else reject(new Error(err.trim().split("\n").slice(-2).join(" ") || `${cmd} stopped (${code})`));
    });
  });
}

async function cajon(videoId: string, send: (e: CajonEvent) => void) {
  const dir = await mkdtemp(join(tmpdir(), "cajon-"));
  try {
    send({ type: "progress", stage: "Getting the song" });
    const { file } = await fetchYouTubeAudio(videoId, dir, tools);
    const song = join(dir, "song.wav");
    const drums = join(dir, "drums.wav");
    await run(tools.ffmpeg, ["-v", "error", "-y", "-i", file, "-ac", "2", "-ar", "44100", song], 5 * 60_000);
    send({ type: "progress", stage: "Listening for the drums" });
    await run(PYTHON, [SEPARATE, song, drums], SEPARATE_LIMIT_MS);
    send({ type: "progress", stage: "Finding the hits" });
    send({ type: "result", onsets: drumOnsets(await decode(drums, tools)) });
  } finally {
    await rm(dir, { recursive: true, force: true }).catch(() => {});
  }
}

let busy = 0;

const server = createServer(async (req, res: ServerResponse) => {
  const url = new URL(req.url ?? "/", "http://container");
  if (req.method === "GET" && url.pathname === "/health") {
    res.writeHead(200, { "Content-Type": "text/plain" }).end(`ok ${busy}`);
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
  // a line every 20 s, so the Worker knows the work is still going
  const beat = setInterval(() => send({ type: "progress", stage: "Working" }), 20_000);
  busy++;
  try {
    await cajon(videoId, send);
  } catch (e) {
    const code = e instanceof PrepError ? e.code : "failed";
    send({ type: "error", code, message: (e as Error).message.slice(0, 300) });
  } finally {
    clearInterval(beat);
    busy--;
    res.end();
  }
});

server.listen(PORT, () => console.log(`cajon service listening on ${PORT}`));
