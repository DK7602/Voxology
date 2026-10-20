// "Add to playlist": pick one of your playlists, or make a new one, for a song.
import { useEffect, useState } from "react";
import type { PlaylistSummary } from "../../shared/api";
import { api } from "../api";
import { Modal, errorText, useToast } from "./ui";

export function AddToPlaylist({ songId, onClose }: { songId: string; onClose: () => void }) {
  const toast = useToast();
  const [lists, setLists] = useState<PlaylistSummary[] | null>(null);
  const [name, setName] = useState("");
  useEffect(() => {
    api.playlists().then((r) => setLists(r.playlists)).catch((e) => toast(errorText(e), true));
  }, [toast]);
  const add = async (id: string, listName: string) => {
    try {
      await api.addToPlaylist(id, songId);
      toast(`Added to ${listName}`);
      onClose();
    } catch (e) {
      toast(errorText(e), true);
    }
  };
  const create = async () => {
    if (!name.trim()) return;
    try {
      const r = await api.createPlaylist(name.trim());
      await add(r.playlist.id, r.playlist.name);
    } catch (e) {
      toast(errorText(e), true);
    }
  };
  return (
    <Modal title="Add to playlist" onClose={onClose}>
      <div style={{ display: "grid", gap: 6 }}>
        {lists === null && <div className="muted">Loading…</div>}
        {lists?.map((l) => (
          <button key={l.id} className="menu-item" onClick={() => add(l.id, l.name)}>
            {l.name} <span className="faint" style={{ marginLeft: "auto" }}>{l.count} songs</span>
          </button>
        ))}
        {lists?.length === 0 && <div className="muted">No playlists yet.</div>}
      </div>
      <form
        style={{ display: "flex", gap: 8, marginTop: 16 }}
        onSubmit={(e) => {
          e.preventDefault();
          void create();
        }}
      >
        <input className="input" placeholder="New playlist name" value={name} onChange={(e) => setName(e.target.value)} maxLength={80} />
        <button className="btn btn-primary" type="submit">Create</button>
      </form>
    </Modal>
  );
}
