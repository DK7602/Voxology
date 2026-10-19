// The song-prep service inside the Cloudflare container. Only the SongAlong Worker can reach it.
//   POST /prep   { videoId }  -> NDJSON PrepEvent stream (progress..., then result or error)
//   GET  /search?q=...        -> { results }   (yt-dlp search, used when no YouTube API key is set)
//   GET  /health              -> ok

import { spawn } from "node:child_process";
import { createServer, type ServerResponse } from "node:http";
import { isVideoId, type PrepEvent } from "../shared/prep";
import { PrepError, prepareVideo, searchYouTube, type Tools } from "./core";

const tools: Tools = {
  ytDlp: process.env.YTDLP ?? "yt-dlp",
  ffmpeg: process.env.FFMPEG ?? "ffmpeg",
  extraPath: ["/usr/local/bin", "/root/.deno/bin"],
};
const PORT = Number(process.env.PORT ?? 8080);

// YouTube changes often and yt-dlp follows within days: update it when the container starts.
spawn(tools.ytDlp, ["-U"], { stdio: "ignore" }).on("error", () => {});

let busy = 0;

function send(res: ServerResponse, e: PrepEvent) {
  res.write(JSON.stringify(e) + "\n");
}

const server = createServer(async (req, res) => {
  const url = new URL(req.url ?? "/", "http://container");
  try {
    if (req.method === "GET" && url.pathname === "/health") {
      res.writeHead(200, { "Content-Type": "text/plain" }).end(`ok ${busy}`);
      return;
    }
    if (req.method === "GET" && url.pathname === "/search") {
      const q = (url.searchParams.get("q") ?? "").trim().slice(0, 200);
      if (!q) {
        res.writeHead(400, { "Content-Type": "application/json" }).end(JSON.stringify({ error: "Missing q" }));
        return;
      }
      const results = await searchYouTube(q, tools);
      res.writeHead(200, { "Content-Type": "application/json" }).end(JSON.stringify({ results }));
      return;
    }
    if (req.method === "POST" && url.pathname === "/prep") {
      let body = "";
      for await (const chunk of req) body += chunk;
      const { videoId } = JSON.parse(body || "{}") as { videoId?: unknown };
      res.writeHead(200, { "Content-Type": "application/x-ndjson", "Cache-Control": "no-store" });
      if (!isVideoId(videoId)) {
        send(res, { type: "error", code: "failed", message: "Bad video id" });
        res.end();
        return;
      }
      busy++;
      try {
        const analysis = await prepareVideo(videoId, tools, (stage, fraction) => send(res, { type: "progress", stage, fraction }));
        send(res, { type: "result", analysis });
      } catch (e) {
        const code = e instanceof PrepError ? e.code : "failed";
        send(res, { type: "error", code, message: (e as Error).message });
      } finally {
        busy--;
        res.end();
      }
      return;
    }
    res.writeHead(404).end();
  } catch (e) {
    if (!res.headersSent) {
      const err = e instanceof PrepError ? e : null;
      res.writeHead(err?.code === "blocked" ? 429 : 500, { "Content-Type": "application/json" });
    }
    res.end(JSON.stringify({ error: (e as Error).message, code: e instanceof PrepError ? e.code : "failed" }));
  }
});

server.listen(PORT, () => console.log(`song prep listening on ${PORT}`));
