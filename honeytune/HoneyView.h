#pragma once

#include "vox/HoneyTune.h"

#include <cmath>
#include <cstdio>
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
    double shift = 0.0;        // seconds: moved earlier (-) / later (+)
    double length = 1.0;       // stretched (> 1) / shortened (< 1)
    double formant = 0.0;      // semitones: + thinner, - deeper
    bool timed() const { return std::abs (shift) > 1.0e-6 || std::abs (length - 1.0) > 1.0e-6; }
    bool isDefault() const { return ! moved && drift < 0.0 && vibrato < 0.0 && ! timed() && std::abs (formant) < 1.0e-6; }
};

/** The song's key as the host knows it (its key signature / scale track), when it shares one. */
struct HostKey
{
    bool valid = false;
    int key = 0;     // 0 = C ... 11 = B
    int scale = 1;   // vox::kScaleNames
    bool operator== (const HostKey& o) const { return valid == o.valid && (! valid || (key == o.key && scale == o.scale)); }
};

/** A host key signature (ARA: root on the circle of fifths, 0 = C, 1 = G, -1 = F; which of the 12 intervals
    above the root are used) as a key and the closest of our scales. */
inline HostKey hostKeyFromSignature (int circleOfFifths, const bool (&used)[12])
{
    HostKey h;
    h.valid = true;
    h.key = ((circleOfFifths * 7) % 12 + 12) % 12;
    int best = 1 << 20;
    for (int sc = 1; sc < vox::kScales; ++sc)   // never Chromatic: a key signature always has a scale
    {
        int miss = 0;
        for (size_t i = 0; i < 12; ++i) miss += vox::kScaleMasks[static_cast<size_t> (sc)][i] != used[i] ? 1 : 0;
        if (miss < best) { best = miss; h.scale = sc; }
    }
    return h;
}

/** Under this the voice alone can't tell the key (Auto then snaps to the nearest note, any note). */
constexpr double kSureEnough = 0.45;

/** The key and scale actually used: "Auto" takes the host's key when it has one, else the detected key
    (and its scale when sure enough). */
inline void resolveKey (const Settings& s, const vox::KeyGuess& guess, int& key, int& scale, const HostKey& host = {})
{
    if (s.key == kAutoKey && host.valid)
    {
        key = host.key;
        scale = s.scale == 0 ? host.scale : s.scale;
        return;
    }
    key = s.key == kAutoKey ? guess.key : s.key;
    scale = s.key == kAutoKey && s.scale == 0 && guess.confidence >= kSureEnough ? (guess.minor ? 2 : 1) : s.scale;
}

/** "Auto" fell back to any note because the voice alone wasn't clear (the editor asks you to pick the key). */
inline bool keyUnsure (const Settings& s, const vox::KeyGuess& guess, const HostKey& host = {})
{
    return s.key == kAutoKey && ! host.valid && s.scale == 0 && guess.confidence < kSureEnough;
}

/** The red glow: the note was SUNG off the key (in either view), or it will play off the key (Tuned).
    Snap pulls sung-off notes onto the key, so judging only where they land would never show any red. */
inline bool glowsRed (bool wasOff, bool offWhereItLands, bool originalView)
{
    return wasOff || (! originalView && offWhereItLands);
}

/** Where the key / scale puts a note (the nearest allowed note, MIDI). */
inline double keyNote (double pitch, int key, int scale)
{
    return vox::PitchCorrector::targetNote (pitch, key, scale, -1);
}

/** The notes as they will sound: hand edits first, then the settings. */
inline std::vector<vox::honey::Note> applyEdits (std::vector<vox::honey::Note> notes, const std::vector<NoteEdit>& edits,
                                                 const Settings& s, int key, int scale, double sampleRate = 48000.0)
{
    for (size_t i = 0; i < notes.size(); ++i)
    {
        auto& n = notes[i];
        const NoteEdit e = i < edits.size() ? edits[i] : NoteEdit {};
        n.target = e.moved ? e.target : n.pitch + (keyNote (n.pitch, key, scale) - n.pitch) * s.snap;
        n.drift = e.drift >= 0.0 ? e.drift : s.drift;
        n.vibrato = e.vibrato >= 0.0 ? e.vibrato : s.vibrato;
        n.formant = e.formant;
        if (e.timed())
        {
            n.outStart = n.start + e.shift * sampleRate;
            n.outEnd = n.outStart + (n.end - n.start) * std::max (0.1, e.length);
        }
        n.edited = true;
    }
    return notes;
}

