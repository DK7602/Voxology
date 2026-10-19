// A small D1Database stand-in on node:sqlite, close enough to D1's semantics for the API tests:
// bind() returns a new statement, undefined bind values throw, booleans become 0/1,
// batch() is one transaction, foreign keys are enforced.

import { DatabaseSync, type StatementSync } from "node:sqlite";

type SqlValue = null | number | bigint | string | Uint8Array;

function toSqlValue(v: unknown): SqlValue {
  if (v === undefined) throw new TypeError("D1_TYPE_ERROR: Type 'undefined' not supported for value 'undefined'");
  if (typeof v === "boolean") return v ? 1 : 0;
  if (v === null || typeof v === "number" || typeof v === "bigint" || typeof v === "string" || v instanceof Uint8Array) {
    return v;
  }
  if (v instanceof ArrayBuffer) return new Uint8Array(v);
  throw new TypeError(`D1_TYPE_ERROR: Type '${typeof v}' not supported`);
}

const plain = (row: unknown) => ({ ...(row as Record<string, unknown>) });

function meta(changes = 0, lastRowId = 0) {
  return { duration: 0, size_after: 0, rows_read: 0, rows_written: changes, last_row_id: lastRowId, changed_db: changes > 0, changes };
}

export class FakeStatement {
  constructor(
    private readonly db: FakeD1,
    readonly sql: string,
    private readonly params: SqlValue[] = [],
  ) {}

  bind(...values: unknown[]): FakeStatement {
    return new FakeStatement(this.db, this.sql, values.map(toSqlValue));
  }

  private stmt(): StatementSync {
    return this.db.sqlite.prepare(this.sql);
  }

  /** Executes synchronously (also used inside batch transactions). */
  execute() {
    const stmt = this.stmt();
    if (stmt.columns().length > 0) {
      const results = stmt.all(...this.params).map(plain);
      return { success: true as const, results, meta: meta(results.length) };
    }
    const r = stmt.run(...this.params);
    return { success: true as const, results: [], meta: meta(Number(r.changes), Number(r.lastInsertRowid)) };
  }

  async first<T = Record<string, unknown>>(column?: string): Promise<T | null> {
    const row = this.stmt().get(...this.params);
    if (row === undefined) return null;
    if (column === undefined) return plain(row) as T;
    return ((row as Record<string, unknown>)[column] ?? null) as T;
  }

  async all() {
    return this.execute();
  }

  async run() {
    return this.execute();
  }

  async raw(): Promise<unknown[][]> {
    return this.stmt().all(...this.params).map((r) => Object.values(r as object));
  }
}

export class FakeD1 {
  readonly sqlite = new DatabaseSync(":memory:");

  constructor() {
    this.sqlite.exec("PRAGMA foreign_keys = ON");
  }

  prepare(sql: string): FakeStatement {
    return new FakeStatement(this, sql);
  }

  async batch(statements: FakeStatement[]) {
    this.sqlite.exec("BEGIN");
    try {
      const out = statements.map((s) => s.execute());
      this.sqlite.exec("COMMIT");
      return out;
    } catch (err) {
      this.sqlite.exec("ROLLBACK");
      throw err;
    }
  }

  async exec(sql: string) {
    this.sqlite.exec(sql);
    return { count: sql.split(";").filter((s) => s.trim()).length, duration: 0 };
  }

  /** Synchronous query for test assertions. */
  query<T = Record<string, unknown>>(sql: string, ...params: unknown[]): T[] {
    return this.sqlite.prepare(sql).all(...params.map(toSqlValue)).map(plain) as T[];
  }
}
