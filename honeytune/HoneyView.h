#pragma once

#include "vox/HoneyLink.h"
#include "vox/HoneyTune.h"
#include "suite/RackLink.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <memory>
#include <string>
#include <utility>
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
    int added = 0, removed = 0;   // notes you switched into / out of the key (bit n: 0 = C .. 11 = B)
    double a4 = 0.0;           // tuning: A = this many Hz (0 = Auto: the beat's, from Voxology on the track; else 440)
    bool operator== (const Settings& o) const
    {
        auto same = [] (double a, double b) { return std::abs (a - b) < 1.0e-9; };
        return key == o.key && scale == o.scale && same (snap, o.snap) && same (drift, o.drift) && same (vibrato, o.vibrato)
               && added == o.added && removed == o.removed && same (a4, o.a4);
    }
};

/** The tunings offered (A = Hz; 0 = Auto). */
inline constexpr double kTunings[] = { 0.0, 432.0, 436.0, 438.0, 439.0, 440.0, 441.0, 442.0, 443.0, 444.0, 446.0 };

/** A change you made to one note. Negative drift / vibrato = follow the settings. */
struct NoteEdit
{
    bool moved = false;        // target set by hand
    double target = 0.0;       // MIDI (when moved)
    double drift = -1.0, vibrato = -1.0;
    double shift = 0.0;        // seconds: moved earlier (-) / later (+)
    double length = 1.0;       // stretched (> 1) / shortened (< 1)
    double formant = 0.0;      // semitones: + thinner, - deeper
    double gainDb = 0.0;       // its level: louder (+) / quieter (-)
    bool muted = false;
    bool checked = false;      // you checked it ("notes to check" leaves it alone); changes nothing you hear
    bool timed() const { return std::abs (shift) > 1.0e-6 || std::abs (length - 1.0) > 1.0e-6; }
    bool levelled() const { return muted || std::abs (gainDb) > 1.0e-6; }
    /** Nothing changed that you'd hear (a note only `checked` still counts as untouched). */
    bool isDefault() const { return ! moved && drift < 0.0 && vibrato < 0.0 && ! timed() && std::abs (formant) < 1.0e-6 && ! levelled(); }
    /** ... nor checked: nothing to save. */
    bool isBlank() const { return isDefault() && ! checked; }
    bool operator== (const NoteEdit& o) const
    {
        auto same = [] (double a, double b) { return std::abs (a - b) < 1.0e-9; };
        return moved == o.moved && (! moved || same (target, o.target)) && same (drift, o.drift) && same (vibrato, o.vibrato)
               && same (shift, o.shift) && same (length, o.length) && same (formant, o.formant) && same (gainDb, o.gainDb)
               && muted == o.muted && checked == o.checked;
    }
};

/** A note's level: Ctrl + drag, the menu (+ / - 3 dB) and M (mute). */
inline constexpr double kMinGainDb = -30.0, kMaxGainDb = 12.0;

/** SNAP TO NOTE (0..1) for a note `semis` from the key's note: how far of the way it's pulled. Not the same share for
    every note (Melodyne's "correct pitch" idea): the further off, the harder the pull, so a partial setting fixes the
    notes you hear as wrong first and leaves the nearly-right ones (and their life) alone. 0 = none, 1 = all the way;
    a note a quarter tone off is pulled exactly `snap` of the way. (70 %: 50 cents off -> 4.5 cents, 5 cents -> 4.) */
inline double snapPull (double semis, double snap)
{
    snap = std::clamp (snap, 0.0, 1.0);
    if (snap >= 1.0) return 1.0;
    return 1.0 - std::pow (1.0 - snap, std::abs (semis) / 0.25);
}

/** Values from outside (a project's archive: damaged, or from a newer / older version) made safe: inside the ranges
    the editor offers, never NaN or inf (a NaN target or a huge shift broke the render's time map). */
