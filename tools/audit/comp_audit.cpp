// Compressor audit: Auto-Edit's Compressor settings for a vocal, then the vocal before / after the compressor (as the
// chain feeds it): punch (Auto-Edit's microDyn: 50 ms P95 - P50 while singing), average squeeze, peak catch, and the
// distortion the compressor adds on a steady low note pushed into it (THD+N of a 110 Hz tone, same settings).
// Build: g++ -std=c++20 -O2 -Idsp/include tools/audit/comp_audit.cpp build-t/dsp/libvox_dsp.a -lpthread -o comp_audit
// Run:   comp_audit in.f64 style   (raw doubles, mono 48 kHz)
#include "vox/AutoEdit.h"
#include "vox/VocalChain.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <numbers>
#include <vector>

using namespace vox;

int main (int argc, char** argv)
{
    if (argc < 3) { std::printf ("usage: comp_audit in.f64 style\n"); return 1; }
    const double sr = 48000.0;
    std::ifstream in (argv[1], std::ios::binary);
    std::vector<float> x;
    double v;
    while (in.read (reinterpret_cast<char*> (&v), sizeof v)) x.push_back (static_cast<float> (v));
    AutoEditSettings s;
    s.style = std::atoi (argv[2]);
    const auto r = autoEdit ({ x }, sr, s);
    const auto& c = r.params.comp;
    std::printf ("%s  style %s: Peak %.1f dB, Level %.1f dB %.1f:1, makeup %.1f\n", argv[1] + std::string (argv[1]).find_last_of ('/') + 1,
                 kStyleNames[static_cast<size_t> (s.style)], c.peakThrDb, c.thrDb, c.ratio, c.makeupDb);
    for (const auto& rs : r.reasons) if (rs.module == "comp" && rs.control != "Makeup") std::printf ("  [%s] %s: %s\n", rs.control.c_str(), rs.value.c_str(), rs.why.substr (0, 110).c_str());
    ChainParams p = r.params;
    p.comp.enabled = false; p.saturation.enabled = false; p.doubler.amount = 0; p.delay.mix = 0; p.reverb.mix = 0;
    const auto y0 = VocalChain::render ({ x }, sr, p);
    std::vector<double> y (y0[0].begin(), y0[0].end());
    const auto before = analyseVocal ({ y0[0] }, sr);
    VocalCompressor vc; vc.prepare (sr, 1); vc.setParams (c);
    double* ptr = y.data(); vc.process (&ptr, 1, static_cast<int> (y.size()));
    const auto after = analyseVocal ({ std::vector<float> (y.begin(), y.end()) }, sr);
    std::printf ("PUNCH (microDyn): %.1f -> %.1f dB; loud vs quiet lines %.1f -> %.1f dB\n", before.microDynDb, after.microDynDb, before.rangeDb, after.rangeDb);

    // THD+N: 110 Hz tone at the vocal's loud level (its RMS + 6 dB), 1 s, through the same compressor.
    const double amp = std::pow (10.0, (before.voiceRmsDb + 6.0) / 20.0) * std::sqrt (2.0);
    std::vector<double> t (static_cast<size_t> (sr));
    for (size_t i = 0; i < t.size(); ++i) t[i] = amp * std::sin (2 * std::numbers::pi * 110.0 * i / sr);
    VocalCompressor vt; vt.prepare (sr, 1); vt.setParams (c);
    double* tp = t.data(); vt.process (&tp, 1, static_cast<int> (t.size()));
    // fit the fundamental over the last 0.5 s, residual = THD+N
    double sa = 0, ca = 0; const size_t a0 = t.size() / 2;
    for (size_t i = a0; i < t.size(); ++i) { sa += t[i] * std::sin (2 * std::numbers::pi * 110.0 * i / sr); ca += t[i] * std::cos (2 * std::numbers::pi * 110.0 * i / sr); }
    sa *= 2.0 / (t.size() - a0); ca *= 2.0 / (t.size() - a0);
    double er = 0, et = 0;
    for (size_t i = a0; i < t.size(); ++i) { const double f = sa * std::sin (2 * std::numbers::pi * 110.0 * i / sr) + ca * std::cos (2 * std::numbers::pi * 110.0 * i / sr); er += (t[i] - f) * (t[i] - f); et += t[i] * t[i]; }
    std::printf ("DISTORTION on a loud 110 Hz note: THD+N %.3f %% (%.0f dB)\n", 100 * std::sqrt (er / et), 10 * std::log10 (er / et));
    return 0;
}
