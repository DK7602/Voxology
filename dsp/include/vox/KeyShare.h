#pragma once

#include <cstdint>
#include <string>

/** The beat's key, shared from Voxology (on the beat) to Honey Tune (on a vocal clip) inside one running host.

    They are separate plug-in files: they meet in the suite link's shared memory (suite/SuiteLink.h, the beat key),
    only inside that process (another program or a closed DAW never sees it). Voxology writes it about once a second
    while a beat key is known; Honey Tune ignores it once it's a few seconds old (that Voxology was removed, or left
    BEAT mode). Message thread only. (It used to go through the process's environment, which isn't thread-safe on
    macOS / Linux while the host reads its own variables.) */
namespace vox::keyshare {

struct BeatKeyShare
{
    int key = 0;              // 0 = C ... 11 = B (the beat's home note)
    int scale = 1;            // vox::kScaleNames (the beat's own mode)
    double confidence = 0.0;  // 0..1
    std::int64_t ms = 0;      // when it was written (steady clock, ms)
};

constexpr std::int64_t kMaxAgeMs = 5000;

std::int64_t nowMs() noexcept;
std::string encode (const BeatKeyShare& k);
bool decode (const std::string& text, BeatKeyShare& k);

/** Voxology: the beat key as of now (ms: when, for tests; < 0 = now). */
void publish (int key, int scale, double confidence, std::int64_t ms = -1);
/** Honey Tune: the beat key if a Voxology in this host wrote one in the last kMaxAgeMs. */
bool read (BeatKeyShare& k);

} // namespace vox::keyshare
