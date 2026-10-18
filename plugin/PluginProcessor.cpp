#include "PluginProcessor.h"
#include "vox/KeyShare.h"
#include "vox/SongStyle.h"
#include "VoxWebEditor.h"

VoxologyAudioProcessor::VoxologyAudioProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",  juce::AudioChannelSet::mono(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)
                          .withInput  ("Beat (side-chain)", juce::AudioChannelSet::stereo(), false)),
      parameters (*this, nullptr, "VoxologyParams", createParameterLayout())
{
    reader.attach (parameters);
    levelMatchParam = parameters.getRawParameterValue ("levelMatch");
    modeParam = parameters.getRawParameterValue ("mode");
    umAmountParam = parameters.getRawParameterValue ("umAmount");
    umFocusParam = parameters.getRawParameterValue ("umFocus");
    keySrcParam = parameters.getRawParameterValue ("ptKeySrc");
    midiParam = parameters.getRawParameterValue ("ptMidi");
    recParam = parameters.getRawParameterValue ("recMode");
    linkSlot = vox::UnmaskLink::instance().claim();
    autoEdit.liveKeySource = [this] (vox::AutoEditSettings& s)
    {
        // The key the audio thread follows (Key on Auto): Auto-Edit keeps it instead of guessing from the voice.
        const int source = meters.bkSource.load();
        s.beatKeyKnown = meters.bkState.load() == 1 && source > 0;
        if (! s.beatKeyKnown) return;
        vox::BeatKey::Result r;
        r.ready = true;
        r.setRoot = meters.bkSet.load();
        r.tonicOffset = meters.bkMode.load();
        vox::followBeatKey (r, 0, s.beatKeyNote, s.beatScale);
        s.beatKeyName = std::string (vox::kNoteNames[static_cast<size_t> (r.tonic())]) + " " + vox::modeName (r.tonicOffset);
        s.beatKeyFromVoice = source == 3;
    };
    autoEdit.unmaskOnBeatSource = [this] { return vox::UnmaskLink::instance().readBeatKey (linkSlot).present; };
    chains.beforeLoad = [this] { autoEdit.takeUndoSnapshot (false); };
    suiteId = static_cast<std::uint64_t> (juce::Random::getSystemRandom().nextInt64()) ^ reinterpret_cast<std::uintptr_t> (this);
    if (suiteId == 0) suiteId = 1;
    daw.setUnpicked (detectedDaw, suite::dawName (detectedDaw));   // until you pick one (here or in Mixology / Masterology)
    startTimerHz (4);
}

namespace
{
    // A state loaded while a different style (or DAW) is already live in the project (a preset, A / B compare) comes
    // back as not picked, so it follows the project instead of switching all three plug-ins.
    void followLiveChoice (suite::SharedChoice& c, suite::Choice which)
    {
        suite::StyleInfo live;
        if (c.style().pickedAt != 0 && suite::Link::process().readChoice (which, live, suite::nowMs()) && live.key != c.style().key)
            c.setUnpicked (c.style().key, c.style().name);
    }
}

VoxologyAudioProcessor::~VoxologyAudioProcessor()
{
    stopTimer();
    cancelPendingUpdate();
    vox::UnmaskLink::instance().release (linkSlot);
    suite::Link::process().releaseVocal (suiteSlot, suiteId);
    const juce::ScopedLock sl (styleLock);
    songStyle.leave (suite::Link::process(), suiteId);
    daw.leave (suite::Link::process(), suiteId);
}

void VoxologyAudioProcessor::storeBeatMeters (const vox::BeatKey::Result& r) noexcept
{
    meters.bkKey.store (r.tonic());
    meters.bkMode.store (r.tonicOffset);
    meters.bkSet.store (r.setRoot);
    meters.bkUnclear.store (r.unclear ? 1 : 0);
    meters.bkOpen.store (r.unclear ? r.openNote : -1);
    meters.bkConf.store (static_cast<float> (r.confidence));
    meters.bkTune.store (static_cast<float> (r.tuneCents));
    meters.bkHeard.store (static_cast<float> (r.heardSeconds));
}

void VoxologyAudioProcessor::setUnmaskSourceByName (const juce::String& name)
{
    {
        const juce::ScopedLock sl (styleLock);
        unmaskSourceName = name;
        pendingSourceName = name;
    }
    if (name.isEmpty()) unmaskSource.store (-1);
    resolveUnmaskSource();
}

void VoxologyAudioProcessor::resolveUnmaskSource()
{
    const juce::ScopedLock sl (styleLock);
    if (pendingSourceName.isEmpty()) return;
    for (const auto& s : vox::UnmaskLink::instance().sources())
        if (s.slot != linkSlot && juce::String (s.name) == pendingSourceName)
        {
            unmaskSource.store (s.slot);
            pendingSourceName.clear();
            return;
        }
}

void VoxologyAudioProcessor::releaseResources()
{
    suitePresence.stop();   // off the suite link at the next tick
}

void VoxologyAudioProcessor::leaveSuite()
{
    // Mixology stops seeing this vocal at once, and a style or DAW only this instance kept alive lapses (another
    // project, or the next one opened, doesn't take it).
    auto& link = suite::Link::process();
    link.releaseVocal (suiteSlot, suiteId);
    suiteSlot = -1;
    const juce::ScopedLock sl (styleLock);
    songStyle.leave (link, suiteId);
    daw.leave (link, suiteId);
    mixology = {};
}

