// Saturation audit: Auto-Edit's Saturation for a vocal, the harmonics it estimated vs what the real (oversampled)
// module adds on the vocal (processed minus dry, vs the dry level), and aliasing: a loud 9 kHz tone through the same
// settings, energy not at a harmonic of 9 kHz (folded products) vs the tone.
// Build: g++ -std=c++20 -O2 -Idsp/include tools/audit/sat_audit.cpp build-t/dsp/libvox_dsp.a -lpthread -o sat_audit
// Run:   sat_audit in.f64 style
#include "vox/AutoEdit.h"
#include "vox/Fft.h"
#include "vox/Saturation.h"
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
    if (argc < 3) return 1;
    const double sr = 48000.0;
    std::ifstream in (argv[1], std::ios::binary);
    std::vector<float> x; double v;
    while (in.read (reinterpret_cast<char*> (&v), sizeof v)) x.push_back (static_cast<float> (v));
    AutoEditSettings s; s.style = std::atoi (argv[2]);
    const auto r = autoEdit ({ x }, sr, s);
    const auto sp = r.params.saturation;
    ChainParams p = r.params; p.saturation.enabled = false; p.doubler.amount = 0; p.delay.mix = 0; p.reverb.mix = 0; p.outputDb = 0;
    const auto dryCh = VocalChain::render ({ x }, sr, p);
    const double est = Saturation::estimateHarmonicsDb (dryCh, sp);
    Saturation sat; sat.prepare (sr, 1); sat.setParams (sp); sat.reset();
    std::vector<double> y (dryCh[0].begin(), dryCh[0].end());
    std::vector<double*> ptr { y.data() };
    sat.process (ptr.data(), 1, static_cast<int> (y.size()));
    const int L = Saturation::latencySamples();
    double ed = 0, er = 0;
    for (size_t i = 0; i + L < y.size(); ++i) { const double d = dryCh[0][i]; const double w = y[i + L] - d; ed += d * d; er += w * w; }
    double pk = 0; for (float f : dryCh[0]) pk = std::max (pk, std::abs (static_cast<double> (f)));
    std::printf ("%-36s %s drive %.1f mix %.0f: harmonics estimated %.1f dB, real %.1f dB (dry peaks %.1f dBFS)\n", argv[1] + std::string (argv[1]).find_last_of ('/') + 1,
                 sp.mode == SaturationMode::tape ? "Tape" : sp.mode == SaturationMode::tube ? "Tube" : "Clip", sp.driveDb, sp.mix, est, 10 * std::log10 (er / ed), 20 * std::log10 (pk));
    // aliasing with a loud 9 kHz tone (peak at the dry vocal's peak level)
    std::vector<double> t (1 << 16);
    for (size_t i = 0; i < t.size(); ++i) t[i] = pk * std::sin (2 * std::numbers::pi * 9000.0 * i / sr);
    Saturation s2; s2.prepare (sr, 1); s2.setParams (sp); s2.reset();
    std::vector<double*> tp { t.data() };
    s2.process (tp.data(), 1, static_cast<int> (t.size()));
    const int N = 16384;
    const auto P = powerSpectrum (t.data() + 20000, N);
    double tone = 0, alias = 0, harm = 0;
    for (size_t k = 1; k < P.size(); ++k)
    {
        const double f = k * sr / N;
        const double nearest = std::round (f / 9000.0) * 9000.0;
        if (std::abs (f - 9000.0) < 30) tone += P[k];
        else if (nearest > 0 && std::abs (f - nearest) < 30) harm += P[k];
        else alias += P[k];
    }
    std::printf ("   9 kHz tone at that peak: harmonics %.0f dB, other (aliasing + noise) %.0f dB vs the tone\n", 10 * std::log10 (harm / tone + 1e-30), 10 * std::log10 (alias / tone + 1e-30));
    return 0;
}
