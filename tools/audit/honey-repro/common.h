#pragma once
#include "vox/HoneyTune.h"
#include <cmath>
#include <cstdio>
#include <numbers>
#include <vector>
struct Sung { double midi, seconds, gap; };
inline std::vector<float> sing (const std::vector<Sung>& line, double sr, double lead = 0.5)
{
    std::vector<float> x (static_cast<size_t> (lead * sr), 0.0f);
    double ph = 0.0;
    for (const auto& s : line)
    {
        const auto n = static_cast<size_t> (s.seconds * sr);
        for (size_t i = 0; i < n; ++i)
        {
            const double t = i / sr;
            const double env = std::min ({ 1.0, t / 0.02, (s.seconds - t) / 0.02 });
            const double f = 440.0 * std::pow (2.0, (s.midi - 69.0) / 12.0);
            ph += f / sr;
            double v = 0.0;
            for (int h = 1; h * f < 6000.0; ++h) v += std::sin (2 * std::numbers::pi * h * ph) / h;
            x.push_back (static_cast<float> (0.1 * env * v));
        }
        for (size_t i = 0; i < static_cast<size_t> (s.gap * sr); ++i) x.push_back (0.0f);
    }
    return x;
}
inline double rmsDb (const std::vector<float>& x, double sr, double a, double b)
{
    double e = 0; size_t n = 0;
    for (size_t i = static_cast<size_t> (a * sr); i < static_cast<size_t> (b * sr) && i < x.size(); ++i, ++n) e += x[i] * x[i];
    return 10 * std::log10 (e / std::max<size_t> (1, n) + 1e-20);
}
inline double hzAt (const std::vector<float>& x, double sr, double fromSec, double lenSec)
{
    const auto from = static_cast<size_t> (fromSec * sr), len = static_cast<size_t> (lenSec * sr);
    const int minLag = static_cast<int> (sr / 1000), maxLag = static_cast<int> (sr / 70);
    double best = -1; int bl = minLag;
    std::vector<double> rs;
    for (int lag = minLag; lag <= maxLag; ++lag)
    {
        double xy = 0, xx = 0, yy = 0;
        for (size_t i = from; i < from + len && i + lag < x.size(); ++i) { const double a = x[i], b = x[i + lag]; xy += a * b; xx += a * a; yy += b * b; }
        const double r = xy / std::sqrt (xx * yy + 1e-30);
        rs.push_back (r);
        if (r > best) best = r;
    }
    for (int lag = minLag + 1; lag < maxLag; ++lag)
    {
        const double r = rs[lag - minLag];
        if (r > 0.9 * best && r >= rs[lag - minLag - 1] && r >= rs[lag - minLag + 1]) { bl = lag; break; }
    }
    return best > 0.5 ? sr / bl : 0.0;
}
inline double midiOf (double hz) { return hz > 0 ? 69.0 + 12.0 * std::log2 (hz / 440.0) : 0.0; }
