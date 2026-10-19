// SongAlong PC helper: prepares songs on your own computer when the cloud can't (YouTube often
// refuses cloud servers but not home connections). Leave it running; it checks in with your
// SongAlong site every few seconds and prepares songs you asked for.
//
//   node songalong-helper.mjs --site https://your-songalong-site --token sah_...
//
// It uses yt-dlp, ffmpeg and deno. If Sample Snagger is installed its copies are reused; anything
// missing is downloaded once into the SongAlong tools folder.

import { spawn } from "node:child_process";
import { createWriteStream, existsSync } from "node:fs";
import { chmod, mkdir, readFile, rename, rm, writeFile } from "node:fs/promises";
import { homedir, platform, arch } from "node:os";
import { dirname, join } from "node:path";
import { pipeline } from "node:stream/promises";
import { Readable } from "node:stream";
import { createGunzip, inflateRawSync } from "node:zlib";
import type { PrepAnalysis } from "../shared/prep";
import { PrepError, prepareVideo, type Tools } from "./core";

const VERSION = "1.0.0";
const isWin = platform() === "win32";
const isMac = platform() === "darwin";
const exe = (name: string) => (isWin ? name + ".exe" : name);

function arg(name: string): string | undefined {
  const i = process.argv.indexOf("--" + name);
  return i > 0 ? process.argv[i + 1] : undefined;
}

const site = (arg("site") ?? process.env.SONGALONG_SITE ?? "").replace(/\/+$/, "");
const token = arg("token") ?? process.env.SONGALONG_TOKEN ?? "";

function say(msg: string) {
  const t = new Date().toLocaleTimeString();
  console.log(`[${t}] ${msg}`);
}

// ---- tools ----

function toolsDir(): string {
  if (isWin) return join(process.env.LOCALAPPDATA ?? join(homedir(), "AppData", "Local"), "SongAlong", "tools");
  if (isMac) return join(homedir(), "Library", "Application Support", "SongAlong", "tools");
  return join(homedir(), ".songalong", "tools");
}

function sampleSnaggerTools(): string {
  if (isWin) return join(process.env.APPDATA ?? join(homedir(), "AppData", "Roaming"), "Sample Snagger", "Tools");
  if (isMac) return join(homedir(), "Library", "Application Support", "Sample Snagger", "Tools");
  return join(homedir(), ".config", "Sample Snagger", "Tools");
}

function onPath(name: string): Promise<boolean> {
  return new Promise((resolve) => {
    const p = spawn(name, ["--version"], { stdio: "ignore", windowsHide: true });
    p.on("error", () => resolve(false));
    p.on("close", (code) => resolve(code === 0));
  });
}

async function download(url: string, dest: string) {
  const res = await fetch(url, { redirect: "follow" });
  if (!res.ok || !res.body) throw new Error(`download failed (${res.status}): ${url}`);
  await mkdir(dirname(dest), { recursive: true });
  const tmp = dest + ".part";
  await pipeline(Readable.fromWeb(res.body as never), createWriteStream(tmp));
  await rename(tmp, dest);
}

/** Finds a tool in our folder, Sample Snagger's, or PATH; downloads it if missing. */
async function ensureTool(name: "yt-dlp" | "ffmpeg" | "deno", ours: string): Promise<string> {
  const own = join(ours, exe(name));
  if (existsSync(own)) return own;
  const snagger = join(sampleSnaggerTools(), exe(name));
  if (existsSync(snagger)) return snagger;
  if (await onPath(name)) return name;

  say(`Downloading ${name} (one time)...`);
  const a = arch() === "arm64" ? "arm64" : "x64";
  if (name === "yt-dlp") {
    const asset = isWin ? "yt-dlp.exe" : isMac ? "yt-dlp_macos" : a === "arm64" ? "yt-dlp_linux_aarch64" : "yt-dlp_linux";
    await download(`https://github.com/yt-dlp/yt-dlp/releases/latest/download/${asset}`, own);
  } else if (name === "ffmpeg") {
    const asset = isWin ? "ffmpeg-win32-x64.gz" : isMac ? `ffmpeg-darwin-${a}.gz` : `ffmpeg-linux-${a}.gz`;
    const gz = own + ".gz";
    await download(`https://github.com/eugeneware/ffmpeg-static/releases/download/b6.1.1/${asset}`, gz);
    await pipeline((await import("node:fs")).createReadStream(gz), createGunzip(), createWriteStream(own));
    await rm(gz, { force: true });
  } else {
    const asset = isWin
      ? "deno-x86_64-pc-windows-msvc.zip"
      : isMac
        ? a === "arm64" ? "deno-aarch64-apple-darwin.zip" : "deno-x86_64-apple-darwin.zip"
        : a === "arm64" ? "deno-aarch64-unknown-linux-gnu.zip" : "deno-x86_64-unknown-linux-gnu.zip";
    const zip = own + ".zip";
    await download(`https://github.com/denoland/deno/releases/latest/download/${asset}`, zip);
    await writeFile(own, unzipEntry(await readFile(zip), exe("deno")));
    await rm(zip, { force: true });
  }
  if (!isWin) await chmod(own, 0o755);
  return own;
}

