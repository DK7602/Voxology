import { Component, type ReactNode } from "react";

/** Catches a page that fails while drawing, so the app shows a way out instead of a blank screen.
 *  `resetKey` (the page address) clears it when the person goes somewhere else. */
export class ErrorBoundary extends Component<{ resetKey: string; children: ReactNode }, { failed: boolean; key: string }> {
  state = { failed: false, key: this.props.resetKey };

  static getDerivedStateFromError() {
    return { failed: true };
  }

  static getDerivedStateFromProps(props: { resetKey: string }, state: { failed: boolean; key: string }) {
    return props.resetKey !== state.key ? { failed: false, key: props.resetKey } : null;
  }

  componentDidCatch(error: unknown) {
    console.error("Page failed:", error);
  }

  render() {
    if (!this.state.failed) return this.props.children;
    return (
      <div className="panel page-panel empty" role="alert">
        <h2 style={{ marginTop: 0 }}>Something went wrong on this page</h2>
        <p className="muted">Reloading usually fixes it.</p>
        <div style={{ display: "flex", gap: 10, justifyContent: "center", flexWrap: "wrap" }}>
          <button className="btn btn-primary" onClick={() => location.reload()}>Reload</button>
          <a className="btn" href="/">Home</a>
        </div>
      </div>
    );
  }
}
