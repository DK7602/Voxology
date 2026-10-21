// Rearranging a grid of cards: press and hold a card to pick it up (on a computer, just drag it),
// move it to a new place, let go. Picking a card up and letting go without moving opens its menu
// instead; right-click opens the menu too. The page scrolls while a card is held near the top or
// bottom of the screen.
import { useRef, useState, type CSSProperties, type DragEvent, type MouseEvent, type PointerEvent } from "react";

interface PressListeners {
  move: (e: globalThis.PointerEvent) => void;
  up: (e: globalThis.PointerEvent) => void;
  cancel: (e: globalThis.PointerEvent) => void;
  blur: () => void;
}

const HOLD_MS = 450;
/** moving further than this (px) is a drag (or, before a card is picked up by touch, a scroll) */
const SLOP = 8;
/** how close to the top / bottom of the screen (px) a held card scrolls the page; the bottom
 *  allows for the phone tab bar */
const EDGE_TOP = 90;
const EDGE_BOTTOM = 150;

interface Box {
  x: number;
  y: number;
  w: number;
  h: number;
}

interface Drag {
  index: number;
  dx: number;
  dy: number;
  target: number;
  moved: boolean;
}

/** a finger moving while a card is held drags it, not the page */
const noScroll = (e: TouchEvent) => e.preventDefault();

/** The click a browser sends when the finger comes up lands on whatever is now under it. */
function swallowNextClick() {
  const stop = (e: Event) => {
    e.preventDefault();
    e.stopPropagation();
  };
  window.addEventListener("click", stop, { capture: true, once: true });
  window.setTimeout(() => window.removeEventListener("click", stop, { capture: true }), 600);
}

