// The SongAlong API Worker. Static assets (the React app) are served by Workers Assets; only
// /api/* reaches this code (run_worker_first).

import { Hono, type MiddlewareHandler } from "hono";
import { HTTPException } from "hono/http-exception";
import { ValidationError } from "../shared/validate";
import { audio } from "./audio";
import { auth } from "./auth";
import type { AppEnv } from "./env";
import { appOrigin, HttpError, type Ctx } from "./http";
import { playlists } from "./playlists";
import { loadSession } from "./session";
import { songs } from "./songs";
import { helper } from "./helperApi";
import { oauth } from "./oauth";
import { youtube } from "./youtube";
import { stopIdleContainers } from "./watchdog";
import type { Env } from "./env";

export type { Env } from "./env";
export { PrepContainer } from "./prepContainer";
export { CajonContainer } from "./cajonContainer";

const securityHeaders: MiddlewareHandler<AppEnv> = async (c, next) => {
  await next();
  const headers = c.res.headers;
  if (!headers.has("Cache-Control")) headers.set("Cache-Control", "no-store");
  headers.set("X-Content-Type-Options", "nosniff");
};

const SAFE_METHODS = new Set(["GET", "HEAD", "OPTIONS"]);

// Apple posts its sign-in result from appleid.apple.com; the state cookie protects that route.
const CROSS_SITE_OK = /^\/api\/auth\/oauth\/[a-z]+\/callback$/;

/** Blocks cross-site writes: an Origin header, when sent, must be this app's. */
const sameOriginWrites: MiddlewareHandler<AppEnv> = async (c, next) => {
  if (!SAFE_METHODS.has(c.req.method) && !CROSS_SITE_OK.test(c.req.path)) {
    const origin = c.req.header("Origin");
    const allowed = new Set([new URL(c.req.url).origin, appOrigin(c)]);
    const crossSite = origin === undefined ? c.req.header("Sec-Fetch-Site") === "cross-site" : !allowed.has(origin);
    if (crossSite) throw new HttpError(403, "Cross-site request blocked");
  }
  await next();
};

function handleError(err: Error, c: Ctx) {
  if (err instanceof HttpError) return c.json({ error: err.message }, err.status, err.headers);
  if (err instanceof ValidationError) return c.json({ error: err.message }, 400);
  if (err instanceof HTTPException && err.status < 500) return c.json({ error: err.message || "Bad request" }, err.status);
  console.error("Unhandled API error:", err);
  return c.json({ error: "Something went wrong. Please try again." }, 500);
}

const app = new Hono<AppEnv>().basePath("/api");

app.use("*", securityHeaders);
app.use("*", sameOriginWrites);
app.use("*", loadSession);

app.route("/auth/oauth", oauth);
app.route("/auth", auth);
app.route("/songs", songs);
app.route("/playlists", playlists);
app.route("/audio", audio);
app.route("/youtube", youtube);
app.route("/helper", helper);

app.notFound((c) => c.json({ error: "Not found" }, 404));
app.onError(handleError);

/** The API (tests call it directly). */
export { app };

export default {
  fetch: app.fetch,
  // every 5 minutes (wrangler.jsonc "triggers"): stop any container left running with no work
  async scheduled(_event, env, ctx) {
    ctx.waitUntil(stopIdleContainers(env));
  },
} satisfies ExportedHandler<Env>;