/** Extracts one file from a zip (stored or deflated) — enough for the deno download. */
function unzipEntry(zip: Buffer, fileName: string): Buffer {
  const eocd = zip.lastIndexOf(Buffer.from([0x50, 0x4b, 0x05, 0x06]));
  if (eocd < 0) throw new Error("not a zip file");
  const count = zip.readUInt16LE(eocd + 10);
  let p = zip.readUInt32LE(eocd + 16);
  for (let i = 0; i < count; i++) {
    const method = zip.readUInt16LE(p + 10);
    const compSize = zip.readUInt32LE(p + 20);
    const nameLen = zip.readUInt16LE(p + 28);
    const extraLen = zip.readUInt16LE(p + 30);
    const commentLen = zip.readUInt16LE(p + 32);
    const local = zip.readUInt32LE(p + 42);
    const name = zip.subarray(p + 46, p + 46 + nameLen).toString();
    if (name.split("/").pop() === fileName) {
      const lNameLen = zip.readUInt16LE(local + 26);
      const lExtraLen = zip.readUInt16LE(local + 28);
      const data = zip.subarray(local + 30 + lNameLen + lExtraLen, local + 30 + lNameLen + lExtraLen + compSize);
      return method === 0 ? Buffer.from(data) : inflateRawSync(data);
    }
    p += 46 + nameLen + extraLen + commentLen;
  }
  throw new Error(`${fileName} not found in zip`);
}

async function setupTools(): Promise<Tools> {
  const dir = toolsDir();
  await mkdir(dir, { recursive: true });
  const ytDlp = await ensureTool("yt-dlp", dir);
  const ffmpeg = await ensureTool("ffmpeg", dir);
  const deno = await ensureTool("deno", dir);
  // keep yt-dlp current: YouTube changes often
  if (ytDlp.startsWith(dir)) spawn(ytDlp, ["-U"], { stdio: "ignore", windowsHide: true }).on("error", () => {});
  const extraPath = [deno === "deno" ? "" : dirname(deno), dirname(ytDlp)].filter(Boolean);
  return { ytDlp, ffmpeg, extraPath };
}

// ---- talking to SongAlong ----

async function api(path: string, init: RequestInit = {}): Promise<Response> {
  return fetch(site + path, {
    ...init,
    headers: { Authorization: `Bearer ${token}`, "Content-Type": "application/json", "User-Agent": `SongAlong-Helper/${VERSION}`, ...(init.headers ?? {}) },
  });
}

async function work(tools: Tools) {
  const res = await api(`/api/helper/next?v=${VERSION}`);
  if (res.status === 401) {
    say("This helper's key isn't valid any more. Create a new one in SongAlong → Settings → Your PC helper.");
    process.exit(1);
  }
  if (!res.ok) throw new Error(`SongAlong answered ${res.status}`);
  const { job } = (await res.json()) as { job: { videoId: string; title: string } | null };
  if (!job) return false;

  say(`Preparing "${job.title || job.videoId}"...`);
  let last = 0;
  const progress = (stage: string, fraction: number) => {
    const now = Date.now();
    if (now - last < 1000 && fraction < 1) return;
    last = now;
    void api(`/api/helper/jobs/${job.videoId}/progress`, { method: "POST", body: JSON.stringify({ stage, fraction }) }).catch(() => {});
  };
  let analysis: PrepAnalysis;
  try {
    analysis = await prepareVideo(job.videoId, tools, progress);
  } catch (e) {
    const code = e instanceof PrepError ? e.code : "failed";
    say(`  couldn't prepare it: ${(e as Error).message}`);
    await api(`/api/helper/jobs/${job.videoId}/fail`, { method: "POST", body: JSON.stringify({ code, message: (e as Error).message }) });
    return true;
  }
  progress("Sending to SongAlong", 0.95);
  const done = await api(`/api/helper/jobs/${job.videoId}/result`, { method: "POST", body: JSON.stringify({ analysis }) });
  if (done.ok) say(`  ready: ${analysis.meta.title} (${analysis.detect.key}, ${Math.round(analysis.detect.bpm)} BPM)`);
  else say(`  SongAlong couldn't save it (${done.status}): ${(await done.text()).slice(0, 200)}`);
  return true;
}

async function main() {
  if (!site || !token) {
    console.log("Usage: node songalong-helper.mjs --site https://your-songalong-site --token sah_...");
    console.log("Get the token in SongAlong → Settings → Your PC helper.");
    process.exit(1);
  }
  say(`SongAlong PC helper ${VERSION}. Getting tools ready...`);
  const tools = await setupTools();
  say(`Connected to ${site}. Leave this window open; songs you pick will be prepared here when the cloud can't.`);
  let failures = 0;
  for (;;) {
    try {
      const didWork = await work(tools);
      failures = 0;
      if (!didWork) await new Promise((r) => setTimeout(r, 3000));
    } catch (e) {
      failures++;
      if (failures === 1 || failures % 20 === 0) say(`Can't reach SongAlong right now (${(e as Error).message}). Retrying...`);
      await new Promise((r) => setTimeout(r, Math.min(30000, 3000 * failures)));
    }
  }
}

void main();
