// Finds the beat, key and chords of a recording, in the browser.
import type { DetectResult } from "./detect";

export type { DetectResult } from "./detect";

/** Decodes the file, mixes it to mono and analyses it in a Web Worker. */
export async function analyseAudioFile(
  file: Blob,
  onProgress?: (fraction: number, stage: string) => void,
): Promise<DetectResult> {
  onProgress?.(0, "Decoding");
  const data = await file.arrayBuffer();
  const ctx = new AudioContext();
  let buffer: AudioBuffer;
  try {
    buffer = await ctx.decodeAudioData(data);
  } catch {
    throw new Error("This file couldn't be read as audio. Try an MP3 or WAV.");
  } finally {
    void ctx.close();
  }
  const mono = new Float32Array(buffer.length);
  for (let ch = 0; ch < buffer.numberOfChannels; ch++) {
    const d = buffer.getChannelData(ch);
    for (let i = 0; i < d.length; i++) mono[i] += d[i] / buffer.numberOfChannels;
  }
  const worker = new Worker(new URL("./detect.worker.ts", import.meta.url), { type: "module" });
  try {
    return await new Promise<DetectResult>((resolve, reject) => {
      worker.onmessage = (e: MessageEvent<{ type: string; fraction?: number; stage?: string; result?: DetectResult; message?: string }>) => {
        const m = e.data;
        if (m.type === "progress") onProgress?.(m.fraction ?? 0, m.stage ?? "");
        else if (m.type === "result") resolve(m.result!);
        else reject(new Error(m.message ?? "Chord detection failed."));
      };
      worker.onerror = () => reject(new Error("Chord detection failed."));
      worker.postMessage({ samples: mono, sampleRate: buffer.sampleRate }, [mono.buffer]);
    });
  } finally {
    worker.terminate();
  }
}