struct NoteView
{
    vox::honey::Note note;     // target / drift / vibrato as they will sound
    NoteEdit edit;
    bool wasOff = false;       // sung more than 25 cents from the key's nearest note
    bool off = false;          // where it will SOUND is more than 25 cents from the key's nearest note
    bool fixed = false;        // was off-key, now lands on a note of the key
};

/** The host's bars and beats over the clip (from the song's tempo map and time signatures), in clip
    seconds. Empty when the host doesn't share them: the editor then shows seconds. */
struct Timeline
{
    struct Line { double seconds; int bar; int beat; };   // bar from 1, beat from 1 (1 = the bar line)
    std::vector<Line> lines;
    double songOffset = 0.0;   // song time = clip time + songOffset

    bool valid() const { return lines.size() >= 2; }

    /** "Bar 5 . 2" style position for a clip time, plus the song time. */
    std::string describe (double clipSeconds) const
    {
        const double song = clipSeconds + songOffset;
        const int ms = static_cast<int> (std::round (std::max (0.0, song) * 1000.0));
        char time[32];
        std::snprintf (time, sizeof time, "%d:%02d.%03d", ms / 60000, (ms / 1000) % 60, ms % 1000);
        if (! valid()) return time;
        const Line* at = &lines.front();
        for (const auto& l : lines) { if (l.seconds > clipSeconds) break; at = &l; }
        char out[64];
        std::snprintf (out, sizeof out, "%d . %d     %s", at->bar, at->beat, time);
        return out;
    }
};

/** One clip, ready to draw. */
struct Snapshot
{
    Timeline timeline;
    int status = 0;            // 0 waiting, 1 listening, 2 ready, 3 couldn't read
    std::string name;
    std::shared_ptr<const vox::honey::Track> track;
    std::vector<NoteView> notes;
    int key = 0, scale = 0;
    vox::KeyGuess guess;
    bool keyFromHost = false;  // the key came from the host's project
    bool keyUnsure = false;    // Auto couldn't tell the key from the voice: snapping to any note
    double seconds = 0.0;
};

inline Snapshot makeSnapshot (std::shared_ptr<const vox::honey::Track> track, const std::vector<vox::honey::Note>& notes,
                              const std::vector<NoteEdit>& edits, const vox::KeyGuess& guess, const Settings& s,
                              const HostKey& host = {})
{
    Snapshot snap;
    snap.track = track;
    snap.guess = guess;
    resolveKey (s, guess, snap.key, snap.scale, host);
    snap.keyFromHost = s.key == kAutoKey && host.valid;
    snap.keyUnsure = keyUnsure (s, guess, host);
    if (track != nullptr && ! track->time.empty())
        snap.seconds = track->time.back() / track->sampleRate;
    const auto sounding = applyEdits (notes, edits, s, snap.key, snap.scale, track != nullptr ? track->sampleRate : 48000.0);
    for (size_t i = 0; i < notes.size(); ++i)
    {
        NoteView v;
        v.note = sounding[i];
        v.edit = i < edits.size() ? edits[i] : NoteEdit {};
        v.wasOff = std::abs (notes[i].pitch - keyNote (notes[i].pitch, snap.key, snap.scale)) > 0.25;
        v.off = std::abs (v.note.target - keyNote (v.note.target, snap.key, snap.scale)) > 0.25;
        v.fixed = v.wasOff && ! v.off;
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
    /** Undo / redo the last edit (false: nothing to undo / redo). */
    virtual bool undo() { return false; }
    virtual bool redo() { return false; }
    /** A / B: hear (and see) the clip as it was recorded, edits kept but bypassed. */
    virtual void setOriginal (bool) = 0;
    virtual bool isOriginal() = 0;
    /** Ask the host to move its playhead to this clip time (seconds). */
    virtual void seek (double /*clipSeconds*/) {}
};

} // namespace honeyui
