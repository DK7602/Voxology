// De-Esser audit: Auto-Edit's De-Esser settings for a vocal, then the module run on the vocal as the chain feeds it
// (low cut + Tone EQ + Dynamic EQ): how much it cuts the "s" frames, whether it touches voiced (pitched) frames,
// and how much of each "s" onset gets through before the cut arrives.
// Build: g++ -std=c++20 -O2 -Idsp/include tools/audit/deess_audit.cpp build-t/dsp/libvox_dsp.a -lpthread -o deess_audit
// Run:   deess_audit in.f64 style   (raw doubles, mono 48 kHz)
#include "vox/AutoEdit.h"
#include "vox/VocalChain.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <vector>

using namespace vox;

int main (int argc, char** argv)
{
    if (argc < 3) { std::printf ("usage: deess_audit in.f64 style\n"); return 1; }
    const double sr = 48000.0;
    std::ifstream in (argv[1], std::ios::binary);
    std::vector<float> x;
    double v;
    while (in.read (reinterpret_cast<char*> (&v), sizeof v)) x.push_back (static_cast<float> (v));
    AutoEditSettings s;
    s.style = std::atoi (argv[2]);
    const auto r = autoEdit ({ x }, sr, s);
    const auto& d = r.params.deEsser;
    std::printf ("%s  style %s: De-Esser amount %.0f %% at %.0f Hz, sens %.0f; analysis sibilance %.1f dB\n", argv[1], kStyleNames[static_cast<size_t> (s.style)],
                 d.amount, d.freqHz, d.sensitivity, r.analysis.sibilanceDb);
    for (const auto& rs : r.reasons) if (rs.module == "deess") std::printf ("  [%s] %s: %s\n", rs.control.c_str(), rs.value.c_str(), rs.why.c_str());
    if (d.amount < 0.05) return 0;

    ChainParams p;
    p.deEsser.amount = 0.0;
    p.cleanup = r.params.cleanup; p.cleanup.gateRangeDb = 0; p.cleanup.popAmount = 0; p.cleanup.breathDb = 0;
    p.eq = r.params.eq;
    p.dynEq = r.params.dynEq;
    const auto yy = VocalChain::render ({ x }, sr, p);
    const std::vector<double> src (yy[0].begin(), yy[0].end());
    std::vector<double> out = src;
    DeEsser de; de.prepare (sr, 1); de.setParams (d);
    const int n = static_cast<int> (src.size());
    std::vector<double> cutDb (static_cast<size_t> (n));
    for (int i = 0; i < n; ++i) { double* ptr = out.data() + i; de.process (&ptr, 1, 1); cutDb[static_cast<size_t> (i)] = -de.takeCutDb(); }

    // Above 5 kHz (as Auto-Edit's sibilance measure).
    std::vector<double> hfIn (src), hfOut (out);
    {
        Biquad a1, a2, b1, b2;
        for (auto* f : { &a1, &a2, &b1, &b2 }) design::apply (*f, design::butterworth (true, 5000.0, sr));
        for (auto& s0 : hfIn) s0 = a2.process (a1.process (s0));
        for (auto& s0 : hfOut) s0 = b2.process (b1.process (s0));
    }
    const int F = 240;   // 5 ms frames
    const int nf = n / F - 8;
    std::vector<double> lev (static_cast<size_t> (nf)), ratio (static_cast<size_t> (nf)), clar (static_cast<size_t> (nf), 0.0), fcut (static_cast<size_t> (nf));
    for (int f = 0; f < nf; ++f)
    {
        double e = 0, eh = 0, c = 0;
        for (int i = f * F; i < (f + 1) * F; ++i) { e += src[static_cast<size_t> (i)] * src[static_cast<size_t> (i)]; eh += hfIn[static_cast<size_t> (i)] * hfIn[static_cast<size_t> (i)]; c = std::max (c, cutDb[static_cast<size_t> (i)]); }
        lev[static_cast<size_t> (f)] = 10 * std::log10 (e / F + 1e-20);
        ratio[static_cast<size_t> (f)] = 10 * std::log10 (eh / (e + 1e-30) + 1e-20);
        fcut[static_cast<size_t> (f)] = c;
        const double* q = src.data() + f * F;
        if (f * F + 1440 + 700 < n)
        {
            double best = 0;
            for (int lag = 80; lag <= 686; lag += 2)
            {
                double xy = 0, xx = 0, y2 = 0;
                for (int i = 0; i < 1440; i += 3) { xy += q[i] * q[i + lag]; xx += q[i] * q[i]; y2 += q[i + lag] * q[i + lag]; }
                best = std::max (best, xy / std::sqrt (xx * y2 + 1e-30));
            }
            clar[static_cast<size_t> (f)] = best;
        }
    }
    std::vector<double> ls; for (double l : lev) if (l > -70) ls.push_back (l);
    std::sort (ls.begin(), ls.end());
    const double voice = ls[ls.size() * 7 / 10];
    int sibN = 0, sibCut = 0, vN = 0, vCut = 0; double sibSum = 0, vSum = 0;
    for (int f = 0; f < nf; ++f)
    {
        const auto fi = static_cast<size_t> (f);
        if (lev[fi] < voice - 30) continue;
        const bool sib = ratio[fi] > -6.0;
        const bool voiced = ! sib && clar[fi] > 0.8 && ratio[fi] < -15.0;
        if (sib) { ++sibN; sibSum += fcut[fi]; if (fcut[fi] > 2) ++sibCut; }
        if (voiced) { ++vN; vSum += fcut[fi]; if (fcut[fi] > 1) ++vCut; }
    }
    // Onsets: a sibilant run after >= 30 ms not sibilant; how much of the first 5 ms above 5 kHz is cut vs 10 - 30 ms in.
    std::vector<double> onsetCut, bodyCut;
    for (int f = 6; f + 6 < nf; ++f)
    {
        const auto fi = static_cast<size_t> (f);
        bool start = ratio[fi] > -6.0 && lev[fi] > voice - 30;
        for (int k = 1; k <= 6 && start; ++k) if (ratio[fi - static_cast<size_t> (k)] > -6.0) start = false;
        if (! start) continue;
        auto dB = [&] (int a, int b) { double e0 = 0, e1 = 0; for (int i = a * F; i < b * F; ++i) { e0 += hfIn[static_cast<size_t> (i)] * hfIn[static_cast<size_t> (i)]; e1 += hfOut[static_cast<size_t> (i)] * hfOut[static_cast<size_t> (i)]; } return 10 * std::log10 ((e1 + 1e-30) / (e0 + 1e-30)); };
        onsetCut.push_back (dB (f, f + 1));
        bodyCut.push_back (dB (f + 2, f + 6));
    }
    std::sort (onsetCut.begin(), onsetCut.end()); std::sort (bodyCut.begin(), bodyCut.end());
    // Level of the "s" after (Auto-Edit's measure, p90 of sibilant frames above 5 kHz vs voice), before / after.
    std::printf ("S FRAMES: %d; cut > 2 dB on %.0f %%, avg cut %.1f dB\n", sibN, 100.0 * sibCut / std::max (1, sibN), sibSum / std::max (1, sibN));
    std::printf ("VOICED (vowel) FRAMES: %d; cut > 1 dB on %.1f %%, avg cut %.2f dB\n", vN, 100.0 * vCut / std::max (1, vN), vSum / std::max (1, vN));
    if (! onsetCut.empty())
        std::printf ("S ONSETS: %zu; above-5-kHz change in the first 5 ms median %.1f dB vs 10 - 30 ms in %.1f dB\n", onsetCut.size(), onsetCut[onsetCut.size() / 2], bodyCut[bodyCut.size() / 2]);
    return 0;
}
