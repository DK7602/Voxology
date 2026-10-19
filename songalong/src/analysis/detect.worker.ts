/// <reference lib="webworker" />
// Runs the chord / beat detection off the main thread.
import { detectChords } from "./detect";

self.onmessage = (e: MessageEvent<{ samples: Float32Array; sampleRate: number; vocabulary?: "basic" | "full" }>) => {
  const { samples, sampleRate, vocabulary } = e.data;
  try {
    const result = detectChords(samples, sampleRate, {
      vocabulary,
      onProgress: (fraction, stage) => self.postMessage({ type: "progress", fraction, stage }),
    });
    self.postMessage({ type: "result", result });
  } catch (err) {
    self.postMessage({ type: "error", message: err instanceof Error ? err.message : String(err) });
  }
};
