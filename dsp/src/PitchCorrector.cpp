#include "vox/PitchCorrector.h"

#include "vox/FilterDesign.h"

#include <algorithm>
#include <cmath>
#include <numbers>
#ifdef VOX_PITCH_DEBUG
#include <cstdio>
double dbgFrom = 0, dbgTo = 0;
#endif

namespace vox {

namespace {
constexpr double kYinThreshold = 0.12;     // first dip under this = the period
constexpr double kVoicedAperiodicity = 0.25;
constexpr double kVoicedLevelDb = -55.0;   // quieter than this is never pitched
constexpr double kUnvoicedGrainSeconds = 0.005;

int nextPow2 (int v) { int p = 1; while (p < v) p <<= 1; return p; }
}

void PitchCorrector::prepare (double sampleRate, int numChannels)
{
    sr = sampleRate;
    channels = std::clamp (numChannels, 1, 2);
    const double maxP = std::ceil (sr / kMinHz);
    // Room for a grain to be fully laid down before its samples leave (see synthesiseUpTo).
    latency = std::max (static_cast<int> (std::lround (kLatencySeconds * sr)), static_cast<int> (std::ceil (2.5 * maxP)) + 16);
    ringSize = nextPow2 (4 * latency + 4 * static_cast<int> (maxP));
    mask = ringSize - 1;
    for (auto& v : in) v.assign (static_cast<size_t> (ringSize), 0.0);
    for (auto& v : acc) v.assign (static_cast<size_t> (ringSize), 0.0);
    wsum.assign (static_cast<size_t> (ringSize), 0.0);
    mono.assign (static_cast<size_t> (ringSize), 0.0);

    dec = std::max (1, static_cast<int> (std::lround (sr / 12000.0)));
    const double fsd = sr / dec;
    const auto tauMax = static_cast<size_t> (std::ceil (fsd / kMinHz)) + 2;
    diff.assign (tauMax + 1, 0.0);
    cmnd.assign (tauMax + 1, 0.0);
    dbuf.assign (static_cast<size_t> (nextPow2 (static_cast<int> (4 * tauMax) + 64)), 0.0);
    dmask = static_cast<int> (dbuf.size()) - 1;
    const auto lp = design::butterworth (false, 0.42 * fsd, sr);
    design::apply (aa1, lp);
    design::apply (aa2, lp);
    hop = std::max (32, static_cast<int> (std::lround (0.00267 * sr)));
    levelCoeff = design::onePole (0.010, sr);
    reset();
}

void PitchCorrector::reset() noexcept
{
    for (auto& v : in) std::fill (v.begin(), v.end(), 0.0);
    for (auto& v : acc) std::fill (v.begin(), v.end(), 0.0);
    std::fill (wsum.begin(), wsum.end(), 0.0);
    std::fill (mono.begin(), mono.end(), 0.0);
    std::fill (dbuf.begin(), dbuf.end(), 0.0);
    aa1.reset(); aa2.reset();
    now = 0; dnow = 0; decAcc = 0.0; decCount = 0; hopCount = 0;
    period = 0.0; levelMs = 0.0;
    note = -1; corr = 0.0; sustain = 0.0; voicedRun = 0;
    rawP = { 0.0, 0.0, 0.0 };
    last = {};
    synthPos = anaPos = 0.0;
    drift = 0.0;
    frameCount = 0;
    wasNeutral = true;
}

int PitchCorrector::targetNote (double midi, int key, int scale, int current) noexcept
{
    const auto& m = kScaleMasks[static_cast<size_t> (std::clamp (scale, 0, kScales - 1))];
    auto allowed = [&] (int n) { return m[static_cast<size_t> (((n - key) % 12 + 12) % 12)]; };
    const int centre = static_cast<int> (std::lround (midi));
    int best = centre;
    double bestDist = 1.0e9;
    for (int n = centre - 4; n <= centre + 4; ++n)
        if (allowed (n) && std::abs (n - midi) < bestDist) { bestDist = std::abs (n - midi); best = n; }
    // Hysteresis: stay on the current note until another is clearly (0.3 semitone) closer.
    if (current >= 0 && current != best && allowed (current) && std::abs (current - midi) <= bestDist + 0.3)
        return current;
    return best;
}

void PitchCorrector::analyse() noexcept
{
    const auto tauMax = diff.size() - 1;
    const size_t W = tauMax;
    if (dnow < static_cast<int64_t> (W + tauMax)) return;

    // YIN on the newest W + tauMax decimated samples.
    const int64_t start = dnow - static_cast<int64_t> (W + tauMax);
    auto x = [&] (size_t j) { return dbuf[static_cast<size_t> ((start + static_cast<int64_t> (j)) & dmask)]; };
    diff[0] = 0.0;
    for (size_t tau = 1; tau <= tauMax; ++tau)
    {
        double s = 0.0;
        for (size_t j = 0; j < W; ++j) { const double d = x (j) - x (j + tau); s += d * d; }
        diff[tau] = s;
    }
    cmnd[0] = 1.0;
    double running = 0.0;
    for (size_t tau = 1; tau <= tauMax; ++tau)
    {
        running += diff[tau];
        cmnd[tau] = running > 0.0 ? diff[tau] * static_cast<double> (tau) / running : 1.0;
    }
    const double fsd = sr / dec;
    const auto tauMin = static_cast<size_t> (std::floor (fsd / kMaxHz));
    size_t pick = 0;
    for (size_t tau = std::max<size_t> (2, tauMin); tau < tauMax; ++tau)
        if (cmnd[tau] < kYinThreshold)
        {
            while (tau + 1 < tauMax && cmnd[tau + 1] < cmnd[tau]) ++tau;
            pick = tau;
            break;
        }
    if (pick == 0)
    {
        double best = 1.0e9;
        for (size_t tau = std::max<size_t> (2, tauMin); tau < tauMax; ++tau)
            if (cmnd[tau] < best) { best = cmnd[tau]; pick = tau; }
    }
    const double aper = cmnd[pick];
    const double levelDb = 10.0 * std::log10 (levelMs + 1.0e-24) + 3.0103;
    const bool rawVoiced = pick > 1 && pick + 1 <= tauMax && aper < kVoicedAperiodicity && levelDb > kVoicedLevelDb;
    // Two readings in a row before switching between note and breath / consonant, so a voice
    // that flickers at a word edge doesn't make the grains jump back and forth.
    voicedRun = rawVoiced ? std::max (1, voicedRun + 1) : std::min (-1, voicedRun - 1);
    const bool voiced = period > 0.0 ? voicedRun > -2 : voicedRun >= 2;
    const double hopSec = hop / sr;

    if (voiced)
    {
        // Parabolic interpolation, then refine at full rate by normalised cross-correlation.
        const double a = cmnd[pick - 1], b = cmnd[pick], c = cmnd[pick + 1];
        const double den = a - 2.0 * b + c;
        const double tauD = static_cast<double> (pick) + (std::abs (den) > 1.0e-12 ? 0.5 * (a - c) / den : 0.0);
        const double pEst = tauD * dec;
        const int lo = std::max (8, static_cast<int> (std::floor (pEst)) - dec - 1);
        const int hi = static_cast<int> (std::ceil (pEst)) + dec + 1;
        const int len = static_cast<int> (std::ceil (pEst));
        double bestR = -2.0; int bestL = static_cast<int> (std::lround (pEst));
        std::array<double, 64> rs {};
        const int span = std::min (hi - lo + 1, 64);
        for (int k = 0; k < span; ++k)
        {
            const int lag = lo + k;
            double xy = 0.0, xx = 0.0, yy = 0.0;
            for (int j = 0; j < len; ++j)
            {
                const double p = mono[static_cast<size_t> ((now - 1 - j) & mask)];
                const double q = mono[static_cast<size_t> ((now - 1 - j - lag) & mask)];
                xy += p * q; xx += p * p; yy += q * q;
            }
            rs[static_cast<size_t> (k)] = xy / std::sqrt (xx * yy + 1.0e-30);
            if (rs[static_cast<size_t> (k)] > bestR) { bestR = rs[static_cast<size_t> (k)]; bestL = lag; }
        }
        double p = bestL;
        const int bk = bestL - lo;
        if (bk > 0 && bk + 1 < span)
        {
            const double ra = rs[static_cast<size_t> (bk - 1)], rb = rs[static_cast<size_t> (bk)], rc = rs[static_cast<size_t> (bk + 1)];
            const double d2 = ra - 2.0 * rb + rc;
            if (std::abs (d2) > 1.0e-12) p += std::clamp (0.5 * (ra - rc) / d2, -0.5, 0.5);
        }
        // Median of the last three readings: a one-reading octave glitch (gritty voices do that)
        // never reaches the grains.
        rawP[2] = rawP[1]; rawP[1] = rawP[0]; rawP[0] = p;
        if (rawP[1] > 0.0 && rawP[2] > 0.0)
            p = std::max (std::min (rawP[0], rawP[1]), std::min (std::max (rawP[0], rawP[1]), rawP[2]));
        period = p;

        const double midi = 69.0 + 12.0 * std::log2 (sr / period / 440.0);
        const int prevNote = note;
        note = targetNote (midi, params.key, params.scale, note);
        sustain = note == prevNote ? sustain + hopSec : 0.0;
        const double desired = (note - midi) * std::clamp (params.amount, 0.0, 100.0) / 100.0;
        const double tau = params.speedMs / 1000.0 * (1.0 + 3.0 * std::clamp (params.humanize, 0.0, 100.0) / 100.0 * std::clamp (sustain / 0.6, 0.0, 1.0));
        corr += (desired - corr) * (tau <= 1.0e-4 ? 1.0 : 1.0 - std::exp (-hopSec / tau));
        last = { true, midi, note, corr };
        pushFrame (static_cast<double> (now - 1) - 0.5 * (len + p), period, corr);
    }
    else
    {
        period = 0.0;
        rawP = { 0.0, 0.0, 0.0 };
        corr += (0.0 - corr) * (1.0 - std::exp (-hopSec / 0.03));
        sustain += hopSec;
        if (sustain > 0.2) note = -1;   // a real gap: the next phrase picks its note afresh
        last = { false, 0.0, -1, corr };
        pushFrame (static_cast<double> (now - 1) - 0.25 * sr / kMinHz, 0.0, corr);
    }
}

void PitchCorrector::pushFrame (double time, double p, double c) noexcept
{
    // Keep the readings in time order (an unvoiced reading is stamped a little later than a voiced one).
    if (frameCount > 0)
        time = std::max (time, frames[static_cast<size_t> ((frameCount - 1) % kFrames)].time + 1.0);
    frames[static_cast<size_t> (frameCount++ % kFrames)] = { time, p, c };
}

PitchCorrector::Frame PitchCorrector::frameAt (double t) const noexcept
{
    if (frameCount == 0) return {};
    // Frames are in time order; find the pair around t (newest first) and interpolate.
    const int n = std::min (frameCount, kFrames);
    const Frame* after = nullptr;
    for (int k = 0; k < n; ++k)
    {
        const Frame& f = frames[static_cast<size_t> ((frameCount - 1 - k) % kFrames)];
        if (f.time <= t)
        {
            if (after == nullptr) return f;
            const double x = (t - f.time) / std::max (1.0e-9, after->time - f.time);
            Frame r;
            r.time = t;
            r.corr = f.corr + (after->corr - f.corr) * x;
            if (f.period > 0.0 && after->period > 0.0) r.period = f.period + (after->period - f.period) * x;
            else r.period = x < 0.5 ? f.period : after->period;
            return r;
        }
        after = &f;
    }
    return *after;   // older than everything we kept: the oldest reading
}

double PitchCorrector::alignMark (double prevMark, double candidate, double P) const noexcept
{
    // Real voices aren't perfectly periodic: put the new mark where the waveform best matches the
    // one at the previous mark (normalised cross-correlation over one period, within +-15 % of a
    // period), so repeated / skipped cycles join on matching shapes instead of ticking.
    const int len = static_cast<int> (P);
    const int reach = std::max (2, static_cast<int> (0.15 * P));
    const auto p0 = static_cast<int64_t> (std::lround (prevMark)) - len / 2;
    const auto c0 = static_cast<int64_t> (std::lround (candidate)) - len / 2;
    double best = -2.0; int bestD = 0;
    for (int d = -reach; d <= reach; ++d)
    {
        double xy = 0.0, xx = 0.0, yy = 0.0;
        for (int k = 0; k < len; k += 2)
        {
            const double a = mono[static_cast<size_t> ((p0 + k) & mask)];
            const double b = mono[static_cast<size_t> ((c0 + d + k) & mask)];
            xy += a * b; xx += a * a; yy += b * b;
        }
        const double r = xy / std::sqrt (xx * yy + 1.0e-30);
        if (r > best) { best = r; bestD = d; }
    }
    return std::round (candidate) + bestD + (candidate - std::round (candidate));
}

void PitchCorrector::synthesiseUpTo (int64_t limit) noexcept
{
    const double uvP = kUnvoicedGrainSeconds * sr;
    while (synthPos <= static_cast<double> (limit))
    {
        const Frame fr = frameAt (synthPos);
        const bool voiced = fr.period > 0.0;
        const double P = voiced ? fr.period : uvP;
        // Corrections under 3 cents aren't worth a repeated / skipped cycle: leave the voice alone.
        const double ratio = voiced && std::abs (fr.corr) >= 0.03 ? std::pow (2.0, fr.corr / 12.0) : 1.0;

        // The analysis point is the synthesis point plus a drift that only ever changes smoothly:
        // in a note it moves by P - P/ratio per grain (that's the pitch shift), and when it passes
        // half a period one cycle is repeated or skipped - once, at a matching waveform point. It is
        // never re-derived from scratch when the detected period changes (that made the read point
        // jump around at word edges: ticks).
        if (voiced)
        {
            if (drift > 0.5 * P)
                drift = alignMark (synthPos + drift, synthPos + drift - P, P) - synthPos;
            else if (drift < -0.5 * P)
                drift = alignMark (synthPos + drift, synthPos + drift + P, P) - synthPos;
        }
        // In breaths / consonants the offset just holds: a constant offset joins seamlessly (any
        // change in it misaligns two overlapping grains = a tick at the end of a note).
        anaPos = synthPos + drift;

#ifdef VOX_PITCH_DEBUG
        if (synthPos > dbgFrom && synthPos < dbgTo)
            std::fprintf (stderr, "grain s=%.1f a=%.1f P=%.2f ratio=%.5f voiced=%d drift=%.1f\n", synthPos, anaPos, P, ratio, voiced ? 1 : 0, drift);
#endif
        const auto first = static_cast<int64_t> (std::ceil (synthPos - P));
        const auto lastJ = static_cast<int64_t> (std::floor (synthPos + P));
        for (int64_t j = first; j <= lastJ; ++j)
        {
            const double u = static_cast<double> (j) - synthPos;
            const double w = 0.5 + 0.5 * std::cos (std::numbers::pi * u / P);
            if (w <= 0.0) continue;
            const double src = anaPos + u;
            const auto s0 = static_cast<int64_t> (std::floor (src));
            const double f = src - static_cast<double> (s0);
            const auto i0 = static_cast<size_t> (s0 & mask), i1 = static_cast<size_t> ((s0 + 1) & mask), o = static_cast<size_t> (j & mask);
            for (int c = 0; c < channels; ++c)
            {
                const auto& buf = in[static_cast<size_t> (c)];
                acc[static_cast<size_t> (c)][o] += w * (f == 0.0 ? buf[i0] : buf[i0] * (1.0 - f) + buf[i1] * f);
            }
            wsum[o] += w;
        }
        // Next grain: one output period later; the input moves on one full period (drift grows by
        // P - P/ratio, the pitch change).
        synthPos += P / ratio;
        drift += P - P / ratio;   // 0 when unvoiced (ratio 1)
    }
}

void PitchCorrector::process (double* const* ch, int nch, int n) noexcept
{
    nch = std::min (nch, channels);
    const bool neutral = isNeutral (params);
    const double maxP = std::ceil (sr / kMinHz);

    for (int i = 0; i < n; ++i)
    {
        double m = 0.0;
        for (int c = 0; c < channels; ++c)
        {
            const double v = ch[std::min (c, nch - 1)][i];
            in[static_cast<size_t> (c)][static_cast<size_t> (now & mask)] = v;
            m += v;
        }
        m /= channels;
        mono[static_cast<size_t> (now & mask)] = m;
        levelMs += (m * m - levelMs) * levelCoeff;
        const double filtered = aa2.process (aa1.process (m));
        if (++decCount >= dec)
        {
            decCount = 0;
            dbuf[static_cast<size_t> (dnow & dmask)] = filtered;
            ++dnow;
        }
        ++now;
        if (++hopCount >= hop) { hopCount = 0; analyse(); }

        const int64_t t = now - 1 - latency;   // the sample leaving now
        if (neutral)
        {
            if (! wasNeutral) wasNeutral = true;
            for (int c = 0; c < nch; ++c)
                ch[c][i] = t >= 0 ? in[static_cast<size_t> (c)][static_cast<size_t> (t & mask)] : 0.0;
            continue;
        }
        if (wasNeutral)
        {
            // Starting up: grains begin just ahead of what's leaving; until they cover a sample,
            // the delayed input plays (identical, because the correction starts at 0).
            wasNeutral = false;
            corr = 0.0;
            // Grains reach P before their centre: start one max period in, so none lands on a slot
            // that has already left (it would come back one ring-length later).
            synthPos = anaPos = static_cast<double> (t + 1) + maxP;
            drift = 0.0;
            for (auto& v : acc) std::fill (v.begin(), v.end(), 0.0);
            std::fill (wsum.begin(), wsum.end(), 0.0);
        }
        synthesiseUpTo (now - 1 - static_cast<int64_t> (std::ceil (1.5 * maxP)));

        const auto ti = static_cast<size_t> (t & mask);
        const double ws = wsum[ti];
        for (int c = 0; c < nch; ++c)
        {
            double y = 0.0;
            if (t >= 0)
                y = ws > 0.05 ? acc[static_cast<size_t> (c)][ti] / ws : in[static_cast<size_t> (c)][ti];
            ch[c][i] = y;
        }
        for (auto& v : acc) v[ti] = 0.0;
        wsum[ti] = 0.0;
    }
}

// ------------------------------------------------------------------------------------------------
KeyGuess detectKey (const std::vector<double>& midiNotes)
{
    KeyGuess g;
    if (midiNotes.size() < 20) return g;
    std::array<double, 12> hist {};
    for (double m : midiNotes)
        hist[static_cast<size_t> (((static_cast<int> (std::lround (m)) % 12) + 12) % 12)] += 1.0;
    static constexpr std::array<double, 12> major { 6.35, 2.23, 3.48, 2.33, 4.38, 4.09, 2.52, 5.19, 2.39, 3.66, 2.29, 2.88 };
    static constexpr std::array<double, 12> minor { 6.33, 2.68, 3.52, 5.38, 2.60, 3.53, 2.54, 4.75, 3.98, 2.69, 3.34, 3.17 };
    auto corrWith = [&] (const std::array<double, 12>& prof, int key)
    {
        double mx = 0, my = 0;
        for (int i = 0; i < 12; ++i) { mx += hist[static_cast<size_t> (i)]; my += prof[static_cast<size_t> (i)]; }
        mx /= 12; my /= 12;
        double sxy = 0, sxx = 0, syy = 0;
        for (int i = 0; i < 12; ++i)
        {
            const double x = hist[static_cast<size_t> ((i + key) % 12)] - mx, y = prof[static_cast<size_t> (i)] - my;
            sxy += x * y; sxx += x * x; syy += y * y;
        }
        return sxy / std::sqrt (sxx * syy + 1e-30);
    };
    double best = -2, second = -2;
    for (int k = 0; k < 12; ++k)
        for (int mode = 0; mode < 2; ++mode)
        {
            const double r = corrWith (mode ? minor : major, k);
            if (r > best) { second = best; best = r; g.key = k; g.minor = mode == 1; }
            else if (r > second) second = r;
        }
    g.confidence = std::clamp (best * 0.7 + (best - second) * 3.0, 0.0, 1.0);
    const int scale = g.minor ? 2 : 1;
    double off = 0.0;
    for (double m : midiNotes)
        off += std::abs (m - PitchCorrector::targetNote (m, g.key, scale, -1));
    g.offCents = 100.0 * off / static_cast<double> (midiNotes.size());
    return g;
}

} // namespace vox
