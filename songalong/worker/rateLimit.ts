// Fixed-window counters in the rate_limits table: a window opens at the first hit and the
// count resets once it is older than `windowMs`.

export interface Limit {
  key: string;
  max: number;
  windowMs: number;
}

/** Longest window in use; rows older than this are stale and can be deleted. */
export const LONGEST_WINDOW_MS = 60 * 60 * 1000;

/** The first limit that is already used up, if any. */
export async function exceeded(db: D1Database, limits: Limit[], now = Date.now()): Promise<Limit | null> {
  if (limits.length === 0) return null;
  const rows = await db
    .prepare(`SELECT key, window_start, count FROM rate_limits WHERE key IN (${limits.map(() => "?").join(", ")})`)
    .bind(...limits.map((l) => l.key))
    .all<{ key: string; window_start: number; count: number }>();
  for (const limit of limits) {
    const row = rows.results.find((r) => r.key === limit.key);
    if (row && row.window_start > now - limit.windowMs && row.count >= limit.max) return limit;
  }
  return null;
}

/** Counts one hit against every limit. */
export async function hit(db: D1Database, limits: Limit[], now = Date.now()): Promise<void> {
  if (limits.length === 0) return;
  // SQLite evaluates every SET expression against the old row, so both CASEs see the same window.
  const sql = `INSERT INTO rate_limits (key, window_start, count) VALUES (?1, ?2, 1)
    ON CONFLICT(key) DO UPDATE SET
      window_start = CASE WHEN rate_limits.window_start <= ?3 THEN ?2 ELSE rate_limits.window_start END,
      count = CASE WHEN rate_limits.window_start <= ?3 THEN 1 ELSE rate_limits.count + 1 END`;
  await db.batch(limits.map((l) => db.prepare(sql).bind(l.key, now, now - l.windowMs)));
}

export function deleteStale(db: D1Database, now = Date.now()): D1PreparedStatement {
  return db.prepare("DELETE FROM rate_limits WHERE window_start < ?").bind(now - LONGEST_WINDOW_MS);
}
