// Long-press (or right-click) on something that is also a link opens a menu instead of following
// the link. On a touch screen: hold still for half a second.
import { useRef, type MouseEvent, type PointerEvent } from "react";

/** The click a browser sends when the finger (or button) comes up after a long press lands on
 *  whatever is now under it: the opened menu. Swallow it. */
function swallowNextClick() {
  const stop = (e: Event) => {
    e.preventDefault();
    e.stopPropagation();
  };
  window.addEventListener("click", stop, { capture: true, once: true });
  window.setTimeout(() => window.removeEventListener("click", stop, { capture: true }), 600);
}

const HOLD_MS = 500;
/** moving further than this (px) is a scroll or a drag, not a press */
const SLOP = 10;

export function useLongPress(onLongPress: () => void) {
  const timer = useRef<number | null>(null);
  const start = useRef<{ x: number; y: number } | null>(null);
  const fired = useRef(false);
  const clear = () => {
    if (timer.current !== null) window.clearTimeout(timer.current);
    timer.current = null;
  };
  const fire = () => {
    clear();
    fired.current = true;
    navigator.vibrate?.(15);
    onLongPress();
  };
  return {
    onPointerDown: (e: PointerEvent) => {
      if (e.button !== 0) return; // a right-click arrives as contextmenu
      fired.current = false;
      start.current = { x: e.clientX, y: e.clientY };
      clear();
      timer.current = window.setTimeout(fire, HOLD_MS);
    },
    onPointerMove: (e: PointerEvent) => {
      if (start.current && Math.hypot(e.clientX - start.current.x, e.clientY - start.current.y) > SLOP) clear();
    },
    onPointerUp: () => {
      clear();
      if (fired.current) swallowNextClick();
      fired.current = false;
    },
    onPointerCancel: clear,
    onPointerLeave: clear,
    onContextMenu: (e: MouseEvent) => {
      // right-click, the menu key, or a touch hold the browser reported first; never the browser's own menu
      e.preventDefault();
      if (!fired.current) fire();
    },
  };
}
