// Prints Auto-Edit's report for a synthetic vocal (or a raw float32 mono file at 48 kHz).
#include "../tests/Signals.h"
#include "vox/AutoEdit.h"
#include <cstdio>
#include <fstream>
int main (int argc, char** argv)
{
    std::vector<float> x;
    if (argc > 1) { std::ifstream f (argv[1], std::ios::binary); float v; while (f.read ((char*) &v, 4)) x.push_back (v); }
    else x = testsig::vocal (48000.0, 14.0, -58.0, -1.0);
    const int style = argc > 2 ? std::atoi (argv[2]) : 0;
    const auto r = vox::autoEdit ({ x }, 48000.0, { style, 1, 140.0 });
    std::printf ("%s\n\n", r.summary.c_str());
    for (auto& n : r.notes) std::printf ("NOTE %s\n\n", n.c_str());
    for (auto& q : r.reasons) std::printf ("[%s] %s = %s\n    %s\n", q.module.c_str(), q.control.c_str(), q.value.c_str(), q.why.c_str());
    for (auto& s : r.suggestions) std::printf ("TIP %s\n", s.c_str());
    auto& a = r.analysis;
    std::printf ("\nvoice %.1f noise %.1f noisePk %.1f quietPk %.1f f0 %.0f/%.0f sib %.1f@%.0f (%.1f%%) range %.1f rumble %.1f lufs %.1f -> %.1f\n",
                 a.voiceRmsDb, a.noiseFloorDb, a.noisePeakDb, a.quietWordPeakDb, a.f0Median, a.f0Low, a.sibilanceDb, a.sibilanceHz, a.sibilantShare, a.rangeDb, a.rumbleDb, a.inputLufs, r.outputLufs);
    for (size_t i = 0; i < a.bandDb.size(); ++i) std::printf ("%g:%.1f ", a.bandHz[i], a.bandDb[i]);
    std::printf ("\n");
}