void VoxologyAudioProcessor::timerCallback()
{
    // Only while the host runs this Voxology: a project that is open but not active keeps its timer running, and
    // must neither take another song's style nor look present to Mixology. It joins again at the next prepareToPlay.
    if (suitePresence.update (suite::nowMs()) == suite::Presence::Step::leave)
        leaveSuite();
    publishSuite();   // four times a second (its heartbeat)
    if (++timerTicks % 4 != 0) return;

    resolveUnmaskSource();   // a saved choice: wait for that vocal's Voxology to appear (project loading)

    // Share a Voxology-on-the-beat's key with Honey Tune in this host (it can't see the link: another plug-in file).
    const auto bk = vox::UnmaskLink::instance().readBeatKey (-1);
    if (bk.present && bk.beat.ready && bk.beat.confidence >= kBeatKeySure && suitePresence.takingPart())
    {
        int key = 0, scale = 1;
        vox::followBeatKey (bk.beat, 0, key, scale);
        vox::keyshare::publish (key, scale, bk.beat.confidence);
    }
}

void VoxologyAudioProcessor::updateTrackProperties (const TrackProperties& properties)
{
    // The AU wrapper can call this from a property listener off the message thread: the name goes over there.
    if (! juce::MessageManager::existsAndIsCurrentThread())
    {
        juce::MessageManager::callAsync ([safe = juce::WeakReference<VoxologyAudioProcessor> (this), properties] {
            if (auto* p = safe.get()) p->updateTrackProperties (properties);
        });
        return;
    }
    if (properties.name.has_value() && properties.name->isNotEmpty())
    {
        vox::UnmaskLink::instance().setName (linkSlot, properties.name->toStdString());
        trackName = *properties.name;
        publishSuite();
    }
}

void VoxologyAudioProcessor::publishSuite()
{
    if (! suitePresence.takingPart()) return;
    auto& link = suite::Link::process();
    const auto now = suite::nowMs();
    suite::MixInfo mix;
    const bool mixologyHere = link.readMix (mix, now);
    // The song's style: the newest choice in Voxology, Mixology or Masterology wins (from a moment after joining, so
    // the plug-ins of a project just switched away from have left first).
    bool adopted = false;
    if (suitePresence.settled (now))
    {
        const juce::ScopedLock sl (styleLock);
        adopted = songStyle.sync (link, suiteId, now);
        if (daw.sync (link, suiteId, now)) ++styleVersion;
    }
    if (adopted)
    {
        ++styleVersion;
        suggestVocalStyle();
    }
    if (suiteSlot < 0) suiteSlot = link.claimVocal (suiteId, now);
    if (suiteSlot < 0) return;   // 64 vocals already: try again later

    vox::ChainParams p;
    reader.read (p, hostBpm.load());
    using F = suite::VocalInfo;
    F v;
    v.trackName = trackName.toStdString();
    v.version = static_cast<std::uint32_t> (JucePlugin_VersionCode);
    if (! p.bypass && ! isHostBypassed()) v.flags |= F::processing;   // (the DAW's own bypass too)
    if (isBeatMode())
        v.flags |= F::beat;   // on the beat: only listening for the key (and Unmask)
    else
    {
        auto any = [] (const auto& gains) { return std::any_of (gains.begin(), gains.end(), [] (double g) { return std::abs (g) >= 0.5; }); };
        if (! vox::VocalCompressor::isNeutral (p.comp) || ! vox::Rider::isNeutral (p.rider)) v.flags |= F::compressor;
        if (! vox::DeEsser::isNeutral (p.deEsser)) v.flags |= F::deEsser;
        if (p.reverb.enabled && (p.reverb.mix >= 5.0 || p.throws.wash >= 5.0)) v.flags |= F::reverb;   // (percent)
        if (p.delay.enabled && (p.delay.mix >= 5.0 || p.throws.echo >= 5.0)) v.flags |= F::delay;
        if (! vox::Doubler::isNeutral (p.doubler) || (p.doubler.enabled && ! vox::HarmonyVoices::isNeutral (p.voices))) v.flags |= F::doubler;
        if (! vox::PitchCorrector::isNeutral (p.pitch)) v.flags |= F::pitch;
        if (p.eq.enabled && any (p.eq.gainDb)) v.flags |= F::eq;
        if (! vox::Saturation::isNeutral (p.saturation)) v.flags |= F::saturation;
        if (p.cleanup.enabled && p.cleanup.gateRangeDb >= 0.05) v.flags |= F::gate;
        const auto style = juce::roundToInt (parameters.getRawParameterValue ("aeStyle")->load());
        v.style = static_cast<F::Style> (static_cast<std::uint32_t> (juce::jlimit (0, static_cast<int> (F::kStyles) - 1, style)) + 1u);
        v.reverbMix = p.reverb.enabled ? static_cast<float> (p.reverb.mix / 100.0) : 0.0f;
        v.delayMix = p.delay.enabled ? static_cast<float> (p.delay.mix / 100.0) : 0.0f;
        // What the last Auto-Edit heard in the recording itself: Mixology's tools say fix it first. Once the vocal
        // has come in for 20 s with no overs and no clipped stretch since that report, it was fixed at the source
        // (clip gain lowered): no longer told.
        v.recordedPeakDb = autoEdit.recordedPeakDb();
        v.clippedSpots = static_cast<std::uint32_t> (std::max (0, autoEdit.recordedClippedSpots()));
        const int runs = meters.clipRuns.load(), overs = meters.overs.load();
        if (v.recordedPeakDb != seenRecordedPeak || static_cast<int> (v.clippedSpots) != seenRecordedSpots
            || runs != seenClipRuns || overs != seenOvers)
        {
            seenRecordedPeak = v.recordedPeakDb;
            seenRecordedSpots = static_cast<int> (v.clippedSpots);
            seenClipRuns = runs;
            seenOvers = overs;
            cleanVoiceSeconds = 0.0;
        }
        else if (meters.inShort.load() > -45.0f)
            cleanVoiceSeconds += 0.25;   // four times a second, while the vocal comes in
        const bool fixedSince = cleanVoiceSeconds >= 20.0;
        if (! fixedSince && v.recordedPeakDb > 0.0f) v.flags |= F::recordedHot;      // the same rules as Auto-Edit's
        if (! fixedSince && v.clippedSpots >= 3) v.flags |= F::recordedClipped;      // TOO HOT and CLIPPED RECORDING notes
    }
    // How much the compressor takes off lately (its loudest moments fall away over a few seconds).
    suiteCompDb = std::max (meters.suiteGr.exchange (0.0f), suiteCompDb * 0.9f);
    v.compressionDb = (v.flags & F::compressor) != 0 ? suiteCompDb : 0.0f;
    if (! link.publishVocal (suiteSlot, suiteId, v, now)) suiteSlot = -1;   // taken over while this one was silent: claim again

    // Mixology's answer for this vocal: the footer says "MIXOLOGY ✓" only when it found the track.
    MixologyView view;
    view.here = mixologyHere;
    if (suiteSlot >= 0)
        view.answered = link.vocalAck (suiteSlot, suiteId, v.trackName.empty() ? 0u : suite::nameHash (v.trackName), view.ack, now);
    const juce::ScopedLock sl (styleLock);
    mixology = view;
}

