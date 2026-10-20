// Small building blocks: popover menus, modals, toggles, toasts.
import {
  createContext,
  useCallback,
  useContext,
  useEffect,
  useLayoutEffect,
  useRef,
  useState,
  type CSSProperties,
  type ReactNode,
} from "react";
import { createPortal } from "react-dom";
import { IconClose } from "./Icons";

// ---------- popover ----------

interface PopoverProps {
  /** the element that opens it */
  trigger: (props: { onClick: () => void; "aria-expanded": boolean; ref: (el: HTMLElement | null) => void }) => ReactNode;
  children: (close: () => void) => ReactNode;
  align?: "start" | "end" | "center";
  width?: number;
}

/** A menu that opens under its trigger, rendered in a portal so panels can't clip it. */
export function Popover({ trigger, children, align = "start", width }: PopoverProps) {
  const [open, setOpen] = useState(false);
  const anchor = useRef<HTMLElement | null>(null);
  const pop = useRef<HTMLDivElement | null>(null);
  const [style, setStyle] = useState<CSSProperties>({});
  const close = useCallback(() => setOpen(false), []);

  useLayoutEffect(() => {
    if (!open || !anchor.current) return;
    const place = () => {
      const r = anchor.current!.getBoundingClientRect();
      const w = width ?? Math.max(r.width, 200);
      let left = align === "end" ? r.right - w : align === "center" ? r.left + r.width / 2 - w / 2 : r.left;
      left = Math.max(8, Math.min(left, window.innerWidth - w - 8));
      const below = window.innerHeight - r.bottom;
      const s: CSSProperties = { left: left + window.scrollX, width: w, position: "absolute" };
      if (below < 260 && r.top > below) s.bottom = window.innerHeight - r.top - window.scrollY + 6;
      else s.top = r.bottom + window.scrollY + 6;
      setStyle(s);
    };
    place();
    window.addEventListener("resize", place);
    window.addEventListener("scroll", place, true);
    return () => {
      window.removeEventListener("resize", place);
      window.removeEventListener("scroll", place, true);
    };
  }, [open, align, width]);

  useEffect(() => {
    if (!open) return;
    const onDown = (e: PointerEvent) => {
      const t = e.target as Node;
      if (pop.current?.contains(t) || anchor.current?.contains(t)) return;
      setOpen(false);
    };
    const onKey = (e: KeyboardEvent) => e.key === "Escape" && setOpen(false);
    document.addEventListener("pointerdown", onDown);
    document.addEventListener("keydown", onKey);
    return () => {
      document.removeEventListener("pointerdown", onDown);
      document.removeEventListener("keydown", onKey);
    };
  }, [open]);

  return (
    <>
      {trigger({
        onClick: () => setOpen((o) => !o),
        "aria-expanded": open,
        ref: (el) => {
          anchor.current = el;
        },
      })}
      {open &&
        createPortal(
          <div ref={pop} className="popover" role="menu" style={style}>
            {children(close)}
          </div>,
          document.body,
        )}
    </>
  );
}

// ---------- modal ----------

export function Modal({
  title,
  onClose,
  children,
  wide,
}: {
  title: ReactNode;
  onClose: () => void;
  children: ReactNode;
  wide?: boolean;
}) {
  useEffect(() => {
    const onKey = (e: KeyboardEvent) => e.key === "Escape" && onClose();
    document.addEventListener("keydown", onKey);
    return () => document.removeEventListener("keydown", onKey);
  }, [onClose]);
  return createPortal(
    <div className="modal-backdrop" onPointerDown={(e) => e.target === e.currentTarget && onClose()}>
      <div className="panel modal" role="dialog" aria-modal="true" style={wide ? { width: "min(860px, 100%)" } : undefined}>
        <div className="modal-head">
          <h2 style={{ fontSize: 24 }}>{title}</h2>
          <button className="icon-btn" onClick={onClose} aria-label="Close">
            <IconClose size={20} />
          </button>
        </div>
        {children}
      </div>
    </div>,
    document.body,
  );
}

// ---------- toggle ----------

export function Toggle({ checked, onChange, label }: { checked: boolean; onChange: (v: boolean) => void; label: string }) {
  return (
    <button
      type="button"
      role="switch"
      aria-checked={checked}
      aria-label={label}
      className="toggle"
      onClick={() => onChange(!checked)}
    />
  );
}

// ---------- toasts ----------

/** a button on a toast, e.g. Undo */
export interface ToastAction {
  label: string;
  run: () => void;
}
interface Toast {
  id: number;
  text: string;
  error?: boolean;
  action?: ToastAction;
}
const ToastContext = createContext<(text: string, error?: boolean, action?: ToastAction) => void>(() => {});

export function ToastProvider({ children }: { children: ReactNode }) {
  const [toasts, setToasts] = useState<Toast[]>([]);
  const dismiss = useCallback((id: number) => setToasts((t) => t.filter((x) => x.id !== id)), []);
  const push = useCallback(
    (text: string, error = false, action?: ToastAction) => {
      const id = Date.now() + Math.random();
      setToasts((t) => [...t, { id, text, error, action }]);
      // a toast with a button stays long enough to reach it
      setTimeout(() => dismiss(id), error || action ? 6000 : 3200);
    },
    [dismiss],
  );
  return (
    <ToastContext.Provider value={push}>
      {children}
      <div className="toast-stack" aria-live="polite">
        {toasts.map((t) => (
          <div key={t.id} className={"toast" + (t.error ? " error" : "")}>
            {t.text}
            {t.action && (
              <button
                className="toast-action"
                onClick={() => {
                  t.action!.run();
                  dismiss(t.id);
                }}
              >
                {t.action.label}
              </button>
            )}
          </div>
        ))}
      </div>
    </ToastContext.Provider>
  );
}

export function useToast() {
  return useContext(ToastContext);
}

/** Error message from anything thrown. */
export function errorText(e: unknown): string {
  return e instanceof Error ? e.message : String(e);
}

export function downloadBlob(data: BlobPart, filename: string, type: string) {
  const url = URL.createObjectURL(new Blob([data], { type }));
  const a = document.createElement("a");
  a.href = url;
  a.download = filename;
  document.body.appendChild(a);
  a.click();
  a.remove();
  setTimeout(() => URL.revokeObjectURL(url), 1000);
}

export function safeFileName(s: string): string {
  return (s || "song").replace(/[^\w\s.-]+/g, "").trim().replace(/\s+/g, " ").slice(0, 80) || "song";
}
