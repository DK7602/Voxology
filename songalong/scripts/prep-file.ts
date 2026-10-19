// Runs the song-prep audio pipeline on a local file (no YouTube):
//   npx vite-node scripts/prep-file.ts path/to/song.mp3
import { prepareLocalFile } from "../prep/core";

const file = process.argv[2];
if (!file) throw new Error("usage: prep-file <audio file>");
const t0 = performance.now();
const a = await prepareLocalFile(file, { title: file, channel: "", duration: 0 }, { ytDlp: "yt-dlp", ffmpeg: "ffmpeg" }, (s, f) =>
  process.stdout.write(`\r${Math.round(f * 100)}% ${s}          `),
);
console.log(`\n${((performance.now() - t0) / 1000).toFixed(2)} s: ${a.detect.key}, ${a.detect.bpm} BPM, ${a.detect.chords.length} chord changes, ${a.chunks.length} speech pieces (${Math.round(a.chunks.reduce((n, c) => n + c.mp3.length, 0) / 1024)} KB base64)`);
