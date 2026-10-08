// Whole chain "do no harm": Auto-Edit on each vocal (style Rap if mostly rapped, else Pop), the whole insert chain
// rendered (space effects off), and how much it changed: tone (mean |difference| of the third-octave balance, 160 Hz -
// 10 kHz), punch (microDyn), "s" level, loudness. One CSV row per file.
// Build: g++ -std=c++20 -O2 -Idsp/include tools/audit/whole_chain.cpp build-t/dsp/libvox_dsp.a -lpthread -o whole_chain
// Run:   whole_chain a.f64 b.f64 ... > out.csv
#include "vox/AutoEdit.h"
#include "vox/VocalChain.h"

#include <cmath>
#include <cstdio>
#include <fstream>
#include <vector>

using namespace vox;

int main (int argc, char** argv)
{
    for (int k = 1; k < argc; ++k)
    {
        std::ifstream in (argv[k], std::ios::binary);
        std::vector<float> x; double v;
        while (in.read (reinterpret_cast<char*> (&v), sizeof v)) x.push_back (static_cast<float> (v));
        const auto a = analyseVocal ({ x }, 48000.0);
        if (a.voicedSeconds < 3.0) continue;
        AutoEditSettings s; s.style = a.heldShare < 25.0 ? 1 : 5;
        const auto r = autoEdit ({ x }, 48000.0, s);
        if (! r.ok) continue;
        ChainParams p = r.params; p.doubler.amount = 0; p.delay.mix = 0; p.reverb.mix = 0;
        const auto y = VocalChain::render ({ x }, 48000.0, p);
        const auto b = analyseVocal ({ y[0] }, 48000.0);
        double d = 0; int n = 0;
        const auto& bands = analysisBands();
        for (size_t i = 0; i < bands.size(); ++i) if (bands[i] >= 160 && bands[i] <= 10000) { d += std::abs (b.bandDb[i] - a.bandDb[i]); ++n; }
        std::printf ("%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f\n", d / n, a.microDynDb, b.microDynDb, a.sibilanceDb, b.sibilanceDb, a.inputLufs, b.inputLufs);
    }
}
