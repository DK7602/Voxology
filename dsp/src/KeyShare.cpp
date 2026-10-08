#include "vox/KeyShare.h"

#include <chrono>
#include <cstdio>

#if defined (_WIN32)
 #ifndef WIN32_LEAN_AND_MEAN
  #define WIN32_LEAN_AND_MEAN
 #endif
 #ifndef NOMINMAX
  #define NOMINMAX
 #endif
 #include <windows.h>
#else
 #include <cstdlib>
#endif

namespace vox::keyshare {

namespace {
constexpr const char* kName = "VOXOLOGY_BEAT_KEY";

// The process's own environment block: with a static C runtime every plug-in has its own copy of
// getenv / putenv's table, so Windows goes through the Win32 calls (one per process).
void setVar (const std::string& value)
{
#if defined (_WIN32)
    SetEnvironmentVariableA (kName, value.c_str());
#else
    setenv (kName, value.c_str(), 1);
#endif
}

bool getVar (std::string& value)
{
#if defined (_WIN32)
    char buf[128] {};
    const DWORD n = GetEnvironmentVariableA (kName, buf, sizeof buf);
    if (n == 0 || n >= sizeof buf) return false;
    value.assign (buf, n);
    return true;
#else
    const char* v = std::getenv (kName);
    if (v == nullptr) return false;
    value = v;
    return true;
#endif
}
} // namespace

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
    setVar (encode ({ key, scale, confidence, ms < 0 ? nowMs() : ms }));
}

bool read (BeatKeyShare& k)
{
    std::string text;
    BeatKeyShare got;
    if (! getVar (text) || ! decode (text, got)) return false;
    const auto age = nowMs() - got.ms;
    if (age < 0 || age > kMaxAgeMs) return false;
    k = got;
    return true;
}

} // namespace vox::keyshare
