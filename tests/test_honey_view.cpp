// Honey Tune editor rules (honeytune/HoneyView.h: no JUCE): what glows red, and which key Auto uses.
#include "HoneyView.h"
#include "vox/KeyShare.h"

#include <catch2/catch_test_macros.hpp>

#include <limits>

using namespace honeyui;

namespace {
vox::honey::Note note (double pitch, double startSeconds)
{
    vox::honey::Note n;
    n.pitch = n.target = pitch;
    n.start = startSeconds * 48000.0;
    n.end = n.start + 0.4 * 48000.0;
    return n;
}
}

TEST_CASE ("Honey Tune: red = will play off-key (Snap's fixes turn gold, a note moved off the key turns red)", "[honey][view]")
{
    // C major: C4 sung 40 cents sharp, D4 in tune, C#4 (not in the key at all).
    const std::vector<vox::honey::Note> notes { note (60.4, 0.0), note (62.0, 0.5), note (61.0, 1.0) };
    std::vector<NoteEdit> edits (notes.size());
    Settings s;
    s.key = 0;
    s.scale = 1;
    s.snap = 1.0;   // the default: every note lands on a note of the key
    const auto snap = makeSnapshot (nullptr, notes, edits, {}, s);
    REQUIRE (snap.notes.size() == 3);

    // Snap fixed both off-key notes: gold (fixed), no red glow.
    CHECK (snap.notes[0].wasOff);
    CHECK (snap.notes[0].fixed);
    CHECK (snap.notes[2].fixed);
    for (const auto& v : snap.notes)
        CHECK_FALSE (glowsRed (v.wasOff, v.off, false));
    // With Snap off they play as sung: the off-key ones glow red.
    s.snap = 0.0;
    const auto asSung = makeSnapshot (nullptr, notes, edits, {}, s);
    CHECK (glowsRed (asSung.notes[0].wasOff, asSung.notes[0].off, false));
    CHECK (glowsRed (asSung.notes[2].wasOff, asSung.notes[2].off, false));
    CHECK_FALSE (glowsRed (asSung.notes[1].wasOff, asSung.notes[1].off, false));
    s.snap = 1.0;
    // Original view: red = sung off-key (that's how it plays there).
    for (const auto& v : snap.notes)
        CHECK (glowsRed (v.wasOff, v.off, true) == v.wasOff);

    // A note sung in key but dragged off the key glows red too (it will play off-key).
    edits[1].moved = true;
    edits[1].target = 63.0;   // D#4
    const auto dragged = makeSnapshot (nullptr, notes, edits, {}, s);
    CHECK_FALSE (dragged.notes[1].wasOff);
    CHECK (dragged.notes[1].off);
    CHECK (glowsRed (dragged.notes[1].wasOff, dragged.notes[1].off, false));
}

TEST_CASE ("Honey Tune: Auto takes the project's key; an unsure voice key is flagged", "[honey][view]")
{
    // Host key signatures: root on the circle of fifths, intervals used.
    const bool minorUsed[12] { true, false, true, true, false, true, false, true, true, false, true, false };
    const bool majorUsed[12] { true, false, true, false, true, true, false, true, false, true, false, true };
    const bool dorianUsed[12] { true, false, true, true, false, true, false, true, false, true, true, false };
    auto em = hostKeyFromSignature (4, minorUsed);     // E minor
    CHECK (em.valid);
    CHECK (em.key == 4);
    CHECK (em.scale == 2);
    auto f = hostKeyFromSignature (-1, majorUsed);     // F major
    CHECK (f.key == 5);
    CHECK (f.scale == 1);
    CHECK (hostKeyFromSignature (2, dorianUsed).scale == 6);   // D dorian

    // The voice alone (Gallas): "D major or G major, 40 % sure".
    vox::KeyGuess unsure;
    unsure.key = 2; unsure.minor = false; unsure.confidence = 0.40; unsure.ambiguous = true; unsure.altKey = 7;
    Settings s;   // Auto key, scale "Chromatic" = follow Auto
    int key = -1, scale = -1;
    resolveKey (s, unsure, key, scale);
    CHECK (scale == 0);                  // any note
    CHECK (keyUnsure (s, unsure));       // ...and the editor says so

    resolveKey (s, unsure, key, scale, em);
    CHECK (key == 4);
    CHECK (scale == 2);                  // the project's E minor
    CHECK_FALSE (keyUnsure (s, unsure, em));

    const std::vector<vox::honey::Note> notes { note (60.0, 0.0) };
    const auto snap = makeSnapshot (nullptr, notes, std::vector<NoteEdit> (1), unsure, s, em);
    CHECK (snap.keyFromHost);
    CHECK (snap.key == 4);
    CHECK_FALSE (snap.keyUnsure);

    // A key you pick yourself wins over the project's.
    s.key = 9;
    s.scale = 1;
    resolveKey (s, unsure, key, scale, em);
    CHECK (key == 9);
    CHECK (scale == 1);
    CHECK_FALSE (keyUnsure (s, unsure));

    // A sure voice key isn't flagged.
    vox::KeyGuess sure = unsure;
    sure.confidence = 0.9;
    CHECK_FALSE (keyUnsure (Settings {}, sure));
}

