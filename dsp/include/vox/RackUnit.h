#pragma once

#include "vox/VocalChain.h"
#include "suite/RackLink.h"

#include <array>
#include <string>
#include <vector>

/** Sonicology Rack (Mixology's docs/rack/RACK-PLAN.md): what Voxology tells the Rack view about its modules.
    Framework-free and unit-tested; the plug-in only fills a State from its parameters and meters.

    A vocal: the 13 cells in the order Voxology's window numbers them (01 Pitch .. 13 Output). Each with its state (on;
    idle when switched on but set to do nothing; off; Pitch rests while Honey Tune tunes the track), a one-line summary,
    its knobs (named and written the way Voxology's window shows them), the Tone EQ's and Dynamic EQ's bands and its
    meter (gain reduction where it has one). On the beat (Beat mode): one cell, the Beat Listener (the key it hears). */
namespace vox::rackunit
{
    /** The last block's readings (dB; cuts <= 0), from the plug-in's own copies, never the window's held meters. */
    struct Meters
    {
        float gate = 0.0f, deEss = 0.0f, rider = 0.0f, peakGr = 0.0f, levelGr = 0.0f;
        std::array<float, kDynBands> dynEq {};
    };

    struct State
    {
        ChainParams params;            // as the chain plays them (Pitch's key: the one it follows now)
        bool hasMeters = false;        // audio came through lately
        Meters meters;
        bool pitchRests = false;       // Honey Tune tunes this track: Pitch rests
        bool keyAuto = true;           // Key: Auto (follows the beat / the voice) or Manual
        int keyState = 0;              // Auto: 1 following a key, 2 nothing heard yet, 3 listening (not sure yet)

        bool beat = false;             // Beat mode: only listening for the key
        int beatState = 0;             // Beat mode: 4 listening, 5 has a key
        int beatKey = 0, beatMode = 0; // its home note (0 = C) and mode (semitones above the major key's tonic: 9 = minor)
        float beatTuneCents = 0.0f;
        float unmaskAmount = 0.0f;     // %
    };

    /** The modules (brief: keys, states and summaries only; full: values, bands and meters too). */
    std::vector<rack::Module> modules (const State&, bool full);

    /** A key as Voxology writes it: "F Minor", "C Chromatic" -> "Chromatic (any note)". */
    std::string keyText (int key, int scale);
}
