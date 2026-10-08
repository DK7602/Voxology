// Honey Tune editor rules (honeytune/HoneyView.h: no JUCE): what glows red, and which key Auto uses.
#include "HoneyView.h"
#include "vox/KeyShare.h"

#include <catch2/catch_test_macros.hpp>

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

TEST_CASE ("Honey Tune: notes sung off-key glow red even after Snap pulls them into the key", "[honey][view]")
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

    // Snap fixed both off-key notes (they will play in key)...
    CHECK (snap.notes[0].wasOff);
    CHECK_FALSE (snap.notes[0].off);
    CHECK (snap.notes[0].fixed);
    CHECK (snap.notes[2].wasOff);
    CHECK_FALSE (snap.notes[2].off);
    // ...and they still glow red in the Tuned view (the old rule, "will play off-key", showed none at Snap 100 %).
    CHECK (glowsRed (snap.notes[0].wasOff, snap.notes[0].off, false));
    CHECK (glowsRed (snap.notes[2].wasOff, snap.notes[2].off, false));
    CHECK_FALSE (glowsRed (snap.notes[1].wasOff, snap.notes[1].off, false));
    // Original view: red = sung off-key.
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
