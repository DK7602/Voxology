// Honey Tune: the note editor, as an ARA plug-in (applied to a clip, like Melodyne).
//
// Each audio source (the clip's recording) is read in full, analysed into notes (vox::honey) on a
// background thread, and rendered with the document's settings plus your hand edits per note. The
// playback renderer plays the rendered audio; until it's ready the host plays the clip untouched.
// Settings and note edits are saved in the host's project through the ARA archive.

#include <juce_audio_utils/juce_audio_utils.h>

#include "HoneyPanel.h"
#include "vox/KeyShare.h"

#include <ARA_Library/Utilities/ARATimelineConversion.h>

#include <limits>
#include <map>
#include <memory>

using namespace juce;
using honeyui::Settings;
using honeyui::NoteEdit;

namespace
{
    constexpr int64 kArchiveV2 = 0x484e5932;   // "HNY2"
    constexpr int64 kArchiveV3 = 0x484e5933;   // "HNY3": edits also hold timing, length and formant

    /** A hand edit as saved: tied to the note's start, so it finds its note again after re-analysis. */
    struct SavedEdit { double start = 0.0; NoteEdit edit; };

    using Audio = std::vector<std::vector<float>>;   // per channel

    /** What we know about one recording, shared between the model, the background jobs, the editor
        and the audio thread. The rendered audio is swapped in under a spin lock, held only for the swap; the one
        it replaces is freed later on the background thread, never by the audio thread (it may still be playing it). */
    struct SourceState
    {
        std::atomic<bool> alive { true };
        std::atomic<int> status { 0 };   // 0 waiting, 1 listening, 2 ready, 3 couldn't read, 4 too long (not enough memory)
        std::atomic<bool> samplesChanged { true };
        std::atomic<double> playPosition { -1.0 };   // where playback is in this recording (samples), from the audio thread
        std::atomic<uint32> playStamp { 0 };          // when that was (ms counter): stale = stopped
        std::string persistentId, name;

        // Background thread only
        double sampleRate = 48000.0;
        std::shared_ptr<const Audio> original;            // the recording (also what plays as `recorded`: one copy)
        std::vector<vox::honey::Note> renderedNotes;      // what the current render was made from
        const vox::honey::Track* renderedTrack = nullptr;
        std::vector<std::shared_ptr<const Audio>> retired;   // swapped out, freed once no block is playing them

        // Shared (dataLock)
        CriticalSection dataLock;
        std::shared_ptr<const vox::honey::Track> track;
        std::vector<vox::honey::Note> notes;
        vox::KeyGuess guess;
        std::vector<NoteEdit> edits;           // one per note
        std::vector<SavedEdit> pending;        // restored from the project, waiting for the notes

        // Render requests (coalesced: one job at a time, re-run if asked again meanwhile)
        std::atomic<int> wanted { 0 };
        std::atomic<int> done { 0 };              // the `wanted` count the current render answers
        std::atomic<bool> queued { false };
        std::atomic<bool> waitedOut { false };    // an offline render gave up waiting for it (until it next renders)

        /** Work still to come for what plays: listening, or a render asked for and not done yet. */
        bool busy() const { const int st = status; return st == 1 || (st == 2 && done < wanted); }

        SpinLock lock;
        std::shared_ptr<const Audio> rendered, recorded;
        std::atomic<bool> playOriginal { false };   // A / B

        /** What to play: the tuned clip, or the recording (Original, or while it's still being tuned). Realtime:
            a few tries at the lock (it's only held for a swap); offline (export): waits for it. */
        std::shared_ptr<const Audio> getPlayback (bool realtime)
        {
            if (! realtime)
            {
                const SpinLock::ScopedLockType sl (lock);
                return (playOriginal || rendered == nullptr) ? recorded : rendered;
            }
            for (int tries = 0; tries < 32; ++tries)
            {
                const SpinLock::ScopedTryLockType sl (lock);
                if (sl.isLocked())
                    return (playOriginal || rendered == nullptr) ? recorded : rendered;
            }
            return nullptr;
        }

        /** Background thread: puts `next` in `slot` (rendered / recorded). The one it replaces is kept until no block
            plays it: freeing a whole take (tens of MB) inside the lock made the audio thread's try fail (silence),
            and the audio thread could end up freeing it itself. */
        void swapIn (std::shared_ptr<const Audio>& slot, std::shared_ptr<const Audio> next)
        {
            {
                const SpinLock::ScopedLockType sl (lock);
                std::swap (slot, next);
            }
            if (next != nullptr) retired.push_back (std::move (next));
            // Free those only held here now (once swapped out, nothing can pick them up again).
            retired.erase (std::remove_if (retired.begin(), retired.end(), [] (const auto& a) { return a.use_count() == 1; }), retired.end());
        }