void VoxologyAudioProcessor::pickStyle (const std::string& key, const std::string& name)
{
    {
        const juce::ScopedLock sl (styleLock);
        if (key == songStyle.style().key) return;
        songStyle.pick (key, name);
    }
    ++styleVersion;
    suggestVocalStyle();
    publishSuite();   // the others follow at once
}

void VoxologyAudioProcessor::pickDaw (const std::string& key)
{
    {
        const juce::ScopedLock sl (styleLock);
        // "Found: <your DAW>" ("") is a fresh pick of the DAW found from the host, so every plug-in follows it.
        // (Going back to "not picked" was undone at once: this instance itself kept the earlier pick alive.)
        const auto& wanted = key.empty() ? detectedDaw : key;
        if (wanted == daw.style().key && daw.style().pickedAt != 0) return;
        daw.pick (wanted, suite::dawName (wanted));
    }
    ++styleVersion;
    publishSuite();   // the others follow at once
}

void VoxologyAudioProcessor::suggestVocalStyle()
{
    // A song can have rap verses and a melodic hook: a vocal style you picked on this track stays.
    if (vocalStyleMine.load() || isBeatMode()) return;
    const int v = vox::songstyle::vocalStyleFor (songStyleNow().key);
    if (v < 0) return;
    auto* p = parameters.getParameter ("aeStyle");
    const float norm = p->convertTo0to1 (static_cast<float> (v));
    if (std::abs (p->getValue() - norm) > 1e-4f)
    {
        p->beginChangeGesture();   // a host writing automation records it as a change, not a stray value
        p->setValueNotifyingHost (norm);
        p->endChangeGesture();
    }
}

juce::AudioProcessorValueTreeState::ParameterLayout VoxologyAudioProcessor::createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    VoxParams::addTo (layout);
    return layout;
}

bool VoxologyAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    // Mono vocal in, stereo out (the usual), or mono -> mono, or stereo -> stereo.
    const auto in = layouts.getMainInputChannelSet(), out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;
    if (in != juce::AudioChannelSet::mono() && in != juce::AudioChannelSet::stereo())
        return false;
    // The side-chain (the beat, listened to for its key and for Vocal Fit): off, mono or stereo.
    if (layouts.inputBuses.size() > 1)
    {
        const auto sc = layouts.getChannelSet (true, 1);
        if (! sc.isDisabled() && sc != juce::AudioChannelSet::mono() && sc != juce::AudioChannelSet::stereo())
            return false;
    }
    return in.size() <= out.size();
}

