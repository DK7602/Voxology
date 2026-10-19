// Song artwork: the YouTube thumbnail when there is one, otherwise a generated gold / green
// cover with an instrument motif (deterministic per title).
import { IconGuitar, IconMandolin, IconNote, IconPiano } from "./Icons";

function hash(s: string): number {
  let h = 2166136261;
  for (let i = 0; i < s.length; i++) h = Math.imul(h ^ s.charCodeAt(i), 16777619);
  return h >>> 0;
}

export function Artwork({
  title,
  youtubeId,
  className,
  rounded = 14,
}: {
  title: string;
  youtubeId?: string;
  className?: string;
  rounded?: number;
}) {
  if (youtubeId) {
    return (
      <div className={"artwork " + (className ?? "")} style={{ borderRadius: rounded }}>
        <img src={`https://i.ytimg.com/vi/${youtubeId}/hqdefault.jpg`} alt="" loading="lazy" />
      </div>
    );
  }
  const h = hash(title || "song");
  const hueShift = (h % 40) - 20;
  const angle = h % 360;
  const Icon = [IconGuitar, IconNote, IconPiano, IconMandolin][h % 4];
  return (
    <div
      className={"artwork generated " + (className ?? "")}
      style={{
        borderRadius: rounded,
        background: `radial-gradient(120% 90% at 20% 15%, hsl(${44 + hueShift / 4} 75% 62%), transparent 55%),
          linear-gradient(${angle}deg, hsl(${150 + hueShift} 55% 16%), hsl(${145 + hueShift} 60% 26%) 60%, hsl(${42} 60% 38%))`,
      }}
      aria-hidden="true"
    >
      <Icon size={44} />
      <span className="art-initial">{(title || "♪").trim().charAt(0).toUpperCase()}</span>
    </div>
  );
}

export function Logo({ compact }: { compact?: boolean }) {
  return (
    <span className="logo" aria-label="SongAlong">
      <svg viewBox="0 0 40 52" width={compact ? 26 : 36} height={compact ? 34 : 47} aria-hidden="true">
        <defs>
          <linearGradient id="logo-gold" x1="0" y1="0" x2="1" y2="1">
            <stop offset="0" stopColor="#f6dd8a" />
            <stop offset="0.5" stopColor="#c9a227" />
            <stop offset="1" stopColor="#8a6512" />
          </linearGradient>
          <linearGradient id="logo-green" x1="0" y1="0" x2="1" y2="1">
            <stop offset="0" stopColor="#2a8a55" />
            <stop offset="1" stopColor="#0b3d22" />
          </linearGradient>
        </defs>
        <path d="M22 4v32" stroke="url(#logo-gold)" strokeWidth="4" strokeLinecap="round" />
        <path d="M22 4c4 6 14 7 13 17-2-5-7-7-13-8" fill="url(#logo-gold)" />
        <ellipse cx="13" cy="38" rx="11" ry="8" transform="rotate(-22 13 38)" fill="url(#logo-green)" stroke="url(#logo-gold)" strokeWidth="1.5" />
      </svg>
      {!compact && (
        <span className="logo-text">
          <span className="logo-song">Song</span>
          <span className="logo-along">Along</span>
        </span>
      )}
    </span>
  );
}
