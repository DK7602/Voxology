// The cajon container (cajon/server.ts: Demucs and the drum finder) on a 4-core machine. Its
// Durable Object works through the cajon jobs one at a time from scheduled alarms (each may run
// up to 15 minutes), so no request waits on a song's minute or two of work. See worker/cajon.ts.
import { Container } from "@cloudflare/containers";
import { MAX_DOWNS, nextCajonJob, nextCajonTime, runCajonJob, serviceWait, type CajonStatus } from "./cajon";
import type { Env } from "./env";

/** A next job due further off than this: the machine is stopped now rather than left idling. */
const IDLE_STOP_MS = 60_000;

export class CajonContainer extends Container<Env> {
  defaultPort = 8080;
  // work() stops the machine itself once the queue has nothing due (12 GiB is billed every
  // second it runs, busy or not); this only catches a run that ends some other way
  sleepAfter = "1m";
  enableInternet = true;
  /**
   * A job is being done now. Only this object does jobs, and only one at a time, so while none
   * is, a song left "working" was cut off (a deploy restarted this object mid-job) and is due
   * again at once.
   */
  private busy = false;

  /** Makes sure the queue is being worked through: a run is planned for the next job due. */
  async kick(): Promise<void> {
    const due = await nextCajonTime(this.env.DB, this.busy ? undefined : 0);
    if (due !== null) await this.plan(due);
  }

  /** Plans a run at `at`, unless one is planned by then already. */
  private async plan(at: number): Promise<void> {
    const planned = await this.ctx.storage.get<number>("planned");
    // a plan far in the past never ran (the object was moved or reset): plan again
    if (planned !== undefined && planned <= at + 5_000 && planned > Date.now() - 30 * 60_000) return;
    await this.ctx.storage.put("planned", at);
    await this.schedule(new Date(Math.max(at, Date.now() + 1_000)), "work");
  }

  /** One job, then the next run is planned. */
  async work(): Promise<void> {
    await this.ctx.storage.delete("planned");
    // a run that comes while a job is going does nothing: that job plans the next run
    if (this.busy) return;
    this.busy = true;
    const downs = (await this.ctx.storage.get<number>("downs")) ?? 0;
    let outcome: CajonStatus | "down" | null;
    try {
      outcome = await this.oneJob(downs);
    } finally {
      this.busy = false;
    }
    if (outcome === "down") {
      await this.ctx.storage.put("downs", downs + 1);
      // a machine that didn't answer isn't kept waiting for the next try
      await this.stopMachine();
      // down too often in a row: rest until a new song is queued (queueCajon kicks the queue)
      if (downs + 1 >= MAX_DOWNS) return;
    } else if (outcome !== null && downs > 0) {
      await this.ctx.storage.delete("downs");
    }
    const due = await nextCajonTime(this.env.DB, 0);
    if (due !== null) await this.plan(due);
    // nothing due soon: stop now instead of idling until sleepAfter
    if (due === null || due - Date.now() > IDLE_STOP_MS) await this.stopMachine();
  }

  private async stopMachine(): Promise<void> {
    try {
      await this.stop();
    } catch (e) {
      console.warn("cajon: couldn't stop the machine", (e as Error).message);
    }
  }

  private async oneJob(downs: number): Promise<CajonStatus | "down" | null> {
    const db = this.env.DB;
    const job = await nextCajonJob(db, Date.now(), 0);
    if (!job) return null;
    // (when the service is down, runCajonJob moves the whole queue on, and work() plans after it)
    return runCajonJob(
      db,
      job,
      async (videoId) => {
        // a first start pulls a large image (PyTorch and the model): longer than the usual 8 s
        await this.startAndWaitForPorts({
          ports: this.defaultPort,
          cancellationOptions: { instanceGetTimeoutMS: 5 * 60_000, portReadyTimeoutMS: 3 * 60_000, waitInterval: 1_000 },
        });
        return this.containerFetch("http://cajon/cajon", {
          method: "POST",
          headers: { "Content-Type": "application/json" },
          body: JSON.stringify({ videoId }),
        });
      },
      () => Date.now(),
      serviceWait(downs),
    );
  }
}
