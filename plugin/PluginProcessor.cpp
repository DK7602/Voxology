#include "PluginProcessor.h"
#include "VoxWebEditor.h"

VoxologyAudioProcessor::VoxologyAudioProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",  juce::AudioChannelSet::mono(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
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
    startTimerHz (1);
}

VoxologyAudioProcessor::~VoxologyAudioProcessor()
{
    stopTimer();
    cancelPendingUpdate();
    vox::UnmaskLink::instance().release (linkSlot);
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
    unmaskSourceName = name;
    pendingSourceName = name;
    if (name.isEmpty()) unmaskSource.store (-1);
    resolveUnmaskSource();
}

void VoxologyAudioProcessor::resolveUnmaskSource()
{
    if (pendingSourceName.isEmpty()) return;
    for (const auto& s : vox::UnmaskLink::instance().sources())
        if (s.slot != linkSlot && juce::String (s.name) == pendingSourceName)
        {
            unmaskSource.store (s.slot);
            pendingSourceName.clear();
            return;
        }
}

void VoxologyAudioProcessor::timerCallback()
{
    resolveUnmaskSource();   // a saved choice: wait for that vocal's Voxology to appear (project loading)
}

void VoxologyAudioProcessor::updateTrackProperties (const TrackProperties& properties)
{
    if (properties.name.has_value() && properties.name->isNotEmpty())
        vox::UnmaskLink::instance().setName (linkSlot, properties.name->toStdString());
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
    return in.size() <= out.size();
}

void VoxologyAudioProcessor::prepareToPlay (double sampleRate, int)
{
    const int channels = juce::jmax (1, getTotalNumOutputChannels());
    reader.read (chainParams, hostBpm.load());
    chain.setParams (chainParams);
    chain.prepare (sampleRate, juce::jmin (channels, vox::kMaxChannels));
    recChain.setParams (chainParams);
    recChain.prepare (sampleRate, juce::jmin (channels, vox::kMaxChannels), true);
    usingRec = recParam->load() > 0.5f && ! isBeatMode();
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
    // The same in VOCAL and BEAT, so beat and vocal stay lined up (Record mode: much less, while recording).
    wantedLatency = active().latencySamples();
    setLatencySamples (wantedLatency.load());
}

