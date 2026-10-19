import { defineConfig } from "vitest/config";

// Tests run in plain Node (no Workers runtime): the music engine, chord detection and the
// auth helpers are written against web-standard APIs that Node also has.
export default defineConfig({
  test: { include: ["tests/**/*.test.ts"], environment: "node" },
});
