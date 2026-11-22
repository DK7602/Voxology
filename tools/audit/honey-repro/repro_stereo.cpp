#include "common.h"
#include <random>
using namespace vox::honey;
// Lag (samples) of the best L/R cross-correlation in a window, and its value.
static void lagOf (const std::vector<float>& l, const std::vector<float>& r, size_t a, size_t n, int& lag, double& corr)
{
    double best = -2; int bl = 0;
    for (int k = -150; k <= 150; ++k)
    {
        double xy = 0, xx = 0, yy = 0;
        for (size_t i = a; i < a + n; ++i) { const double x = l[i], y = r[static_cast<size_t> (static_cast<long> (i) + k)]; xy += x * y; xx += x * x; yy += y * y; }
        const double c = xy / std::sqrt (xx * yy + 1e-30);
        if (c > best) { best = c; bl = k; }
    }
    lag = bl; corr = best;
}
int main()
{
    const double sr = 44100.0;
    std::vector<Sung> line;
    const double pitches[] = { 57, 60, 62, 64 };
    for (double p : pitches) line.push_back ({ p, 0.6, 0.3 });
    const auto L = sing (line, sr);
    // The right channel: the same voice through another mic (a little later, darker) plus room noise.
    std::vector<float> R (L.size(), 0.0f);
    std::mt19937 g (1); std::normal_distribution<float> nd (0.0f, 0.004f);
    float lp = 0;
    for (size_t i = 22; i < L.size(); ++i) { lp += 0.35f * (L[i - 22] - lp); R[i] = lp + nd (g); }
    std::vector<float> mono (L.size());
    for (size_t i = 0; i < L.size(); ++i) mono[i] = 0.5f * (L[i] + R[i]);
    const auto t = analyse (mono, sr, false);
    auto notes = findNotes (t);
    std::printf ("%zu notes\n", notes.size());
    notes[1].outStart = notes[1].start + 0.12 * sr;
    notes[1].outEnd = notes[1].outStart + (notes[1].end - notes[1].start) * 1.4;   // later and longer
    const auto yl = render (L, sr, t, notes), yr = render (R, sr, t, notes);
    std::printf ("L/R lag in the original (note 1): ");
    for (double s = notes[1].start / sr + 0.05; s < notes[1].end / sr - 0.06; s += 0.05) { int lag; double c; lagOf (L, R, static_cast<size_t> (s * sr), 1024, lag, c); std::printf ("%d (%.2f) ", lag, c); }
    std::printf ("\nL/R lag after the move/stretch:      ");
    for (double s = notes[1].soundStart() / sr + 0.05; s < notes[1].soundEnd() / sr - 0.06; s += 0.05) { int lag; double c; lagOf (yl, yr, static_cast<size_t> (s * sr), 1024, lag, c); std::printf ("%d (%.2f) ", lag, c); }
    std::printf ("\n");
}
