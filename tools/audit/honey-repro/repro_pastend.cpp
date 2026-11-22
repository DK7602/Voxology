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
    // Note 1 dragged sideways off the right end of the clip (the roll has no right limit) or its right end stretched past it.
    for (int mode = 0; mode < 2; ++mode)
    {
        auto after = notes;
        if (mode == 0) { after[1].outStart = after[1].start + 6.0 * sr; after[1].outEnd = after[1].end + 6.0 * sr; }
        else { after[1].outStart = after[1].start; after[1].outEnd = after[1].start + 10.0 * sr; }
        const auto y = render (x, sr, t, after);
        std::printf (mode == 0 ? "note 1 moved +6 s (past the clip end, %.2f s long):\n" : "note 1's right end stretched 10 s (past the clip end, %.2f s):\n", x.size() / sr);
        for (size_t k = 0; k < notes.size(); ++k)
        {
            const double a = notes[k].start / sr, b = notes[k].end / sr;
            std::printf ("  note %zu sung %.2f..%.2f MIDI %.0f: render there pitch %5.2f level %6.1f dB (original %6.1f dB)\n", k, a, b, notes[k].pitch,
                         midiOf (hzAt (y, sr, a + 0.1, 0.15)), rmsDb (y, sr, a + 0.05, b - 0.05), rmsDb (x, sr, a + 0.05, b - 0.05));
        }
        std::printf ("  last 60 ms level %.1f dB\n", rmsDb (y, sr, x.size() / sr - 0.06, x.size() / sr));
    }
}
