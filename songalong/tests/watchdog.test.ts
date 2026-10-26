import { describe, expect, it } from "vitest";
import { containerNames, PREP_IDLE_MS, prepIsIdle, stopIdleContainers } from "../worker/watchdog";
import { makeEnv } from "./helpers/workerEnv";

describe("container watchdog", () => {
  it("checks every container the app starts", () => {
    expect(containerNames().map((c) => c.name)).toEqual(["prep-0", "prep-1", "prep-2", "prep-search", "cajon"]);
  });

  it("asks each container to stop if idle, and tells which ones did", async () => {
    const asked: string[] = [];
    const ns = (idle: Set<string>) => ({
      idFromName: (n: string) => n,
      get: (id: string) => ({
        stopIfIdle: async () => {
          asked.push(id);
          if (id === "prep-1") throw new Error("not answering");
          return idle.has(id);
        },
      }),
    });
    const { env } = makeEnv({ PREP: ns(new Set(["prep-0", "prep-search"])), CAJON: ns(new Set(["cajon"])) });
    // one container not answering doesn't keep the others from being checked
    expect(await stopIdleContainers(env)).toEqual(["prep-0", "prep-search", "cajon"]);
    expect(asked).toEqual(["prep-0", "prep-1", "prep-2", "prep-search", "cajon"]);
  });

  it("skips containers the app doesn't have", async () => {
    const { env } = makeEnv();
    expect(await stopIdleContainers(env)).toEqual([]);
  });

  it("calls a prep machine idle only with no recent request and no preparation moving", async () => {
    const { env, db } = makeEnv();
    const now = 10 * PREP_IDLE_MS;
    // a request a moment ago: busy
    expect(await prepIsIdle(env.DB, now - 1_000, now)).toBe(false);
    // no request for a while and nothing being prepared: idle (also after a restart, lastRequest 0)
    expect(await prepIsIdle(env.DB, 0, now)).toBe(true);
    // a cloud preparation that saved progress recently: busy
    db.sqlite.exec(`INSERT INTO yt_songs (video_id, status, source, created_at, updated_at) VALUES ('aaaaaaaaaaa', 'working', 'cloud', 0, ${now - 30_000})`);
    expect(await prepIsIdle(env.DB, 0, now)).toBe(false);
    // ... but one that went quiet long ago was cut off, and keeps nothing awake
    db.sqlite.exec(`UPDATE yt_songs SET updated_at = ${now - PREP_IDLE_MS - 1}`);
    expect(await prepIsIdle(env.DB, 0, now)).toBe(true);
    // a song waiting for the user's PC helper isn't the cloud's work
    db.sqlite.exec(`UPDATE yt_songs SET source = 'pc', updated_at = ${now}`);
    expect(await prepIsIdle(env.DB, 0, now)).toBe(true);
  });
});
