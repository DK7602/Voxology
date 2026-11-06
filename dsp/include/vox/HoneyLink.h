#pragma once

// Honey Tune and Voxology on the same track: one tuner at a time.
//
// Honey Tune (an ARA plug-in on the clip) and Voxology (an insert on the track) are separate plug-in files, so they meet
// in a named block of shared memory, one per host process ("VoxologyHoneyLink.v<layout>.<pid>"). This is Voxology's own
// channel: the suite link (suite/SuiteLink.h) is shared byte for byte with Mixology and Masterology and stays as it is.
//   - Honey Tune publishes the tracks it tunes: the name of each track (ARA region sequence) that has a Honey Tune clip.
//   - Each Voxology publishes its track's name, what its 01 Pitch does there (tuning, resting because Honey Tune tunes
//     the track, or tuning anyway because you asked it to) and the key Pitch uses (and where that key comes from).
// Tracks are matched by name (suite::nameHash of the UTF-8 name): an unnamed track never matches, and a name two tracks
// share (two Voxologys, or two tracks in Honey Tune) is reported as ambiguous, never acted on.
// Each publisher refreshes its entries about once a second; an entry silent for kAliveMs is gone (a plug-in removed, a
// project closed or no longer active). Lock-free (a sequence counter per slot, one writer per slot), no allocation.

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace vox::honeylink {

constexpr std::uint32_t kLayout = 1;
constexpr int kSlots = 64;
constexpr std::uint64_t kAliveMs = 3000;

/** What Voxology's 01 Pitch does on its track. */
enum class Pitch : std::uint32_t
{
    off = 0,           // switched off (or Amount 0): not tuning
    tuning = 1,        // tuning (no Honey Tune on this track)
    resting = 2,       // Honey Tune tunes this track: Pitch rests
    tuningAnyway = 3,  // Honey Tune tunes this track, and you told Pitch to tune there anyway
};

/** Where the key Voxology's Pitch uses comes from. */
enum class KeyFrom : std::uint32_t
{
    none = 0,    // nothing heard yet: Key / Scale as set (not shared)
    beat = 1,    // the beat (its side-chain, or a Voxology on the beat)
    voice = 2,   // learned from the singing
    manual = 3,  // Key set to Manual: you picked it
};

/** One Voxology, as published. */
struct VoxInfo
{
    std::uint32_t nameHash = 0;   // its track (0 = the host gave no name)
    Pitch pitch = Pitch::off;
    int key = 0;                  // 0 = C .. 11 = B, what Pitch uses now
    int scale = 0;                // vox::kScaleNames
    KeyFrom keyFrom = KeyFrom::none;
};

/** One track with a Honey Tune clip. sharedName: Honey Tune sees two tracks with this name. */
struct HoneyTrack
{
    std::uint32_t nameHash = 0;
    bool sharedName = false;
};

class Link
{
public:
    explicit Link (void* memory);   // over zero-filled (or in-use) memory of bytes()
    static std::size_t bytes();
    /** This library's handle on the host process's block (opened on first use). */
    static Link& process();

    // ---- Honey Tune: one owner per document --------------------------------------------------------
    /** The tracks this document tunes now (replaces what it published before). Message thread, about once a second. */
    void publishHoney (std::uint64_t owner, const std::vector<HoneyTrack>& tracks, std::uint64_t nowMs);
    void leaveHoney (std::uint64_t owner);

    // ---- Voxology: one owner per instance ----------------------------------------------------------
    void publishVox (std::uint64_t owner, const VoxInfo& info, std::uint64_t nowMs);
    void leaveVox (std::uint64_t owner);

    struct Match
    {
        bool honey = false;       // a Honey Tune tunes a track with this name
        bool ambiguous = false;   // ... but two tracks have that name (in Honey Tune, or another Voxology's track)
        bool act() const { return honey && ! ambiguous; }
    };
    /** Voxology `self` on the track named nameHash: does Honey Tune tune it? */
    Match honeyOn (std::uint32_t nameHash, std::uint64_t self, std::uint64_t nowMs) const;
    /** Honey Tune: the Voxology on the track named nameHash (false: none, or more than one track has that name). */
    bool voxologyOn (std::uint32_t nameHash, VoxInfo& out, std::uint64_t nowMs) const;

    struct Slot
    {
        std::atomic<std::uint32_t> seq;         // odd while its owner writes it
        std::atomic<std::uint64_t> owner;       // 0 = free
        std::atomic<std::uint64_t> heartbeat;   // ms; 0 = free
        std::atomic<std::uint32_t> nameHash, flags, pitch, keyFrom;
        std::atomic<std::int32_t> key, scale;
    };
    struct Block
    {
        std::atomic<std::uint32_t> layout;
        Slot honey[kSlots];
        Slot vox[kSlots];
    };

private:
    Block& block;
};

} // namespace vox::honeylink