        /** Hand edits re-attached to the notes by start time (within 30 ms). Call with dataLock held. */
        // Undo / redo (dataLock): whole edit lists. One step per gesture: edits of the same note less than
        // 0.7 s apart (a drag) count as one.
        std::vector<std::vector<NoteEdit>> undoStack, redoStack;
        uint32 lastPushMs = 0;
        int lastPushIndex = -1;
        void pushUndo (int index, bool force = false)
        {
            const auto now = Time::getMillisecondCounter();
            if (force || index != lastPushIndex || now - lastPushMs > 700)
            {
                undoStack.push_back (edits);
                if (undoStack.size() > 100) undoStack.erase (undoStack.begin());
                redoStack.clear();
            }
            lastPushMs = now;
            lastPushIndex = force ? -1 : index;
        }
        bool step (bool back)
        {
            auto& from = back ? undoStack : redoStack;
            auto& to = back ? redoStack : undoStack;
            if (from.empty() || from.back().size() != edits.size()) return false;
            to.push_back (edits);
            edits = from.back();
            from.pop_back();
            lastPushIndex = -1;
            return true;
        }

        void attach (const std::vector<SavedEdit>& saved)
        {
            undoStack.clear();
            redoStack.clear();
            edits.assign (notes.size(), {});
            const double tolerance = 0.03 * (track != nullptr ? track->sampleRate : 48000.0);
            for (const auto& s : saved)
            {
                int best = -1;
                double bestDist = tolerance;
                for (size_t i = 0; i < notes.size(); ++i)
                    if (const double d = std::abs (notes[i].start - s.start); d <= bestDist) { bestDist = d; best = static_cast<int> (i); }
                if (best >= 0) edits[static_cast<size_t> (best)] = s.edit;
            }
        }

        /** The hand edits as saved. Call with dataLock held. */
        std::vector<SavedEdit> saved() const
        {
            if (track == nullptr) return pending;
            std::vector<SavedEdit> out;
            for (size_t i = 0; i < notes.size() && i < edits.size(); ++i)
                if (! edits[i].isDefault()) out.push_back ({ notes[i].start, edits[i] });
            return out;
        }
    };
}

//==============================================================================
class HoneyAudioSource final : public ARAAudioSource
{
public:
    using ARAAudioSource::ARAAudioSource;
    ~HoneyAudioSource() override { state->alive = false; }
    std::shared_ptr<SourceState> state = std::make_shared<SourceState>();
};

//==============================================================================
class HoneyDocumentController;

class HoneyPlaybackRenderer final : public ARAPlaybackRenderer
{
public:
    using ARAPlaybackRenderer::ARAPlaybackRenderer;

    void prepareToPlay (double sampleRateIn, int maximumSamplesPerBlockIn, int numChannelsIn,
                        AudioProcessor::ProcessingPrecision, AlwaysNonRealtime) override
    {
        sampleRate = sampleRateIn;
        numChannels = numChannelsIn;
        maxBlock = maximumSamplesPerBlockIn;
    }

    bool processBlock (AudioBuffer<float>& buffer, AudioProcessor::Realtime realtime,
                       const AudioPlayHead::PositionInfo& positionInfo) noexcept override
    {
        buffer.clear();
        if (! positionInfo.getIsPlaying())
            return true;
        const auto numSamples = buffer.getNumSamples();
        const auto blockStart = positionInfo.getTimeInSamples().orFallback (0);
        const auto blockRange = Range<int64>::withStartAndLength (blockStart, numSamples);
        bool ok = true;

        for (auto* region : getPlaybackRegions())
        {
            const auto songRange = region->getSampleRange (sampleRate, ARAPlaybackRegion::IncludeHeadAndTail::no);
            const auto renderRange = blockRange.getIntersectionWith (songRange);
            if (renderRange.isEmpty())
                continue;

            // The recording's own sample rate can differ from the song's (a clip that wasn't converted on
            // import): read it at the matching speed (cubic) instead of playing nothing.
            auto* source = static_cast<HoneyAudioSource*> (region->getAudioModification()->getAudioSource());
            const bool live = realtime == AudioProcessor::Realtime::yes;
            if (! live && ! source->state->waitedOut)
            {
                // Export / render in place: wait for the clip's analysis or a render still on its way (it would
                // otherwise bake in silence or the old tuning). Not forever: a clip that never gets there plays as it is.
                for (int ms = 0; source->state->busy() && ms < kOfflineWaitMs; ms += 2)
                    Thread::sleep (2);
                if (source->state->busy()) source->state->waitedOut = true;
            }
            const double ratio = source->getSampleRate() > 0.0 ? source->getSampleRate() / sampleRate : 1.0;
            const bool sameRate = std::abs (ratio - 1.0) < 1.0e-9;
            const double srcAtSongStart = region->getStartInAudioModificationTime() * source->getSampleRate();
            const double srcStart = srcAtSongStart + static_cast<double> (renderRange.getStart() - songRange.getStart()) * ratio;
            source->state->playPosition = srcStart;   // for the editor's playhead
            source->state->playStamp = Time::getMillisecondCounter();
            auto rendered = source->state->getPlayback (live);
            if (rendered == nullptr || rendered->empty())
            {
                ok = false;   // not read yet (or swapping this instant)
                continue;
            }
            const auto& chans = *rendered;
            const int start = static_cast<int> (renderRange.getStart() - blockStart);
            const int len = static_cast<int> (renderRange.getLength());
            for (int c = 0; c < buffer.getNumChannels(); ++c)
            {
                const auto& ch = chans[static_cast<size_t> (std::min<int> (c, static_cast<int> (chans.size()) - 1))];
                const auto size = static_cast<int64> (ch.size());
                auto sample = [&ch, size] (int64 k) { return k >= 0 && k < size ? ch[static_cast<size_t> (k)] : 0.0f; };
                auto* out = buffer.getWritePointer (c, start);
                if (sameRate)
                {
                    const auto first = static_cast<int64> (std::llround (srcStart));
                    for (int i = 0; i < len; ++i) out[i] += sample (first + i);
                    continue;
                }
                for (int i = 0; i < len; ++i)
                {
                    const double pos = srcStart + i * ratio;
                    const auto k = static_cast<int64> (std::floor (pos));
                    const auto f = static_cast<float> (pos - static_cast<double> (k));
                    const float ym1 = sample (k - 1), y0 = sample (k), y1 = sample (k + 1), y2 = sample (k + 2);
                    const float c1 = 0.5f * (y1 - ym1), c2 = ym1 - 2.5f * y0 + 2.0f * y1 - 0.5f * y2, c3 = 0.5f * (y2 - ym1) + 1.5f * (y0 - y1);
                    out[i] += ((c3 * f + c2) * f + c1) * f + y0;
                }
            }
        }
        return ok;
    }

