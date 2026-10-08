// Tone EQ on many vocals: Auto-Edit's five Tone EQ gains per file (style Rap if mostly rapped, else Pop), one CSV
// row each. A finished vocal should get small moves ("do no harm").
// Build: g++ -std=c++20 -O2 -Idsp/include tools/audit/eq_batch.cpp build-t/dsp/libvox_dsp.a -lpthread -o eq_batch
// Run:   eq_batch a.f64 b.f64 ... > eq.csv   (raw doubles, mono 48 kHz)
#include "vox/AutoEdit.h"

#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

int main (int argc, char** argv)
{
    std::printf ("name,style,body,mud,nasal,presence,air,bodyHz,mudHz,nasalHz,presHz,airHz\n");
    for (int k = 1; k < argc; ++k)
    {
        std::ifstream in (argv[k], std::ios::binary);
        std::vector<float> x;
        double v;
        while (in.read (reinterpret_cast<char*> (&v), sizeof v)) x.push_back (static_cast<float> (v));
        const auto a = vox::analyseVocal ({ x }, 48000.0);
        if (a.voicedSeconds < 3.0) continue;
        vox::AutoEditSettings s;
        s.style = a.heldShare < 25.0 ? 1 : 5;
        const auto r = vox::autoEdit ({ x }, 48000.0, s);
        if (! r.ok) continue;
        std::string name = argv[k];
        name = name.substr (name.find_last_of ('/') + 1);
        for (auto& c : name) if (c == ',') c = ' ';
        const auto& e = r.params.eq;
        std::printf ("%s,%d,%.1f,%.1f,%.1f,%.1f,%.1f,%.0f,%.0f,%.0f,%.0f,%.0f\n", name.c_str(), s.style, e.gainDb[0], e.gainDb[1], e.gainDb[2], e.gainDb[3], e.gainDb[4],
                     e.freqHz[0], e.freqHz[1], e.freqHz[2], e.freqHz[3], e.freqHz[4]);
    }
    return 0;
}