inline double finiteOr (double v, double fallback) { return std::isfinite (v) ? v : fallback; }
inline Settings sanitised (Settings s)
{
    s.key = std::clamp (s.key, 0, kAutoKey);
    s.scale = std::clamp (s.scale, 0, vox::kScales - 1);
    s.snap = std::clamp (finiteOr (s.snap, 1.0), 0.0, 1.0);
    s.drift = std::clamp (finiteOr (s.drift, 1.0), 0.0, 1.0);
    s.vibrato = std::clamp (finiteOr (s.vibrato, 1.0), 0.0, 1.0);
    s.added &= 0xfff;
    s.removed &= 0xfff & ~s.added;
    s.a4 = finiteOr (s.a4, 0.0);
    s.a4 = s.a4 <= 0.0 ? 0.0 : std::clamp (s.a4, 415.0, 466.0);
    return s;
}
inline NoteEdit sanitised (NoteEdit e)
{
    if (! std::isfinite (e.target)) e.moved = false;
    e.target = std::clamp (finiteOr (e.target, 0.0), 0.0, 127.0);
    auto keep = [] (double v) { return std::isfinite (v) && v >= 0.0 ? std::min (v, 1.0) : -1.0; };   // < 0: the settings'
    e.drift = keep (e.drift);
    e.vibrato = keep (e.vibrato);
    e.shift = std::clamp (finiteOr (e.shift, 0.0), -3600.0, 3600.0);
    e.length = std::clamp (finiteOr (e.length, 1.0), 0.1, 1000.0);
    e.formant = std::clamp (finiteOr (e.formant, 0.0), -vox::kMaxFormant, vox::kMaxFormant);
    e.gainDb = std::clamp (finiteOr (e.gainDb, 0.0), kMinGainDb, kMaxGainDb);
    return e;
}

/** The song's key from outside the clip: the host's project (its key signature / scale track), the key the Voxology on
    this clip's track tunes to (vox::honeylink), or what a Voxology hears of the beat (vox::keyshare). */
struct HostKey
{
    bool valid = false;
    int key = 0;             // 0 = C ... 11 = B
    int scale = 1;           // vox::kScaleNames
    bool fromBeat = false;   // Voxology's beat key (on the beat, or a beat on a Voxology's side-chain)
    bool fromTrack = false;  // the key the Voxology on this clip's track uses (its beat, or picked there)
    bool weak = false;       // ... which that Voxology learned from the singing: the clip's own sure key comes first
    bool tuned = false;      // ... and the beat's tuning it uses (fromTrack, from the beat)
    int tuneCents = 0;       // vs A = 440 Hz
    bool operator== (const HostKey& o) const
    {
        return valid == o.valid && (! valid || (key == o.key && scale == o.scale && fromBeat == o.fromBeat && fromTrack == o.fromTrack && weak == o.weak
                                                && tuned == o.tuned && tuneCents == o.tuneCents));
    }
};

/** The Voxology on this clip's track, as Honey Tune reads it (vox::honeylink): its Pitch and the key it uses. */
inline HostKey trackKeyFrom (const vox::honeylink::VoxInfo& v)
{
    HostKey h;
    if (v.keyFrom == vox::honeylink::KeyFrom::none) return h;
    h.valid = true;
    h.key = std::clamp (v.key, 0, 11);
    h.scale = std::clamp (v.scale, 0, vox::kScales - 1);
    h.fromTrack = true;
    h.weak = v.keyFrom == vox::honeylink::KeyFrom::voice;
    h.tuned = v.keyFrom == vox::honeylink::KeyFrom::beat;
    h.tuneCents = h.tuned ? std::clamp (v.tuneCents, -50, 50) : 0;
    return h;
}

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

/** Which key from outside the clip Auto uses: the project's (you set it in the host), else the one the Voxology on this
    track tunes to (from the beat, or picked there), else the beat's as any Voxology hears it; a key that Voxology on this
    track only learned from the singing comes last (resolveKey: after the clip's own sure key). */
inline HostKey outsideKey (const HostKey& project, const HostKey& beat, const HostKey& track = {})
{
    if (project.valid) return project;
    if (track.valid && ! track.weak) return track;
    if (beat.valid) return beat;
    return track;
}

/** Under this the voice alone can't tell the key (Auto then snaps to the nearest note, any note). */
constexpr double kSureEnough = 0.45;

/** The key and scale actually used: "Auto" takes the key from outside the clip when there is one (the
    project's, else Voxology's beat key), else the one detected from the voice (and its scale when sure enough). */
/** Auto uses this key from outside the clip (else the voice's). */
inline bool usesOutsideKey (const Settings& s, const vox::KeyGuess& guess, const HostKey& host)
{
    return s.key == kAutoKey && host.valid && ! (host.weak && guess.confidence >= kSureEnough);
}

