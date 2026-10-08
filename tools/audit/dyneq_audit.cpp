// Dynamic EQ audit: Auto-Edit's Dynamic EQ settings for a vocal, then the module run on the vocal after the low cut
// and Tone EQ (as in the chain), per band: how much of the singing it cuts, how deep, and whether the cuts follow the
// melody (a harmonic of the sung note inside the band's bell) more than chance would.
// Build: g++ -std=c++20 -O2 -Idsp/include tools/audit/dyneq_audit.cpp build-t/dsp/libvox_dsp.a -lpthread -o dyneq_audit
// Run:   dyneq_audit in.f64 style [all]   (raw doubles, mono 48 kHz; "all" = every band at 6 dB, Auto-Edit's frequencies)
#include "vox/AutoEdit.h"
#include "vox/DynamicEq.h"
#include "vox/VocalChain.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <vector>

using namespace vox;

int main (int argc, char** argv)
{
    if (argc < 3) { std::printf ("usage: dyneq_audit in.f64 style [all]\n"); return 1; }
    const double sr = 48000.0;
    std::ifstream in (argv[1], std::ios::binary);
    std::vector<float> x;
    double v;
    while (in.read (reinterpret_cast<char*> (&v), sizeof v)) x.push_back (static_cast<float> (v));
    AutoEditSettings s;
    s.style = std::atoi (argv[2]);
    const auto r = autoEdit ({ x }, sr, s);
    auto d = r.params.dynEq;
    const bool all = argc > 3 && std::strcmp (argv[3], "all") == 0;
    if (all) for (auto& c : d.maxCutDb) c = 6.0;
    std::printf ("%s  style %s  sens %.0f\n", argv[1], kStyleNames[static_cast<size_t> (s.style)], d.sensitivity);
    for (int b = 0; b < kDynBands; ++b) std::printf ("  %-6s %5.0f Hz  max %4.1f dB\n", kDynBandInfo[static_cast<size_t> (b)].name, d.freqHz[static_cast<size_t> (b)], d.maxCutDb[static_cast<size_t> (b)]);
    for (const auto& rs : r.reasons) if (rs.module == "dyneq") std::printf ("  [%s] %s\n", rs.control.c_str(), rs.value.c_str());

    // The vocal as the Dynamic EQ hears it: low cut + Tone EQ.
    ChainParams p;
    p.deEsser.amount = 0.0;
    p.cleanup = r.params.cleanup; p.cleanup.gateRangeDb = 0; p.cleanup.popAmount = 0; p.cleanup.breathDb = 0;
    p.eq = r.params.eq;
    const auto yy = VocalChain::render ({ x }, sr, p);
    std::vector<double> y (yy[0].begin(), yy[0].end());
    const std::vector<double> src = y;

    const int F = 480;
    const int nf = static_cast<int> (y.size()) / F - 4;
    std::vector<std::array<double, kDynBands>> cut (static_cast<size_t> (nf));
    DynamicEq de; de.prepare (sr, 1); de.setParams (d);
    for (int f = 0; f < nf; ++f) { double* ptr = y.data() + f * F; de.process (&ptr, 1, F); cut[static_cast<size_t> (f)] = de.takeCutDb(); }

    // Per frame: level, f0 (autocorrelation over 30 ms, 70 - 600 Hz), clarity.
    std::vector<double> lev (static_cast<size_t> (nf)), f0 (static_cast<size_t> (nf), 0.0);
    for (int f = 0; f < nf; ++f)
    {
        const double* q = src.data() + f * F;
        double e = 0; for (int i = 0; i < F; ++i) e += q[i] * q[i];
        lev[static_cast<size_t> (f)] = 10 * std::log10 (e / F + 1e-20);
        const int W = 1440; double best = 0; int bl = 0;
        if (f * F + W + 700 < static_cast<int> (src.size()))
            for (int lag = 80; lag <= 686; ++lag)
            {
                double xy = 0, xx = 0, yy2 = 0;
                for (int i = 0; i < W; i += 2) { xy += q[i] * q[i + lag]; xx += q[i] * q[i]; yy2 += q[i + lag] * q[i + lag]; }
                const double c = xy / std::sqrt (xx * yy2 + 1e-30);
                if (c > best) { best = c; bl = lag; }
            }
        if (best > 0.75) f0[static_cast<size_t> (f)] = sr / bl;
    }
    std::vector<double> sorted;
    for (double l : lev) if (l > -70) sorted.push_back (l);
    std::sort (sorted.begin(), sorted.end());
    const double voice = sorted.empty() ? -30 : sorted[sorted.size() * 7 / 10];

    for (int b = 0; b < kDynBands; ++b)
    {
        const auto bi = static_cast<size_t> (b);
        if (d.maxCutDb[bi] < 0.05) continue;
        const double fc = d.freqHz[bi], q = kDynBandInfo[bi].q;
        const double lo = fc * (std::sqrt (1 + 1 / (4 * q * q)) - 1 / (2 * q)), hi = fc * (std::sqrt (1 + 1 / (4 * q * q)) + 1 / (2 * q));
        int sing = 0, cutN = 0, hitAll = 0, hitCut = 0; double sumCut = 0, deep = 0;
        std::vector<double> cs;
        for (int f = 0; f < nf; ++f)
        {
            const auto fi = static_cast<size_t> (f);
            if (lev[fi] < voice - 20) continue;
            ++sing;
            const double c = -cut[fi][bi];
            bool hit = false;
            if (f0[fi] > 0) for (int h = 1; h <= 8; ++h) if (h * f0[fi] >= lo && h * f0[fi] <= hi && h <= 3) hit = true;
            if (hit) ++hitAll;
            if (c > 1.0) { ++cutN; if (hit) ++hitCut; sumCut += c; cs.push_back (c); }
            deep = std::max (deep, c);
        }
        std::sort (cs.begin(), cs.end());
        std::printf ("%-6s @%5.0f: cuts > 1 dB on %4.1f %% of singing, avg %.1f dB, p90 %.1f, deepest %.1f; low harmonic in the bell: %4.1f %% of singing vs %4.1f %% of cut moments\n",
                     kDynBandInfo[bi].name, fc, 100.0 * cutN / std::max (1, sing), cutN ? sumCut / cutN : 0.0, cs.empty() ? 0.0 : cs[cs.size() * 9 / 10], deep,
                     100.0 * hitAll / std::max (1, sing), cutN ? 100.0 * hitCut / cutN : 0.0);
    }
    return 0;
}