    using ARAPlaybackRenderer::processBlock;

private:
    static constexpr int kOfflineWaitMs = 180000;   // (an hour-long take's analysis takes minutes)
    double sampleRate = 48000.0;
    int numChannels = 2, maxBlock = 512;
};

//==============================================================================
class HoneyDocumentController final : public ARADocumentControllerSpecialisation
{
public:
    using ARADocumentControllerSpecialisation::ARADocumentControllerSpecialisation;
    ~HoneyDocumentController() override
    {
        // The jobs still running stop early (an analysis or a render can take many seconds) and add no new ones, so
        // closing the project doesn't wait on them.
        beatKeyPoll.stopTimer();
        *controllerAlive = false;
        closing = true;
        pool.removeAllJobs (true, 60000);
    }

    Settings getSettings() const { const ScopedLock sl (settingsLock); return settings; }
    honeyui::HostKey getHostKey() const { const ScopedLock sl (settingsLock); return hostKey; }

    /** The song's key from the host (its key signature / scale track: the first one), for Auto. Message thread. */
    void refreshHostKey()
    {
        projectKey = readProjectKey();
        useOutsideKey();
    }

    /** Voxology on the beat (this host, written in the last few seconds), for Auto. Message thread, once a second. */
    void pollBeatKey()
    {
        honeyui::HostKey k;
        if (vox::keyshare::BeatKeyShare b; vox::keyshare::read (b))
        {
            k.valid = true;
            k.key = b.key;
            k.scale = b.scale;
            k.fromBeat = true;
        }
        if (k == beatKey) return;
        beatKey = k;
        useOutsideKey();
    }

    /** The key from outside the clip: the project's, else the beat's. A change re-tunes every clip. */
    void useOutsideKey()
    {
        const auto use = honeyui::outsideKey (projectKey, beatKey);
        {
            const ScopedLock sl (settingsLock);
            if (use == hostKey) return;
            hostKey = use;
        }
        for (auto* src : sources())
            requestRender (src->state);
        changes.sendChangeMessage();
    }

    honeyui::HostKey readProjectKey()
    {
        honeyui::HostKey found;
        for (auto* context : getDocumentController()->getDocument<ARADocument>()->getMusicalContexts<ARAMusicalContext>())
        {
            const ARA::PlugIn::HostContentReader<ARA::kARAContentTypeKeySignatures> reader (context);
            if (! reader || reader.getEventCount() <= 0) continue;
            const auto sig = reader.getDataForEvent (0);
            bool used[12];
            for (int i = 0; i < 12; ++i) used[i] = sig.intervals[i] != ARA::kARAKeySignatureIntervalUnused;
            found = honeyui::hostKeyFromSignature (static_cast<int> (sig.root), used);
            break;
        }
        return found;
    }
    void setSettings (const Settings& s)
    {
        {
            const ScopedLock sl (settingsLock);
            if (s == settings) return;
            settings = s;
        }
        for (auto* src : sources())
            requestRender (src->state);
    }

    std::vector<HoneyAudioSource*> sources()
    {
        return getDocumentController()->getDocument<ARADocument>()->getAudioSources<HoneyAudioSource>();
    }

    honeyui::Snapshot snapshot (const std::shared_ptr<SourceState>& st)
    {
        const auto s = getSettings();
        honeyui::Snapshot snap;
        {
            const ScopedLock sl (st->dataLock);
            snap = honeyui::makeSnapshot (st->track, st->notes, st->edits, st->guess, s, getHostKey());
        }
        snap.status = st->status;
        snap.name = st->name;
        return snap;
    }