TEST_CASE ("Honey Tune: Voxology's beat key reaches Honey Tune (fresh only); the project's key comes first", "[honey][view]")
{
    using namespace vox::keyshare;
    BeatKeyShare k;
    REQUIRE (decode (encode ({ 4, 2, 0.83, 123456 }), k));
    CHECK (k.key == 4);
    CHECK (k.scale == 2);
    CHECK (k.confidence > 0.82);
    CHECK (k.ms == 123456);
    CHECK_FALSE (decode ("rubbish", k));
    CHECK_FALSE (decode ("v1 12 2 0.5 1", k));   // no such key

    // Gallas's beat: E minor, written now -> read back.
    publish (4, 2, 0.9);
    BeatKeyShare got;
    REQUIRE (read (got));
    CHECK (got.key == 4);
    CHECK (got.scale == 2);
    // Written 6 s ago (that Voxology is gone): ignored.
    publish (4, 2, 0.9, nowMs() - 6000);
    CHECK_FALSE (read (got));

    // Order: the project's key, else the beat's; a key you pick beats both; the voice is last.
    HostKey beat;
    beat.valid = true; beat.key = 4; beat.scale = 2; beat.fromBeat = true;
    HostKey project;
    CHECK (outsideKey (project, beat) == beat);
    project.valid = true; project.key = 7; project.scale = 1;
    CHECK (outsideKey (project, beat) == project);

    vox::KeyGuess voice;   // "D major, 40 % sure"
    voice.key = 2; voice.minor = false; voice.confidence = 0.4;
    const std::vector<vox::honey::Note> notes { note (60.0, 0.0) };
    const auto snap = makeSnapshot (nullptr, notes, std::vector<NoteEdit> (1), voice, Settings {}, outsideKey ({}, beat));
    CHECK (snap.key == 4);
    CHECK (snap.scale == 2);
    CHECK (snap.keyFromBeat);
    CHECK_FALSE (snap.keyFromHost);
    CHECK_FALSE (snap.keyUnsure);
}

TEST_CASE ("Honey Tune: values from a project are made safe; a note dragged before the clip starts at 0", "[honey][view]")
{
    const double nan = std::numeric_limits<double>::quiet_NaN(), inf = std::numeric_limits<double>::infinity();
    Settings s;
    s.key = 99; s.scale = -4; s.snap = nan; s.drift = 7.0; s.vibrato = -3.0;
    const auto ok = sanitised (s);
    CHECK (ok.key == kAutoKey);
    CHECK (ok.scale == 0);
    CHECK (ok.snap == 1.0);
    CHECK (ok.drift == 1.0);
    CHECK (ok.vibrato == 0.0);

    NoteEdit e;
    e.moved = true; e.target = nan; e.drift = nan; e.vibrato = 0.4; e.shift = 1.0e300; e.length = -inf; e.formant = -50.0;
    const auto safe = sanitised (e);
    CHECK (! safe.moved);
    CHECK (std::isfinite (safe.target));
    CHECK (safe.drift < 0.0);   // (follows the settings)
    CHECK (safe.vibrato == 0.4);
    CHECK (std::abs (safe.shift) <= 3600.0);
    CHECK (safe.length == 1.0);
    CHECK (safe.formant == -vox::kMaxFormant);

    // A note moved 2 s earlier, sung at 0.5 s: it starts at the clip's start (a negative start meant "not moved").
    const std::vector<vox::honey::Note> notes { note (60.0, 0.5) };
    NoteEdit early;
    early.shift = -2.0;
    const auto sounding = applyEdits (notes, { early }, Settings {}, 0, 1);
    CHECK (sounding[0].outStart == 0.0);
    CHECK (sounding[0].soundStart() == 0.0);
    CHECK (sounding[0].timeMoved());
}

