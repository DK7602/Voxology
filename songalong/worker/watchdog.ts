// A safety net for the container bill. Containers are billed for their full memory every second
// they run, busy or idle, and the containers library only lets one go to sleep once every
// response it streamed has ended: a song-prep stream that was cut off (the phone went to sleep,
// a deploy) can keep a machine awake for days. So every 5 minutes (the cron trigger in
// wrangler.jsonc) each container is asked to stop if it is running with nothing to do.

import type { Env } from "./env";
import { PREP_BUCKETS } from "./youtube";

/** A prep machine with no request this long, and no song whose preparation moved this recently,
 *  has nothing to do (the app treats a preparation this quiet as stalled, too). */
export const PREP_IDLE_MS = 6 * 60_000;

/** Is a prep machine idle: no request to it for a while, and no cloud preparation still moving? */
export async function prepIsIdle(db: D1Database, lastRequest: number, now = Date.now()): Promise<boolean> {
  if (now - lastRequest < PREP_IDLE_MS) return false;
  const moving = await db
    .prepare("SELECT 1 AS moving FROM yt_songs WHERE status = 'working' AND source = 'cloud' AND updated_at > ? LIMIT 1")
    .bind(now - PREP_IDLE_MS)
    .first();
  return !moving;
}

interface Stoppable {
  stopIfIdle(): Promise<boolean>;
}

/** The names of every container the app starts: the prep buckets, prep search, and cajon. */
export function containerNames(): { binding: "PREP" | "CAJON"; name: string }[] {
  return [
    ...Array.from({ length: PREP_BUCKETS }, (_, i) => ({ binding: "PREP" as const, name: `prep-${i}` })),
    { binding: "PREP", name: "prep-search" },
    { binding: "CAJON", name: "cajon" },
  ];
}

/** Asks every container to stop if it is idle; returns the ones that were stopped. */
export async function stopIdleContainers(env: Env): Promise<string[]> {
  const stopped: string[] = [];
  for (const { binding, name } of containerNames()) {
    const ns = env[binding];
    if (!ns) continue;
    try {
      // (what getContainer() does: the container's Durable Object by name)
      const space = ns as unknown as { idFromName(n: string): DurableObjectId; get(id: DurableObjectId): unknown };
      const stub = space.get(space.idFromName(name)) as Stoppable;
      if (await stub.stopIfIdle()) stopped.push(name);
    } catch (e) {
      console.warn(`watchdog: couldn't check ${name}:`, (e as Error).message);
    }
  }
  if (stopped.length) console.log(`watchdog: stopped idle containers: ${stopped.join(", ")}`);
  return stopped;
}
