#pragma once

#include "vox/HoneyTune.h"

#include <cmath>
#include <memory>
#include <string>
#include <vector>

/** What the Honey Tune editor shows and edits (no JUCE: shared by the plug-in and the preview tool). */
namespace honeyui {

constexpr int kAutoKey = 12;   // Key choice "Auto": detected from the clip

/** The document's settings (they apply to every note you haven't changed yourself). */
struct Settings
{
    int key = kAutoKey;        // 0 = C ... 11 = B, 12 = auto
    int scale = 0;             // vox::kScaleNames (0 = chromatic)
    double snap = 1.0;         // 0..1 of the way to the note
    double drift = 1.0;        // keep 0..1
    double vibrato = 1.0;      // keep 0..1
    bool operator== (const Settings& o) const
    {
        auto same = [] (double a, double b) { return std::abs (a - b) < 1.0e-9; };
        return key == o.key && scale == o.scale && same (snap, o.snap) && same (drift, o.drift) && same (vibrato, o.vibrato);
    }
};

/** A change you made to one note. Negative drift / vibrato = follow the settings. */
struct NoteEdit
{
    bool moved = false;        // target set by hand
    double target = 0.0;       // MIDI (when moved)
    double drift = -1.0, vibrato = -1.0;
    bool isDefault() const { return ! moved && drift < 0.0 && vibrato < 0.0; }
};

/** The key and scale actually used: "Auto" takes the detected key (and its scale when sure enough). */
inline void resolveKey (const Settings& s, const vox::KeyGuess& guess, int& key, int& scale)
{
    key = s.key == kAutoKey ? guess.key : s.key;
    scale = s.key == kAutoKey && s.scale == 0 && guess.confidence >= 0.45 ? (guess.minor ? 2 : 1) : s.scale;
}

/** Where the key / scale puts a note (the nearest allowed note, MIDI). */
inline double keyNote (double pitch, int key, int scale)
{
    return vox::PitchCorrector::targetNote (pitch, key, scale, -1);
}

/** The notes as they will sound: hand edits first, then the settings. */
inline std::vector<vox::honey::Note> applyEdits (std::vector<vox::honey::Note> notes, const std::vector<NoteEdit>& edits,
                                                 const Settings& s, int key, int scale)
{
    for (size_t i = 0; i < notes.size(); ++i)
    {
        auto& n = notes[i];
        const NoteEdit e = i < edits.size() ? edits[i] : NoteEdit {};
        n.target = e.moved ? e.target : n.pitch + (keyNote (n.pitch, key, scale) - n.pitch) * s.snap;
        n.drift = e.drift >= 0.0 ? e.drift : s.drift;
        n.vibrato = e.vibrato >= 0.0 ? e.vibrato : s.vibrato;
        n.edited = true;
    }
    return notes;
}

struct NoteView
{
    vox::honey::Note note;     // target / drift / vibrato as they will sound
    NoteEdit edit;
    bool wasOff = false;       // sung more than 25 cents from the key's nearest note
    bool fixed = false;        // ... and now it lands on one (or you placed it yourself)
};

/** One clip, ready to draw. */
struct Snapshot
{
    int status = 0;            // 0 waiting, 1 listening, 2 ready, 3 couldn't read
    std::string name;
    std::shared_ptr<const vox::honey::Track> track;
    std::vector<NoteView> notes;
    int key = 0, scale = 0;
    vox::KeyGuess guess;
    double seconds = 0.0;
};

inline Snapshot makeSnapshot (std::shared_ptr<const vox::honey::Track> track, const std::vector<vox::honey::Note>& notes,
                              const std::vector<NoteEdit>& edits, const vox::KeyGuess& guess, const Settings& s)
{
    Snapshot snap;
    snap.track = track;
    snap.guess = guess;
    resolveKey (s, guess, snap.key, snap.scale);
    if (track != nullptr && ! track->time.empty())
        snap.seconds = track->time.back() / track->sampleRate;
    const auto sounding = applyEdits (notes, edits, s, snap.key, snap.scale);
    for (size_t i = 0; i < notes.size(); ++i)
    {
        NoteView v;
        v.note = sounding[i];
        v.edit = i < edits.size() ? edits[i] : NoteEdit {};
        v.wasOff = std::abs (notes[i].pitch - keyNote (notes[i].pitch, snap.key, snap.scale)) > 0.25;
        v.fixed = v.wasOff && (v.edit.moved || std::abs (v.note.target - keyNote (v.note.target, snap.key, snap.scale)) <= 0.25);
        snap.notes.push_back (v);
    }
    return snap;
}

/** The editor talks to the clip through this. */
struct Model
{
    virtual ~Model() = default;
    virtual Snapshot snapshot() = 0;
    virtual Settings getSettings() = 0;
    virtual void setSettings (const Settings&) = 0;
    virtual void setEdit (int noteIndex, const NoteEdit& edit) = 0;
    virtual void resetAllEdits() = 0;
};

} // namespace honeyui
