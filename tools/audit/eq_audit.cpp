// Tone EQ audit: a vocal's third-octave tone vs Auto-Edit's style target, the Tone EQ it chose, the tone
// predicted after it (analysis + the EQ's response) and measured after it (the vocal rendered through the
// low cut + Tone EQ only, re-analysed).
// Build: g++ -std=c++20 -O2 -Idsp/include tools/audit/eq_audit.cpp build-t/dsp/libvox_dsp.a -lpthread -o eq_audit
// Run:   eq_audit in.f64 style [intensity]   (raw doubles, mono 48 kHz; style 0..7 as kStyleNames)
#include "vox/AutoEdit.h"
#include "vox/VocalChain.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <vector>

using namespace vox;

int main (int argc, char** argv)
{
    if (argc < 3) { std::printf ("usage: eq_audit in.f64 style [intensity]\n"); return 1; }
    const double sr = 48000.0;
    std::ifstream in (argv[1], std::ios::binary);
    std::vector<float> x;
    double v;
    while (in.read (reinterpret_cast<char*> (&v), sizeof v)) x.push_back (static_cast<float> (v));

    AutoEditSettings s;
    s.style = std::atoi (argv[2]);
    s.intensity = argc > 3 ? std::atoi (argv[3]) : 1;
    const auto r = autoEdit ({ x }, sr, s);
    const auto& a = r.analysis;
    const auto& eq = r.params.eq;

    // The style target (copy of AutoEdit.cpp's styleTarget: base + body / presence / air offsets).
    static const std::vector<double> base { -9, -6, -3.5, -2, -1, 0, 0, 0, -0.5, -1, -2, -3, -4, -5, -5.5, -6, -7, -8.5, -10, -12, -14, -17, -22 };
    static const double off[8][3] = { { -1, 1.5, 2 }, { 0, 1, 1 }, { 0.5, 0, 1.5 }, { -3, 2, 1 }, { 1, 0, 1.5 }, { 0, 1.5, 2.5 }, { 1, 0, 0.5 }, { 0.5, 0.5, 1 } };
    const auto& bands = analysisBands();

    // Measured after: low cut + Tone EQ only.
    ChainParams p;
    p.deEsser.amount = 0.0;
    p.cleanup = r.params.cleanup; p.cleanup.gateRangeDb = 0; p.cleanup.popAmount = 0; p.cleanup.breathDb = 0;
    p.eq = eq;
    const auto y = VocalChain::render ({ x }, sr, p);
    ChainParams p0 = p; p0.eq.gainDb = { 0, 0, 0, 0, 0 };
    const auto y0 = VocalChain::render ({ x }, sr, p0);
    const auto after = analyseVocal (y, sr), before = analyseVocal (y0, sr);

    std::printf ("%s  style %s, low cut %.0f Hz\n", argv[1], kStyleNames[static_cast<size_t> (s.style)], r.params.cleanup.lowCutHz);
    std::printf ("EQ: Body %+.1f @%.0f  Mud %+.1f @%.0f  Nasal %+.1f @%.0f  Presence %+.1f @%.0f  Air %+.1f @%.0f\n",
                 eq.gainDb[0], eq.freqHz[0], eq.gainDb[1], eq.freqHz[1], eq.gainDb[2], eq.freqHz[2], eq.gainDb[3], eq.freqHz[3], eq.gainDb[4], eq.freqHz[4]);
    std::printf ("   Hz   raw  lowcut  target  eqResp  predicted  measured   off-target before -> after\n");
    double e0 = 0, e1 = 0; int n = 0;
    for (size_t b = 0; b < bands.size(); ++b)
    {
        const double f = bands[b];
        double t = base[b];
        if (f >= 100 && f <= 250) t += off[s.style][0];
        if (f >= 2500 && f <= 5000) t += off[s.style][1];
        if (f >= 8000) t += off[s.style][2];
        const double resp = VocalEQ::responseDb (eq, f, sr);
        const bool used = f >= 1.3 * r.params.cleanup.lowCutHz && f <= 12500;
        std::printf ("%6.0f %5.1f %6.1f %7.1f %7.1f %9.1f %9.1f   %+6.1f -> %+6.1f%s\n", f, a.bandDb[b], before.bandDb[b], t, resp, before.bandDb[b] + resp, after.bandDb[b],
                     before.bandDb[b] - t, after.bandDb[b] - t, used ? "" : "  (not judged)");
        if (used) { e0 += std::abs (before.bandDb[b] - t); e1 += std::abs (after.bandDb[b] - t); ++n; }
    }
    std::printf ("mean |off target| %.1f -> %.1f dB (bands 1.3 x low cut .. 12.5 kHz)\n", e0 / n, e1 / n);
    for (const auto& rs : r.reasons) if (rs.module == "eq") std::printf ("  [%s] %s: %s\n", rs.control.c_str(), rs.value.c_str(), rs.why.c_str());
    return 0;
}