inline void resolveKey (const Settings& s, const vox::KeyGuess& guess, int& key, int& scale, const HostKey& host = {})
{
    if (usesOutsideKey (s, guess, host))
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

/** The red glow: the note will PLAY off the key (Tuned: where it lands, e.g. moved onto an off-key note;
    Original: as sung). Notes Snap pulled into the key don't glow (they turn gold). */
inline bool glowsRed (bool wasOff, bool offWhereItLands, bool originalView)
{
    return originalView ? wasOff : offWhereItLands;
}

/** Where the key / scale puts a note (the nearest allowed note, MIDI). */
inline double keyNote (double pitch, int key, int scale)
{
    return vox::PitchCorrector::targetNote (pitch, key, scale, -1);
}

/** Semitones from A = 440 Hz to A = hz (0: none). */
inline double tuneOf (double hz) { return hz > 0.0 ? 12.0 * std::log2 (hz / 440.0) : 0.0; }

/** The notes a note can land on: the key's scale with the notes you switched in / out, in a tuning (the whole grid a
    few cents up or down: a beat tuned to A = 443 Hz puts every "in tune" note 12 cents up). */
struct KeyGrid
{
    int key = 0, scale = 0;
    int added = 0, removed = 0;   // bit n: 0 = C .. 11 = B
    double tune = 0.0;            // semitones from A = 440 Hz
    int mask() const { return vox::PitchCorrector::allowedMask (key, scale, added, 0, removed); }
    /** In the key (a row of the roll, MIDI as untuned). */
    bool has (int midi) const { return ((mask() >> (((midi % 12) + 12) % 12)) & 1) != 0; }
    /** The nearest note it may land on (MIDI, in the tuning). */
    double note (double pitch) const
    {
        return vox::PitchCorrector::targetNote (pitch - tune, key, scale, -1, added, 0, removed) + tune;
    }
    /** The note it may land on next to `pitch`'s, above (+1) / below (-1) (in the tuning). */
    double step (double pitch, int dir) const
    {
        double next = std::round (pitch - tune);
        for (int k = 0; k < 12; ++k)
        {
            next += dir > 0 ? 1.0 : -1.0;
            if (has (static_cast<int> (next))) break;
        }
        return next + tune;
    }
};

/** The grid for these settings: the key and scale used, the notes switched in / out (never all of them out), the
    tuning (A4 set, else Auto: `autoCents`, the beat's tuning from Voxology on the track). */
inline KeyGrid keyGrid (const Settings& s, int key, int scale, int autoCents = 0)
{
    KeyGrid g;
    g.key = key;
    g.scale = scale;
    g.added = s.added & 0xfff;
    g.removed = s.removed & 0xfff & ~g.added;
    g.tune = s.a4 > 0.0 ? tuneOf (s.a4) : std::clamp (autoCents, -50, 50) / 100.0;
    if (g.mask() == 0) g.removed = 0;
    return g;
}

/** keyNote() on a grid. */
inline double keyNote (double pitch, const KeyGrid& g) { return g.note (pitch); }

/** A note moved or stretched in time stays this far from its neighbours (and inside the clip). */
constexpr double kTimeGapSeconds = 0.005, kMinNoteSeconds = 0.03;

/** The notes as they will sound: hand edits first, then the settings. A note moved / stretched in time is kept between
    its neighbours (as they sound) and inside the clip (clipSamples; <= 0 = the clip's end isn't known): the render
    never squeezes one note into another, whatever an edit (or a damaged project) says. */
inline std::vector<vox::honey::Note> applyEdits (std::vector<vox::honey::Note> notes, const std::vector<NoteEdit>& edits,
                                                 const Settings& s, const KeyGrid& grid, double sampleRate = 48000.0,
                                                 double clipSamples = 0.0)
{
    for (size_t i = 0; i < notes.size(); ++i)
    {
        auto& n = notes[i];
        const NoteEdit e = i < edits.size() ? edits[i] : NoteEdit {};
        const double toKey = grid.note (n.pitch) - n.pitch;
        n.target = e.moved ? e.target : n.pitch + toKey * snapPull (toKey, s.snap);
        n.drift = e.drift >= 0.0 ? e.drift : s.drift;
        n.vibrato = e.vibrato >= 0.0 ? e.vibrato : s.vibrato;
        n.formant = e.formant;
        n.gainDb = e.gainDb;
        n.muted = e.muted;
        if (e.timed())
        {
            // Not before the clip's start (a negative start means "where it was sung": the note jumped back).
            n.outStart = std::max (0.0, n.start + e.shift * sampleRate);
            n.outEnd = n.outStart + (n.end - n.start) * std::max (0.1, e.length);
        }
        n.edited = true;
    }
    // Between the neighbours, in order (each after the one before as it now sounds, before the next as it would): 5 ms
    // apart, or as close as they were sung (the parts of a split note follow on as they did).
    const double gap = kTimeGapSeconds * sampleRate, minLen = kMinNoteSeconds * sampleRate;
    const auto apart = [&] (const vox::honey::Note& a, const vox::honey::Note& b) { return std::min (gap, std::max (1.0, b.start - a.end)); };
    for (size_t i = 0; i < notes.size(); ++i)
    {
        auto& n = notes[i];
        if (n.outStart < 0.0 && n.outEnd < 0.0) continue;
        const double lo = i > 0 ? notes[i - 1].soundEnd() + apart (notes[i - 1], n) : 0.0;
        double hi = i + 1 < notes.size() ? notes[i + 1].soundStart() - apart (n, notes[i + 1]) : (clipSamples > 0.0 ? clipSamples - gap : 1.0e300);
        if (clipSamples > 0.0) hi = std::min (hi, clipSamples - gap);
        if (hi - lo < minLen) { n.outStart = n.outEnd = -1.0; continue; }   // no room at all: where it was sung
        const double len = std::clamp (n.soundEnd() - n.soundStart(), minLen, hi - lo);
        n.outStart = std::clamp (n.soundStart(), lo, hi - len);
        n.outEnd = n.outStart + len;
    }
    return notes;
}
/** ... with the key and scale alone (the settings' notes switched in / out and A4; Auto tuning = 440). */
inline std::vector<vox::honey::Note> applyEdits (std::vector<vox::honey::Note> notes, const std::vector<NoteEdit>& edits,
                                                 const Settings& s, int key, int scale, double sampleRate = 48000.0,
                                                 double clipSamples = 0.0)
{
    return applyEdits (std::move (notes), edits, s, keyGrid (s, key, scale), sampleRate, clipSamples);
}

/** Your edits carried over to the notes after a split or a join (by the readings the notes share): a part of a split note
    keeps the note's edit (a move as the same step from its own pitch; its timing so the parts stay together as they
    sounded); a joined note keeps the edit of its first edited part (a stretch starts again from 1). */
inline std::vector<NoteEdit> carryEdits (const std::vector<vox::honey::Note>& before, const std::vector<NoteEdit>& edits,
                                         const std::vector<vox::honey::Note>& after, double sampleRate)
{
    std::vector<NoteEdit> out (after.size());
    for (size_t j = 0; j < after.size(); ++j)
    {
        const auto& n = after[j];
        std::vector<size_t> from;
        for (size_t i = 0; i < before.size(); ++i)
            if (before[i].lastReading >= n.firstReading && before[i].firstReading <= n.lastReading) from.push_back (i);
        if (from.empty()) continue;
        size_t src = from.front();
        for (size_t i : from) if (i < edits.size() && ! edits[i].isDefault()) { src = i; break; }
        if (src >= edits.size()) continue;
        const auto& old = before[src];
        auto e = edits[src];
        if (e.moved) e.target = std::clamp (n.pitch + (e.target - old.pitch), 0.0, 127.0);
        if (from.size() == 1)
        {
            // A part: where it sounded inside the note (stretched as the note was).
            if (e.timed()) e.shift += (e.length - 1.0) * (n.start - old.start) / sampleRate;
        }
        else
            e.length = 1.0;
        out[j] = e;
    }
    return out;
}

/** "Notes to check": a note Honey Tune isn't sure it got right, and what D does about it. */
struct NoteCheck
{
    enum class Why { none, split, between, outOfKey, wobbly };
    Why why = Why::none;
    double other = 0.0;   // between / outOfKey: the note D moves it to (MIDI, in the tuning)
    int joinWith = -1;    // split: the neighbour D joins it with
    bool any() const { return why != Why::none; }
};

struct NoteView
{
    vox::honey::Note note;     // target / drift / vibrato as they will sound
    NoteEdit edit;
    bool wasOff = false;       // sung more than 25 cents from the key's nearest note
    bool off = false;          // where it will SOUND is more than 25 cents from the key's nearest note
    bool fixed = false;        // was off-key, now lands on a note of the key
    NoteCheck check;
};

/** The host's bars and beats over the clip (from the song's tempo map and time signatures), in clip
    seconds. Empty when the host doesn't share them: the editor then shows seconds. */
struct Timeline
{
    struct Line { double seconds; int bar; int beat; };   // bar from 1, beat from 1 (1 = the bar line)
    std::vector<Line> lines;
    double songOffset = 0.0;   // song time = clip time + songOffset

    bool valid() const { return lines.size() >= 2; }

    /** The line a time move lands on nearest to `clipSeconds`: a beat, or a half / quarter of one when they're at least
        48 / 96 px apart at this zoom (pixels per second). -1: none (no bars and beats from the host). */
    double nearestLine (double clipSeconds, double pixelsPerSecond) const
    {
        if (! valid()) return -1.0;
        size_t k = 1;
        while (k + 1 < lines.size() && lines[k].seconds < clipSeconds) ++k;
        double best = -1.0;
        for (size_t j = k > 1 ? k - 1 : 1; j <= std::min (k + 1, lines.size() - 1); ++j)
        {
            const double a = lines[j - 1].seconds, beat = lines[j].seconds - a;
            if (beat <= 1.0e-6) continue;
            const double px = beat * pixelsPerSecond;
            const int parts = px >= 96.0 ? 4 : px >= 48.0 ? 2 : 1;
            for (int q = 0; q <= parts; ++q)
            {
                const double at = a + beat * q / parts;
                if (best < 0.0 || std::abs (at - clipSeconds) < std::abs (best - clipSeconds)) best = at;
            }
        }
        return best;
    }

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
    int status = 0;            // 0 waiting, 1 listening, 2 ready, 3 couldn't read, 4 too long (not enough memory)
    std::string name;
    std::shared_ptr<const vox::honey::Track> track;
    std::vector<NoteView> notes;
    int key = 0, scale = 0;
    vox::KeyGuess guess;
    bool keyFromHost = false;  // the key came from the host's project
    bool keyFromBeat = false;  // ... from Voxology on the beat
    bool keyFromVoxology = false;  // ... from the Voxology on this clip's track (the key its Pitch uses)
    // The Voxology on this clip's track (vox::honeylink): is there one, and what does its Pitch do?
    bool voxologyHere = false;
    vox::honeylink::Pitch voxologyPitch = vox::honeylink::Pitch::off;
    bool keyUnsure = false;    // Auto couldn't tell the key from the voice: snapping to any note
    double seconds = 0.0;
    KeyGrid grid;              // the notes they land on (the key, notes switched in / out, the tuning)
    bool tuneAuto = true;      // the tuning is Auto (the beat's from Voxology on the track, else A = 440)
    int autoCents = 0;         // what Auto tuning is (cents vs A = 440: the beat's, from Voxology on the track)
    int checks = 0;            // notes to check
};

/** Notes to check (the amber dot; N goes to the next, D does what it says). For notes you haven't changed or checked:
    - split: a piece under 80 ms touching a neighbour (under 30 ms apart) sung at its pitch (within a semitone), or two
      notes at the same pitch (within 35 cents) under 15 ms apart: one note cut in two. D joins them.
    - outOfKey: sung within 30 cents of a note outside the key, and Snap pulls it a semitone or more to the key: maybe you
      meant that note. D keeps the note you sang (tuned to it); or switch it into the key (Alt + click its key).
    - between: sung 40 - 60 cents from the nearest note of the key, the next one not much further: Snap may pick the
      wrong one. D moves it to the other.
    - wobbly: 0.3 s or longer, and its slow wander (vibrato left out: that's sung on purpose) still 15 cents or more as it
      will sound. D holds it steady (no drift; its vibrato kept).
    The first that fits, in that order. Nothing while Snap is at 0 (but splits and wobbles). */
inline std::vector<NoteCheck> findChecks (const vox::honey::Track* track, const std::vector<NoteView>& notes, const KeyGrid& grid,
                                          const Settings& s, double sampleRate)
{
    std::vector<NoteCheck> out (notes.size());
    const double sr = sampleRate > 0.0 ? sampleRate : 48000.0;
    for (size_t i = 0; i < notes.size(); ++i)
    {
        const auto& v = notes[i];
        if (! v.edit.isDefault() || v.edit.checked) continue;
        auto& c = out[i];
        const auto& n = v.note;
        const double len = (n.end - n.start) / sr;
        // One note cut in two?
        int join = -1;
        double nearest = 1.0e9;
        for (int d : { -1, 1 })
        {
            const auto j = static_cast<long> (i) + d;
            if (j < 0 || j >= static_cast<long> (notes.size())) continue;
            const auto& m = notes[static_cast<size_t> (j)].note;
            const double gap = (d < 0 ? n.start - m.end : m.start - n.end) / sr;
            const double apart = std::abs (m.pitch - n.pitch);
            const bool piece = len < 0.08 && gap < 0.03 && apart < 1.0;
            const bool twin = gap < 0.015 && apart < 0.35;
            if ((piece || twin) && apart < nearest) { nearest = apart; join = static_cast<int> (j); }
        }
        if (join >= 0) { c.why = NoteCheck::Why::split; c.joinWith = join; continue; }
        if (s.snap > 0.0)
        {
            const double sung = n.pitch - grid.tune;   // on the untuned grid
            const double toKey = grid.note (n.pitch) - n.pitch;
            const double semi = std::round (sung);
            if (std::abs (sung - semi) < 0.3 && ! grid.has (static_cast<int> (semi)) && std::abs (toKey) >= 0.7)
            {
                c.why = NoteCheck::Why::outOfKey;
                c.other = semi + grid.tune;
                continue;
            }
            // The note of the key on its other side.
            const double first = grid.note (n.pitch), d1 = std::abs (toKey);
            const double second = grid.step (first, n.pitch > first ? 1 : -1);
            if (d1 >= 0.4 && d1 <= 0.6 && std::abs (second - n.pitch) - d1 < 0.3)
            {
                c.why = NoteCheck::Why::between;
                c.other = second;
                continue;
            }
        }
        if (track != nullptr && len >= 0.3 && n.drift > 0.05)
        {
            // Its slow wander alone (vibrato is sung on purpose: left out).
            auto slow = n;
            slow.vibrato = 0.0;
            if (vox::honey::movementCents (*track, slow) >= 15.0) c.why = NoteCheck::Why::wobbly;
        }
    }
    return out;
}

/** What the note's line says about a note to check (and what D does). */
inline std::string checkText (const NoteView& v)
{
    const auto name = [] (double midi)
    {
        const int m = static_cast<int> (std::lround (midi));
        return std::string (vox::kNoteNames[static_cast<size_t> ((m % 12 + 12) % 12)]) + std::to_string (m / 12 - 1);
    };
    using W = NoteCheck::Why;
    switch (v.check.why)
    {
        case W::split:    return "CHECK: this looks like one note cut in two. D joins it with its neighbour";
        case W::outOfKey: return "CHECK: sung as " + name (v.check.other) + ", which isn't in the key: Snap moves it to " + name (v.note.target)
                                 + ". D keeps " + name (v.check.other) + " (or Alt + click its key to add it to the key)";
        case W::between:  return "CHECK: sung between " + name (std::min (v.note.target, v.check.other)) + " and " + name (std::max (v.note.target, v.check.other))
                                 + ": Snap picks " + name (v.note.target) + ". D moves it to " + name (v.check.other);
        case W::wobbly:   return "CHECK: this long note wanders off pitch. D holds it steady (its vibrato kept)";
        case W::none:     break;
    }
    return {};
}

/** D on a note to check: the edit it makes (split: nothing here, the caller joins). */
inline NoteEdit checkFix (const NoteView& v)
{
    auto e = v.edit;
    using W = NoteCheck::Why;
    if (v.check.why == W::outOfKey || v.check.why == W::between) { e.moved = true; e.target = v.check.other; }
    if (v.check.why == W::wobbly) e.drift = 0.0;
    return e;
}

inline Snapshot makeSnapshot (std::shared_ptr<const vox::honey::Track> track, const std::vector<vox::honey::Note>& notes,
                              const std::vector<NoteEdit>& edits, const vox::KeyGuess& guess, const Settings& s,
                              const HostKey& host = {}, int autoTuneCents = 0)
{
    Snapshot snap;
    snap.track = track;
    snap.guess = guess;
    resolveKey (s, guess, snap.key, snap.scale, host);
    const bool outside = usesOutsideKey (s, guess, host);
    snap.keyFromHost = outside && ! host.fromBeat && ! host.fromTrack;
    snap.keyFromBeat = outside && host.fromBeat;
    snap.keyFromVoxology = outside && host.fromTrack;
    snap.keyUnsure = keyUnsure (s, guess, host);
    if (track != nullptr && ! track->time.empty())
        snap.seconds = track->time.back() / track->sampleRate;
    snap.grid = keyGrid (s, snap.key, snap.scale, autoTuneCents);
    snap.tuneAuto = s.a4 <= 0.0;
    snap.autoCents = std::clamp (autoTuneCents, -50, 50);
    const double sr = track != nullptr ? track->sampleRate : 48000.0;
    const auto sounding = applyEdits (notes, edits, s, snap.grid, sr,
                                      track != nullptr && ! track->level.empty() ? static_cast<double> (track->level.size()) * vox::honey::Track::kLevelHopSeconds * track->sampleRate : 0.0);
    for (size_t i = 0; i < notes.size(); ++i)
    {
        NoteView v;
        v.note = sounding[i];
        v.edit = i < edits.size() ? edits[i] : NoteEdit {};
        v.wasOff = std::abs (notes[i].pitch - snap.grid.note (notes[i].pitch)) > 0.25;
        v.off = std::abs (v.note.target - snap.grid.note (v.note.target)) > 0.25;
        v.fixed = v.wasOff && ! v.off;
        snap.notes.push_back (v);
    }
    const auto checks = findChecks (track.get(), snap.notes, snap.grid, s, sr);
    for (size_t i = 0; i < checks.size(); ++i)
    {
        snap.notes[i].check = checks[i];
        snap.checks += checks[i].any() ? 1 : 0;
    }
    return snap;
}

/** Where Auto's key came from, for the status line ("" = picked by you, or the voice). */
inline std::string keySourceText (const Snapshot& s)
{
    return s.keyFromHost ? " (from the project)" : s.keyFromVoxology ? " (from Voxology on this track)" : s.keyFromBeat ? " (from Voxology's beat)" : "";
}

/** The status line's word on the Voxology on this clip's track ("" = nothing to say). warning: show it in red. */
inline std::string voxologyText (const Snapshot& s, bool& warning)
{
    using P = vox::honeylink::Pitch;
    warning = false;
    if (! s.voxologyHere) return {};
    switch (s.voxologyPitch)
    {
        case P::resting: return "Voxology's Pitch rests on this track";
        case P::tuningAnyway: warning = true;
            return "VOXOLOGY'S PITCH TUNES THIS TRACK TOO (TUNE HERE ANYWAY in its 01 Pitch): it re-tunes your fixes";
        case P::tuning: warning = true;
            return "VOXOLOGY'S PITCH TUNES THIS TRACK TOO: switch it off there (01 Pitch), or give this track its own name";
        case P::off: break;
    }
    return {};
}

/** The selected note's wobble, for the note's line: how much it moves now as it will sound, and as sung. A note you only
    move keeps its wobble (by design): the hint says which controls make it steady. "" = nothing worth saying. */
inline std::string wobbleText (double soundingCents, double sungCents)
{
    const auto c = [] (double v) { return std::to_string (static_cast<int> (std::lround (v))); };
    if (soundingCents >= 12.0)
        return "wobbles " + c (soundingCents) + " cents: NOTE DRIFT + NOTE VIBRATO at 0 make it steady (moving it keeps the wobble)";
    if (sungCents >= 12.0 && sungCents - soundingCents >= 3.0)
        return "steadied: wobble " + c (sungCents) + " -> " + c (soundingCents) + " cents";
    return {};
}

/** Sonicology Rack (Mixology's docs/rack/RACK-PLAN.md): what Honey Tune does on one track (all its clips there), for
    the Rack's HONEY TUNE cell. The plug-in adds each clip's snapshot, then publishes rackModule() for the track. */
struct RackTrack
{
    int clips = 0, listening = 0;             // its clips on the track; ... still being listened to
    int notes = 0, edited = 0, steadied = 0;  // notes; ... you changed by hand; ... that will sound steady (no drift or vibrato)
    bool keyKnown = false;
    int key = 0, scale = 0;                   // the key used (the first clip that has one)
    std::string keyFrom;                      // " (from Voxology's beat)" ... ("" = picked by you, or the voice)
    bool unsure = false;                      // Auto couldn't tell the key: snapping to any note
};

inline void addClip (RackTrack& t, const Snapshot& snap, const Settings& s)
{
    ++t.clips;
    if (snap.status < 2) ++t.listening;
    if (snap.status != 2) return;
    t.notes += static_cast<int> (snap.notes.size());
    for (const auto& n : snap.notes)
    {
        if (! n.edit.isDefault()) ++t.edited;
        const double drift = n.edit.drift >= 0.0 ? n.edit.drift : s.drift;
        const double vibrato = n.edit.vibrato >= 0.0 ? n.edit.vibrato : s.vibrato;
        if (drift < 0.01 && vibrato < 0.01) ++t.steadied;
    }
    if (! t.keyKnown)
    {
        t.keyKnown = true;
        t.key = snap.key;
        t.scale = snap.scale;
        t.keyFrom = keySourceText (snap);
        t.unsure = snap.keyUnsure;
    }
}

inline rack::Module rackModule (const Settings& s, const RackTrack& t, bool full)
{
    const auto pct = [] (double v) { return std::to_string (static_cast<int> (std::lround (std::clamp (v, 0.0, 1.0) * 100.0))) + " %"; };
    const bool autoKey = s.key == kAutoKey;
    const std::string note = vox::kNoteNames[static_cast<size_t> (std::clamp (t.key, 0, 11))];
    const std::string scale = vox::kScaleNames[static_cast<size_t> (std::clamp (t.scale, 0, vox::kScales - 1))];
    std::string key = ! t.keyKnown ? "" : t.unsure ? "any note" : t.scale == 0 ? "Chromatic" : note + " " + scale;
    if (autoKey && ! key.empty()) key = "Auto: " + key + t.keyFrom;

    rack::Module m;
    m.key = "ht.tune";
    m.state = rack::State::on;
    std::string sum;
    const auto add = [&] (const std::string& part) { if (! part.empty()) sum += (sum.empty() ? "" : " \xC2\xB7 ") + part; };
    if (t.notes == 0 && t.listening > 0) add ("Listening to the clip");
    else add (std::to_string (t.edited > 0 ? t.edited : t.notes) + (t.edited > 0 ? (t.edited == 1 ? " note edited" : " notes edited") : t.notes == 1 ? " note" : " notes"));
    if (t.steadied > 0 && t.notes > 0) add (t.steadied == t.notes ? std::string ("all steady") : std::to_string (t.steadied) + " steadied");
    add (key);
    m.summary = sum;
    if (full)
    {
        const auto knob = [&] (const char* name, std::string text, float norm, const char* param = "") {
            m.values.push_back ({ name, std::move (text), norm, false, param });
        };
        knob ("Key", autoKey ? (t.keyKnown && ! t.unsure && t.scale != 0 ? "Auto: " + note : std::string ("Auto")) : note,
              autoKey ? 1.0f : static_cast<float> (std::clamp (s.key, 0, 11)) / 11.0f);
        knob ("Scale", t.keyKnown && ! t.unsure ? scale : std::string ("\xE2\x80\x94"), static_cast<float> (std::clamp (t.scale, 0, vox::kScales - 1)) / static_cast<float> (vox::kScales - 1));
        // The Rack turns these (stage 2: Honey Tune's settings for all its clips, as in its window); Key and Scale stay
        // Honey Tune's own choice.
        knob ("Snap to Note", pct (s.snap), static_cast<float> (std::clamp (s.snap, 0.0, 1.0)), "snap");
        knob ("Keep Drift", pct (s.drift), static_cast<float> (std::clamp (s.drift, 0.0, 1.0)), "drift");
        knob ("Keep Vibrato", pct (s.vibrato), static_cast<float> (std::clamp (s.vibrato, 0.0, 1.0)), "vibrato");
    }
    return m;
}

/** Stage 2: a Rack knob (0..1) on Honey Tune's settings ("snap", "drift", "vibrato": 0..1 as they are). False: not
    one the Rack can turn. */
inline bool rackSet (Settings& s, const std::string& key, float knob)
{
    const double v = std::clamp (std::isfinite (knob) ? static_cast<double> (knob) : 0.0, 0.0, 1.0);
    if (key == "snap") s.snap = v;
    else if (key == "drift") s.drift = v;
    else if (key == "vibrato") s.vibrato = v;
    else return false;
    return true;
}

/** A Rack knob back to Honey Tune's default (a double-click). False: not one the Rack can turn. */
inline bool rackReset (Settings& s, const std::string& key)
{
    const Settings def;
    if (key == "snap") s.snap = def.snap;
    else if (key == "drift") s.drift = def.drift;
    else if (key == "vibrato") s.vibrato = def.vibrato;
    else return false;
    return true;
}

/** The editor talks to the clip through this. */
struct Model
{
    virtual ~Model() = default;
    virtual Snapshot snapshot() = 0;
    virtual Settings getSettings() = 0;
    virtual void setSettings (const Settings&) = 0;
    virtual void setEdit (int noteIndex, const NoteEdit& edit) = 0;
    /** Several notes at once (a selection): one undo step. */
    virtual void setEdits (const std::vector<std::pair<int, NoteEdit>>& edits) { for (const auto& [i, e] : edits) setEdit (i, e); }
    virtual void resetAllEdits() = 0;
    /** Cut note `index` in two at this clip time (seconds) / make notes first .. last one: one undo step each, saved with
        the project. false: not here (too near the note's ends, nothing to join). */
    virtual bool splitNote (int /*index*/, double /*clipSeconds*/) { return false; }
    virtual bool joinNotes (int /*first*/, int /*last*/) { return false; }
    /** Undo / redo the last edit (false: nothing to undo / redo). */
    virtual bool undo() { return false; }
    virtual bool redo() { return false; }
    /** A / B: hear (and see) the clip as it was recorded, edits kept but bypassed. */
    virtual void setOriginal (bool) = 0;
    virtual bool isOriginal() = 0;
    /** Ask the host to move its playhead to this clip time (seconds). */
    virtual void seek (double /*clipSeconds*/) {}
    /** Ask the host to play from this clip time (seconds; < 0: from where its playhead is, or stop it if it plays). */
    virtual void playFrom (double /*clipSeconds*/) {}
    /** Ask the host to loop this stretch of the clip (seconds) and play it. */
    virtual void loop (double /*fromSeconds*/, double /*toSeconds*/) {}
    /** Hear this stretch of the clip (seconds) while the host is stopped, as it will play (on this track only, the
        host's playhead stays put). afterRender: once the edit just made has been rendered. */
    virtual void audition (double /*fromSeconds*/, double /*toSeconds*/, bool /*afterRender*/) {}
    virtual void stopAudition() {}
};

} // namespace honeyui
