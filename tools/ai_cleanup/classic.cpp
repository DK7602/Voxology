// Voxology's classic Cleanup (Noise / Room) on a raw float32 mono 48 kHz file, latency removed.
// usage: classic in.f32 out.f32 noiseDb roomPct
#include "vox/Denoise.h"
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <vector>
int main (int argc, char** argv)
{
    if (argc < 5) return 1;
    std::vector<float> x; { std::ifstream f (argv[1], std::ios::binary); float v; while (f.read ((char*) &v, 4)) x.push_back (v); }
    vox::Denoiser d; d.prepare (48000.0, 1);
    d.setParams ({ std::atof (argv[3]), std::atof (argv[4]), 0.0 });
    const int lat = d.latencySamples();
    std::vector<double> buf (x.size() + lat, 0.0);
    for (size_t i = 0; i < x.size(); ++i) buf[i] = x[i];
    const auto t0 = std::chrono::steady_clock::now();
    for (size_t i = 0; i < buf.size(); i += 480) { double* ch = buf.data() + i; d.process (&ch, 1, (int) std::min<size_t> (480, buf.size() - i)); }
    const double s = std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count();
    std::ofstream o (argv[2], std::ios::binary);
    for (size_t i = 0; i < x.size(); ++i) { const float v = (float) buf[i + lat]; o.write ((const char*) &v, 4); }
    std::printf ("latency %d samples, %.4f s CPU for %.2f s audio (rtf %.4f)\n", lat, s, x.size() / 48000.0, s / (x.size() / 48000.0));
}
