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
    inMeter.prepare (sampleRate, juce::jmax (1, getTotalNumInputChannels()));
    outMeter.prepare (sampleRate, channels);
    levelMatch.prepare (sampleRate);
    appliedMatchDb = 0.0;
    autoEdit.prepare (sampleRate);
    setLatencySamples (chain.latencySamples());
}

template <typename Sample>
void VoxologyAudioProcessor::processAnyPrecision (juce::AudioBuffer<Sample>& buffer)
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

    if (auto* hostPlayHead = getPlayHead())
        if (auto position = hostPlayHead->getPosition())
            if (auto bpm = position->getBpm())
                hostBpm.store (*bpm);

    double inputPeak = 0.0;
    for (int c = 0; c < juce::jmin (numIn, buffer.getNumChannels()); ++c)
        inputPeak = juce::jmax (inputPeak, static_cast<double> (buffer.getMagnitude (c, 0, n)));
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
    chain.setParams (chainParams);
    chain.process (buffer.getArrayOfWritePointers(), nch, n);

    outMeter.process (buffer.getArrayOfReadPointers(), nch, n);
    outputTap.push (buffer.getArrayOfReadPointers(), nch, n);

    auto clampDb = [] (double v) { return static_cast<float> (std::isfinite (v) ? juce::jmax (v, -100.0) : -100.0); };
    meters.inShort.store (clampDb (inMeter.shortTermLufs()));
    meters.outShort.store (clampDb (outMeter.shortTermLufs()));
    holdMax (meters.inPeak, clampDb (inputPeak > 0.0 ? 20.0 * std::log10 (inputPeak) : -100.0));
    double outPeak = 0.0;
    for (int c = 0; c < nch; ++c) outPeak = juce::jmax (outPeak, static_cast<double> (buffer.getMagnitude (c, 0, n)));
    holdMax (meters.outPeak, clampDb (outPeak > 0.0 ? 20.0 * std::log10 (outPeak) : -100.0));
    const auto m = chain.takeMeters();
    holdMin (meters.gate, static_cast<float> (m.gateDb));
    holdMin (meters.deEss, static_cast<float> (m.deEssDb));
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
}

void VoxologyAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)  { processAnyPrecision (buffer); }
void VoxologyAudioProcessor::processBlock (juce::AudioBuffer<double>& buffer, juce::MidiBuffer&) { processAnyPrecision (buffer); }

void VoxologyAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = parameters.copyState().createXml())
    {
        xml->setAttribute ("aeReport", autoEdit.getReportForSaving());
        copyXmlToBinary (*xml, destData);
    }
}

void VoxologyAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (parameters.state.getType()))
        {
            const auto savedReport = xml->getStringAttribute ("aeReport");
            xml->removeAttribute ("aeReport");
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