    void setEdit (const std::shared_ptr<SourceState>& st, int index, const NoteEdit& e)
    {
        {
            const ScopedLock sl (st->dataLock);
            if (index < 0 || index >= static_cast<int> (st->edits.size())) return;
            st->pushUndo (index);
            st->edits[static_cast<size_t> (index)] = e;
        }
        requestRender (st);
    }

    void resetAllEdits (const std::shared_ptr<SourceState>& st)
    {
        {
            const ScopedLock sl (st->dataLock);
            st->pushUndo (-1, true);
            st->edits.assign (st->notes.size(), {});
        }
        requestRender (st);
    }

    bool undoRedo (const std::shared_ptr<SourceState>& st, bool back)
    {
        {
            const ScopedLock sl (st->dataLock);
            if (! st->step (back)) return false;
        }
        requestRender (st);
        return true;
    }

    /** A / B for the whole document: play the recordings as they were (edits kept). */
    void setOriginal (bool o)
    {
        playOriginal = o;
        for (auto* src : sources())
        {
            src->state->playOriginal = o;
            finished (src->state);   // tells the host the audio changed
        }
    }
    bool isOriginal() const { return playOriginal; }

    ChangeBroadcaster changes;   // a clip finished listening / rendering (the editor listens)

protected:
    ARAMusicalContext* doCreateMusicalContext (ARADocument* document, ARA::ARAMusicalContextHostRef hostRef) noexcept override
    {
        auto* context = new ARAMusicalContext (document, hostRef);
        context->addListener (this);   // to hear when the song's key changes
        return context;
    }

    void doUpdateMusicalContextContent (ARAMusicalContext*, ARAContentUpdateScopes scopeFlags) override
    {
        if (scopeFlags.affectHarmonies())
            refreshHostKey();
    }

    void willDestroyMusicalContext (ARAMusicalContext* context) override
    {
        context->removeListener (this);
    }

    void didEndEditing (ARADocument*) override
    {
        refreshHostKey();   // musical contexts added / removed
    }

    ARAAudioSource* doCreateAudioSource (ARADocument* document, ARA::ARAAudioSourceHostRef hostRef) noexcept override
    {
        auto* src = new HoneyAudioSource (document, hostRef);
        src->state->playOriginal = playOriginal.load();
        src->addListener (this);
        return src;
    }

    void doUpdateAudioSourceContent (ARAAudioSource* audioSource, ARAContentUpdateScopes scopeFlags) override
    {
        if (scopeFlags.affectSamples())
            static_cast<HoneyAudioSource*> (audioSource)->state->samplesChanged = true;
    }

    void didEnableAudioSourceSamplesAccess (ARAAudioSource* audioSource, bool enable) override
    {
        // New samples, or not read yet / couldn't read. (While a listen is on its way, or the clip was too long, the
        // same samples aren't read again: hosts enable access many times.)
        auto* src = static_cast<HoneyAudioSource*> (audioSource);
        const int status = src->state->status;
        if (enable && (src->state->samplesChanged || status == 0 || status == 3))
            listen (src);
    }

    void willDestroyAudioSource (ARAAudioSource* audioSource) override
    {
        audioSource->removeListener (this);
    }

    ARAPlaybackRenderer* doCreatePlaybackRenderer() noexcept override
    {
        return new HoneyPlaybackRenderer (getDocumentController());
    }

    bool doRestoreObjectsFromStream (ARAInputStream& input, const ARARestoreObjectsFilter* filter) noexcept override
    {
        const auto first = input.readInt64();
        const bool v3 = first == kArchiveV3;
        const bool v2 = first == kArchiveV2 || v3;
        Settings s;
        const auto key = v2 ? input.readInt64() : first, scale = input.readInt64();
        s.key = static_cast<int> (jlimit<int64> (0, honeyui::kAutoKey, key));
        s.scale = static_cast<int> (jlimit<int64> (0, vox::kScales - 1, scale));
        s.snap = input.readDouble();
        s.drift = input.readDouble();
        s.vibrato = input.readDouble();
        if (input.failed())
            return false;
        s = honeyui::sanitised (s);   // (a damaged or hand-made archive: NaN, huge values)
        if (filter == nullptr || filter->shouldRestoreDocumentData())
            setSettings (s);
        if (! v2)
            return true;

        const auto numSources = input.readInt64();
        for (int64 k = 0; k < numSources && ! input.failed(); ++k)
        {
            const auto id = input.readString().toStdString();
            const auto numEdits = input.readInt64();
            std::vector<SavedEdit> saved;
            for (int64 e = 0; e < numEdits && ! input.failed(); ++e)
            {
                SavedEdit se;
                se.start = input.readDouble();
                se.edit.moved = input.readBool();
                se.edit.target = input.readDouble();
                se.edit.drift = input.readDouble();
                se.edit.vibrato = input.readDouble();
                if (v3)
                {
                    se.edit.shift = input.readDouble();
                    se.edit.length = input.readDouble();
                    se.edit.formant = input.readDouble();
                }
                se.edit = honeyui::sanitised (se.edit);
                if (std::isfinite (se.start) && se.start >= 0.0)
                    saved.push_back (se);
            }
            HoneyAudioSource* target = nullptr;
            if (filter != nullptr)
                target = filter->getAudioSourceToRestoreStateWithID<HoneyAudioSource> (id.c_str());
            else
                for (auto* src : sources())
                    if (src->getPersistentID() == id) target = src;
            if (target == nullptr)
                continue;
            auto st = target->state;
            {
                const ScopedLock sl (st->dataLock);
                if (st->track != nullptr) st->attach (saved);
                else st->pending = saved;
            }
            requestRender (st);
        }
        return ! input.failed();
    }

