// Vocal Fit: Auto-Edit on a vocal with its beat on the "side-chain", printing what Vocal Fit reports.
//   fit_report vocal.f64 beat.f64 [style] [seconds]   (mono f64, 48 kHz; seconds: how much Auto-Edit hears, default 30)

#include "vox/AutoEdit.h"

#include <cstdio>
#include <cstdlib>
#include <fstream>
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
    if (argc < 3) { std::fprintf (stderr, "fit_report vocal.f64 beat.f64 [style] [seconds]\n"); return 1; }
    constexpr double sr = 48000.0;
    auto voc = readF64 (argv[1]), beat = readF64 (argv[2]);
    const size_t n = std::min ({ voc.size(), beat.size(), static_cast<size_t> (sr * (argc > 4 ? std::atof (argv[4]) : 30.0)) });
    voc.resize (n);
    beat.resize (n);
    vox::AutoEditSettings s;
    s.style = argc > 3 ? std::atoi (argv[3]) : 0;
    s.bpm = 140.0;
    s.beat = &beat;
    const auto r = vox::autoEdit ({ voc }, sr, s);
    std::printf ("ok %d  beat heard %d (%.1f s under the voice)\n", r.ok, r.beat.heard, r.beat.underVoiceSeconds);
    for (const auto& f : r.fit) std::printf ("  %-22s %-26s %s\n", f.control.c_str(), f.value.c_str(), f.why.c_str());
    std::printf ("  FIT LEVEL: Output %+.1f dB (full move %+.1f dB) -> %+.1f dB; tone moves %+.1f / %+.1f / %+.1f dB; punch %+.1f dB\n", r.fitLevelDb,
                 r.fitLevelFullDb, r.params.outputDb + r.fitLevelDb, r.fitToneDb[0], r.fitToneDb[1], r.fitToneDb[2], r.fitPunchDb);
    for (const auto& note : r.notes) std::printf ("NOTE %s\n", note.c_str());
}
