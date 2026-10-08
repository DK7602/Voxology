// Renders a vocal through Auto-Edit's whole chain (as the plug-in would set it), for before / after listening.
// Build: g++ -std=c++20 -O2 -Idsp/include tools/audit/render_autoedit.cpp build-t/dsp/libvox_dsp.a -lpthread -o render_autoedit
// Run:   render_autoedit in.f64 style out.f32   (in: raw doubles, mono 48 kHz; out: raw float, interleaved;
//        prints the channel count). Then e.g. ffmpeg -f f32le -ar 48000 -ac 2 -i out.f32 out.mp3
#include "vox/AutoEdit.h"
#include "vox/VocalChain.h"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <vector>

int main (int argc, char** argv)
{
    if (argc < 4) { std::printf ("usage: render_autoedit in.f64 style out.f32\n"); return 1; }
    std::ifstream in (argv[1], std::ios::binary);
    std::vector<float> x;
    double v;
    while (in.read (reinterpret_cast<char*> (&v), sizeof v)) x.push_back (static_cast<float> (v));
    vox::AutoEditSettings s;
    s.style = std::atoi (argv[2]);
    const auto r = vox::autoEdit ({ x }, 48000.0, s);
    const auto y = vox::VocalChain::render ({ x }, 48000.0, r.params);
    std::ofstream out (argv[3], std::ios::binary);
    for (size_t i = 0; i < y[0].size(); ++i)
        for (const auto& c : y) out.write (reinterpret_cast<const char*> (&c[i]), sizeof (float));
    std::printf ("%zu\n", y.size());
    return 0;
}
