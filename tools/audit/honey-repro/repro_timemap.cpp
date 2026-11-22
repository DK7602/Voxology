#include "vox/HoneyTune.h"
#include <cstdio>
using namespace vox::honey;
int main()
{
    const double sr = 48000.0, clip = 10.0 * sr;
    // Five notes 0.4 s long, 0.1 s gaps, starting at 1 s.
    std::vector<Note> notes;
    for (int k = 0; k < 5; ++k) { Note n; n.start = (1.0 + 0.5 * k) * sr; n.end = n.start + 0.4 * sr; n.pitch = n.target = 60; notes.push_back (n); }
    // Case 1: note 1 dragged far right past the clip end (the roll has no right limit; applyEdits doesn't clamp).
    auto a = notes; a[1].outStart = a[1].start + 20.0 * sr; a[1].outEnd = a[1].outStart + 0.4 * sr;
    auto m = timeMap (a, clip, sr);
    std::printf ("case 1 knots (in -> out, s):\n");
    bool mono = true;
    for (size_t i = 0; i < m.in.size(); ++i) { std::printf ("  %8.4f -> %8.4f\n", m.in[i] / sr, m.out[i] / sr); if (i && m.out[i] < m.out[i-1]) mono = false; }
    std::printf ("out rising: %s\n", mono ? "yes" : "NO");
    for (double t : { 2.0, 3.0, 5.0, 9.99 }) std::printf ("  inAt(%.2f s) = %.4f s\n", t, m.inAt (t * sr) / sr);
    // Case 2: note 1 moved +1.2 s, past notes 2 and 3: what changedRegion re-renders, and where things end up.
    auto b = notes; b[1].outStart = b[1].start + 1.2 * sr; b[1].outEnd = b[1].outStart + 0.4 * sr;
    double from = 0, to = 0;
    const bool ok = changedRegion (notes, b, clip, sr, from, to);
    std::printf ("case 2: changedRegion ok=%d from %.3f to %.3f s\n", ok, from / sr, to / sr);
    auto m2 = timeMap (b, clip, sr);
    for (size_t i = 0; i < m2.in.size(); ++i) std::printf ("  %8.4f -> %8.4f\n", m2.in[i] / sr, m2.out[i] / sr);
    std::printf ("  note 1 sounds at %.3f..%.3f (UI), note 2 squeezed? outAt(n2.start..n2.end)= %.3f..%.3f\n",
                 b[1].soundStart() / sr, b[1].soundEnd() / sr, m2.outAt (b[2].start) / sr, m2.outAt (b[2].end) / sr);
    std::printf ("  map at the right join 'to': inAt(to) = %.4f s (old render there = %.4f s)\n", m2.inAt (to) / sr, to / sr);
    return 0;
}
