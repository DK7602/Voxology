// Loads Basic Pitch (prep/models/basic-pitch.onnx) on ONNX Runtime's WebAssembly build. In the
// container both sit next to the bundled server (scripts/build-prep.mjs copies them); during
// development they come from the repository. Where neither is found (the PC helper is a single
// file), there is no model and songs are prepared without a lead part.

import { access, readFile } from "node:fs/promises";
import { fileURLToPath, pathToFileURL } from "node:url";
import type { NoteModel } from "./notes";

const FRAMES = 172;

async function firstThere(urls: URL[]): Promise<URL | null> {
  for (const u of urls) {
    try {
      await access(fileURLToPath(u));
      return u;
    } catch {
      // not here
    }
  }
  return null;
}

let loading: Promise<NoteModel | null> | null = null;

export function loadNoteModel(): Promise<NoteModel | null> {
  loading ??= load().catch((e) => {
    console.warn("note model unavailable:", (e as Error).message);
    return null;
  });
  return loading;
}

async function load(): Promise<NoteModel | null> {
  const here = import.meta.url;
  const ortUrl = await firstThere([new URL("./ort/ort.node.min.mjs", here), new URL("../node_modules/onnxruntime-web/dist/ort.node.min.mjs", here)]);
  const modelUrl = await firstThere([new URL("./models/basic-pitch.onnx", here)]);
  if (!ortUrl || !modelUrl) return null;
  const ort = (await import(/* @vite-ignore */ pathToFileURL(fileURLToPath(ortUrl)).href)) as typeof import("onnxruntime-web");
  ort.env.wasm.numThreads = 1; // its WebAssembly sits next to it, where it looks by default
  const session = await ort.InferenceSession.create(await readFile(fileURLToPath(modelUrl)));
  return async (window) => {
    const out = await session.run({ "serving_default_input_2:0": new ort.Tensor("float32", window, [1, window.length, 1]) });
    const note = out["StatefulPartitionedCall:1"].data as Float32Array;
    const onset = out["StatefulPartitionedCall:2"].data as Float32Array;
    if (note.length !== FRAMES * 88 || onset.length !== FRAMES * 88) throw new Error("unexpected note model output");
    return { note, onset };
  };
}