    bool doStoreObjectsToStream (ARAOutputStream& output, const ARAStoreObjectsFilter* filter) noexcept override
    {
        const auto s = getSettings();
        bool ok = output.writeInt64 (kArchiveV3) && output.writeInt64 (s.key) && output.writeInt64 (s.scale)
               && output.writeDouble (s.snap) && output.writeDouble (s.drift) && output.writeDouble (s.vibrato);
        std::vector<const HoneyAudioSource*> toStore;
        if (filter != nullptr)
            toStore = filter->getAudioSourcesToStore<HoneyAudioSource>();
        else
            for (auto* src : sources()) toStore.push_back (src);
        ok = ok && output.writeInt64 (static_cast<int64> (toStore.size()));
        for (auto* src : toStore)
        {
            std::vector<SavedEdit> saved;
            {
                const ScopedLock sl (src->state->dataLock);
                saved = src->state->saved();
            }
            ok = ok && output.writeString (String (src->getPersistentID())) && output.writeInt64 (static_cast<int64> (saved.size()));
            for (const auto& se : saved)
                ok = ok && output.writeDouble (se.start) && output.writeBool (se.edit.moved) && output.writeDouble (se.edit.target)
                        && output.writeDouble (se.edit.drift) && output.writeDouble (se.edit.vibrato)
                        && output.writeDouble (se.edit.shift) && output.writeDouble (se.edit.length) && output.writeDouble (se.edit.formant);
        }
        return ok;
    }

private:
    /** Read the whole recording (now: sample access is enabled for this call), then analyse it in the background. */
    void listen (HoneyAudioSource* src)
    {
        if (closing) return;
        refreshHostKey();
        pollBeatKey();
        if (! beatKeyPoll.isTimerRunning()) beatKeyPoll.startTimer (1000);
        auto state = src->state;
        state->status = 1;
        state->samplesChanged = false;
        state->persistentId = src->getPersistentID();
        state->name = src->getName() != nullptr ? src->getName() : "clip";
        ARAAudioSourceReader reader (src);
        const auto count = src->getSampleCount();
        const int nch = std::max (1, static_cast<int> (src->getChannelCount()));
        // Read straight into the buffers that are kept (and played as the recording): one copy of the take. Too long
        // for memory (an hour-long take is gigabytes on the way through the analysis): say so. An exception must not
        // leave this call (it comes from the host, through ARA's C interface).
        std::shared_ptr<Audio> chans;
        bool read = false;
        try
        {
            if (count > std::numeric_limits<int>::max()) throw std::bad_alloc();
            const auto len = static_cast<int> (std::max<int64> (0, count));
            chans = std::make_shared<Audio> (static_cast<size_t> (nch));
            std::vector<float*> channels;
            for (auto& ch : *chans) { ch.resize (static_cast<size_t> (std::max (1, len))); channels.push_back (ch.data()); }
            AudioBuffer<float> buf (channels.data(), nch, std::max (1, len));   // (uses chans' memory)
            read = reader.isValid() && len > 0 && reader.read (&buf, 0, len, 0, true, nch > 1);
        }
        catch (const std::bad_alloc&)
        {
            state->status = 4;
            changes.sendChangeMessage();
            return;
        }
        if (! read)
        {
            state->status = 3;
            changes.sendChangeMessage();
            return;
        }
        changes.sendChangeMessage();
        pool.addJob ([this, state, chans = std::shared_ptr<const Audio> (std::move (chans)), sr = src->getSampleRate()]
        {
            try
            {
                analyseClip (state, chans, sr);
            }
            catch (const std::bad_alloc&)
            {
                state->status = 4;   // (the analysis of a very long take)
                changes.sendChangeMessage();
            }
        });
    }