export function useGridReorder(count: number, onMove: (from: number, to: number) => void, onMenu: (index: number) => void) {
  const cards = useRef<(HTMLElement | null)[]>([]);
  const [drag, setDrag] = useState<Drag | null>(null);
  const s = useRef({
    /** a press is under way: from the button or finger going down until it comes up */
    active: false,
    listeners: null as PressListeners | null,
    index: -1,
    pointerId: -1,
    type: "",
    clientX: 0,
    clientY: 0,
    startPageX: 0,
    startPageY: 0,
    boxes: [] as Box[],
    lifted: false,
    moved: false,
    target: -1,
    timer: 0,
    raf: 0,
  });

  const update = () => {
    const g = s.current;
    const dx = g.clientX + window.scrollX - g.startPageX;
    const dy = g.clientY + window.scrollY - g.startPageY;
    if (!g.moved && Math.hypot(dx, dy) > SLOP) g.moved = true;
    const b = g.boxes[g.index];
    const cx = b.x + b.w / 2 + dx;
    const cy = b.y + b.h / 2 + dy;
    // its new place: the card whose middle is nearest
    let best = g.index;
    let bestD = Infinity;
    g.boxes.forEach((o, j) => {
      const d = Math.hypot(o.x + o.w / 2 - cx, o.y + o.h / 2 - cy);
      if (d < bestD) {
        bestD = d;
        best = j;
      }
    });
    g.target = best;
    setDrag({ index: g.index, dx, dy, target: best, moved: g.moved });
  };

  const lift = () => {
    const g = s.current;
    window.clearTimeout(g.timer);
    g.boxes = cards.current.slice(0, count).map((el) => {
      const r = el!.getBoundingClientRect();
      return { x: r.left + window.scrollX, y: r.top + window.scrollY, w: r.width, h: r.height };
    });
    g.lifted = true;
    g.moved = false;
    g.target = g.index;
    window.addEventListener("touchmove", noScroll, { passive: false });
    if (g.type !== "mouse") navigator.vibrate?.(15);
    setDrag({ index: g.index, dx: 0, dy: 0, target: g.index, moved: false });
    const scroll = () => {
      const y = s.current.clientY;
      const step = y < EDGE_TOP ? -Math.ceil((EDGE_TOP - y) / 6) : y > innerHeight - EDGE_BOTTOM ? Math.ceil((y - innerHeight + EDGE_BOTTOM) / 6) : 0;
      if (step && s.current.moved) {
        window.scrollBy(0, step);
        update();
      }
      s.current.raf = requestAnimationFrame(scroll);
    };
    g.raf = requestAnimationFrame(scroll);
  };

  const finish = (drop: boolean) => {
    const g = s.current;
    if (!g.active) return;
    g.active = false;
    window.clearTimeout(g.timer);
    const l = g.listeners;
    if (l) {
      window.removeEventListener("pointermove", l.move, true);
      window.removeEventListener("pointerup", l.up, true);
      window.removeEventListener("pointercancel", l.cancel, true);
      window.removeEventListener("blur", l.blur);
    }
    g.listeners = null;
    if (!g.lifted) return;
    cancelAnimationFrame(g.raf);
    window.removeEventListener("touchmove", noScroll);
    g.lifted = false;
    setDrag(null);
    swallowNextClick();
    if (!drop) return;
    if (g.moved) {
      if (g.target !== g.index) onMove(g.index, g.target);
    } else onMenu(g.index);
  };

  const handlers = (index: number) => ({
    onPointerDown: (e: PointerEvent) => {
      const g = s.current;
      g.type = e.pointerType; // a right-click is a mouse, even right after a touch
      if (e.button !== 0) return;
      finish(false); // a second finger: drop whatever the first was doing
      Object.assign(g, {
        active: true,
        index,
        pointerId: e.pointerId,
        clientX: e.clientX,
        clientY: e.clientY,
        startPageX: e.clientX + window.scrollX,
        startPageY: e.clientY + window.scrollY,
        lifted: false,
        moved: false,
      });
      // the rest of this press is followed on the whole window, so letting go anywhere (off the
      // card, outside the browser) ends it; a mouse moving with no button down never starts one
      const l: PressListeners = {
        move: (ev) => {
          if (ev.pointerId !== g.pointerId) return;
          // the button is up but no release arrived: let go now
          if (ev.pointerType === "mouse" && !(ev.buttons & 1)) return finish(true);
          g.clientX = ev.clientX;
          g.clientY = ev.clientY;
          if (g.lifted) return update();
          if (Math.hypot(ev.clientX + window.scrollX - g.startPageX, ev.clientY + window.scrollY - g.startPageY) <= SLOP) return;
          if (g.type === "mouse") {
            // a mouse drags straight away
            lift();
            update();
          } else window.clearTimeout(g.timer); // a finger moving before the hold is a scroll
        },
        up: (ev) => ev.pointerId === g.pointerId && finish(true),
        cancel: (ev) => ev.pointerId === g.pointerId && finish(false),
        blur: () => finish(false),
      };
      g.listeners = l;
      window.addEventListener("pointermove", l.move, true);
      window.addEventListener("pointerup", l.up, true);
      window.addEventListener("pointercancel", l.cancel, true);
      window.addEventListener("blur", l.blur);
      window.clearTimeout(g.timer);
      g.timer = window.setTimeout(lift, HOLD_MS);
    },
    onContextMenu: (e: MouseEvent) => {
      // never the browser's own menu; a touch hold is handled by the timer, right-click opens ours
      e.preventDefault();
      if (s.current.type !== "touch") onMenu(index);
    },
    // no browser drag-and-drop of the link or its picture
    onDragStart: (e: DragEvent) => e.preventDefault(),
  });

  /** the held card follows the pointer; the cards it passes slide over a place */
  const style = (i: number): CSSProperties => {
    if (!drag) return {};
    if (i === drag.index) {
      return {
        transform: `translate(${drag.dx}px, ${drag.dy}px) scale(1.04)`,
        transition: drag.moved ? "none" : "transform 0.15s",
        zIndex: 5,
        position: "relative",
        boxShadow: "0 18px 40px rgba(0, 0, 0, 0.35)",
      };
    }
    const { index: from, target: to } = drag;
    const slot = from < to && i > from && i <= to ? i - 1 : from > to && i >= to && i < from ? i + 1 : i;
    const a = s.current.boxes[i];
    const b = s.current.boxes[slot];
    if (!a || !b || slot === i) return { transition: "transform 0.18s" };
    return { transform: `translate(${b.x - a.x}px, ${b.y - a.y}px)`, transition: "transform 0.18s" };
  };

  return {
    handlers,
    style,
    lifted: drag?.index ?? null,
    ref: (i: number) => (el: HTMLElement | null) => void (cards.current[i] = el),
  };
}
