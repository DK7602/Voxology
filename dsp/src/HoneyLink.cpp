// Honey Tune and Voxology on the same track: see HoneyLink.h. (Voxology's own channel, not the shared suite link.)

#include "vox/HoneyLink.h"

#include <algorithm>
#include <cstring>
#include <new>
#include <string>

#if defined(_WIN32)
 #ifndef WIN32_LEAN_AND_MEAN
  #define WIN32_LEAN_AND_MEAN
 #endif
 #ifndef NOMINMAX
  #define NOMINMAX
 #endif
 #include <windows.h>
#else
 #include <fcntl.h>
 #include <sys/mman.h>
 #include <sys/stat.h>
 #include <unistd.h>
#endif

namespace vox::honeylink {

namespace {
constexpr auto kRelaxed = std::memory_order_relaxed;
constexpr auto kAcquire = std::memory_order_acquire;
constexpr auto kRelease = std::memory_order_release;
constexpr std::uint32_t kSharedName = 1u;

static_assert (std::atomic<std::uint64_t>::is_always_lock_free);

bool alive (std::uint64_t beat, std::uint64_t now) { return beat != 0 && now - std::min (now, beat) <= kAliveMs; }

void beginWrite (Link::Slot& s)
{
    s.seq.fetch_add (1, std::memory_order_acq_rel);
    std::atomic_thread_fence (kRelease);
}
void endWrite (Link::Slot& s) { s.seq.fetch_add (1, kRelease); }

/** A consistent copy of a slot (false: it was being written every time we looked). */
struct Copy { std::uint64_t owner = 0, heartbeat = 0; std::uint32_t nameHash = 0, flags = 0, pitch = 0, keyFrom = 0; std::int32_t key = 0, scale = 0; };
bool read (const Link::Slot& s, Copy& c)
{
    for (int attempt = 0; attempt < 64; ++attempt)
    {
        const auto before = s.seq.load (kAcquire);
        if (before & 1u) continue;
        c.owner = s.owner.load (kRelaxed);
        c.heartbeat = s.heartbeat.load (kRelaxed);
        c.nameHash = s.nameHash.load (kRelaxed);
        c.flags = s.flags.load (kRelaxed);
        c.pitch = s.pitch.load (kRelaxed);
        c.keyFrom = s.keyFrom.load (kRelaxed);
        c.key = s.key.load (kRelaxed);
        c.scale = s.scale.load (kRelaxed);
        std::atomic_thread_fence (kAcquire);
        if (s.seq.load (kRelaxed) == before) return true;
    }
    return false;
}

/** A free slot (never used, released, or its owner silent past kAliveMs), now this owner's. Only one claimer wins the
    swap of its heartbeat. nullptr when all are taken. */
Link::Slot* claim (Link::Slot* slots, std::uint64_t owner, std::uint64_t now)
{
    for (int i = 0; i < kSlots; ++i)
    {
        auto& s = slots[i];
        auto beat = s.heartbeat.load (kAcquire);
        if (alive (beat, now)) continue;   // (also one being claimed right now: its heartbeat is fresh)
        if (! s.heartbeat.compare_exchange_strong (beat, now, std::memory_order_acq_rel)) continue;
        beginWrite (s);
        s.owner.store (owner, kRelaxed);
        s.nameHash.store (0, kRelaxed);
        endWrite (s);
        return &s;
    }
    return nullptr;
}

void release (Link::Slot& s)
{
    beginWrite (s);
    s.owner.store (0, kRelaxed);
    s.nameHash.store (0, kRelaxed);
    s.flags.store (0, kRelaxed);
    endWrite (s);
    s.heartbeat.store (0, kRelease);
}

// ---- this process's block (one per library, all on the same memory) --------------------------------
class SharedMemory
{
public:
    SharedMemory (const std::string& name, std::size_t bytes) : total (bytes)
    {
#if defined(_WIN32)
        const std::string osName = "Local\\" + name;
        const auto size = static_cast<unsigned long long> (total);
        handle = CreateFileMappingA (INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, static_cast<DWORD> (size >> 32),
                                     static_cast<DWORD> (size & 0xFFFFFFFFull), osName.c_str());
        if (handle != nullptr)
        {
            mapping = MapViewOfFile (handle, FILE_MAP_ALL_ACCESS, 0, 0, total);
            if (mapping == nullptr) { CloseHandle (handle); handle = nullptr; }
        }
#else
        // Never unlinked while the process lives (the block is small); the name carries the process ID.
        const std::string osName = "/" + name;
        const int fd = shm_open (osName.c_str(), O_RDWR | O_CREAT, 0600);
        if (fd >= 0)
        {
            struct stat st {};
            bool ok = fstat (fd, &st) == 0;
            if (ok && static_cast<std::size_t> (st.st_size) < total) ok = ftruncate (fd, static_cast<off_t> (total)) == 0;
            if (ok)
            {
                void* p = mmap (nullptr, total, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
                if (p != MAP_FAILED) mapping = p;
            }
            close (fd);
        }
#endif
        shared = mapping != nullptr;
        if (! shared)
        {
            // (The OS refused: this plug-in file then only sees itself.)
            mapping = ::operator new (total, std::align_val_t { 64 });
            std::memset (mapping, 0, total);
        }
    }
    ~SharedMemory()
    {
        if (! shared) { ::operator delete (mapping, std::align_val_t { 64 }); return; }
#if defined(_WIN32)
        UnmapViewOfFile (mapping);
        CloseHandle (handle);
#else
        munmap (mapping, total);
#endif
    }
    SharedMemory (const SharedMemory&) = delete;
    SharedMemory& operator= (const SharedMemory&) = delete;
    void* data() const { return mapping; }

private:
    void* mapping = nullptr;
    std::size_t total = 0;
    bool shared = false;
#if defined(_WIN32)
    HANDLE handle = nullptr;
#endif
};

std::string blockName()
{
#if defined(_WIN32)
    const auto pid = static_cast<unsigned long> (GetCurrentProcessId());
#else
    const auto pid = static_cast<unsigned long> (getpid());
#endif
    return "VoxologyHoneyLink.v" + std::to_string (kLayout) + "." + std::to_string (pid);
}

struct ProcessHandle
{
    SharedMemory memory { blockName(), Link::bytes() };
    Link link { memory.data() };
};
} // namespace

Link::Link (void* memory) : block (*static_cast<Block*> (memory))
{
    block.layout.store (kLayout, kRelaxed);
}

std::size_t Link::bytes() { return sizeof (Block); }

Link& Link::process()
{
    static ProcessHandle h;
    return h.link;
}

void Link::publishHoney (std::uint64_t owner, const std::vector<HoneyTrack>& tracks, std::uint64_t now)
{
    if (owner == 0) return;
    std::vector<bool> done (tracks.size(), false);
    // Refresh what this document published before; release what it no longer tunes.
    for (auto& s : block.honey)
    {
        if (s.owner.load (kRelaxed) != owner) continue;
        const auto hash = s.nameHash.load (kRelaxed);
        bool kept = false;
        for (std::size_t k = 0; k < tracks.size() && ! kept; ++k)
            if (! done[k] && tracks[k].nameHash == hash && hash != 0)
            {
                done[k] = kept = true;
                beginWrite (s);
                s.flags.store (tracks[k].sharedName ? kSharedName : 0u, kRelaxed);
                endWrite (s);
                s.heartbeat.store (now, kRelease);
            }
        if (! kept) release (s);
    }
    for (std::size_t k = 0; k < tracks.size(); ++k)
    {
        if (done[k] || tracks[k].nameHash == 0) continue;
        auto* s = claim (block.honey, owner, now);
        if (s == nullptr) return;   // (64 tracks already)
        beginWrite (*s);
        s->nameHash.store (tracks[k].nameHash, kRelaxed);
        s->flags.store (tracks[k].sharedName ? kSharedName : 0u, kRelaxed);
        endWrite (*s);
        s->heartbeat.store (now, kRelease);
    }
}

void Link::leaveHoney (std::uint64_t owner)
{
    if (owner == 0) return;
    for (auto& s : block.honey)
        if (s.owner.load (kRelaxed) == owner) release (s);
}

void Link::publishVox (std::uint64_t owner, const VoxInfo& info, std::uint64_t now)
{
    if (owner == 0) return;
    Slot* mine = nullptr;
    for (auto& s : block.vox)
        if (s.owner.load (kRelaxed) == owner) { mine = &s; break; }
    if (mine == nullptr && (mine = claim (block.vox, owner, now)) == nullptr) return;
    beginWrite (*mine);
    mine->nameHash.store (info.nameHash, kRelaxed);
    mine->pitch.store (static_cast<std::uint32_t> (info.pitch), kRelaxed);
    mine->key.store (info.key, kRelaxed);
    mine->scale.store (info.scale, kRelaxed);
    mine->keyFrom.store (static_cast<std::uint32_t> (info.keyFrom), kRelaxed);
    endWrite (*mine);
    mine->heartbeat.store (now, kRelease);
}

void Link::leaveVox (std::uint64_t owner)
{
    if (owner == 0) return;
    for (auto& s : block.vox)
        if (s.owner.load (kRelaxed) == owner) release (s);
}

Link::Match Link::honeyOn (std::uint32_t nameHash, std::uint64_t self, std::uint64_t now) const
{
    Match m;
    if (nameHash == 0) return m;
    for (const auto& s : block.honey)
    {
        Copy c;
        if (! read (s, c) || c.owner == 0 || ! alive (c.heartbeat, now) || c.nameHash != nameHash) continue;
        m.honey = true;
        if (c.flags & kSharedName) m.ambiguous = true;
    }
    if (! m.honey) return m;
    for (const auto& s : block.vox)
    {
        Copy c;
        if (read (s, c) && c.owner != 0 && c.owner != self && alive (c.heartbeat, now) && c.nameHash == nameHash) m.ambiguous = true;
    }
    return m;
}

bool Link::voxologyOn (std::uint32_t nameHash, VoxInfo& out, std::uint64_t now) const
{
    if (nameHash == 0) return false;
    int found = 0;
    VoxInfo v;
    for (const auto& s : block.vox)
    {
        Copy c;
        if (! read (s, c) || c.owner == 0 || ! alive (c.heartbeat, now) || c.nameHash != nameHash) continue;
        ++found;
        v.nameHash = c.nameHash;
        v.pitch = static_cast<Pitch> (std::min<std::uint32_t> (c.pitch, 3u));
        v.key = std::clamp (c.key, 0, 11);
        v.scale = std::max (0, c.scale);
        v.keyFrom = static_cast<KeyFrom> (std::min<std::uint32_t> (c.keyFrom, 3u));
    }
    if (found != 1) return false;
    out = v;
    return true;
}

} // namespace vox::honeylink
