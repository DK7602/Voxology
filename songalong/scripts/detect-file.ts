// Runs chord detection on a local audio file (decoded with ffmpeg):
//   npx vite-node scripts/detect-file.ts path/to/song.mp3
import { execFileSync } from "node:child_process";
import { detectChords } from "../src/analysis/detect";

const file = process.argv[2];
if (!file) throw new Error("usage: detect-file <audio file>");
const sr = 22050;
const raw = execFileSync("ffmpeg", ["-v", "error", "-i", file, "-f", "f32le", "-ac", "1", "-ar", String(sr), "-"], { maxBuffer: 1 << 30 });
const samples = new Float32Array(raw.buffer, raw.byteOffset, raw.byteLength / 4);
const t0 = performance.now();
const r = detectChords(samples, sr);
console.log(`${file}\n  ${r.duration.toFixed(1)} s analysed in ${((performance.now() - t0) / 1000).toFixed(2)} s`);
console.log(`  ${r.bpm} BPM, key ${r.key}, confidence ${r.confidence}`);
console.log("  " + r.chords.slice(0, 24).map((c) => `${c.name}@${c.beat}`).join(" "));
