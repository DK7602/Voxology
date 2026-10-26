// The song-prep container: runs prep/server.ts (yt-dlp, ffmpeg, chord detection).
import { Container } from "@cloudflare/containers";
import type { Env } from "./env";
import { prepIsIdle } from "./watchdog";

export class PrepContainer extends Container<Env> {
  defaultPort = 8080;
  // stay warm a minute after a song or search so the next one starts fast; then stop (and stop
  // billing: 4 GiB is billed every second it runs, busy or not). It starts again in about 8 s.
  sleepAfter = "1m";
  enableInternet = true;
  /** When the last request came (0 after a restart: a machine still running then is orphaned). */
  private lastRequest = 0;

  override async fetch(request: Request): Promise<Response> {
    this.lastRequest = Date.now();
    return super.fetch(request);
  }

  /** The watchdog's check (worker/watchdog.ts): stops the machine if it runs with nothing to do,
   *  even when a cut-off stream keeps sleepAfter from ever stopping it. */
  async stopIfIdle(): Promise<boolean> {
    if (!this.ctx.container?.running) return false;
    if (!(await prepIsIdle(this.env.DB, this.lastRequest))) return false;
    await this.destroy();
    return true;
  }
}
