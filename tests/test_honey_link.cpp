// Honey Tune and Voxology on the same track (vox::honeylink, Voxology's own channel): which Voxology rests its Pitch.
#include "suite/SuiteLink.h"
#include "vox/HoneyLink.h"

#include <catch2/catch_test_macros.hpp>

#include <cstring>
#include <memory>
#include <new>

using namespace vox::honeylink;

namespace {
/** A fresh block in this process's own memory (the plug-ins use the host process's shared one). */
struct Fresh
{
    Fresh() : mem (::operator new (Link::bytes(), std::align_val_t { 64 })) { std::memset (mem, 0, Link::bytes()); link = std::make_unique<Link> (mem); }
    ~Fresh() { link.reset(); ::operator delete (mem, std::align_val_t { 64 }); }
    void* mem;
    std::unique_ptr<Link> link;
};
}

TEST_CASE ("Honey Tune link: a Voxology on the track Honey Tune tunes rests its Pitch; unnamed or shared names never", "[honey][link]")
{
    Fresh f;
    auto& link = *f.link;
    const auto lead = suite::nameHash ("Chorus Vox"), verse = suite::nameHash ("Verse Vox");
    const std::uint64_t doc = 11, voxA = 21, voxB = 22;
    std::uint64_t now = 100000;

    // No Honey Tune yet.
    CHECK_FALSE (link.honeyOn (lead, voxA, now).honey);
    // Honey Tune tunes "Chorus Vox": the Voxology there should rest, the one on "Verse Vox" not.
    link.publishHoney (doc, { { lead, false } }, now);
    link.publishVox (voxA, { lead, Pitch::resting, 5, 2, KeyFrom::beat }, now);
    link.publishVox (voxB, { verse, Pitch::tuning, 0, 1, KeyFrom::voice }, now);
    CHECK (link.honeyOn (lead, voxA, now).act());
    CHECK_FALSE (link.honeyOn (verse, voxB, now).honey);
    CHECK_FALSE (link.honeyOn (0, voxA, now).honey);   // an unnamed track never matches

    // Honey Tune reads the Voxology on its clip's track: its Pitch and the key it uses.
    VoxInfo v;
    REQUIRE (link.voxologyOn (lead, v, now));
    CHECK (v.pitch == Pitch::resting);
    CHECK (v.key == 5);
    CHECK (v.scale == 2);
    CHECK (v.keyFrom == KeyFrom::beat);
    CHECK_FALSE (link.voxologyOn (suite::nameHash ("Bass"), v, now));

    // Two Voxologys on tracks with the same name: can't tell which one Honey Tune is on, so neither acts.
    link.publishVox (voxB, { lead, Pitch::tuning, 0, 1, KeyFrom::none }, now);
    CHECK (link.honeyOn (lead, voxA, now).honey);
    CHECK (link.honeyOn (lead, voxA, now).ambiguous);
    CHECK_FALSE (link.honeyOn (lead, voxB, now).act());
    CHECK_FALSE (link.voxologyOn (lead, v, now));
    link.leaveVox (voxB);
    CHECK (link.honeyOn (lead, voxA, now).act());

    // Two tracks in Honey Tune with that name: the same.
    link.publishHoney (doc, { { lead, true } }, now);
    CHECK (link.honeyOn (lead, voxA, now).ambiguous);
    link.publishHoney (doc, { { lead, false }, { verse, false } }, now);
    CHECK (link.honeyOn (lead, voxA, now).act());

    // Honey Tune's clip leaves that track (or the project stops): Pitch tunes again.
    link.publishHoney (doc, { { verse, false } }, now);
    CHECK_FALSE (link.honeyOn (lead, voxA, now).honey);
    link.publishHoney (doc, { { lead, false } }, now);
    link.leaveHoney (doc);
    CHECK_FALSE (link.honeyOn (lead, voxA, now).honey);

    // A Honey Tune that went silent (the host closed it without a word) counts for kAliveMs only.
    link.publishHoney (doc, { { lead, false } }, now);
    CHECK (link.honeyOn (lead, voxA, now + kAliveMs).honey);
    CHECK_FALSE (link.honeyOn (lead, voxA, now + kAliveMs + 1).honey);
    // ... and its slots are taken over by the next one.
    now += 10000;
    for (std::uint64_t d = 100; d < 100 + kSlots; ++d) link.publishHoney (d, { { static_cast<std::uint32_t> (d), false } }, now);
    CHECK (link.honeyOn (static_cast<std::uint32_t> (100 + kSlots - 1), voxA, now).honey);
}

TEST_CASE ("Honey Tune link: two plug-in files see one block per process", "[honey][link]")
{
    // (Both plug-ins open it by the same name; within the tests this library's handle is used twice.)
    auto& a = Link::process();
    auto& b = Link::process();
    const auto name = suite::nameHash ("Test track for the link");
    a.publishHoney (777, { { name, false } }, suite::nowMs());
    CHECK (b.honeyOn (name, 1, suite::nowMs()).honey);
    a.leaveHoney (777);
    CHECK_FALSE (b.honeyOn (name, 1, suite::nowMs()).honey);
}
