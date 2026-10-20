// Bundles the song-prep code for its two homes:
//   prep/dist/server.mjs            -> the Cloudflare container (see prep/Dockerfile)
//   public/helper/songalong-helper.mjs -> the PC helper, downloadable from the site
import { copyFile, mkdir } from "node:fs/promises";
import { build } from "esbuild";

const common = { bundle: true, platform: "node", format: "esm", target: "node20", logLevel: "warning" };
await build({ ...common, entryPoints: ["prep/server.ts"], outfile: "prep/dist/server.mjs" });
await build({
  ...common,
  entryPoints: ["prep/helper.ts"],
  outfile: "public/helper/songalong-helper.mjs",
  banner: { js: "// SongAlong PC helper (generated from prep/helper.ts). Run: node songalong-helper.mjs --site <url> --token <key>" },
});
// the note model for lead parts (container only): ONNX Runtime's WebAssembly build and Basic Pitch
await mkdir("prep/dist/ort", { recursive: true });
await mkdir("prep/dist/models", { recursive: true });
// one file with what it imports, next to the WebAssembly it loads from its own folder
await build({ ...common, stdin: { contents: 'export * from "onnxruntime-web";', resolveDir: "." }, outfile: "prep/dist/ort/ort.node.min.mjs" });
for (const f of ["ort-wasm-simd-threaded.mjs", "ort-wasm-simd-threaded.wasm"]) {
  await copyFile(`node_modules/onnxruntime-web/dist/${f}`, `prep/dist/ort/${f}`);
}
await copyFile("prep/models/basic-pitch.onnx", "prep/dist/models/basic-pitch.onnx");
console.log("prep bundles built");
