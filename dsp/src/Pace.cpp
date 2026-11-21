#include "vox/Pace.h"

#include <algorithm>
#include <chrono>
#include <thread>

namespace vox::pace {

namespace {
using Clock = std::chrono::steady_clock;
struct State
{
    bool on = false;
    double workMs = 0.0, restMs = 0.0;
    Clock::time_point since {}, last {};
    double longest = 0.0, rested = 0.0;
};
thread_local State state;
double ms (Clock::duration d) { return std::chrono::duration<double, std::milli> (d).count(); }
}

Scope::Scope (double workMs, double restMs) noexcept
    : wasOn (state.on), wasWork (state.workMs), wasRest (state.restMs)
{
    state.on = workMs > 0.0 && restMs > 0.0;
    state.workMs = workMs;
    state.restMs = restMs;
    state.since = state.last = Clock::now();
    state.longest = state.rested = 0.0;
}

Scope::~Scope()
{
    state.on = wasOn;
    state.workMs = wasWork;
    state.restMs = wasRest;
}

Gentler::Gentler (double workMs, double restMs) noexcept
    : active (state.on), wasWork (state.workMs), wasRest (state.restMs)
{
    if (! active) return;
    state.workMs = std::min (state.workMs, workMs);
    state.restMs = std::max (state.restMs, restMs);
}

Gentler::~Gentler()
{
    if (! active) return;
    state.workMs = wasWork;
    state.restMs = wasRest;
}

void tick() noexcept
{
    if (! state.on)
        return;
    const auto now = Clock::now();
    state.longest = std::max (state.longest, ms (now - state.last));
    state.last = now;
    if (ms (now - state.since) < state.workMs)
        return;
    std::this_thread::sleep_for (std::chrono::duration<double, std::milli> (state.restMs));
    const auto after = Clock::now();
    state.rested += ms (after - now);
    state.since = state.last = after;
}

double longestStretchMs() noexcept { return state.longest; }
double restedMs() noexcept { return state.rested; }

} // namespace vox::pace
