#include "common.h"
using namespace vox::honey;
int main()
{
    const double sr = 44100.0;
    // 8 notes of 0.4 s, 0.3 s apart (breaths): each its own phrase (gap > 0.15 s).
    std::vector<Sung> line;
    const double pitches[] = { 57, 60, 62, 64, 65, 67, 69, 71 };
    for (double p : pitches) line.push_back ({ p, 0.4, 0.3 });
    const auto x = sing (line, sr);
    const auto t = analyse (x, sr, false);
    const auto notes = findNotes (t);
    std::printf ("%zu notes\n", notes.size());
    for (size_t i = 0; i < notes.size(); ++i) std::printf ("  %zu: %.3f..%.3f s  midi %.2f\n", i, notes[i].start / sr, notes[i].end / sr, notes[i].pitch);
    auto prev = render (x, sr, t, notes);   // the render before the edit (as the plug-in keeps it)
    // The edit: note 2 dragged +1.5 s later (the roll allows it: no limit against neighbours)
    auto after = notes;
    after[2].outStart = after[2].start + 1.5 * sr;
    after[2].outEnd = after[2].end + 1.5 * sr;
    double from = 0, to = 0;
    const bool ok = changedRegion (notes, after, static_cast<double> (x.size()), sr, from, to);
    std::printf ("changedRegion ok=%d  [%.3f, %.3f) s of %.3f s (%s)\n", ok, from / sr, to / sr, x.size() / sr,
                 (to - from) < 0.6 * x.size() ? "partial render used" : "full render");
    renderPart (x, sr, t, after, prev, from, to);
    const auto full = render (x, sr, t, after);
    const double ns = after[2].soundStart() / sr, ne = after[2].soundEnd() / sr;
    std::printf ("note 2 should sound at %.3f..%.3f s (MIDI %.1f)\n", ns, ne, notes[2].pitch);
    std::printf ("  full render  : pitch there %.2f, level %.1f dB\n", midiOf (hzAt (full, sr, ns + 0.1, 0.15)), rmsDb (full, sr, ns + 0.05, ne - 0.05));
    std::printf ("  partial      : pitch there %.2f, level %.1f dB\n", midiOf (hzAt (prev, sr, ns + 0.1, 0.15)), rmsDb (prev, sr, ns + 0.05, ne - 0.05));
    // Where note 3 and 4 were sung
    for (int k : { 3, 4 })
    {
        const double a = notes[k].start / sr, b = notes[k].end / sr;
        std::printf ("note %d (sung %.2f..%.2f, MIDI %.0f): full pitch %.2f lvl %.1f dB | partial pitch %.2f lvl %.1f dB\n", k, a, b, notes[k].pitch,
                     midiOf (hzAt (full, sr, a + 0.1, 0.15)), rmsDb (full, sr, a + 0.05, b - 0.05), midiOf (hzAt (prev, sr, a + 0.1, 0.15)), rmsDb (prev, sr, a + 0.05, b - 0.05));
    }
    double maxd = 0; size_t at = 0;
    for (size_t i = 0; i < x.size(); ++i) if (std::abs (full[i] - prev[i]) > maxd) { maxd = std::abs (full[i] - prev[i]); at = i; }
    std::printf ("max |full - partial| = %.3f at %.3f s\n", maxd, at / sr);
    return 0;
}
