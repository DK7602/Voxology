// Rider audit: Auto-Edit's Rider settings for a vocal, then the Rider run on the vocal as the chain feeds it (up to the
// De-Esser): does it even out loud and quiet lines, what gain it gives breaths / quiet bits vs words, how loud the
// first 100 ms of each phrase come out vs the rest of it (late reaction), and how fast the gain moves inside words.
// Build: g++ -std=c++20 -O2 -Idsp/include tools/audit/rider_audit.cpp build-t/dsp/libvox_dsp.a -lpthread -o rider_audit
// Run:   rider_audit in.f64 style [rangeDb]   (raw doubles, mono 48 kHz; rangeDb overrides Auto-Edit's, e.g. to test)
#include "vox/AutoEdit.h"
#include "vox/VocalChain.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <vector>

using namespace vox;

static double pct (std::vector<double> v, double p) { if (v.empty()) return 0; std::sort (v.begin(), v.end()); return v[static_cast<size_t> (p / 100.0 * (v.size() - 1))]; }

int main (int argc, char** argv)
{
    if (argc < 3) { std::printf ("usage: rider_audit in.f64 style [rangeDb]\n"); return 1; }
    const double sr = 48000.0;
    std::ifstream in (argv[1], std::ios::binary);
    std::vector<float> x;
    double v;
    while (in.read (reinterpret_cast<char*> (&v), sizeof v)) x.push_back (static_cast<float> (v));
    AutoEditSettings s;
    s.style = std::atoi (argv[2]);
    const auto r = autoEdit ({ x }, sr, s);
    auto rp = r.params.rider;
    if (argc > 3) rp.rangeDb = std::atof (argv[3]);
    if (argc > 4) rp.speed = std::atoi (argv[4]);
    std::printf ("%s  style %s: Rider range %.1f dB, target %.0f dB, speed %d\n", argv[1], kStyleNames[static_cast<size_t> (s.style)], rp.rangeDb, rp.targetDb, rp.speed);
    for (const auto& rs : r.reasons) if (rs.module == "rider") std::printf ("  [%s] %s\n", rs.control.c_str(), rs.value.c_str());
    if (rp.rangeDb < 0.05) return 0;

    ChainParams p = r.params;
    p.rider.enabled = false; p.comp.enabled = false; p.saturation.enabled = false;
    p.doubler.amount = 0; p.delay.mix = 0; p.reverb.mix = 0;
    const auto yy = VocalChain::render ({ x }, sr, p);
    const std::vector<double> src (yy[0].begin(), yy[0].end());
    std::vector<double> out = src, g (src.size());
    Rider rd; rd.prepare (sr, 1); rd.setParams (rp);
    for (size_t i = 0; i < out.size(); ++i) { double* ptr = out.data() + i; rd.process (&ptr, 1, 1); g[i] = rd.currentGainDb(); }

    const int F = 480;   // 10 ms
    const int nf = static_cast<int> (src.size()) / F;
    std::vector<double> lin (static_cast<size_t> (nf)), lout (static_cast<size_t> (nf)), gf (static_cast<size_t> (nf)), hfr (static_cast<size_t> (nf));
    Biquad h1; design::apply (h1, design::butterworth (true, 1500.0, sr));
    Biquad l1; design::apply (l1, design::butterworth (false, 800.0, sr));
    for (int f = 0; f < nf; ++f)
    {
        double e0 = 0, e1 = 0, gg = 0, eh = 0, el = 0;
        for (int i = f * F; i < (f + 1) * F; ++i)
        {
            const auto ii = static_cast<size_t> (i);
            e0 += src[ii] * src[ii]; e1 += out[ii] * out[ii]; gg += g[ii];
            const double a = h1.process (src[ii]), b = l1.process (src[ii]); eh += a * a; el += b * b;
        }
        const auto fi = static_cast<size_t> (f);
        lin[fi] = 10 * std::log10 (e0 / F + 1e-20); lout[fi] = 10 * std::log10 (e1 / F + 1e-20); gf[fi] = gg / F;
        hfr[fi] = 10 * std::log10 ((eh + 1e-20) / (el + 1e-20));   // airy (breath) vs voiced
    }
    std::vector<double> act; for (double l : lin) if (l > -60) act.push_back (l);
    const double voice = pct (act, 70);
    // Phrase evenness: 400 ms levels of loud-enough stretches, P90 - P10.
    auto spread = [&] (const std::vector<double>& L)
    {
        std::vector<double> w;
        for (int f = 0; f + 40 <= nf; f += 40)
        {
            double e = 0; int k = 0;
            for (int j = f; j < f + 40; ++j) if (lin[static_cast<size_t> (j)] > voice - 20) { e += std::pow (10, L[static_cast<size_t> (j)] / 10); ++k; }
            if (k >= 20) w.push_back (10 * std::log10 (e / k));
        }
        return pct (w, 90) - pct (w, 10);
    };
    // Phrases: stretches of voice split by >= 250 ms gaps; each phrase's level (frames within 20 dB of the voice).
    auto phrases = [&] (const std::vector<double>& L)
    {
        std::vector<double> w;
        int f = 0;
        while (f < nf)
        {
            while (f < nf && lin[static_cast<size_t> (f)] < voice - 25) ++f;
            int quiet = 0; double e = 0; int k = 0;
            while (f < nf && quiet < 25)
            {
                const auto fi = static_cast<size_t> (f);
                if (lin[fi] < voice - 25) ++quiet; else quiet = 0;
                if (lin[fi] > voice - 20) { e += std::pow (10, L[fi] / 10); ++k; }
                ++f;
            }
            if (k >= 30) w.push_back (10 * std::log10 (e / k));
        }
        return w;
    };
    const auto pin = phrases (lin), pout = phrases (lout);
    std::printf ("LINES: loud vs quiet (400 ms, P90 - P10) %.1f dB -> %.1f dB; phrase to phrase (%zu phrases, P90 - P10) %.1f -> %.1f dB\n",
                 spread (lin), spread (lout), pin.size(), pct (pin, 90) - pct (pin, 10), pct (pout, 90) - pct (pout, 10));
    // Gain on words vs on breaths / quiet airy bits (12 - 30 dB under the voice, more 1.5k+ than <800 Hz).
    std::vector<double> gw, gb, gq;
    for (int f = 0; f < nf; ++f)
    {
        const auto fi = static_cast<size_t> (f);
        if (lin[fi] > voice - 10) gw.push_back (gf[fi]);
        else if (lin[fi] > voice - 30 && lin[fi] < voice - 12) { if (hfr[fi] > 3) gb.push_back (gf[fi]); else gq.push_back (gf[fi]); }
    }
    std::printf ("GAIN: on words median %+.1f dB; on breaths / airy bits median %+.1f (p90 %+.1f, n %zu); on quiet voiced bits median %+.1f (p90 %+.1f)\n",
                 pct (gw, 50), pct (gb, 50), pct (gb, 90), gb.size(), pct (gq, 50), pct (gq, 90));
    // Phrase starts: after >= 250 ms below voice - 25, the first 100 ms vs 300 - 600 ms in, before and after.
    std::vector<double> dIn, dOut;
    for (int f = 25; f + 60 < nf; ++f)
    {
        bool gap = true;
        for (int k = 1; k <= 25 && gap; ++k) if (lin[static_cast<size_t> (f - k)] > voice - 25) gap = false;
        if (! gap || lin[static_cast<size_t> (f)] < voice - 15) continue;
        auto m = [&] (const std::vector<double>& L, int a, int b) { double e = 0; for (int j = a; j < b; ++j) e += std::pow (10, L[static_cast<size_t> (j)] / 10); return 10 * std::log10 (e / (b - a)); };
        dIn.push_back (m (lin, f, f + 10) - m (lin, f + 30, f + 60));
        dOut.push_back (m (lout, f, f + 10) - m (lout, f + 30, f + 60));
        f += 60;
    }
    std::vector<double> diff; for (size_t i = 0; i < dIn.size(); ++i) diff.push_back (dOut[i] - dIn[i]);
    std::printf ("PHRASE STARTS: %zu; first 100 ms vs later changed by median %+.1f dB (p10 %+.1f, p90 %+.1f)\n", dIn.size(), pct (diff, 50), pct (diff, 10), pct (diff, 90));
    // Gain movement inside words: |change over 50 ms| while singing.
    std::vector<double> mv;
    for (int f = 5; f < nf; ++f) if (lin[static_cast<size_t> (f)] > voice - 10 && lin[static_cast<size_t> (f - 5)] > voice - 10) mv.push_back (std::abs (gf[static_cast<size_t> (f)] - gf[static_cast<size_t> (f - 5)]));
    std::printf ("MOVEMENT: gain change over 50 ms inside words median %.2f dB, p95 %.2f dB\n", pct (mv, 50), pct (mv, 95));
    return 0;
}
