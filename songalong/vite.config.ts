import { defineConfig } from "vite";
import react from "@vitejs/plugin-react";
import { cloudflare } from "@cloudflare/vite-plugin";

// Local dev runs without the song-prep container (needs Docker) and Workers AI (needs a
// Cloudflare login) unless SONGALONG_FULL_DEV=1. Songs then go straight to the PC helper queue.
export default defineConfig(({ command }) => {
  const lean = command === "serve" && process.env.SONGALONG_FULL_DEV !== "1";
  return {
    plugins: [
      react(),
      cloudflare(
        lean
          ? {
              config: (c) => {
                delete c.containers;
                delete c.ai;
                c.durable_objects = { bindings: [] };
                c.migrations = [];
              },
            }
          : {},
      ),
    ],
    worker: { format: "es" },
  };
});
