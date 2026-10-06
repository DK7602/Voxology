#include "vox/Unmask.h"

#include <algorithm>
#include <chrono>

namespace vox {

using design::onePole;

// ================================================================================================
// BandAnalyser
void BandAnalyser::prepare (double sampleRate)
{
    sr = sampleRate;
    for (size_t b = 0; b < bp.size(); ++b)
        design::apply (bp[b], design::bandPass (std::min (kUnmaskHz[b], 0.4 * sr), kUnmaskQ, sr));
    coeff = onePole (0.010, sr);   // two stages: ~20 ms
    reset();
}

void BandAnalyser::reset() noexcept
{
    for (auto& f : bp) f.reset();
    ms.fill (0.0);
    ms2.fill (0.0);
}

void BandAnalyser::process (double x) noexcept
{
    for (size_t b = 0; b < bp.size(); ++b)
    {
        const double y = bp[b].process (x);
        ms[b] += (y * y - ms[b]) * coeff;
        ms2[b] += (ms[b] - ms2[b]) * coeff;
    }
}

void BandAnalyser::process (const double* mono, int n) noexcept
{
    for (int i = 0; i < n; ++i) process (mono[i]);
}

std::array<float, kUnmaskBands> BandAnalyser::levelsDb() const noexcept
{
    std::array<float, kUnmaskBands> out {};
    for (size_t b = 0; b < out.size(); ++b)
        out[b] = static_cast<float> (ms2[b] > 1.0e-14 ? 10.0 * std::log10 (ms2[b]) : kUnmaskSilentDb);
    return out;
}

// ================================================================================================
// UnmaskLink
UnmaskLink::UnmaskLink()
{
    for (auto& s : slots) s.frames = std::vector<Frame> (kFrames);
}

UnmaskLink& UnmaskLink::instance()
{
    static UnmaskLink link;
    return link;
}

int64_t UnmaskLink::nowMs() noexcept
{
    return std::chrono::duration_cast<std::chrono::milliseconds> (std::chrono::steady_clock::now().time_since_epoch()).count();
}

int UnmaskLink::claim()
{
    for (int i = 0; i < kSlots; ++i)
    {
        bool expected = false;
        auto& s = slots[static_cast<size_t> (i)];
        if (s.used.compare_exchange_strong (expected, true))
        {
            s.write.store (0);
            s.lastMs.store (0);
            const std::lock_guard<std::mutex> g (nameLock);
            names[static_cast<size_t> (i)] = "Vocal " + std::to_string (i + 1);
            return i;
        }
    }
    return -1;
}

void UnmaskLink::release (int slot)
{
    if (slot < 0 || slot >= kSlots) return;
    auto& s = slots[static_cast<size_t> (slot)];
    s.lastMs.store (0);
    s.used.store (false);
}

void UnmaskLink::setName (int slot, const std::string& name)
{
    if (slot < 0 || slot >= kSlots) return;
    const std::lock_guard<std::mutex> g (nameLock);
    names[static_cast<size_t> (slot)] = name;
}

std::vector<UnmaskLink::Source> UnmaskLink::sources() const
{
    std::vector<Source> out;
    const std::lock_guard<std::mutex> g (nameLock);
    for (int i = 0; i < kSlots; ++i)
        if (slots[static_cast<size_t> (i)].used.load())
            out.push_back ({ i, names[static_cast<size_t> (i)], isLive (i) });
    return out;
}

bool UnmaskLink::isLive (int slot) const noexcept
{
    if (slot < 0 || slot >= kSlots) return false;
    const auto& s = slots[static_cast<size_t> (slot)];
    const auto last = s.lastMs.load();
    return s.used.load() && last > 0 && nowMs() - last < 1000;
}

void UnmaskLink::publish (int slot, int64_t pos, const std::array<float, kUnmaskBands>& db) noexcept
{
    if (slot < 0 || slot >= kSlots) return;
    auto& s = slots[static_cast<size_t> (slot)];
    const uint32_t w = s.write.load (std::memory_order_relaxed);
    auto& f = s.frames[w % kFrames];
    const uint32_t seq = f.seq.load (std::memory_order_relaxed);
    f.seq.store (seq + 1, std::memory_order_release);   // odd: being written
    std::atomic_thread_fence (std::memory_order_release);
    f.pos.store (pos, std::memory_order_relaxed);
    for (size_t b = 0; b < db.size(); ++b) f.db[b].store (db[b], std::memory_order_relaxed);
    f.seq.store (seq + 2, std::memory_order_release);   // even: done
    s.write.store (w + 1, std::memory_order_release);
    s.lastMs.store (nowMs(), std::memory_order_relaxed);
}

bool UnmaskLink::read (int slot, int64_t pos, int64_t maxAgeSamples, std::array<float, kUnmaskBands>& db) const noexcept
{
    if (slot < 0 || slot >= kSlots || ! isLive (slot)) return false;
    const auto& s = slots[static_cast<size_t> (slot)];
    const uint32_t w = s.write.load (std::memory_order_acquire);
    if (w == 0) return false;

    auto tryRead = [&] (uint32_t index, int64_t& framePos) -> bool
    {
        const auto& f = s.frames[index % kFrames];
        for (int attempt = 0; attempt < 3; ++attempt)
        {
            const uint32_t a = f.seq.load (std::memory_order_acquire);
            if (a & 1u) continue;
            framePos = f.pos.load (std::memory_order_relaxed);
            for (size_t b = 0; b < db.size(); ++b) db[b] = f.db[b].load (std::memory_order_relaxed);
            std::atomic_thread_fence (std::memory_order_acquire);
            if (f.seq.load (std::memory_order_relaxed) == a) return true;
        }
        return false;
    };

    int64_t fp = kNoPosition;
    if (pos == kNoPosition)
        return tryRead (w - 1, fp);

    // Newest first: the first frame at or before pos (positions jump when you move the playhead).
    const uint32_t oldest = w > static_cast<uint32_t> (kFrames - 8) ? w - static_cast<uint32_t> (kFrames - 8) : 0u;
    for (uint32_t i = w; i > oldest; --i)
    {
        if (! tryRead (i - 1, fp)) continue;
        if (fp == kNoPosition) break;
        if (fp <= pos)
            return pos - fp <= maxAgeSamples;
    }
    // Not there (the beat is ahead of the vocal, or the playhead jumped): use the newest.
    return tryRead (w - 1, fp);
}

void UnmaskLink::resetForTests()
{
    for (int i = 0; i < kSlots; ++i) release (i);
}

// ================================================================================================
// Unmask
void Unmask::prepare (double sampleRate, int numChannels)
{
    sr = sampleRate;
    channels = std::clamp (numChannels, 1, kMaxChannels);
    beat.prepare (sr);
    atk = onePole (0.015, sr);
    rel = onePole (0.100, sr);
    peakFall = 6.0 / sr;   // dB per sample: 6 dB/s
    reset();
}

void Unmask::reset() noexcept
{
    beat.reset();
    vocalPeak.fill (kUnmaskSilentDb);
    dip.fill (0.0);
    designed.fill (0.0);
    for (auto& band : bell) for (auto& f : band) f = Biquad {};
    countdown = 0;
    running = false;
}

void Unmask::design (size_t b) noexcept
{
    designed[b] = dip[b];
    const auto c = design::bell (std::min (kUnmaskHz[b], 0.4 * sr), -dip[b], kUnmaskDipQ, sr);
    for (auto& f : bell[b]) design::apply (f, c);
}

std::array<double, kUnmaskBands> Unmask::takeDipDb() noexcept
{
    std::array<double, kUnmaskBands> v {};
    for (size_t b = 0; b < v.size(); ++b) { v[b] = -maxDip[b]; maxDip[b] = 0.0; }
    return v;
}

void Unmask::process (double* const* ch, int nch, int n, const float* vocalDb) noexcept
{
    nch = std::min (nch, channels);
    bool anyDip = false;
    for (double d : dip) anyDip = anyDip || d > 0.0;
    if (isNeutral (params) && ! anyDip)
    {
        if (running) reset();
        return;
    }
    running = true;

    // Targets for this stretch: how present the vocal is in each band (vs its own recent peak there).
    std::array<double, kUnmaskBands> target {};
    const double amount = std::clamp (params.amount, 0.0, 100.0) / 100.0;
    const auto beatDb = beat.levelsDb();
    for (size_t b = 0; b < target.size(); ++b)
    {
        double v = kUnmaskSilentDb;
        if (vocalDb != nullptr) v = vocalDb[b];
        vocalPeak[b] = std::max (v, vocalPeak[b] - peakFall * n);
        const double presence = v <= -90.0 ? 0.0 : std::clamp ((v - (vocalPeak[b] - kPresenceWindowDb)) / kPresenceRampDb, 0.0, 1.0);
        const double beatThere = std::clamp ((beatDb[b] - kBeatFloorDb) / 15.0, 0.0, 1.0);
        target[b] = amount * kMaxDipDb * kUnmaskWeight[b] * presence * beatThere;
    }

    for (int i = 0; i < n; ++i)
    {
        // Listen to the beat's middle (what the dips act on).
        double mid = 0.0;
        for (int c = 0; c < nch; ++c) mid += ch[c][i];
        beat.process (mid / nch);

        for (size_t b = 0; b < dip.size(); ++b)
        {
            dip[b] += (target[b] - dip[b]) * (target[b] > dip[b] ? atk : rel);
            if (dip[b] < 1.0e-4 && target[b] == 0.0) dip[b] = 0.0;
            maxDip[b] = std::max (maxDip[b], dip[b]);
        }
        if (--countdown <= 0)
        {
            countdown = 16;
            for (size_t b = 0; b < dip.size(); ++b)
                if (std::abs (dip[b] - designed[b]) > 0.01 || (dip[b] == 0.0 && designed[b] != 0.0)) design (b);
        }

        if (nch == 2 && params.centreOnly)
        {
            // Mid / side: only the middle is dipped, the sides (the beat's width) stay as they are.
            double m = 0.5 * (ch[0][i] + ch[1][i]);
            const double s = 0.5 * (ch[0][i] - ch[1][i]);
            for (size_t b = 0; b < bell.size(); ++b) m = bell[b][0].process (m);
            ch[0][i] = m + s;
            ch[1][i] = m - s;
        }
        else
            for (int c = 0; c < nch; ++c)
            {
                double x = ch[c][i];
                for (size_t b = 0; b < bell.size(); ++b) x = bell[b][static_cast<size_t> (c)].process (x);
                ch[c][i] = x;
            }
    }
}

} // namespace vox
