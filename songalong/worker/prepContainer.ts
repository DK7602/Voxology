// The song-prep container: runs prep/server.ts (yt-dlp, ffmpeg, chord detection).
import { Container } from "@cloudflare/containers";
import type { Env } from "./env";

export class PrepContainer extends Container<Env> {
  defaultPort = 8080;
  // stay warm a minute after a song or search so the next one starts fast; then stop (and stop
  // billing: 4 GiB is billed every second it runs, busy or not). It starts again in about 8 s.
  sleepAfter = "1m";
  enableInternet = true;
}