void VoxologyAudioProcessor::prepareToPlay (double sampleRate, int)
{
    const int channels = juce::jmax (1, getTotalNumOutputChannels());
    // Both chains are made fresh here: let a clean on the message thread finish first, and keep the next one out.
    for (int s = standby.load(); s == standbyCleaning || ! standby.compare_exchange_weak (s, standbyCleaning);)
        if (s == standbyCleaning) { juce::Thread::yield(); s = standby.load(); }
    reader.read (chainParams, hostBpm.load());
    chain.setParams (chainParams);
    chain.prepare (sampleRate, juce::jmin (channels, vox::kMaxChannels));
    recChain.setParams (chainParams);
    recChain.prepare (sampleRate, juce::jmin (channels, vox::kMaxChannels), true);
    usingRec = recParam->load() > 0.5f && ! isBeatMode();
    switchPos = -1;
    standby.store (standbyClean);
    inMeter.prepare (sampleRate, juce::jmax (1, getTotalNumInputChannels()));
    outMeter.prepare (sampleRate, channels);
    levelMatch.prepare (sampleRate);
    appliedMatchDb = 0.0;
    autoEdit.prepare (sampleRate);
    preparedRate = sampleRate;
    vocalBands.prepare (sampleRate);
    hopCount = 0;
    unmask.prepare (sampleRate, juce::jmin (channels, vox::kMaxChannels));
    beatKey.prepare (sampleRate);
    sideKey.prepare (sampleRate);
    voiceKey.reset();
    // The same in VOCAL and BEAT, so beat and vocal stay lined up (Record mode: much less, while recording).
    wantedLatency = active().latencySamples();
    setLatencySamples (wantedLatency.load());
    suitePresence.start();   // on the suite link from the next tick
}

void VoxologyAudioProcessor::handleAsyncUpdate()
{
    setLatencySamples (wantedLatency.load());
    cleanStandby();   // the chain Record mode just left: ready for the next switch
}

void VoxologyAudioProcessor::cleanStandby() noexcept
{
    // Only from dirty: never while the audio thread may be using it (a switch starts only once it's clean).
    if (int s = standbyDirty; ! standby.compare_exchange_strong (s, standbyCleaning))
        return;
    standbyChain().reset();
    standby.store (standbyClean);
}

template <typename Sample>
void VoxologyAudioProcessor::switchChains (juce::AudioBuffer<Sample>& buffer, int nch, int n) noexcept
{
    // Both chains hear the vocal; the new one (fresh) plays once it's had its latency (+ 10 ms to settle), crossfaded
    // in over 20 ms (equal power: the two are a latency apart). Then the old one is left to the message thread.
    auto& from = active();
    auto& to = standbyChain();
    to.setParams (chainParams);
    const int warm = to.latencySamples() + juce::roundToInt (0.010 * preparedRate);
    const int fade = juce::jmax (1, juce::roundToInt (0.020 * preparedRate));
    constexpr int kPart = 256;
    std::array<std::array<Sample, kPart>, vox::kMaxChannels> copy;
    for (int start = 0; start < n; start += kPart)
    {
        const int len = juce::jmin (kPart, n - start);
        std::array<Sample*, vox::kMaxChannels> a {}, b {};
        for (int c = 0; c < nch; ++c)
        {
            a[static_cast<size_t> (c)] = buffer.getWritePointer (c, start);
            b[static_cast<size_t> (c)] = copy[static_cast<size_t> (c)].data();
            std::copy_n (a[static_cast<size_t> (c)], len, b[static_cast<size_t> (c)]);
        }
        from.process (a.data(), nch, len);
        to.process (b.data(), nch, len);
        for (int i = 0; i < len; ++i, ++switchPos)
        {
            if (switchPos < warm) continue;
            const double t = juce::jmin (1.0, (switchPos - warm + 1) / static_cast<double> (fade));
            const double in = std::sin (0.5 * juce::MathConstants<double>::pi * t), out = std::cos (0.5 * juce::MathConstants<double>::pi * t);
            for (int c = 0; c < nch; ++c)
            {
                auto& y = a[static_cast<size_t> (c)][i];
                y = static_cast<Sample> (out * static_cast<double> (y) + in * static_cast<double> (b[static_cast<size_t> (c)][i]));
            }
        }
    }
    if (switchPos < warm + fade)
        return;
    usingRec = ! usingRec.load();
    switchPos = -1;
    standby.store (standbyDirty);
    wantedLatency = active().latencySamples();
    triggerAsyncUpdate();   // the host hears of the new latency; the old chain is cleaned
}