    void analyseClip (const std::shared_ptr<SourceState>& state, const std::shared_ptr<const Audio>& chans, double sr)
    {
        state->sampleRate = sr;
        state->original = chans;
        state->swapIn (state->recorded, chans);
        // The detector hears the clip in mono (a mono take as it is: no copy).
        std::vector<float> mixed;
        if (chans->size() > 1)
        {
            mixed.assign (chans->front().size(), 0.0f);
            for (const auto& ch : *chans)
                for (size_t i = 0; i < mixed.size(); ++i) mixed[i] += ch[i] / static_cast<float> (chans->size());
        }
        const auto& mono = chans->size() > 1 ? mixed : chans->front();
        auto track = std::make_shared<vox::honey::Track> (vox::honey::analyse (mono, sr, true, &closing));
        if (closing) return;
        auto notes = vox::honey::findNotes (*track);
        std::vector<double> sung;
        for (double m : track->midi) if (m > 0.0) sung.push_back (m);
        const auto guess = vox::detectKey (sung);
        {
            const ScopedLock sl (state->dataLock);
            const auto keep = state->saved();   // hand edits (or the ones restored from the project)
            state->track = track;
            state->notes = std::move (notes);
            state->guess = guess;
            state->attach (keep);
            state->pending.clear();
        }
        requestRender (state);
    }

    void requestRender (const std::shared_ptr<SourceState>& state)
    {
        ++state->wanted;
        if (closing || state->queued.exchange (true))
            return;   // closing: no new jobs; else a job is waiting: it will pick this up
        pool.addJob ([this, state]
        {
            for (;;)
            {
                const int asked = state->wanted;
                bool rendered = false;
                try
                {
                    rendered = renderNow (*state);
                }
                catch (const std::bad_alloc&)
                {
                    state->status = 4;
                    changes.sendChangeMessage();
                }
                if (closing) return;
                state->done = asked;
                state->waitedOut = false;
                if (rendered)
                    finished (state);
                state->queued = false;
                if (state->wanted == asked || state->queued.exchange (true))
                    return;
            }
        });
    }

    bool renderNow (SourceState& st)
    {
        if (st.original == nullptr || st.original->empty()) return false;
        const auto s = getSettings();
        std::shared_ptr<const vox::honey::Track> track;
        std::vector<vox::honey::Note> notes;
        {
            const ScopedLock sl (st.dataLock);
            if (st.track == nullptr) return false;
            int key = 0, scale = 0;
            honeyui::resolveKey (s, st.guess, key, scale, getHostKey());
            track = st.track;
            notes = honeyui::applyEdits (st.notes, st.edits, s, key, scale, st.sampleRate);
        }
        // An edit to a few notes: re-render only around them (from the gap before to the gap after),
        // on top of the current render. A new analysis, the key / settings moving most notes: all of it.
        std::shared_ptr<const Audio> current;
        {
            const SpinLock::ScopedLockType sl (st.lock);
            current = st.rendered;
        }
        const auto& original = *st.original;
        const double clip = static_cast<double> (original.front().size());
        double from = 0.0, to = 0.0;
        const bool part = current != nullptr && current->size() == original.size() && st.renderedTrack == track.get()
                          && vox::honey::changedRegion (st.renderedNotes, notes, clip, st.sampleRate, from, to) && to - from < 0.6 * clip;
        if (part && to <= from) { st.status = 2; return true; }   // nothing changed
        auto out = std::make_shared<Audio>();
        for (size_t c = 0; c < original.size(); ++c)
        {
            if (part)
            {
                out->push_back ((*current)[c]);
                vox::honey::renderPart (original[c], st.sampleRate, *track, notes, out->back(), from, to, vox::honey::kTransitionMs, &closing);
            }
            else
                out->push_back (vox::honey::render (original[c], st.sampleRate, *track, notes, vox::honey::kTransitionMs, &closing));
        }
        if (closing) return false;   // (stopped part way: thrown away)
        st.renderedNotes = notes;
        st.renderedTrack = track.get();
        current.reset();   // (so the old render can be freed right away, if no block is playing it)
        st.swapIn (st.rendered, std::move (out));
        st.status = 2;
        return true;
    }

    void finished (std::shared_ptr<SourceState> state)
    {
        // Tell the host the audio changed (message thread), unless the recording has gone meanwhile.
        MessageManager::callAsync ([state, this, token = controllerAlive]
        {
            if (! *token || ! state->alive) return;
            for (auto* src : sources())
                if (src->state == state)
                    for (auto* mod : src->getAudioModifications())
                    {
                        mod->notifyContentChanged (ARAContentUpdateScopes::samplesAreAffected(), true);
                        for (auto* region : mod->getPlaybackRegions())
                            region->notifyContentChanged (ARAContentUpdateScopes::samplesAreAffected(), true);
                    }
            changes.sendChangeMessage();
        });
    }

    std::shared_ptr<std::atomic<bool>> controllerAlive = std::make_shared<std::atomic<bool>> (true);
    mutable CriticalSection settingsLock;
    Settings settings;
    honeyui::HostKey hostKey;   // the song's key from outside the clip, in use (settingsLock)
    honeyui::HostKey projectKey, beatKey;   // message thread
    TimedCallback beatKeyPoll { [this] { pollBeatKey(); } };
    std::atomic<bool> playOriginal { false };
    std::atomic<bool> closing { false };   // the document is going: jobs stop early, no new ones
    ThreadPool pool { 1, 0, Thread::Priority::low };   // (the DAW and its audio come first)
};

