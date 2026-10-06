#include "vox/DeClip.h"

#include <algorithm>
#include <cmath>

namespace vox {

void DeClip::prepare (int numChannels)
{
    chans.assign (static_cast<size_t> (std::max (1, numChannels)), {});
    for (auto& c : chans)
    {
        c.ring.assign (kRing, 0.0);
        c.pendingA.reserve (32);
        c.pendingB.reserve (32);
    }
    const size_t W = 2 * kContext + kMaxRun;
    win.assign (W, 0.0);
    known.assign (W, 0.0);
    ac.assign (kOrder + 1, 0.0);
    coef.assign (kOrder + 1, 0.0);
    R.assign (2 * (kOrder + 1), 0.0);
    A.assign (static_cast<size_t> (kMaxRun * kMaxRun), 0.0);
    rhs.assign (kMaxRun, 0.0);
    sol.assign (kMaxRun, 0.0);
    reset();
}

void DeClip::reset() noexcept
{
    for (auto& c : chans)
    {
        std::fill (c.ring.begin(), c.ring.end(), 0.0);
        c.runStart = -1;
        c.runValue = 0.0;
        c.pendingA.clear();
        c.pendingB.clear();
        c.peak = 0.0;
    }
    now = 0;
    repairs = 0;
}

void DeClip::repair (Channel& c, int64_t a, int64_t b) noexcept
{
    // Window: kContext known samples, the run [a, b], kContext known samples.
    const int M = static_cast<int> (b - a + 1);
    const int W = 2 * kContext + M;
    const int64_t w0 = a - kContext;
    for (int i = 0; i < W; ++i) win[static_cast<size_t> (i)] = c.ring[static_cast<size_t> ((w0 + i) & kMask)];
    const double clip = c.runValue;

    // All-pole model from the known samples (autocorrelation of the two sides, Hann-tapered; Levinson).
    const int p = kOrder;
    std::fill (ac.begin(), ac.end(), 0.0);
    for (int side = 0; side < 2; ++side)
    {
        const int s0 = side == 0 ? 0 : kContext + M;
        for (int i = 0; i < kContext; ++i)
            known[static_cast<size_t> (i)] = win[static_cast<size_t> (s0 + i)] * (0.5 - 0.5 * std::cos (2.0 * 3.141592653589793 * (i + 0.5) / kContext));
        for (int k = 0; k <= p; ++k)
        {
            double s = 0.0;
            for (int i = k; i < kContext; ++i) s += known[static_cast<size_t> (i)] * known[static_cast<size_t> (i - k)];
            ac[static_cast<size_t> (k)] += s;
        }
    }
    if (ac[0] <= 1.0e-12) return;
    ac[0] *= 1.0 + 1.0e-4;   // a touch of white noise: keeps the model stable
    std::fill (coef.begin(), coef.end(), 0.0);
    coef[0] = 1.0;
    double err = ac[0];
    for (int i = 1; i <= p; ++i)
    {
        double acc = ac[static_cast<size_t> (i)];
        for (int j = 1; j < i; ++j) acc += coef[static_cast<size_t> (j)] * ac[static_cast<size_t> (i - j)];
        const double k = -acc / err;
        for (int j = 1; j <= i / 2; ++j)
        {
            const double t1 = coef[static_cast<size_t> (j)], t2 = coef[static_cast<size_t> (i - j)];
            coef[static_cast<size_t> (j)] = t1 + k * t2;
            if (j != i - j) coef[static_cast<size_t> (i - j)] = t2 + k * t1;
        }
        coef[static_cast<size_t> (i)] = k;
        err *= 1.0 - k * k;
        if (err <= 0.0) return;
    }
    // Rc(d) = sum_k c_k c_{k+d}: the normal equations of the prediction error are Toeplitz in it.
    for (int d = 0; d <= p; ++d)
    {
        double s = 0.0;
        for (int k = 0; k + d <= p; ++k) s += coef[static_cast<size_t> (k)] * coef[static_cast<size_t> (k + d)];
        R[static_cast<size_t> (d)] = s;
    }
    auto Rc = [&] (int d) { d = std::abs (d); return d <= p ? R[static_cast<size_t> (d)] : 0.0; };
    // Unknowns x_i = win[kContext + i]: minimise the model's prediction error over the window.
    for (int i = 0; i < M; ++i)
    {
        for (int j = 0; j < M; ++j) A[static_cast<size_t> (i * M + j)] = Rc (i - j);
        double s = 0.0;
        const int gi = kContext + i;
        for (int g = std::max (0, gi - p); g <= std::min (W - 1, gi + p); ++g)
            if (g < kContext || g >= kContext + M) s += Rc (gi - g) * win[static_cast<size_t> (g)];
        rhs[static_cast<size_t> (i)] = -s;
    }
    // Cholesky (A is symmetric positive definite).
    for (int i = 0; i < M; ++i)
    {
        for (int j = 0; j <= i; ++j)
        {
            double s = A[static_cast<size_t> (i * M + j)];
            for (int k = 0; k < j; ++k) s -= A[static_cast<size_t> (i * M + k)] * A[static_cast<size_t> (j * M + k)];
            if (i == j)
            {
                if (s <= 1.0e-18) return;
                A[static_cast<size_t> (i * M + i)] = std::sqrt (s);
            }
            else A[static_cast<size_t> (i * M + j)] = s / A[static_cast<size_t> (j * M + j)];
        }
    }
    for (int i = 0; i < M; ++i)
    {
        double s = rhs[static_cast<size_t> (i)];
        for (int k = 0; k < i; ++k) s -= A[static_cast<size_t> (i * M + k)] * sol[static_cast<size_t> (k)];
        sol[static_cast<size_t> (i)] = s / A[static_cast<size_t> (i * M + i)];
    }
    for (int i = M - 1; i >= 0; --i)
    {
        double s = sol[static_cast<size_t> (i)];
        for (int k = i + 1; k < M; ++k) s -= A[static_cast<size_t> (k * M + i)] * sol[static_cast<size_t> (k)];
        sol[static_cast<size_t> (i)] = s / A[static_cast<size_t> (i * M + i)];
    }
    // Keep it consistent with the clip: the true signal was at the clip level or beyond, same sign; never
    // wildly beyond (a model blow-up): at most 4x (+12 dB) the clip level.
    for (int i = 0; i < M; ++i)
    {
        double v = sol[static_cast<size_t> (i)];
        if (! std::isfinite (v)) return;
        v = clip > 0.0 ? std::clamp (v, clip, 4.0 * clip) : std::clamp (v, 4.0 * clip, clip);
        sol[static_cast<size_t> (i)] = v;
    }
    for (int i = 0; i < M; ++i) c.ring[static_cast<size_t> ((a + i) & kMask)] = sol[static_cast<size_t> (i)];
    ++repairs;
}

void DeClip::process (double* const* ch, int nch, int n) noexcept
{
    nch = std::min (nch, static_cast<int> (chans.size()));
    const double peakFall = 0.99999;   // ~ -1 dB / 2.4 s at 48 kHz
    for (int i = 0; i < n; ++i)
    {
        const int64_t t = now;
        for (int c = 0; c < nch; ++c)
        {
            auto& s = chans[static_cast<size_t> (c)];
            const double v = ch[c][i];
            s.ring[static_cast<size_t> (t & kMask)] = v;
            s.peak = std::max (std::abs (v), s.peak * peakFall);
            if (enabled)
            {
                // Follow flat runs: the same loud value (within 0.01 %) sample after sample.
                const double prev = s.ring[static_cast<size_t> ((t - 1) & kMask)];
                const bool flatHere = std::abs (v) > 0.01 && std::abs (v) > 0.25 * s.peak && std::abs (v - prev) <= 1.0e-4 * std::abs (v);
                if (flatHere)
                {
                    if (s.runStart < 0) { s.runStart = t - 1; s.runValue = prev; }
                }
                else if (s.runStart >= 0)
                {
                    const int64_t a = s.runStart, b = t - 1;
                    if (b - a + 1 >= 3 && b - a + 1 <= kMaxRun && s.pendingA.size() < s.pendingA.capacity())
                    {
                        s.pendingA.push_back (a);
                        s.pendingB.push_back (b);
                        // (the sign / level of the run: its value)
                        s.runValue = s.ring[static_cast<size_t> (a & kMask)];
                    }
                    s.runStart = -1;
                }
                // Repair the runs whose right-hand context has arrived.
                for (size_t k = 0; k < s.pendingA.size();)
                {
                    if (t >= s.pendingB[k] + kContext)
                    {
                        s.runValue = s.ring[static_cast<size_t> (s.pendingA[k] & kMask)];
                        repair (s, s.pendingA[k], s.pendingB[k]);
                        s.pendingA.erase (s.pendingA.begin() + static_cast<long> (k));
                        s.pendingB.erase (s.pendingB.begin() + static_cast<long> (k));
                    }
                    else ++k;
                }
            }
            const int64_t o = t - kLookahead;
            ch[c][i] = o >= 0 ? s.ring[static_cast<size_t> (o & kMask)] : 0.0;
        }
        ++now;
    }
}

} // namespace vox
