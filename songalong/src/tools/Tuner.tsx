import { useEffect, useRef, useState } from "react";
import { FRETTED } from "../../shared/music/voicings";
import { midiName, midiToFreq } from "../../shared/music/notes";
import type { Instrument } from "../../shared/types";
import { strum, unlockAudio } from "../player/synth";

/** Pitch of a mono buffer by normalised autocorrelation (good for single plucked strings). */
export function detectPitch(buf: Float32Array, sampleRate: number, minHz = 50, maxHz = 1200): number | null {
  let rms = 0;
  for (let i = 0; i < buf.length; i++) rms += buf[i] * buf[i];
  rms = Math.sqrt(rms / buf.length);
  if (rms < 0.01) return null;
  const minLag = Math.floor(sampleRate / maxHz);
  const maxLag = Math.min(Math.floor(sampleRate / minHz), buf.length >> 1);
  const n = buf.length - maxLag;
  let bestLag = -1;
  let best = 0;
  const corr = new Float32Array(maxLag + 2);
  for (let lag = minLag; lag <= maxLag; lag++) {
    let c = 0;
    let e1 = 0;
    let e2 = 0;
    for (let i = 0; i < n; i++) {
      c += buf[i] * buf[i + lag];
      e1 += buf[i] * buf[i];
      e2 += buf[i + lag] * buf[i + lag];
    }
    corr[lag] = c / Math.sqrt(e1 * e2 + 1e-12);
  }
  // first strong peak (avoids octave-down errors)
  const top = Math.max(...corr.slice(minLag, maxLag + 1));
  for (let lag = minLag + 1; lag < maxLag; lag++) {
    if (corr[lag] > 0.9 * top && corr[lag] >= corr[lag - 1] && corr[lag] >= corr[lag + 1]) {
      bestLag = lag;
      best = corr[lag];
      break;
    }
  }
  if (bestLag < 0 || best < 0.6) return null;
  // parabolic interpolation
  const a = corr[bestLag - 1];
  const b = corr[bestLag];
  const c = corr[bestLag + 1];
  const shift = (a - c) / (2 * (a - 2 * b + c) || 1);
  return sampleRate / (bestLag + shift);
}

export function Tuner({ instrument = "guitar" }: { instrument?: Instrument }) {
  const inst = instrument === "piano" ? FRETTED.guitar : FRETTED[instrument];
  const [on, setOn] = useState(false);
  const [error, setError] = useState<string | null>(null);
  const [reading, setReading] = useState<{ freq: number; midi: number; cents: number } | null>(null);
  const stop = useRef<(() => void) | null>(null);

  useEffect(() => () => stop.current?.(), []);

  const start = async () => {
    setError(null);
    try {
      const ctx = await unlockAudio();
      const stream = await navigator.mediaDevices.getUserMedia({ audio: { echoCancellation: false, noiseSuppression: false, autoGainControl: false } });
      const src = ctx.createMediaStreamSource(stream);
      const analyser = ctx.createAnalyser();
      const bass = inst.id === "bass";
      analyser.fftSize = bass ? 8192 : 4096;
      src.connect(analyser);
      const buf = new Float32Array(analyser.fftSize);
      // bass: average every 4 samples first (its notes are all under 400 Hz) so the search stays quick
      const small = new Float32Array(analyser.fftSize / 4);
      let raf = 0;
      let smooth = 0;
      const loop = () => {
        analyser.getFloatTimeDomainData(buf);
        let f: number | null;
        if (bass) {
          for (let i = 0; i < small.length; i++) small[i] = 0.25 * (buf[4 * i] + buf[4 * i + 1] + buf[4 * i + 2] + buf[4 * i + 3]);
          f = detectPitch(small, ctx.sampleRate / 4, 35, 400);
        } else f = detectPitch(buf, ctx.sampleRate);
        if (f) {
          smooth = smooth && Math.abs(Math.log2(f / smooth)) < 0.05 ? smooth * 0.7 + f * 0.3 : f;
          const midiF = 69 + 12 * Math.log2(smooth / 440);
          const midi = Math.round(midiF);
          setReading({ freq: smooth, midi, cents: Math.round((midiF - midi) * 100) });
        }
        raf = requestAnimationFrame(loop);
      };
      loop();
      stop.current = () => {
        cancelAnimationFrame(raf);
        stream.getTracks().forEach((t) => t.stop());
        src.disconnect();
        setOn(false);
      };
      setOn(true);
    } catch {
      setError("Microphone access is needed for the tuner. Allow it in your browser and try again.");
    }
  };

  const nearestString = reading
    ? inst.tuning.reduce((a, b) => (Math.abs(b - reading.midi) < Math.abs(a - reading.midi) ? b : a))
    : null;
  const cents = reading?.cents ?? 0;
  const inTune = reading && Math.abs(cents) <= 5;

  return (
    <div style={{ display: "grid", gap: 16, justifyItems: "center" }}>
      <div style={{ display: "flex", gap: 8, flexWrap: "wrap", justifyContent: "center" }}>
        {inst.tuning.map((m, i) => (
          <button
            key={i}
            className={"btn" + (nearestString === m ? " active" : "")}
            onClick={async () => {
              const ctx = await unlockAudio();
              strum([m], ctx.currentTime + 0.02, { instrument: inst.id, velocity: 0.9 });
            }}
            title={`Play ${midiName(m)} (${midiToFreq(m).toFixed(1)} Hz)`}
          >
            {midiName(m)}
          </button>
        ))}
      </div>
      <div className="display" style={{ fontSize: 72, lineHeight: 1, color: inTune ? "var(--green-hi)" : "var(--heading)" }}>
        {reading ? midiName(reading.midi) : "—"}
      </div>
      <svg viewBox="0 0 300 80" width="300" aria-label={reading ? `${cents} cents` : "No note"}>
        <line x1="20" x2="280" y1="60" y2="60" stroke="var(--ink-faint)" strokeWidth="2" />
        {[-50, -25, 0, 25, 50].map((c) => (
          <g key={c}>
            <line x1={150 + c * 2.6} x2={150 + c * 2.6} y1={c === 0 ? 30 : 48} y2="60" stroke="var(--ink-soft)" strokeWidth={c === 0 ? 3 : 1.5} />
            <text x={150 + c * 2.6} y="76" textAnchor="middle" fontSize="11" fill="var(--ink-soft)">{c > 0 ? "+" + c : c}</text>
          </g>
        ))}
        {reading && (
          <circle cx={150 + Math.max(-50, Math.min(50, cents)) * 2.6} cy="44" r="9" fill={inTune ? "var(--green-hi)" : "var(--gold)"} />
        )}
      </svg>
      <div className="muted">{reading ? `${reading.freq.toFixed(1)} Hz · ${cents > 0 ? "+" : ""}${cents} cents ${inTune ? "· in tune" : cents < 0 ? "· tune up" : "· tune down"}` : on ? "Play one string..." : "Standard tuning: " + inst.tuningNames.join(" ")}</div>
      {error && <div className="notice error">{error}</div>}
      {!on ? (
        <button className="btn btn-primary" onClick={start}>Start tuner (uses the microphone)</button>
      ) : (
        <button className="btn" onClick={() => stop.current?.()}>Stop</button>
      )}
    </div>
  );
}