TEST_CASE ("Honey Tune: Auto takes the key Voxology's Pitch uses (a beat on its side-chain too) and says where it came from", "[honey][view]")
{
    using namespace vox::keyshare;
    // The user's case: no Voxology in BEAT mode; a vocal Voxology hears the beat on its side-chain (F minor) and offers it.
    // Honey Tune's Auto used to fall back to the voice (Chromatic): it now gets F minor.
    BeatKeyShare mine;
    REQUIRE (offer (5, 2, 0.8, mine));
    BeatKeyShare got;
    REQUIRE (read (got));
    CHECK (got.key == 5);
    CHECK (got.scale == 2);
    // Another Voxology hearing a different key, not clearly surer, doesn't take turns with it (each change re-tunes)...
    BeatKeyShare other;
    CHECK_FALSE (offer (0, 1, 0.82, other));
    REQUIRE (read (got));
    CHECK (got.key == 5);
    // ...a clearly surer one does; and the first one can follow its own beat's change.
    CHECK (offer (0, 1, 0.95, other));
    REQUIRE (read (got));
    CHECK (got.key == 0);
    CHECK_FALSE (offer (7, 1, 0.8, mine));   // (another Voxology's key is there now, surer)
    CHECK (offer (2, 2, 0.95, other));      // its own: always
    REQUIRE (read (got));
    CHECK (got.key == 2);

    // The order: the key you pick > the project's > the Voxology on this track (its beat, or picked there) > Voxology's
    // beat key > the clip's own voice (when sure) > the key the Voxology on this track learned from the singing.
    vox::KeyGuess unsure;   // the voice alone: "D major, 40 % sure"
    unsure.key = 2; unsure.minor = false; unsure.confidence = 0.40;
    vox::honeylink::VoxInfo v;
    v.nameHash = 77; v.pitch = vox::honeylink::Pitch::resting; v.key = 5; v.scale = 2; v.keyFrom = vox::honeylink::KeyFrom::beat;
    const auto track = trackKeyFrom (v);
    CHECK (track.valid);
    CHECK (track.fromTrack);
    CHECK_FALSE (track.weak);
    HostKey beat;
    beat.valid = true; beat.key = 0; beat.scale = 1; beat.fromBeat = true;
    HostKey project;
    CHECK (outsideKey (project, beat, track) == track);
    const std::vector<vox::honey::Note> notes { note (60.0, 0.0) };
    auto snap = makeSnapshot (nullptr, notes, std::vector<NoteEdit> (1), unsure, Settings {}, outsideKey (project, beat, track));
    CHECK (snap.key == 5);
    CHECK (snap.scale == 2);
    CHECK (snap.keyFromVoxology);
    CHECK_FALSE (snap.keyUnsure);
    CHECK (keySourceText (snap) == " (from Voxology on this track)");
    project.valid = true; project.key = 7; project.scale = 1;
    CHECK (outsideKey (project, beat, track) == project);
    project = {};
    // Learned from the singing: the beat's key, and the clip's own sure key, come first; else it beats "unsure".
    v.keyFrom = vox::honeylink::KeyFrom::voice;
    const auto weak = trackKeyFrom (v);
    CHECK (weak.weak);
    CHECK (outsideKey (project, beat, weak) == beat);
    snap = makeSnapshot (nullptr, notes, std::vector<NoteEdit> (1), unsure, Settings {}, outsideKey (project, {}, weak));
    CHECK (snap.key == 5);
    CHECK_FALSE (snap.keyUnsure);
    vox::KeyGuess sure = unsure;
    sure.confidence = 0.9;
    snap = makeSnapshot (nullptr, notes, std::vector<NoteEdit> (1), sure, Settings {}, outsideKey (project, {}, weak));
    CHECK (snap.key == 2);
    CHECK_FALSE (snap.keyFromVoxology);
    // Nothing heard there yet: no key from that Voxology.
    v.keyFrom = vox::honeylink::KeyFrom::none;
    CHECK_FALSE (trackKeyFrom (v).valid);
}

TEST_CASE ("Honey Tune: the status line says what Voxology's Pitch does on this track; the note line says how to steady a wobble", "[honey][view]")
{
    using P = vox::honeylink::Pitch;
    Snapshot s;
    bool warning = true;
    CHECK (voxologyText (s, warning).empty());
    CHECK_FALSE (warning);
    s.voxologyHere = true;
    s.voxologyPitch = P::resting;
    CHECK (voxologyText (s, warning).find ("rests") != std::string::npos);
    CHECK_FALSE (warning);
    s.voxologyPitch = P::tuningAnyway;
    CHECK (voxologyText (s, warning).find ("TUNES THIS TRACK TOO") != std::string::npos);
    CHECK (warning);
    s.voxologyPitch = P::tuning;
    CHECK (voxologyText (s, warning).find ("own name") != std::string::npos);
    CHECK (warning);
    s.voxologyPitch = P::off;
    CHECK (voxologyText (s, warning).empty());

    // A note that wobbles 30 cents: the hint names the two controls (moving it keeps the wobble); once steadied, says so.
    CHECK (wobbleText (30.0, 30.0).find ("NOTE DRIFT + NOTE VIBRATO at 0") != std::string::npos);
    CHECK (wobbleText (2.0, 30.0) == "steadied: wobble 30 -> 2 cents");
    CHECK (wobbleText (3.0, 4.0).empty());   // a steady note: nothing to say
}
