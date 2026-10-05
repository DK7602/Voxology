#pragma once

#include <cmath>
#include <complex>
#include <numbers>
#include <utility>
#include <vector>

namespace vox {

/** Small in-place radix-2 FFT (power-of-two sizes), for offline analysis only. */
inline void fft (std::vector<std::complex<double>>& a)
{
    const size_t n = a.size();
    for (size_t i = 1, j = 0; i < n; ++i)
    {
        size_t bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) std::swap (a[i], a[j]);
    }
    for (size_t len = 2; len <= n; len <<= 1)
    {
        const double ang = -2.0 * std::numbers::pi / static_cast<double> (len);
        const std::complex<double> wl (std::cos (ang), std::sin (ang));
        for (size_t i = 0; i < n; i += len)
        {
            std::complex<double> w (1.0, 0.0);
            for (size_t k = 0; k < len / 2; ++k)
            {
                const auto u = a[i + k], v = a[i + k + len / 2] * w;
                a[i + k] = u + v;
                a[i + k + len / 2] = u - v;
                w *= wl;
            }
        }
    }
}

/** Power spectrum (|X|^2, bins 0..n/2) of a Hann-windowed frame. */
inline std::vector<double> powerSpectrum (const double* x, size_t n)
{
    std::vector<std::complex<double>> a (n);
    for (size_t i = 0; i < n; ++i)
    {
        const double w = 0.5 - 0.5 * std::cos (2.0 * std::numbers::pi * static_cast<double> (i) / static_cast<double> (n));
        a[i] = { x[i] * w, 0.0 };
    }
    fft (a);
    std::vector<double> p (n / 2 + 1);
    for (size_t i = 0; i <= n / 2; ++i) p[i] = std::norm (a[i]);
    return p;
}

} // namespace vox
