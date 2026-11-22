#include "common.h"
using namespace vox::honey;
int main()
{
    const double sr = 44100.0;
    std::vector<Sung> line;
    const double pitches[] = { 57, 60, 62, 64, 65, 67, 69, 71 };
    for (double p : pitches) line.push_back ({ p, 0.4, 0.3 });
    const auto x = sing (line, sr);
    const auto t = analyse (x, sr, false);
    const auto notes = findNotes (t);
    auto moved = notes;
    moved[2].outStart = moved[2].start + 1.5 * sr;
    moved[2].outEnd = moved[2].end + 1.5 * sr;
    // The plug-in's render after that edit (a full render, to be generous), then Ctrl + Z: back to the notes as sung.
    auto prev = render (x, sr, t, moved);
    double from = 0, to = 0;
    changedRegion (moved, notes, static_cast<double> (x.size()), sr, from, to);
    std::printf ("undo: changedRegion [%.3f, %.3f) s (%s)\n", from / sr, to / sr, (to - from) < 0.6 * x.size() ? "partial" : "full");
    renderPart (x, sr, t, notes, prev, from, to);
    const auto clean = render (x, sr, t, notes);
    for (size_t k = 0; k < notes.size(); ++k)
    {
        const double a = notes[k].start / sr, b = notes[k].end / sr;
        std::printf ("  note %zu (MIDI %.0f): after undo pitch %5.2f lvl %6.1f dB | clean render pitch %5.2f lvl %6.1f dB\n", k, notes[k].pitch,
                     midiOf (hzAt (prev, sr, a + 0.1, 0.15)), rmsDb (prev, sr, a + 0.05, b - 0.05),
                     midiOf (hzAt (clean, sr, a + 0.1, 0.15)), rmsDb (clean, sr, a + 0.05, b - 0.05));
    }
}
