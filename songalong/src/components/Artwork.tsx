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
      <img className="logo-emblem" src="/brand/emblem.webp" alt="" width={compact ? 28 : 58} height={compact ? 28 : 58} />
      {!compact && <img className="logo-text" src="/brand/wordmark.webp" alt="" width={174} height={48} />}
    </span>
  );
}
