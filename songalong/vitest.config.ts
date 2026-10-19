import { fileURLToPath } from "node:url";
import { defineConfig } from "vitest/config";

// Tests run in plain Node (no Workers runtime): the music engine, chord detection and the
// Worker routes are written against web-standard APIs that Node also has.
export default defineConfig({
  resolve: {
    alias: {
      "cloudflare:workers": fileURLToPath(new URL("./tests/helpers/cloudflare-workers-stub.ts", import.meta.url)),
      "cloudflare:sockets": fileURLToPath(new URL("./tests/helpers/cloudflare-sockets-stub.ts", import.meta.url)),
    },
  },
  test: {
    include: ["tests/**/*.test.ts"],
    environment: "node",
    server: { deps: { inline: ["@cloudflare/containers"] } },
  },
});
