import { useState } from "react";
import { useInstall } from "../install";
import { IconDownload, IconPlus } from "./Icons";
import { Modal } from "./ui";

/** Safari's Share icon: a square with an arrow out of the top. */
function ShareIcon() {
  return (
    <svg width="22" height="22" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="1.8" strokeLinecap="round" strokeLinejoin="round" aria-hidden="true">
      <path d="M12 3v12" />
      <path d="m8 7 4-4 4 4" />
      <path d="M8 10H6a2 2 0 0 0-2 2v7a2 2 0 0 0 2 2h12a2 2 0 0 0 2-2v-7a2 2 0 0 0-2-2h-2" />
    </svg>
  );
}

export function IosInstallSteps() {
  return (
    <ol className="install-steps">
      <li>
        Open SongAlong in <b>Safari</b> and tap the <b>Share</b> button <span className="install-glyph"><ShareIcon /></span>
        <span className="muted"> (bottom of the screen on iPhone, top on iPad)</span>.
      </li>
      <li>
        Scroll down and tap <b>Add to Home Screen</b> <span className="install-glyph"><IconPlus size={18} /></span>.
      </li>
      <li>
        Tap <b>Add</b>. SongAlong appears on your Home Screen with its own icon and opens full screen.
      </li>
    </ol>
  );
}

/** "Install SongAlong": one tap on Android / desktop Chrome, the Share-menu steps on iPhone and
 *  iPad. Shows nothing once installed or where the browser can't install. */
export function InstallButton({ className = "btn" }: { className?: string }) {
  const { state, install } = useInstall();
  const [steps, setSteps] = useState(false);
  if (state === "installed" || state === "none") return null;
  return (
    <>
      <button className={className} onClick={() => (state === "prompt" ? void install() : setSteps(true))}>
        <IconDownload size={20} /> Install SongAlong
      </button>
      {steps && (
        <Modal title="Install SongAlong" onClose={() => setSteps(false)}>
          <IosInstallSteps />
        </Modal>
      )}
    </>
  );
}
