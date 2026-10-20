// What a long-press (or right-click) on a song card offers: take the song off this list, save it to
// (or remove it from) the library, add it to a playlist.
import { useState } from "react";
import type { SongSummary } from "../../shared/api";
import { api } from "../api";
import { AddToPlaylist } from "./AddToPlaylist";
import { IconClose, IconHeart, IconList } from "./Icons";
import { Modal, errorText, useToast } from "./ui";

/** where the card is: Home's "Keep playing" row, Discover, My Library, or elsewhere */
export type SongList = "recent" | "discover" | "library" | "other";

export interface SongChange {
  /** taken off this list (true), or put back by Undo (false) */
  removed?: boolean;
  favorite?: boolean;
}

export function SongMenu({
  song,
  list,
  onClose,
  onChange,
}: {
  song: SongSummary;
  list: SongList;
  onClose: () => void;
  onChange: (change: SongChange) => void;
}) {
  const toast = useToast();
  const [picking, setPicking] = useState(false);
  if (picking) return <AddToPlaylist songId={song.id} onClose={onClose} />;

  const run = async (work: () => Promise<void>) => {
    onClose();
    try {
      await work();
    } catch (e) {
      toast(errorText(e), true);
    }
  };
  /** hide from Keep playing / Discover, with Undo */
  const hide = (where: "recent" | "discover", label: string) =>
    run(async () => {
      await api.hideSong(song.id, where);
      onChange({ removed: true });
      toast(`Removed from ${label}`, false, {
        label: "Undo",
        run: () => void api.hideSong(song.id, where, false).then(() => onChange({ removed: false }), (e) => toast(errorText(e), true)),
      });
    });
  const setSaved = (on: boolean) =>
    run(async () => {
      await api.favorite(song.id, on);
      onChange({ favorite: on });
      toast(on ? "Saved to your library" : "Removed from your library", false, {
        label: "Undo",
        run: () => void api.favorite(song.id, !on).then(() => onChange({ favorite: !on }), (e) => toast(errorText(e), true)),
      });
    });
  /** off the library, nothing deleted: a song you made is hidden from it, a saved song unsaved
   *  (saving it again brings it back) */
  const removeFromLibrary = () =>
    run(async () => {
      if (song.mine) await api.hideSong(song.id, "library");
      if (song.favorite) await api.favorite(song.id, false);
      onChange({ removed: true });
      toast("Removed from your library", false, {
        label: "Undo",
        run: () =>
          void (async () => {
            if (song.mine) await api.hideSong(song.id, "library", false);
            if (song.favorite) await api.favorite(song.id, true);
            onChange({ removed: false });
          })().catch((e) => toast(errorText(e), true)),
      });
    });

  return (
    <Modal title={song.title || "Untitled"} onClose={onClose}>
      {song.artist && <p className="muted" style={{ marginTop: -8 }}>{song.artist}</p>}
      <div className="song-menu">
        {list === "recent" && (
          <button className="menu-item" onClick={() => hide("recent", "Keep playing")}>
            <IconClose size={20} /> Remove from Keep playing
          </button>
        )}
        {list === "discover" && (
          <button className="menu-item" onClick={() => hide("discover", "Discover")}>
            <IconClose size={20} /> Remove from Discover
          </button>
        )}
        {list === "library" && (
          <button className="menu-item" onClick={removeFromLibrary}>
            <IconClose size={20} /> Remove from library
          </button>
        )}
        {list !== "library" && (
          <button className="menu-item" onClick={() => setSaved(!song.favorite)}>
            <IconHeart size={20} filled={song.favorite} /> {song.favorite ? "Remove from library" : "Save to library"}
          </button>
        )}
        <button className="menu-item" onClick={() => setPicking(true)}>
          <IconList size={20} /> Add to playlist
        </button>
      </div>
    </Modal>
  );
}
