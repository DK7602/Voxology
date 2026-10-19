// The song-prep container: runs prep/server.ts (yt-dlp, ffmpeg, chord detection).
import { Container } from "@cloudflare/containers";
import type { Env } from "./env";

export class PrepContainer extends Container<Env> {
  defaultPort = 8080;
  // stay warm a little after a song so the next one starts fast; then stop (and stop billing)
  sleepAfter = "3m";
  enableInternet = true;
}