template <typename Sample>
void VoxologyAudioProcessor::processAnyPrecision (juce::AudioBuffer<Sample>& buffer, const juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    const int numIn = getTotalNumInputChannels();
    const int numOut = getTotalNumOutputChannels();
    const int n = buffer.getNumSamples();
    const int nch = juce::jmin (buffer.getNumChannels(), numOut, vox::kMaxChannels);

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
    const bool beatMode = isBeatMode();

    double inputPeak = 0.0;
    for (int c = 0; c < juce::jmin (numIn, buffer.getNumChannels()); ++c)
        inputPeak = juce::jmax (inputPeak, static_cast<double> (buffer.getMagnitude (c, 0, n)));
    if (! beatMode)
        autoEdit.capture (buffer, numIn);
    inputTap.push (buffer.getArrayOfReadPointers(), juce::jmin (numIn, buffer.getNumChannels()), n);
    inMeter.process (buffer.getArrayOfReadPointers(), juce::jmin (numIn, buffer.getNumChannels()), n);

    // MATCH: level-matched A/B, learned while B (the processed vocal) plays; never in an offline export.
    reader.read (chainParams, hostBpm.load());
    const bool listeningB = ! chainParams.bypass && ! chainParams.listenOriginal;
    const bool matching = levelMatchParam->load() > 0.5f && ! isNonRealtime();
    // The output meter reads after the MATCH gain; take that gain back out before comparing.
    levelMatch.update (inMeter.shortTermLufs(), outMeter.shortTermLufs() - (listeningB ? appliedMatchDb : 0.0), inputPeak, n, listeningB);
    chainParams.gainProcessedDb = matching ? levelMatch.gainForBDb() : 0.0;
    chainParams.gainOriginalDb = matching ? levelMatch.gainForADb() : 0.0;
    appliedMatchDb = chainParams.gainProcessedDb;
    const bool wantUnmask = ! chainParams.bypass && ! chainParams.listenOriginal;   // A / B and bypass hear the beat untouched
    auto& link = vox::UnmaskLink::instance();
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
        if (wasBeat) link.clearKey (linkSlot);
        // VOCAL: Pitch follows the surest beat in the project (Key / Scale stay as the fallback).
        int state = 0;
        if (keySrcParam->load() < 0.5f)
        {
            const auto bk = link.readBeatKey (linkSlot);
            // Follow once sure; keep following unless it gets much less sure (no flip-flopping).
            const double need = followingBeat ? kBeatKeySure - 0.2 : kBeatKeySure;
            state = ! bk.present ? 2 : (! bk.beat.ready || bk.beat.confidence < need) ? 3 : 1;
            if (state == 1)
            {
                vox::followBeatKey (bk.beat, chainParams.pitch.scale, chainParams.pitch.key, chainParams.pitch.scale, &chainParams.pitch.extraNotes);
                chainParams.pitch.tuneCents = bk.beat.tuneCents;
            }
            if (bk.present) storeBeatMeters (bk.beat);
        }
        followingBeat = state == 1;
        meters.bkState.store (state);
        // MIDI: Notes = only the notes held right now (none held: the key as usual); Learn = the last notes played.
        handleMidi (midi);
        int held = 0;
        for (int k = 0; k < 128; ++k) if (midiHeld[static_cast<size_t> (k)] > 0) held |= 1 << (k % 12);
        const int midiMode = juce::roundToInt (midiParam->load());
        chainParams.pitch.onlyNotes = midiMode == 1 ? held : midiMode == 2 ? midiLearned.load() : 0;
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
    // Record mode: the low-latency chain (switching starts it fresh; the host hears of the new latency).
    if (const bool rec = recParam->load() > 0.5f && ! beatMode; rec != usingRec)
    {
        usingRec = rec;
        active().reset();
        wantedLatency = active().latencySamples();
        triggerAsyncUpdate();
    }
    meters.recMode.store (usingRec ? 1 : 0);
    meters.latencyMs10.store (juce::roundToInt (10000.0 * active().latencySamples() / juce::jmax (1.0, preparedRate)));
    auto& live = active();
    live.setParams (chainParams);
    live.process (buffer.getArrayOfWritePointers(), nch, n);

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
    holdMin (meters.gate, static_cast<float> (m.gateDb));
    holdMin (meters.pops, static_cast<float> (m.popDb));
    holdMin (meters.breath, static_cast<float> (m.breathDb));
    holdMin (meters.deEss, static_cast<float> (m.deEssDb));
    for (size_t b = 0; b < meters.dynEq.size(); ++b) holdMin (meters.dynEq[b], static_cast<float> (m.dynEqDb[b]));
    meters.rider.store (static_cast<float> (m.riderDb));
    holdMin (meters.peakGr, static_cast<float> (m.peakGrDb));
    holdMin (meters.levelGr, static_cast<float> (m.levelGrDb));
    addTo (meters.satResidual, static_cast<float> (m.satResidual));
    addTo (meters.satSignal, static_cast<float> (m.satSignal));
    meters.matchDb.store (static_cast<float> (chainParams.listenOriginal ? levelMatch.gainForADb() : levelMatch.gainForBDb()));
    meters.bpm.store (static_cast<float> (hostBpm.load()));
    meters.pitchSung.store (m.pitch.voiced ? static_cast<float> (m.pitch.sungMidi) : 0.0f);
    meters.pitchTarget.store (m.pitch.voiced ? m.pitch.targetMidi : -1);
    meters.pitchCorr.store (static_cast<float> (m.pitch.correction));
    meters.hvNotes[0].store (m.voiceNotes[0]);
    meters.hvNotes[1].store (m.voiceNotes[1]);
}

void VoxologyAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)  { processAnyPrecision (buffer, midi); }
void VoxologyAudioProcessor::processBlock (juce::AudioBuffer<double>& buffer, juce::MidiBuffer& midi) { processAnyPrecision (buffer, midi); }

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
        xml->setAttribute ("umSource", unmaskSourceName);
        xml->setAttribute ("midiLearned", midiLearned.load());
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
            xml->removeAttribute ("aeReport");
            xml->removeAttribute ("aeReference");
            autoEdit.restoreReference (savedReference);
            setUnmaskSourceByName (xml->getStringAttribute ("umSource"));
            midiLearned.store (xml->getIntAttribute ("midiLearned", 0) & 0xFFF);
            xml->removeAttribute ("umSource");
            xml->removeAttribute ("midiLearned");
            parameters.replaceState (juce::ValueTree::fromXml (*xml));
            autoEdit.onStateRestored (savedReport);
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
