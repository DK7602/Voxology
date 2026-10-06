#include "vox/Crepe.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <numbers>

namespace vox::crepe {

extern const std::uint16_t kTinyWeights[];
extern const int kTinyWeightCount;

namespace {
float halfToFloat (std::uint16_t h)
{
    const std::uint32_t sign = (h & 0x8000u) << 16, expo = (h >> 10) & 0x1Fu, mant = h & 0x3FFu;
    std::uint32_t bits = 0;
    if (expo == 0)
    {
        if (mant == 0) bits = sign;
        else
        {
            // Subnormal: normalise.
            int e = -1; std::uint32_t m = mant;
            do { ++e; m <<= 1; } while ((m & 0x400u) == 0);
            bits = sign | static_cast<std::uint32_t> (127 - 15 - e) << 23 | (m & 0x3FFu) << 13;
        }
    }
    else if (expo == 31) bits = sign | 0x7F800000u | mant << 13;
    else bits = sign | (expo + 112u) << 23 | mant << 13;
    float f;
    std::memcpy (&f, &bits, sizeof f);
    return f;
}
}

Tiny::Tiny()
{
    w.resize (static_cast<size_t> (kTinyWeightCount));
    for (size_t i = 0; i < w.size(); ++i) w[i] = halfToFloat (kTinyWeights[i]);
    // Layout (see CrepeWeights.cpp): per layer kernel (width x in x out), bias, gamma, beta, mean, var.
    const int widths[6] = { 512, 64, 64, 64, 64, 64 }, outs[6] = { 128, 16, 16, 16, 32, 64 };
    size_t at = 0;
    int in = 1;
    for (int l = 0; l < 6; ++l)
    {
        auto& c = conv[static_cast<size_t> (l)];
        c = { widths[l], in, outs[l], l == 0 ? 4 : 1, at, 0, 0, 0 };
        at += static_cast<size_t> (widths[l] * in * outs[l]);
        c.bias = at; at += static_cast<size_t> (outs[l]);
        const size_t gamma = at, beta = at + static_cast<size_t> (outs[l]), mean = beta + static_cast<size_t> (outs[l]), var = mean + static_cast<size_t> (outs[l]);
        at = var + static_cast<size_t> (outs[l]);
        // Batch norm (Keras, eps 1e-3) as a scale and shift, written over gamma / beta.
        for (int o = 0; o < outs[l]; ++o)
        {
            const auto k = static_cast<size_t> (o);
            const float s = w[gamma + k] / std::sqrt (w[var + k] + 1.0e-3f);
            w[beta + k] = w[beta + k] - w[mean + k] * s;
            w[gamma + k] = s;
        }
        c.scale = gamma; c.shift = beta;
        in = outs[l];
    }
    dense = at; at += 256 * 360;
    denseBias = at;
    a.resize (256 * 128);
    b.resize (256 * 128);
}

Estimate Tiny::run (const float* frame)
{
    // Normalise (zero mean, unit deviation), as the network was trained.
    double mean = 0.0, sq = 0.0;
    for (int i = 0; i < kFrame; ++i) mean += frame[i];
    mean /= kFrame;
    for (int i = 0; i < kFrame; ++i) sq += (frame[i] - mean) * (frame[i] - mean);
    const double sd = std::max (1.0e-8, std::sqrt (sq / kFrame));
    for (int i = 0; i < kFrame; ++i) a[static_cast<size_t> (i)] = static_cast<float> ((frame[i] - mean) / sd);

    int T = kFrame;
    float* x = a.data();
    float* y = b.data();
    std::vector<float> accum (128);
    for (const auto& c : conv)
    {
        // 'same' padding (TensorFlow): out = ceil(T / stride), extra padding at the end.
        const int out = (T + c.stride - 1) / c.stride;
        const int pad = std::max ((out - 1) * c.stride + c.width - T, 0), before = pad / 2;
        const float* K = w.data() + c.kernel;
        const float* bias = w.data() + c.bias;
        const float* scale = w.data() + c.scale;
        const float* shift = w.data() + c.shift;
        // conv -> ReLU -> batch norm, then max-pool by 2 (written straight into y).
        const int pooled = out / 2;
        for (int t = 0; t < pooled * 2; ++t)
        {
            std::copy (bias, bias + c.out, accum.begin());
            for (int k = 0; k < c.width; ++k)
            {
                const int src = t * c.stride + k - before;
                if (src < 0 || src >= T) continue;
                const float* xr = x + static_cast<size_t> (src) * static_cast<size_t> (c.in);
                const float* kr = K + static_cast<size_t> (k) * static_cast<size_t> (c.in * c.out);
                for (int ci = 0; ci < c.in; ++ci)
                {
                    const float xv = xr[ci];
                    const float* kc = kr + static_cast<size_t> (ci) * static_cast<size_t> (c.out);
                    for (int co = 0; co < c.out; ++co) accum[static_cast<size_t> (co)] += xv * kc[co];
                }
            }
            float* yr = y + static_cast<size_t> (t / 2) * static_cast<size_t> (c.out);
            for (int co = 0; co < c.out; ++co)
            {
                const float v = std::max (0.0f, accum[static_cast<size_t> (co)]) * scale[co] + shift[co];
                yr[co] = (t % 2 == 0) ? v : std::max (yr[co], v);
            }
        }
        T = pooled;
        std::swap (x, y);
    }
    // Classifier: 4 x 64 (time-major) -> 360 sigmoids.
    std::array<float, 360> s {};
    const float* D = w.data() + dense;
    for (int o = 0; o < 360; ++o) s[static_cast<size_t> (o)] = w[denseBias + static_cast<size_t> (o)];
    for (int i = 0; i < 256; ++i)
    {
        const float xv = x[i];
        const float* dr = D + static_cast<size_t> (i) * 360;
        for (int o = 0; o < 360; ++o) s[static_cast<size_t> (o)] += xv * dr[o];
    }
    int peak = 0;
    for (int o = 0; o < 360; ++o)
    {
        s[static_cast<size_t> (o)] = 1.0f / (1.0f + std::exp (-s[static_cast<size_t> (o)]));
        if (s[static_cast<size_t> (o)] > s[static_cast<size_t> (peak)]) peak = o;
    }
    double num = 0.0, den = 0.0;
    for (int o = std::max (0, peak - 4); o <= std::min (359, peak + 4); ++o)
    {
        const double cents = 1997.3794084376191 + 7180.0 / 359.0 * o;
        num += s[static_cast<size_t> (o)] * cents;
        den += s[static_cast<size_t> (o)];
    }
    Estimate e;
    e.confidence = s[static_cast<size_t> (peak)];
    e.hz = den > 0.0 ? 10.0 * std::pow (2.0, num / den / 1200.0) : 0.0;
    return e;
}

std::vector<float> resampleTo16k (const std::vector<float>& x, double sr)
{
    if (std::abs (sr - kRate) < 0.5) return x;
    const double ratio = kRate / sr;
    const auto n = static_cast<size_t> (std::floor (static_cast<double> (x.size()) * ratio));
    std::vector<float> y (n);
    // Windowed sinc (Blackman, 32 zero crossings each side of the output rate's Nyquist, cut at 0.45 x 16 kHz).
    const double cutoff = std::min (1.0, ratio) * 0.9;   // relative to the input's Nyquist
    const int half = static_cast<int> (std::ceil (32.0 / cutoff));
    for (size_t j = 0; j < n; ++j)
    {
        const double centre = static_cast<double> (j) / ratio;
        const auto c0 = static_cast<long> (std::floor (centre));
        double acc = 0.0, wsum = 0.0;
        for (long i = c0 - half + 1; i <= c0 + half; ++i)
        {
            if (i < 0 || i >= static_cast<long> (x.size())) continue;
            const double t = static_cast<double> (i) - centre;
            const double u = t / half;
            const double win = 0.42 + 0.5 * std::cos (std::numbers::pi * u) + 0.08 * std::cos (2.0 * std::numbers::pi * u);
            const double arg = std::numbers::pi * t * cutoff;
            const double sinc = std::abs (arg) < 1.0e-9 ? 1.0 : std::sin (arg) / arg;
            acc += x[static_cast<size_t> (i)] * sinc * win;
            wsum += sinc * win;
        }
        y[j] = static_cast<float> (wsum != 0.0 ? acc / wsum : 0.0);
    }
    return y;
}

} // namespace vox::crepe