template <typename Sample>
void VoxologyAudioProcessor::processAnyPrecision (juce::AudioBuffer<Sample>& buffer, const juce::MidiBuffer& midi, bool hostBypass)
{
    juce::ScopedNoDenormals noDenormals;
    hostBypassed.store (hostBypass, std::memory_order_relaxed);
    const int numIn = getMainBusNumInputChannels();
    const int numOut = getTotalNumOutputChannels();
    const int n = buffer.getNumSamples();
    const int nch = juce::jmin (buffer.getNumChannels(), numOut, vox::kMaxChannels);
    const bool beatMode = isBeatMode();

    // A bad sample from the host (NaN / inf) comes in as silence: in a filter, a meter or a key detector it would stay
    // until the next prepare.
    for (int c = 0; c < buffer.getNumChannels(); ++c)
    {
        auto* x = buffer.getWritePointer (c);
        for (int i = 0; i < n; ++i)
            if (! std::isfinite (x[i])) x[i] = Sample (0);
    }

    // The side-chain (VOCAL: the beat, for its key and Vocal Fit). Read it first: its channels share the buffer with
    // the outputs, which the lines below write.
    const bool sideOn = ! beatMode && sideChainOn();
    if (sideOn)
    {
        const auto sc = getBusBuffer (buffer, true, 1);
        const int scn = sc.getNumChannels();
        float scPeak = 0.0f;
        for (int c = 0; c < scn; ++c) scPeak = juce::jmax (scPeak, static_cast<float> (sc.getMagnitude (c, 0, n)));
        sideQuietSeconds = scPeak > 1.0e-4f ? 0.0 : sideQuietSeconds + n / juce::jmax (1.0, preparedRate);   // -80 dBFS
        std::array<double, kHop> mono {};
        for (int s = 0; s < n && scn > 0; s += kHop)
        {
            const int len = juce::jmin (kHop, n - s);
            for (int i = 0; i < len; ++i)
            {
                double m = 0.0;
                for (int c = 0; c < scn; ++c) m += static_cast<double> (sc.getReadPointer (c)[s + i]);
                mono[static_cast<size_t> (i)] = m / scn;
            }
            sideKey.process (mono.data(), len);
        }
    }

    // Auto-Edit records the vocal, and the beat for Vocal Fit, here: the side-chain's channels are still intact.
    if (! beatMode)
    {
        const auto side = sideOn ? getBusBuffer (buffer, true, 1) : juce::AudioBuffer<Sample>();
        autoEdit.setOffline (isNonRealtime());   // Auto Analyze doesn't listen to a bounce
        autoEdit.capture (buffer, numIn, sideOn ? &side : nullptr);
    }

    // A mono vocal feeds both sides; any other extra output channel starts silent.
    if (numIn == 1 && numOut >= 2 && buffer.getNumChannels() >= 2)
        buffer.copyFrom (1, 0, buffer, 0, 0, n);
    for (int c = juce::jmax (numIn, numIn == 1 ? 2 : numIn); c < buffer.getNumChannels(); ++c)
        buffer.clear (c, 0, n);
    if (nch <= 0)
        return;

    int64_t songPos = vox::UnmaskLink::kNoPosition;
    if (auto* hostPlayHead = getPlayHead())
        if (auto position = hostPlayHead->getPosition())
        {
            if (auto bpm = position->getBpm())
                hostBpm.store (*bpm);
            if (position->getIsPlaying())
                if (auto t = position->getTimeInSamples())
                    songPos = *t;
        }

    double inputPeak = 0.0;
    for (int c = 0; c < juce::jmin (numIn, buffer.getNumChannels()); ++c)
        inputPeak = juce::jmax (inputPeak, static_cast<double> (buffer.getMagnitude (c, 0, n)));
    inputTap.push (buffer.getArrayOfReadPointers(), juce::jmin (numIn, buffer.getNumChannels()), n);
    inMeter.process (buffer.getArrayOfReadPointers(), juce::jmin (numIn, buffer.getNumChannels()), n);

    // MATCH: level-matched A/B, learned while B (the processed vocal) plays; never in an offline export.
    reader.read (chainParams, hostBpm.load());
    if (hostBypass) chainParams.bypass = true;   // the DAW's bypass: Voxology's own (delayed dry, glided)
    const bool listeningB = ! chainParams.bypass && ! chainParams.listenOriginal;
    const bool matching = levelMatchParam->load() > 0.5f && ! isNonRealtime();
    // The output meter reads after the MATCH gain; take that gain back out before comparing.
    levelMatch.update (inMeter.shortTermLufs(), outMeter.shortTermLufs() - (listeningB ? appliedMatchDb : 0.0), inputPeak, n, listeningB);
    chainParams.gainProcessedDb = matching ? levelMatch.gainForBDb() : 0.0;
    chainParams.gainOriginalDb = matching ? levelMatch.gainForADb() : 0.0;
    appliedMatchDb = chainParams.gainProcessedDb;
    const bool wantUnmask = ! chainParams.bypass && ! chainParams.listenOriginal;   // A / B and bypass hear the beat untouched
    auto& link = vox::UnmaskLink::instance();
    // MIDI: the notes held, in both modes (a note let go while on the beat would otherwise stay held in VOCAL).
    handleMidi (midi);
    if (beatMode)
    {
        // BEAT: learn the beat's key and tuning from what comes in, and offer it to the vocals.
        if (! wasBeat) beatKey.reset();
        std::array<double, kHop> mono {};
        for (int s = 0; s < n; s += kHop)
        {
            const int len = juce::jmin (kHop, n - s);
            for (int i = 0; i < len; ++i)
            {
                double m = 0.0;
                for (int c = 0; c < nch; ++c) m += static_cast<double> (buffer.getReadPointer (c)[s + i]);
                mono[static_cast<size_t> (i)] = m / nch;
            }
            beatKey.process (mono.data(), len);
        }
        const auto r = beatKey.result();
        link.publishKey (linkSlot, { true, r });
        meters.bkState.store (r.ready ? 5 : 4);
        storeBeatMeters (r);
    }
    else
    {
        if (wasBeat) { link.clearKey (linkSlot); voiceKey.reset(); }
        // VOCAL, Key on Auto: Pitch follows the surest key it can hear: the beat on the side-chain, then a
        // Voxology on the beat, then the voice itself (Key / Scale stay as the fallback).
        int state = 0, source = 0;
        if (keySrcParam->load() < 0.5f)
        {
            const auto bk = link.readBeatKey (linkSlot);
            const auto side = sideKey.result();
            const auto voice = voiceKey.result();
            const bool sideHeard = sideOn && side.heardSeconds > 0.0;
            // Start following once sure; keep following unless it gets much less sure (no flip-flopping).
            auto sure = [this] (const vox::BeatKey::Result& r, int src) { return r.ready && r.confidence >= (followSource == src ? kBeatKeySure - 0.2 : kBeatKeySure); };
            const vox::BeatKey::Result* use = nullptr;
            if (sideHeard && sure (side, 2)) { use = &side; source = 2; }
            else if (bk.present && sure (bk.beat, 1)) { use = &bk.beat; source = 1; }
            else if (sure (voice, 3)) { use = &voice; source = 3; }
            if (use != nullptr)
            {
                state = 1;
                vox::followBeatKey (*use, chainParams.pitch.scale, chainParams.pitch.key, chainParams.pitch.scale, &chainParams.pitch.extraNotes);
                chainParams.pitch.tuneCents = use->tuneCents;
                storeBeatMeters (*use);
            }
            else
            {
                // Not sure yet: show what it's listening to (a beat before the voice).
                use = sideHeard ? &side : bk.present ? &bk.beat : voice.heardSeconds > 0.0 ? &voice : nullptr;
                source = sideHeard ? 2 : bk.present ? 1 : use != nullptr ? 3 : 0;
                state = use != nullptr ? 3 : 2;
                if (use != nullptr) storeBeatMeters (*use);
            }
        }
        followSource = state == 1 ? source : 0;
        meters.scState.store (! sideOn ? 0 : sideQuietSeconds > 2.0 ? 1 : 2);
        meters.bkState.store (state);
        meters.bkSource.store (source);
        // MIDI: Notes = only the notes held right now (none held: the key as usual); Learn = the last notes played.
        int held = 0;
        for (int k = 0; k < 128; ++k) if (midiHeld[static_cast<size_t> (k)] > 0) held |= 1 << (k % 12);
        const int midiMode = juce::roundToInt (midiParam->load());
        chainParams.pitch.onlyNotes = midiMode == 1 ? held : midiMode == 2 ? midiLearned.load() : 0;
        // Voices MIDI: the voices sing the notes held right now, lowest first (up to kMaxVoices).
        chainParams.voices.notes.fill (-1);
        int heldNow = 0;
        for (int k = 0; k < 128; ++k)
            if (midiHeld[static_cast<size_t> (k)] > 0)
            {
                if (chainParams.voices.midi && heldNow < vox::kMaxVoices) chainParams.voices.notes[static_cast<size_t> (heldNow)] = k;
                ++heldNow;
            }
        meters.heldNotes.store (heldNow);
        meters.midiNotes.store (midiMode == 2 ? midiLearned.load() : held);
        const auto& pp = chainParams.pitch;
        meters.notesUsed.store (vox::PitchCorrector::allowedMask (pp.key, pp.scale, pp.extraNotes, pp.onlyNotes, pp.removedNotes));
        meters.keyUsed.store (chainParams.pitch.key);
        meters.scaleUsed.store (chainParams.pitch.scale);
    }
    wasBeat = beatMode;
    if (beatMode)
    {
        // BEAT: the vocal chain stays out (its delayed dry path keeps the latency the same in both modes).
        chainParams.bypass = true;
        chainParams.listenOriginal = false;
    }
    // Record mode: the low-latency chain. Switching starts the other chain fresh (cleaned on the message thread) and
    // crossfades to it once it hears the vocal; then the host hears of the new latency.
    if (const bool rec = recParam->load() > 0.5f && ! beatMode; switchPos < 0 && rec != usingRec.load())
    {
        if (isNonRealtime()) cleanStandby();   // (a bounce: no message thread to wait for)
        if (standby.load() == standbyClean) switchPos = 0;
        else triggerAsyncUpdate();             // (still to be cleaned: next block)
    }
    auto& live = active();
    live.setParams (chainParams);
    if (switchPos >= 0) switchChains (buffer, nch, n);
    else live.process (buffer.getArrayOfWritePointers(), nch, n);
    meters.recMode.store (usingRec.load() ? 1 : 0);
    meters.latencyMs10.store (juce::roundToInt (10000.0 * active().latencySamples() / juce::jmax (1.0, preparedRate)));

    // Positions of what comes out of this block (both modes have the same latency, so they line up).
    const int lat = live.latencySamples();
    if (! beatMode)
    {
        // VOCAL: publish the processed vocal's band levels every 128 samples.
        for (int i = 0; i < n; ++i)
        {
            double m = 0.0;
            for (int c = 0; c < nch; ++c) m += static_cast<double> (buffer.getReadPointer (c)[i]);
            vocalBands.process (m / nch);
            if (++hopCount >= kHop)
            {
                hopCount = 0;
                link.publish (linkSlot, songPos == vox::UnmaskLink::kNoPosition ? songPos : songPos + i - lat, vocalBands.levelsDb());
            }
        }
    }
    else
    {
        // BEAT: dip where the vocal sings, read for the same song position, a hop at a time.
        unmask.setParams ({ wantUnmask ? static_cast<double> (umAmountParam->load()) : 0.0, umFocusParam->load() < 0.5f });
        const int chosen = unmaskSource.load();
        const auto maxAge = static_cast<int64_t> (0.25 * preparedRate);   // (a quarter second of song: older is stale)
        int heard = 0;
        std::array<float, vox::kUnmaskBands> db {}, one {};
        std::array<std::array<double, kHop>, vox::kMaxChannels> tmp {};
        for (int s = 0; s < n; s += kHop)
        {
            const int len = juce::jmin (kHop, n - s);
            const int64_t pos = songPos == vox::UnmaskLink::kNoPosition ? songPos : songPos + s - lat;
            db.fill (static_cast<float> (vox::kUnmaskSilentDb));
            int found = 0;
            for (int slot = 0; slot < vox::UnmaskLink::kSlots; ++slot)
            {
                if (slot == linkSlot || (chosen >= 0 && slot != chosen)) continue;
                if (! link.read (slot, pos, maxAge, one)) continue;
                ++found;
                for (size_t b = 0; b < db.size(); ++b) db[b] = juce::jmax (db[b], one[b]);   // every vocal: the loudest
            }
            heard = juce::jmax (heard, found);
            std::array<double*, vox::kMaxChannels> ptr {};
            for (int c = 0; c < nch; ++c)
            {
                ptr[static_cast<size_t> (c)] = tmp[static_cast<size_t> (c)].data();
                for (int i = 0; i < len; ++i) tmp[static_cast<size_t> (c)][static_cast<size_t> (i)] = static_cast<double> (buffer.getReadPointer (c)[s + i]);
            }
            unmask.process (ptr.data(), nch, len, found > 0 ? db.data() : nullptr);
            for (int c = 0; c < nch; ++c)
                for (int i = 0; i < len; ++i) buffer.getWritePointer (c)[s + i] = static_cast<Sample> (tmp[static_cast<size_t> (c)][static_cast<size_t> (i)]);
        }
        meters.umLink.store (heard);
        for (size_t b = 0; b < db.size(); ++b) meters.umVocal[b].store (db[b]);
        const auto dips = unmask.takeDipDb();
        for (size_t b = 0; b < dips.size(); ++b) holdMin (meters.umDip[b], static_cast<float> (dips[b]));
    }

    outMeter.process (buffer.getArrayOfReadPointers(), nch, n);
    outputTap.push (buffer.getArrayOfReadPointers(), nch, n);

    auto clampDb = [] (double v) { return static_cast<float> (std::isfinite (v) ? juce::jmax (v, -100.0) : -100.0); };
    meters.inShort.store (clampDb (inMeter.shortTermLufs()));
    meters.outShort.store (clampDb (outMeter.shortTermLufs()));
    holdMax (meters.inPeak, clampDb (inputPeak > 0.0 ? 20.0 * std::log10 (inputPeak) : -100.0));
    double outPeak = 0.0;
    for (int c = 0; c < nch; ++c) outPeak = juce::jmax (outPeak, static_cast<double> (buffer.getMagnitude (c, 0, n)));
    holdMax (meters.outPeak, clampDb (outPeak > 0.0 ? 20.0 * std::log10 (outPeak) : -100.0));
    const auto m = live.takeMeters();
    if (! beatMode)
        voiceKey.add (m.pitch.voiced, m.pitch.sungMidi, m.pitch.clarity, n / juce::jmax (1.0, preparedRate));
    holdMin (meters.gate, static_cast<float> (m.gateDb));
    holdMin (meters.pops, static_cast<float> (m.popDb));
    holdMin (meters.breath, static_cast<float> (m.breathDb));
    holdMin (meters.noise, static_cast<float> (m.noiseDb));
    holdMin (meters.smooth, static_cast<float> (m.smoothDb));
    holdMin (meters.click, static_cast<float> (m.clickDb));
    meters.clicks.fetch_add (m.clicks);
    meters.clipRuns.fetch_add (m.input.runs);
    meters.overs.fetch_add (m.input.overs);
    holdMin (meters.deEss, static_cast<float> (m.deEssDb));
    for (size_t b = 0; b < meters.dynEq.size(); ++b) holdMin (meters.dynEq[b], static_cast<float> (m.dynEqDb[b]));
    meters.rider.store (static_cast<float> (m.riderDb));
    holdMin (meters.peakGr, static_cast<float> (m.peakGrDb));
    holdMin (meters.levelGr, static_cast<float> (m.levelGrDb));
    holdMax (meters.suiteGr, static_cast<float> (-(m.peakGrDb + m.levelGrDb)));
    addTo (meters.satResidual, static_cast<float> (m.satResidual));
    addTo (meters.satSignal, static_cast<float> (m.satSignal));
    meters.matchDb.store (static_cast<float> (chainParams.listenOriginal ? levelMatch.gainForADb() : levelMatch.gainForBDb()));
    meters.bpm.store (static_cast<float> (hostBpm.load()));
    meters.pitchSung.store (m.pitch.voiced ? static_cast<float> (m.pitch.sungMidi) : 0.0f);
    meters.pitchTarget.store (m.pitch.voiced ? m.pitch.targetMidi : -1);
    meters.pitchCorr.store (static_cast<float> (m.pitch.correction));
    for (size_t k = 0; k < meters.hvNotes.size(); ++k) meters.hvNotes[k].store (m.voiceNotes[k]);
}

void VoxologyAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)  { processAnyPrecision (buffer, midi, false); }
void VoxologyAudioProcessor::processBlock (juce::AudioBuffer<double>& buffer, juce::MidiBuffer& midi) { processAnyPrecision (buffer, midi, false); }
void VoxologyAudioProcessor::processBlockBypassed (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)  { processAnyPrecision (buffer, midi, true); }
void VoxologyAudioProcessor::processBlockBypassed (juce::AudioBuffer<double>& buffer, juce::MidiBuffer& midi) { processAnyPrecision (buffer, midi, true); }

void VoxologyAudioProcessor::handleMidi (const juce::MidiBuffer& midi) noexcept
{
    for (const auto meta : midi)
    {
        const auto m = meta.getMessage();
        if (m.isNoteOn())
        {
            // Learn: a new chord / phrase (played after letting go of everything) starts a new scale.
            const int prev = midiHeldCount == 0 ? 0 : midiLearned.load();
            midiLearned.store (prev | (1 << (m.getNoteNumber() % 12)));
            ++midiHeld[static_cast<size_t> (m.getNoteNumber())];
            ++midiHeldCount;
        }
        else if (m.isNoteOff() && midiHeld[static_cast<size_t> (m.getNoteNumber())] > 0)
        {
            --midiHeld[static_cast<size_t> (m.getNoteNumber())];
            --midiHeldCount;
        }
        else if (m.isAllNotesOff() || m.isAllSoundOff())
        {
            midiHeld.fill (0);
            midiHeldCount = 0;
        }
    }
}

void VoxologyAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = parameters.copyState().createXml())
    {
        xml->setAttribute ("aeReport", autoEdit.getReportForSaving());
        xml->setAttribute ("aeReference", autoEdit.getReferenceForSaving());
        xml->setAttribute ("umSource", getUnmaskSourceName());
        xml->setAttribute ("midiLearned", midiLearned.load());
        xml->setAttribute ("uiRed", uiRed.load() ? 1 : 0);
        xml->setAttribute ("chainName", getChainName());
        xml->setAttribute ("aeWatch", autoEdit.isWatchOn() ? 1 : 0);
        {
            // The song's style (shared with Mixology and Masterology) + when it was picked; and whether you picked
            // this track's vocal style yourself.
            const juce::ScopedLock sl (styleLock);
            xml->setAttribute ("songStyle", juce::String::fromUTF8 (songStyle.save().c_str()));
            xml->setAttribute ("vsMine", vocalStyleMine.load() ? 1 : 0);
            if (daw.style().pickedAt != 0)   // your DAW, when you picked one (else it is found from the host each time)
                xml->setAttribute ("daw", juce::String::fromUTF8 (daw.save().c_str()));
        }
        copyXmlToBinary (*xml, destData);
    }
}

void VoxologyAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (parameters.state.getType()))
        {
            const auto savedReport = xml->getStringAttribute ("aeReport");
            const auto savedReference = xml->getStringAttribute ("aeReference");
            const bool watch = xml->getIntAttribute ("aeWatch", 0) != 0;
            xml->removeAttribute ("aeReport");
            xml->removeAttribute ("aeReference");
            setUnmaskSourceByName (xml->getStringAttribute ("umSource"));
            midiLearned.store (xml->getIntAttribute ("midiLearned", 0) & 0xFFF);
            xml->removeAttribute ("umSource");
            uiRed.store (xml->getIntAttribute ("uiRed", 0) != 0);
            setChainName (xml->getStringAttribute ("chainName"));
            xml->removeAttribute ("chainName");
            xml->removeAttribute ("aeWatch");
            {
                // The song's style. A project from before it existed keeps its vocal style as it was (you chose it).
                const bool hasStyle = xml->hasAttribute ("songStyle");
                const juce::ScopedLock sl (styleLock);
                songStyle = suite::SongStyle { suite::StyleInfo::From::voxology };
                songStyle.restore (xml->getStringAttribute ("songStyle").toStdString());
                followLiveChoice (songStyle, suite::Choice::style);   // a preset doesn't change the song's style
                vocalStyleMine.store (hasStyle ? xml->getIntAttribute ("vsMine", 0) != 0 : true);
                ++styleVersion;
            }
            xml->removeAttribute ("songStyle");
            {
                const juce::ScopedLock sl (styleLock);
                daw = suite::SharedChoice { suite::StyleInfo::From::voxology, suite::Choice::daw };
                if (! daw.restore (xml->getStringAttribute ("daw").toStdString()))
                    daw.setUnpicked (detectedDaw, suite::dawName (detectedDaw));
                followLiveChoice (daw, suite::Choice::daw);
            }
            xml->removeAttribute ("daw");
            xml->removeAttribute ("vsMine");
            xml->removeAttribute ("midiLearned");
            xml->removeAttribute ("uiRed");
            parameters.replaceState (juce::ValueTree::fromXml (*xml));
            // Auto-Edit's report, reference and Auto Analyze live on the message thread (its timer reads them):
            // a host loading the project on another thread hands them over.
            auto restoreAutoEdit = [this, savedReport, savedReference, watch] {
                autoEdit.restoreReference (savedReference);
                autoEdit.setWatchOn (watch);
                autoEdit.onStateRestored (savedReport);
            };
            if (juce::MessageManager::existsAndIsCurrentThread()) restoreAutoEdit();
            else juce::MessageManager::callAsync ([safe = juce::WeakReference<VoxologyAudioProcessor> (this), restoreAutoEdit] {
                if (safe.get() != nullptr) restoreAutoEdit();
            });
        }
}

juce::AudioProcessorEditor* VoxologyAudioProcessor::createEditor()
{
    // The web UI; a plain list of controls if no web view is available (Windows without WebView2).
    if (VoxWebEditor::isSupported())
        return new VoxWebEditor (*this);
    return new juce::GenericAudioProcessorEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new VoxologyAudioProcessor();
}
