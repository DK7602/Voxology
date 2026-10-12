// Split-vocal study: Auto-Edit's analysis of vocals as CSV (the real stem and the same vocal split from the mix).
//   split_study name a.f64 [name b.f64 ...] > out.csv     (mono f64, 48 kHz)
#include "vox/AutoEdit.h"

#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

static std::vector<float> readF64 (const char* path)
{
    std::ifstream in (path, std::ios::binary);
    std::vector<float> x;
    double v;
    while (in.read (reinterpret_cast<char*> (&v), sizeof v)) x.push_back (static_cast<float> (v));
    return x;
}

int main (int argc, char** argv)
{
    constexpr double sr = 48000.0;
    std::printf ("name,voiced,sib,microDyn,tail,range,voiceRms,noiseFloor,noisePeak,gaps,lowBass,held,f0");
    for (double f : vox::analysisBands()) std::printf (",b%.0f", f);
    std::printf ("\n");
    for (int i = 1; i + 1 < argc; i += 2)
    {
        const std::vector<std::vector<float>> v { readF64 (argv[i + 1]) };
        const auto a = vox::analyseVocal (v, sr);
        std::printf ("\"%s\",%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%d,%.2f,%.1f,%.1f", argv[i], a.voicedSeconds, a.sibilanceDb, a.microDynDb, a.tailDb,
                     a.rangeDb, a.voiceRmsDb, a.noiseFloorDb, a.noisePeakDb, a.heardGaps ? 1 : 0, a.lowBassDb, a.heldShare, a.f0Median);
        for (double d : a.bandDb) std::printf (",%.2f", d);
        std::printf ("\n");
        std::fflush (stdout);
    }
}
