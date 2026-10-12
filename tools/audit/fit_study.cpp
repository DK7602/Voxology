// Vocal Fit study: how finished pro vocals sit against their own beats.
// For each song: the vocal stem and the beat (everything else), both mono f64 at 48 kHz, the same length.
// Prints one CSV row per song: Auto-Edit's analysis of the vocal + analyseBeat() of the beat under it.
//   fit_study name1 vocal1.f64 beat1.f64 [name2 vocal2.f64 beat2.f64 ...] > fit.csv

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
    const auto& bands = vox::analysisBands();
    std::printf ("name,voiced,held,f0,sibDb,sibHz,microDyn,rangeDb,vocalOverBeat,beatLufs,bright,low,busy,under");
    for (double f : bands) std::printf (",v%.0f", f);
    for (double f : bands) std::printf (",b%.0f", f);
    for (double f : bands) std::printf (",o%.0f", f);
    std::printf ("\n");
    for (int i = 1; i + 2 < argc; i += 3)
    {
        auto voc = readF64 (argv[i + 1]), beat = readF64 (argv[i + 2]);
        const size_t n = std::min (voc.size(), beat.size());
        voc.resize (n);
        beat.resize (n);
        const std::vector<std::vector<float>> v { voc };
        const auto a = vox::analyseVocal (v, sr);
        const auto b = vox::analyseBeat (beat, v, sr);
        std::printf ("\"%s\",%.2f,%.1f,%.1f,%.2f,%.0f,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f", argv[i], a.voicedSeconds, a.heldShare, a.f0Median,
                     a.sibilanceDb, a.sibilanceHz, a.microDynDb, a.rangeDb, b.vocalOverBeatDb, b.lufs, b.brightDb, b.lowDb, b.busyDb, b.underVoiceSeconds);
        for (double d : a.bandDb) std::printf (",%.2f", d);
        for (double d : b.bandDb) std::printf (",%.2f", d);
        for (double d : b.overDb) std::printf (",%.2f", d);
        std::printf ("\n");
        std::fflush (stdout);
    }
}
