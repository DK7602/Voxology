// Bundles the song-prep code for its two homes:
//   prep/dist/server.mjs            -> the Cloudflare container (see prep/Dockerfile)
//   public/helper/songalong-helper.mjs -> the PC helper, downloadable from the site
import { build } from "esbuild";

const common = { bundle: true, platform: "node", format: "esm", target: "node20", logLevel: "warning" };
await build({ ...common, entryPoints: ["prep/server.ts"], outfile: "prep/dist/server.mjs" });
await build({
  ...common,
  entryPoints: ["prep/helper.ts"],
  outfile: "public/helper/songalong-helper.mjs",
  banner: { js: "// SongAlong PC helper (generated from prep/helper.ts). Run: node songalong-helper.mjs --site <url> --token <key>" },
});
console.log("prep bundles built");
