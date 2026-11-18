// The audio thread never allocates memory: an allocation there can wait on the memory manager's lock while another
// thread (Auto-Edit, the Coach, Honey Tune's analysis) allocates a lot, and the DAW's audio cuts out. Counted with a
// replaced global operator new, only while the chain processes.
#include "Signals.h"
#include "vox/AutoEdit.h"
#include "vox/Pace.h"
#include "vox/VocalChain.h"

#include <atomic>
#include <memory>
#include <cstdlib>
#include <new>

#include <catch2/catch_test_macros.hpp>

namespace {
std::atomic<bool> counting { false };
std::atomic<long> allocations { 0 };
}

void* operator new (std::size_t n)
{
    if (counting.load (std::memory_order_relaxed))
        allocations.fetch_add (1, std::memory_order_relaxed);
    if (void* p = std::malloc (n > 0 ? n : 1))
        return p;
    throw std::bad_alloc();
}
void* operator new[] (std::size_t n) { return ::operator new (n); }
void operator delete (void* p) noexcept { std::free (p); }
void operator delete[] (void* p) noexcept { std::free (p); }
void operator delete (void* p, std::size_t) noexcept { std::free (p); }
void operator delete[] (void* p, std::size_t) noexcept { std::free (p); }
// The no-throw forms too (std::stable_sort's buffer uses them): ours all come from malloc, so every delete must free
// what one of these gave (else AddressSanitizer sees its own new freed with free()).
void* operator new (std::size_t n, const std::nothrow_t&) noexcept
{
    if (counting.load (std::memory_order_relaxed))
        allocations.fetch_add (1, std::memory_order_relaxed);
    return std::malloc (n > 0 ? n : 1);
}
void* operator new[] (std::size_t n, const std::nothrow_t& t) noexcept { return ::operator new (n, t); }
void operator delete (void* p, const std::nothrow_t&) noexcept { std::free (p); }
void operator delete[] (void* p, const std::nothrow_t&) noexcept { std::free (p); }

using namespace vox;

namespace {
constexpr double kSr = 48000.0;

/** Plays `x` through `chain` in host-sized blocks, handing over `params` every block as the plug-in does; returns the
    allocations made while processing. */
long play (VocalChain& chain, const std::vector<float>& x, const ChainParams& params, int block, ChainParams* switchTo = nullptr)
{
    std::vector<double> l (static_cast<size_t> (block)), r (static_cast<size_t> (block));
    double* ch[2] = { l.data(), r.data() };
    allocations.store (0);
    const int n = static_cast<int> (x.size());
    for (int start = 0; start + block <= n; start += block)
    {
        for (int i = 0; i < block; ++i)
            l[static_cast<size_t> (i)] = r[static_cast<size_t> (i)] = x[static_cast<size_t> (start + i)];
        const bool second = switchTo != nullptr && start > n / 2;
        counting.store (true);
        chain.setParams (second ? *switchTo : params);
        chain.process (ch, 2, block);
        counting.store (false);
    }
    return allocations.load();
}
}

TEST_CASE ("Real time: the vocal chain never allocates while it plays, whatever Auto-Edit set", "[realtime]")
{
    {   // the counter itself counts
        allocations.store (0);
        counting.store (true);
        std::vector<double> v (64);
        counting.store (false);
        CHECK (allocations.load() == 1);
    }
    const auto take = testsig::vocal (kSr, 8.0, -58.0, -6.0);
    for (int style : { 0 })   // (one style: each Auto-Edit takes seconds, more under the sanitizers)
    {
        AutoEditSettings s { style, 2, 140.0 };
        const auto r = autoEdit ({ take }, kSr, s);
        REQUIRE (r.ok);
        INFO ("style " << style);
        VocalChain chain;
        chain.setParams (r.params);
        chain.prepare (kSr, 2);
        CHECK (play (chain, take, r.params, 512) == 0);

        // Everything on at once, then a jump to another style's whole plan mid-song (APPLY TO TRACK, a chain load).
        auto all = r.params;
        all.pitch.enabled = all.cleanup.enabled = all.eq.enabled = all.dynEq.enabled = all.deEsser.enabled = true;
        all.rider.enabled = all.comp.enabled = all.saturation.enabled = all.doubler.enabled = all.dimension.enabled = true;
        all.delay.enabled = all.reverb.enabled = true;
        AutoEditSettings other { (style + 3) % kStyles, 0, 140.0 };
        auto next = autoEdit ({ take }, kSr, other).params;
        VocalChain busy;
        busy.setParams (all);
        busy.prepare (kSr, 2);
        CHECK (play (busy, take, all, 256, &next) == 0);
    }
}

TEST_CASE ("Real time: Auto-Edit resting between stretches of work decides exactly the same", "[realtime]")
{
    // Voxology's analysis rests on purpose while the song plays (vox/Pace.h): only the timing may change.
    const auto take = testsig::vocal (kSr, 8.0, -58.0, -6.0);
    const AutoEditSettings s { 0, 1, 140.0 };
    const auto plain = autoEdit ({ take }, kSr, s);
    double rested = 0.0, longest = 0.0;
    std::unique_ptr<AutoEditResult> paced;
    {
        const pace::Scope gentle (20.0, 8.0);
        paced = std::make_unique<AutoEditResult> (autoEdit ({ take }, kSr, s));
        rested = pace::restedMs();
        longest = pace::longestStretchMs();
    }
    REQUIRE (plain.ok);
    REQUIRE (paced->ok);
    CHECK (paced->summary == plain.summary);
    REQUIRE (paced->reasons.size() == plain.reasons.size());
    for (size_t i = 0; i < plain.reasons.size(); ++i)
    {
        INFO (plain.reasons[i].module << " " << plain.reasons[i].control);
        CHECK (paced->reasons[i].value == plain.reasons[i].value);
    }
    CHECK (paced->params.comp.thrDb == plain.params.comp.thrDb);
    CHECK (paced->params.deEsser.amount == plain.params.deEsser.amount);
    CHECK (paced->params.eq.gainDb == plain.params.eq.gainDb);
    INFO ("rested " << rested << " ms; longest stretch between rests " << longest << " ms");
    CHECK (rested > 0.0);
}
