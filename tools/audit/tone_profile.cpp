// Tone profile: Auto-Edit's analysis of one or more vocals as one CSV row each (name, voiced seconds, held share,
// pitched share, f0 median, sibilance, micro-dynamics, then the 23 third-octave bands vs the 500 Hz - 2 kHz average).
// Used to measure finished pro vocals (e.g. MUSDB18 vocal stems) against Auto-Edit's built-in style targets.
// Build: g++ -std=c++20 -O2 -Idsp/include tools/audit/tone_profile.cpp build-t/dsp/libvox_dsp.a -lpthread -o tone_profile
// Run:   tone_profile a.f64 b.f64 ... > tones.csv   (raw doubles, mono 48 kHz)
#include "vox/AutoEdit.h"

#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

int main (int argc, char** argv)
{
    std::printf ("name,voiced,held,pitched,f0,sib,microdyn,rms");
    for (double f : vox::analysisBands()) std::printf (",b%.0f", f);
    std::printf ("\n");
    for (int k = 1; k < argc; ++k)
    {
        std::ifstream in (argv[k], std::ios::binary);
        std::vector<float> x;
        double v;
        while (in.read (reinterpret_cast<char*> (&v), sizeof v)) x.push_back (static_cast<float> (v));
        const auto a = vox::analyseVocal ({ x }, 48000.0);
        std::string name = argv[k];
        name = name.substr (name.find_last_of ('/') + 1);
        for (auto& c : name) if (c == ',') c = ' ';
        std::printf ("%s,%.2f,%.1f,%.1f,%.0f,%.1f,%.1f,%.1f", name.c_str(), a.voicedSeconds, a.heldShare, a.pitchedShare, a.f0Median, a.sibilanceDb, a.microDynDb, a.voiceRmsDb);
        for (double b : a.bandDb) std::printf (",%.2f", b);
        std::printf ("\n");
    }
    return 0;
}
