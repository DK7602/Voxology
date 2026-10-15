#pragma once

#include "AutoEdit.h"

#include <string>
#include <vector>

/** Auto Analyze: after Auto-Edit, Voxology keeps listening whenever the song plays. Every 12 seconds of voice it
    measures the vocal again (on the raw vocal and the beat, the same way) and compares what it hears with what
    Auto-Edit heard when it set the chain. When this part of the song needs something new (louder, noisier, more "s",
    a different tone, clipping, a beat that arrived or got louder), it says so in the Learn window, with the settings
    Auto-Edit would choose now. It never changes anything by itself: you press APPLY.

    This file only compares measurements (framework-free, tests/test_autoanalyze.cpp); the plug-in listens, runs the
    checks on a background thread and turns the modules named here into knob moves. */
namespace vox {

inline constexpr double kAnalyzeVoiceSeconds = 12.0;   // one check per 12 s of voice (as much as Auto-Edit hears)

/** One new need: what changed since Auto-Edit, in Learn's words, and what fixes it. */
struct AnalyzeNeed
{
    std::string key;                 // one per kind: "clip", "level", "noise", "low", "sib", "tone", "dynamics", "beatnew", "beatlevel", "yourvocal"
    std::string note;                // "TITLE: text" + "\nNEED: ..." + "\nSTEP: ..." lines (the same format as Auto-Edit's notes)
    std::vector<Module> retune;      // modules whose controls Auto-Edit would now set differently (empty = advice only)
    bool fitLevel = false;           // the fix includes Output (FIT LEVEL against the beat), when Auto-Edit offers a move
};

/** What changed between the vocal (and beat) Auto-Edit heard and a new part of the song, measured the same way
    (analyseVocal, analyseBeat: a fraction of a second, so every check can afford it). Empty = this part fits the
    settings. Readings a saved report doesn't have (-120 / 0) are skipped. Only when something comes back does the
    plug-in run the full Auto-Edit on the part (seconds of work) to find the settings that fix it. */
std::vector<AnalyzeNeed> findNewNeeds (const VocalAnalysis& was, const BeatAnalysis& wasBeat, const VocalAnalysis& now, const BeatAnalysis& nowBeat);

} // namespace vox
