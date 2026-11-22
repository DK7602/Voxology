#include "common.h"
using namespace vox::honey;
int main()
{
    const double sr = 44100.0;
    // Phrase 1: a lone note; phrase 2 (from ~1.4 s): four notes sung legato (no gap).
    std::vector<Sung> line { { 57, 0.4, 0.5 }, { 60, 0.4, 0.0 }, { 62, 0.4, 0.0 }, { 64, 0.4, 0.0 }, { 65, 0.4, 0.4 } };
    const auto x = sing (line, sr);
    const auto t = analyse (x, sr, false);
    auto notes = findNotes (t);
    std::printf ("%zu notes:", notes.size());
    for (auto& n : notes) std::printf (" [%.4f..%.4f]", n.start / sr, n.end / sr);
    std::printf ("\nhop %.2f ms\n", (t.time[1] - t.time[0]) / sr * 1000);
    const auto plain = render (x, sr, t, notes);
    auto moved = notes;
    moved[0].outStart = moved[0].start + 0.05 * sr; moved[0].outEnd = moved[0].end + 0.05 * sr;   // only note 0 moves
    const auto m = timeMap (moved, static_cast<double> (x.size()), sr);
    for (size_t i = 0; i < m.in.size(); ++i) std::printf ("  knot %.4f -> %.4f (%+.2f ms)\n", m.in[i] / sr, m.out[i] / sr, (m.out[i] - m.in[i]) / sr * 1000);
    const auto y = render (x, sr, t, moved);
    // The untouched legato phrase: how far is it from the render without the move?
    const double a = notes[1].start / sr, b = notes.back().end / sr;
    double e = 0, s = 0;
    for (size_t i = static_cast<size_t> (a * sr); i < static_cast<size_t> (b * sr); ++i) { const double d = y[i] - plain[i]; e += d * d; s += plain[i] * plain[i]; }
    std::printf ("untouched notes 1..%zu: difference to the plain render %.1f dB\n", notes.size() - 1, 10 * std::log10 (e / s + 1e-30));
    for (size_t k = 1; k < notes.size(); ++k) { double e2 = 0, s2 = 0, ex = 0; for (size_t i = static_cast<size_t> (notes[k].start + 0.03 * sr); i < static_cast<size_t> (notes[k].end - 0.03 * sr); ++i) { const double d = y[i] - plain[i]; e2 += d * d; s2 += plain[i] * plain[i]; ex += (y[i] - x[i]) * (y[i] - x[i]); } std::printf ("  note %zu: vs plain render %.1f dB, vs input %.1f dB\n", k, 10 * std::log10 (e2 / s2 + 1e-30), 10 * std::log10 (ex / s2 + 1e-30)); }
}
// (appended) per-note error
