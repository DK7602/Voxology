#include "vox/KeyShare.h"

#include "suite/SuiteLink.h"

#include <algorithm>
#include <chrono>
#include <cstdio>

namespace vox::keyshare {

std::int64_t nowMs() noexcept
{
    return std::chrono::duration_cast<std::chrono::milliseconds> (std::chrono::steady_clock::now().time_since_epoch()).count();
}

std::string encode (const BeatKeyShare& k)
{
    char buf[96];
    std::snprintf (buf, sizeof buf, "v1 %d %d %.3f %lld", k.key, k.scale, k.confidence, static_cast<long long> (k.ms));
    return buf;
}

bool decode (const std::string& text, BeatKeyShare& k)
{
    BeatKeyShare out;
    long long ms = 0;
    if (std::sscanf (text.c_str(), "v1 %d %d %lf %lld", &out.key, &out.scale, &out.confidence, &ms) != 4) return false;
    if (out.key < 0 || out.key > 11 || out.scale < 1 || out.scale > 9) return false;
    out.ms = ms;
    k = out;
    return true;
}

void publish (int key, int scale, double confidence, std::int64_t ms)
{
    const auto when = ms < 0 ? nowMs() : ms;
    suite::Link::process().publishBeatKey (key, scale, static_cast<float> (confidence), static_cast<std::uint64_t> (std::max<std::int64_t> (1, when)));
}

bool read (BeatKeyShare& k)
{
    BeatKeyShare got;
    float sure = 0.0f;
    std::uint64_t when = 0;
    if (! suite::Link::process().readBeatKey (got.key, got.scale, sure, when)) return false;
    if (got.key < 0 || got.key > 11 || got.scale < 1 || got.scale > 9) return false;
    got.confidence = sure;
    got.ms = static_cast<std::int64_t> (when);
    const auto age = nowMs() - got.ms;
    if (age < 0 || age > kMaxAgeMs) return false;
    k = got;
    return true;
}

} // namespace vox::keyshare