//==============================================================================
class HoneyProcessor final : public AudioProcessor,
                             public AudioProcessorARAExtension
{
public:
    HoneyProcessor()
        : AudioProcessor (BusesProperties().withInput ("Input", AudioChannelSet::stereo(), true)
                                           .withOutput ("Output", AudioChannelSet::stereo(), true)) {}

    void prepareToPlay (double sampleRate, int samplesPerBlock) override
    {
        prepareToPlayForARA (sampleRate, samplesPerBlock, getMainBusNumOutputChannels(), getProcessingPrecision());
    }
    void releaseResources() override { releaseResourcesForARA(); }

    bool isBusesLayoutSupported (const BusesLayout& layouts) const override
    {
        const auto out = layouts.getMainOutputChannelSet();
        return out == AudioChannelSet::mono() || out == AudioChannelSet::stereo();
    }

    void processBlock (AudioBuffer<float>& buffer, MidiBuffer& midi) override
    {
        ScopedNoDenormals noDenormals;
        // The song position, for the editor's playhead (the host always sends it here; the renderer's own
        // stamp below only comes while a clip is being rendered).
        if (auto* ph = getPlayHead())
            if (const auto pos = ph->getPosition(); pos.hasValue() && pos->getIsPlaying())
                if (const auto t = pos->getTimeInSeconds())
                {
                    songSeconds = *t;
                    songStamp = Time::getMillisecondCounter();
                }
        // Not applied as an ARA extension (or not ready): pass the audio through.
        if (! processBlockForARA (buffer, isRealtime(), getPlayHead()))
            processBlockBypassed (buffer, midi);
    }
    using AudioProcessor::processBlock;

    const String getName() const override { return "Honey Tune"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { double t = 0.0; return getTailLengthSecondsForARA (t) ? t : 0.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const String getProgramName (int) override { return "Default"; }
    void changeProgramName (int, const String&) override {}
    std::atomic<double> songSeconds { 0.0 };   // host song position while playing (audio thread -> editor)
    std::atomic<uint32> songStamp { 0 };

    void getStateInformation (MemoryBlock&) override {}
    void setStateInformation (const void*, int) override {}

    bool hasEditor() const override { return true; }
    AudioProcessorEditor* createEditor() override;
    AudioProcessorARAExtension* getARAClientExtensions() override { return this; }
};

//==============================================================================
/** The editor shows one clip: the one you selected in the host (else the first). */
class HoneyEditor final : public AudioProcessorEditor,
                          public AudioProcessorEditorARAExtension,
                          private ARAEditorView::Listener,
                          private ChangeListener,
                          private Timer,
                          private honeyui::Model
{
public:
    explicit HoneyEditor (HoneyProcessor& p)
        : AudioProcessorEditor (&p), AudioProcessorEditorARAExtension (&p), processor (p)
    {
        addAndMakeVisible (panel);
        if (auto* view = getARAEditorView())
        {
            dc = ARADocumentControllerSpecialisation::getSpecialisedDocumentController<HoneyDocumentController> (view->getDocumentController());
            view->addListener (this);
            dc->changes.addChangeListener (this);
            pickSource (view->getViewSelection().getPlaybackRegions<ARAPlaybackRegion>());
            startTimerHz (30);   // playhead
        }
        panel.setModel (dc != nullptr ? this : nullptr);
        setResizable (true, false);
        setResizeLimits (900, 460, 4000, 2400);
        setSize (1280, 720);   // everything on one row from about 1240 px; narrower windows wrap (HoneyPanel::layoutBars)
    }

    ~HoneyEditor() override
    {
        if (dc != nullptr) dc->changes.removeChangeListener (this);
        if (auto* view = getARAEditorView()) view->removeListener (this);
    }

    void paint (Graphics& g) override { g.fillAll (Colour (0xfff3ede0)); }
    void resized() override { panel.setBounds (getLocalBounds()); }

    AudioProcessorEditorARAExtension* getARAClientExtensions() override { return this; }

private:
    // honeyui::Model
    honeyui::Snapshot snapshot() override
    {
        if (dc == nullptr || state == nullptr) return {};
        auto snap = dc->snapshot (state);
        for (auto* src : dc->sources())
            if (src->state == state)
                for (auto* mod : src->getAudioModifications())
                    if (! mod->getPlaybackRegions().empty())
                    {
                        snap.timeline = buildTimeline (mod->getPlaybackRegions().front(), snap.seconds);
                        return snap;
                    }
        return snap;
    }

    /** The song's bars and beats over this clip, from the host's tempo map and time signatures. */
    static honeyui::Timeline buildTimeline (ARAPlaybackRegion* region, double clipSeconds)
    {
        honeyui::Timeline tl;
        tl.songOffset = region->getStartInPlaybackTime() - region->getStartInAudioModificationTime();
        auto* sequence = region->getRegionSequence();
        auto* context = sequence != nullptr ? sequence->getMusicalContext() : nullptr;
        if (context == nullptr) return tl;
        const ARA::PlugIn::HostContentReader<ARA::kARAContentTypeTempoEntries> tempoReader (context);
        const ARA::PlugIn::HostContentReader<ARA::kARAContentTypeBarSignatures> barReader (context);
        if (! tempoReader || ! barReader) return tl;
        const ARA::TempoConverter<decltype (tempoReader)> tempo (tempoReader);
        const ARA::BarSignaturesConverter<decltype (barReader)> bars (barReader);
        const auto q0 = tempo.getQuarterForTime (tl.songOffset - 2.0), q1 = tempo.getQuarterForTime (tl.songOffset + clipSeconds + 2.0);
        for (int b = bars.getBarIndexForQuarter (q0); b <= bars.getBarIndexForQuarter (q1) && tl.lines.size() < 50000; ++b)
        {
            const auto barStart = bars.getQuarterForBarIndex (b);
            const auto sig = bars.getBarSignatureForQuarter (barStart);
            const double quartersPerBeat = 4.0 / std::max (1, (int) sig.denominator);
            for (int k = 0; k < std::max (1, (int) sig.numerator); ++k)
                tl.lines.push_back ({ tempo.getTimeForQuarter (barStart + k * quartersPerBeat) - tl.songOffset, b >= 0 ? b + 1 : b, k + 1 });
        }
        return tl;
    }
    Settings getSettings() override { return dc != nullptr ? dc->getSettings() : Settings {}; }
    void setSettings (const Settings& s) override { if (dc != nullptr) dc->setSettings (s); }
    void setEdit (int i, const NoteEdit& e) override { if (dc != nullptr && state != nullptr) dc->setEdit (state, i, e); }
    void resetAllEdits() override { if (dc != nullptr && state != nullptr) dc->resetAllEdits (state); }
    bool undo() override { return dc != nullptr && state != nullptr && dc->undoRedo (state, true); }
    bool redo() override { return dc != nullptr && state != nullptr && dc->undoRedo (state, false); }
    void setOriginal (bool o) override { if (dc != nullptr) dc->setOriginal (o); }
    bool isOriginal() override { return dc != nullptr && dc->isOriginal(); }
    void seek (double clipSeconds) override
    {
        if (dc == nullptr || state == nullptr) return;
        if (auto* playback = dc->getDocumentController()->getHostPlaybackController())
            playback->requestSetPlaybackPosition (clipSeconds + panel.roll.getSnapshot().timeline.songOffset);
    }

    void onNewSelection (const ARAViewSelection& sel) override
    {
        pickSource (sel.getPlaybackRegions<ARAPlaybackRegion>());
    }

    void pickSource (const std::vector<ARAPlaybackRegion*>& regions)
    {
        if (dc == nullptr) return;
        std::shared_ptr<SourceState> next;
        if (! regions.empty())
            next = static_cast<HoneyAudioSource*> (regions.front()->getAudioModification()->getAudioSource())->state;
        else if (state == nullptr || ! state->alive)
            if (const auto all = dc->sources(); ! all.empty())
                next = all.front()->state;
        if (next != nullptr && next != state)
        {
            state = next;
            panel.roll.setSelected (-1);
            panel.refresh();
        }
    }

    void changeListenerCallback (ChangeBroadcaster*) override
    {
        if (state == nullptr || ! state->alive) pickSource ({});
        panel.refresh();
    }

    void timerCallback() override
    {
        // While listening (or if a clip was removed), keep the status fresh.
        if (state == nullptr || ! state->alive) { state = nullptr; pickSource ({}); panel.refresh(); return; }
        // Playhead: where the host is playing in this clip (hidden once playback stops). The song position
        // the host gives this plug-in, mapped into the clip; else where the renderer last played the clip.
        const auto now = Time::getMillisecondCounter();
        double at = -1.0;
        if (now - processor.songStamp.load() < 400)
        {
            const auto& tl = panel.roll.getSnapshot().timeline;
            const double t = processor.songSeconds.load() - tl.songOffset;
            if (t >= 0.0 && t <= panel.roll.getSnapshot().seconds + 0.5) at = t;
        }
        if (at < 0.0 && now - state->playStamp.load() < 400)
            at = state->playPosition.load() / std::max (1.0, state->sampleRate);
        panel.setPlayhead (at);
        // Now and then: the status while listening, and the bars / beats if the song's tempo changed.
        if (++slowTicks % (state->status != 2 ? 15 : 45) == 0 && ! panel.roll.isMouseButtonDown()) panel.refresh();
    }

    HoneyProcessor& processor;
    HoneyDocumentController* dc = nullptr;
    std::shared_ptr<SourceState> state;
    HoneyPanel panel;
    int slowTicks = 0;
};

AudioProcessorEditor* HoneyProcessor::createEditor() { return new HoneyEditor (*this); }

//==============================================================================
const ARA::ARAFactory* JUCE_CALLTYPE createARAFactory()
{
    return ARADocumentControllerSpecialisation::createARAFactory<HoneyDocumentController>();
}

AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new HoneyProcessor();
}
