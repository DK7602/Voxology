import { useEffect, useRef, useState } from "react";
import { FRETTED } from "../../shared/music/voicings";
import { midiName, midiToFreq } from "../../shared/music/notes";
import type { Instrument } from "../../shared/types";
import { strum, unlockAudio } from "../player/synth";
import { detectPitch } from "./pitch";

/**
 * A strobe display: bands of stripes drift right when the note is sharp and left when it is flat,
 * faster the further off it is, and stand still when it is in tune. The lower bands move at half
 * and a quarter of the speed of the top one, so even a cent or two shows as a slow drift.
 */
function Strobe({ live }: { live: React.MutableRefObject<{ cents: number | null; at: number }> }) {
  const canvas = useRef<HTMLCanvasElement>(null);
  useEffect(() => {
    const cv = canvas.current;
    if (!cv) return;
    const g = cv.getContext("2d");
    if (!g) return;
    let raf = 0;
    let last = performance.now();
    let phase = 0;
    let glow = 0;
    const draw = (now: number) => {
      const dt = Math.min(0.05, (now - last) / 1000);
      last = now;
      const dpr = window.devicePixelRatio || 1;
      const w = cv.clientWidth;
      const h = cv.clientHeight;
      if (cv.width !== Math.round(w * dpr) || cv.height !== Math.round(h * dpr)) {
        cv.width = Math.round(w * dpr);
        cv.height = Math.round(h * dpr);
      }
      g.setTransform(dpr, 0, 0, dpr, 0, 0);
      const { cents, at } = live.current;
      const fresh = cents !== null && now - at < 600;
      // drift speed: 4 px a second per cent (top band), capped
      if (fresh) phase += Math.max(-60, Math.min(60, cents!)) * 4 * dt;
      const inTune = fresh && Math.abs(cents!) <= 2;
      glow += ((inTune ? 1 : 0) - glow) * Math.min(1, dt * 6);
      g.clearRect(0, 0, w, h);
      const rows = [
        { y: 0, hgt: h * 0.4, stripe: 28, speed: 1 },
        { y: h * 0.42, hgt: h * 0.3, stripe: 14, speed: 0.5 },
        { y: h * 0.74, hgt: h * 0.26, stripe: 7, speed: 0.25 },
      ];
      for (const r of rows) {
        const off = ((((phase * r.speed) % (2 * r.stripe)) + 2 * r.stripe) % (2 * r.stripe)) - 2 * r.stripe;
        const grad = g.createLinearGradient(0, r.y, 0, r.y + r.hgt);
        const a = fresh ? 1 : 0.25;
        if (glow > 0.5) {
          grad.addColorStop(0, `rgba(120, 255, 170, ${a})`);
          grad.addColorStop(1, `rgba(20, 160, 90, ${a})`);
        } else {
          grad.addColorStop(0, `rgba(255, 233, 160, ${a})`);
          grad.addColorStop(0.5, `rgba(233, 180, 70, ${a})`);
          grad.addColorStop(1, `rgba(160, 105, 20, ${a})`);
        }
        g.fillStyle = grad;
        for (let x = off; x < w; x += 2 * r.stripe) g.fillRect(x, r.y, r.stripe, r.hgt);
      }
      raf = requestAnimationFrame(draw);
    };
    raf = requestAnimationFrame(draw);
    return () => cancelAnimationFrame(raf);
  }, [live]);
  return <canvas ref={canvas} className="strobe" aria-hidden="true" />;
}

export function Tuner({ instrument = "guitar" }: { instrument?: Instrument }) {
  const inst = instrument === "piano" ? FRETTED.guitar : FRETTED[instrument];
  const [on, setOn] = useState(false);
  const [error, setError] = useState<string | null>(null);
  const [reading, setReading] = useState<{ freq: number; midi: number; cents: number } | null>(null);
  const stop = useRef<(() => void) | null>(null);
  // the strobe reads this every frame (no re-render needed)
  const live = useRef<{ cents: number | null; at: number }>({ cents: null, at: 0 });

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
      let shown = 0;
      const recent: number[] = [];
      const loop = () => {
        analyser.getFloatTimeDomainData(buf);
        let f: number | null;
        if (bass) {
          for (let i = 0; i < small.length; i++) small[i] = 0.25 * (buf[4 * i] + buf[4 * i + 1] + buf[4 * i + 2] + buf[4 * i + 3]);
          f = detectPitch(small, ctx.sampleRate / 4, 35, 400);
        } else f = detectPitch(buf, ctx.sampleRate);
        if (f) {
          const jump = !smooth || Math.abs(Math.log2(f / smooth)) >= 0.05;
          smooth = jump ? f : smooth * 0.7 + f * 0.3;
          if (jump) recent.length = 0;
          const midiF = 69 + 12 * Math.log2(smooth / 440);
          const midi = Math.round(midiF);
          // the median of the last few readings keeps the number steady
          recent.push((midiF - midi) * 100);
          if (recent.length > 7) recent.shift();
          const cents = [...recent].sort((a, b) => a - b)[recent.length >> 1];
          live.current = { cents, at: performance.now() };
          const now = performance.now();
          if (now - shown > 90) {
            shown = now;
            setReading({ freq: smooth, midi, cents: Math.round(cents) });
          }
        }
        raf = requestAnimationFrame(loop);
      };
      loop();
      stop.current = () => {
        cancelAnimationFrame(raf);
        stream.getTracks().forEach((t) => t.stop());
        src.disconnect();
        live.current = { cents: null, at: 0 };
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
  const inTune = reading && Math.abs(cents) <= 2;

  return (
    <div className="tuner">
      <div className="tuner-strings">
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
      <div className={"tuner-face" + (inTune ? " in-tune" : "")}>
        <div className="tuner-note">
          <span className="flat-mark" aria-hidden="true">♭</span>
          <b>{reading ? midiName(reading.midi) : "—"}</b>
          <span className="sharp-mark" aria-hidden="true">♯</span>
        </div>
        <Strobe live={live} />
        <div className="tuner-readout" aria-live="polite">
          {reading
            ? inTune
              ? "In tune"
              : `${cents > 0 ? "+" : ""}${cents} cents · ${cents < 0 ? "tune up" : "tune down"}`
            : on
              ? "Play one string…"
              : "Standard tuning: " + inst.tuningNames.join(" ")}
          {reading && <span className="faint"> · {reading.freq.toFixed(1)} Hz</span>}
        </div>
      </div>
      {error && <div className="notice error">{error}</div>}
      {!on ? (
        <button className="btn btn-primary" onClick={start}>Start tuner (uses the microphone)</button>
      ) : (
        <button className="btn" onClick={() => stop.current?.()}>Stop</button>
      )}
    </div>
  );
}
