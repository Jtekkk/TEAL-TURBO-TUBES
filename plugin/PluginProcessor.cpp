// TURBO TUBES — plugin/PluginProcessor.cpp

#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "Presets.h"

//==============================================================================
TurboTubesProcessor::TurboTubesProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)
                          .withInput ("Sidechain", juce::AudioChannelSet::stereo(), false)),
      apvts (*this, &undoManager, "TurboTubes", ttp::createParameterLayout())
{
    auto raw = [this] (const char* id) { return apvts.getRawParameterValue (id); };
    pInTrim = raw (ttp::id::inTrim);   pDrive = raw (ttp::id::drive);
    pTube = raw (ttp::id::tube);       pTurbo = raw (ttp::id::turbo);
    pBias = raw (ttp::id::bias);       pSag = raw (ttp::id::sag);
    pInertia = raw (ttp::id::inertia); pLowCut = raw (ttp::id::lowCut);
    pTilt = raw (ttp::id::tilt);       pHighCut = raw (ttp::id::highCut);
    pMix = raw (ttp::id::mix);         pOutTrim = raw (ttp::id::outTrim);
    pAutoGain = raw (ttp::id::autoGain); pDelta = raw (ttp::id::delta);
    pOs = raw (ttp::id::os);           pOsQ = raw (ttp::id::osQuality);
    pProcMode = raw (ttp::id::procMode); pWidth = raw (ttp::id::width);
    pMsBal = raw (ttp::id::msBal);     pVariance = raw (ttp::id::variance);
    pScExt = raw (ttp::id::scExt);     pScHp = raw (ttp::id::scHp);
    pScLp = raw (ttp::id::scLp);       pBypass = raw (ttp::id::bypass);

    for (auto& s : snapshots)
        s = juce::ValueTree();
}

//==============================================================================
bool TurboTubesProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto mainIn  = layouts.getMainInputChannelSet();
    const auto mainOut = layouts.getMainOutputChannelSet();
    if (mainIn != mainOut)
        return false;
    if (mainIn != juce::AudioChannelSet::mono() && mainIn != juce::AudioChannelSet::stereo())
        return false;

    const auto sc = layouts.getChannelSet (true, 1);
    return sc.isDisabled() || sc == juce::AudioChannelSet::mono()
                           || sc == juce::AudioChannelSet::stereo();
}

void TurboTubesProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    engine.setParams (makeEngineParams());
    engine.prepare (sampleRate, samplesPerBlock, getMainBusNumInputChannels());
    lastReportedLatency = engine.latencySamples();
    setLatencySamples (lastReportedLatency);
}

double TurboTubesProcessor::getTailLengthSeconds() const
{
    return engine.latencySamples() / std::max (44100.0, getSampleRate());
}

juce::AudioProcessorParameter* TurboTubesProcessor::getBypassParameter() const
{
    return apvts.getParameter (ttp::id::bypass);
}

tt::EngineParams TurboTubesProcessor::makeEngineParams() const noexcept
{
    tt::EngineParams p;
    p.inTrimDb  = pInTrim->load();
    p.driveDb   = pDrive->load() * 3.6f;          // 0-10 knob → 0-36 dB
    p.biasKnob  = pBias->load() * 0.01f;
    p.sag       = pSag->load() * 0.01f;
    p.inertia   = pInertia->load() * 0.01f;
    p.lowCutHz  = pLowCut->load();
    p.tiltDb    = pTilt->load();
    p.highCutHz = pHighCut->load();
    p.mix       = pMix->load() * 0.01f;
    p.outTrimDb = pOutTrim->load();
    p.width     = pWidth->load() * 0.01f;
    p.msBal     = pMsBal->load() * 0.01f;
    p.drift     = pVariance->load() * 0.01f;
    p.scHpHz    = pScHp->load();
    p.scLpHz    = pScLp->load();
    p.model     = (int) pTube->load();
    p.osIndex   = (int) pOs->load();
    p.pristine  = pOsQ->load() > 0.5f;
    p.turbo     = pTurbo->load() > 0.5f;
    p.autoGain  = pAutoGain->load() > 0.5f;
    p.delta     = pDelta->load() > 0.5f;
    p.msMode    = pProcMode->load() > 0.5f;
    p.scExternal = pScExt->load() > 0.5f;
    p.bypass    = pBypass->load() > 0.5f;
    p.driftSeed = driftSeed.load (std::memory_order_relaxed);
    return p;
}

void TurboTubesProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const int n = buffer.getNumSamples();
    if (n == 0)
        return;

    auto mainBus = getBusBuffer (buffer, true, 0);
    const int numCh = mainBus.getNumChannels();

    float* io[2] = { mainBus.getWritePointer (0),
                     numCh > 1 ? mainBus.getWritePointer (1) : nullptr };

    const float* sc[2] = { nullptr, nullptr };
    int scChannels = 0;
    if (getBusCount (true) > 1)
    {
        auto scBus = getBusBuffer (buffer, true, 1);
        scChannels = scBus.getNumChannels();
        for (int c = 0; c < scChannels && c < 2; ++c)
            sc[c] = scBus.getReadPointer (c);
    }

    engine.setParams (makeEngineParams());
    engine.process (io, n, scChannels > 0 ? sc : nullptr, scChannels);

    // Latency changes (oversampling switches) are reported from the message
    // thread; the engine keeps dry/wet aligned internally in the meantime.
    if (engine.latencySamples() != lastReportedLatency)
        triggerAsyncUpdate();

    // Clear any output channels beyond the main bus (paranoia for odd hosts).
    for (int ch = getMainBusNumOutputChannels(); ch < buffer.getNumChannels(); ++ch)
        if (ch >= getMainBusNumInputChannels() + scChannels)
            buffer.clear (ch, 0, n);
}

void TurboTubesProcessor::handleAsyncUpdate()
{
    lastReportedLatency = engine.latencySamples();
    setLatencySamples (lastReportedLatency);
}

//==============================================================================
int TurboTubesProcessor::getNumPrograms()
{
    return (int) ttp::factoryPresets().size();
}

const juce::String TurboTubesProcessor::getProgramName (int index)
{
    const auto& presets = ttp::factoryPresets();
    if (index >= 0 && index < (int) presets.size())
        return presets[(size_t) index].name;
    return {};
}

void TurboTubesProcessor::setCurrentProgram (int index)
{
    const auto& presets = ttp::factoryPresets();
    if (index < 0 || index >= (int) presets.size())
        return;
    currentProgram = index;

    undoManager.beginNewTransaction ("Load preset");

    // Reset everything to defaults first so presets are complete states…
    for (auto* param : getParameters())
        if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (param))
            if (ranged->paramID != ttp::id::bypass)
                ranged->setValueNotifyingHost (ranged->getDefaultValue());

    // …then apply the preset's explicit values.
    for (const auto& [paramId, plainValue] : presets[(size_t) index].values)
        if (auto* ranged = apvts.getParameter (paramId))
            ranged->setValueNotifyingHost (ranged->convertTo0to1 (plainValue));

    updateHostDisplay (ChangeDetails().withProgramChanged (true));
}

//==============================================================================
void TurboTubesProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    juce::ValueTree root ("TurboTubesState");
    root.setProperty ("activeSnapshot", activeSnapshot, nullptr);
    root.setProperty ("driftSeed", (juce::int64) driftSeed.load(), nullptr);
    root.setProperty ("program", currentProgram, nullptr);
    root.appendChild (apvts.copyState(), nullptr);

    for (int i = 0; i < 4; ++i)
    {
        juce::ValueTree slot ("Snapshot");
        slot.setProperty ("index", i, nullptr);
        if (snapshots[i].isValid())
            slot.appendChild (snapshots[i].createCopy(), nullptr);
        root.appendChild (slot, nullptr);
    }

    juce::MemoryOutputStream mos (destData, true);
    root.writeToStream (mos);
}

void TurboTubesProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    auto root = juce::ValueTree::readFromData (data, (size_t) sizeInBytes);
    if (! root.isValid() || ! root.hasType ("TurboTubesState"))
        return;

    activeSnapshot = (int) root.getProperty ("activeSnapshot", 0);
    driftSeed.store ((uint32_t) (juce::int64) root.getProperty ("driftSeed", (juce::int64) 0x54554245), std::memory_order_relaxed);
    currentProgram = (int) root.getProperty ("program", 0);

    for (const auto& child : root)
    {
        if (child.hasType (apvts.state.getType()))
            apvts.replaceState (child.createCopy());
        else if (child.hasType ("Snapshot"))
        {
            const int i = (int) child.getProperty ("index", -1);
            if (i >= 0 && i < 4 && child.getNumChildren() > 0)
                snapshots[i] = child.getChild (0).createCopy();
        }
    }
}

//==============================================================================
void TurboTubesProcessor::switchToSnapshot (int index)
{
    index = juce::jlimit (0, 3, index);
    if (index == activeSnapshot)
        return;

    undoManager.beginNewTransaction ("Switch snapshot");
    snapshots[activeSnapshot] = apvts.copyState();   // save where we are
    if (snapshots[index].isValid())
        apvts.replaceState (snapshots[index].createCopy());
    activeSnapshot = index;
}

void TurboTubesProcessor::copyActiveSnapshotTo (int index)
{
    index = juce::jlimit (0, 3, index);
    snapshots[index] = apvts.copyState();
}

void TurboTubesProcessor::reseedDrift()
{
    driftSeed.store ((uint32_t) juce::Random::getSystemRandom().nextInt(), std::memory_order_relaxed);
}

//==============================================================================
juce::AudioProcessorEditor* TurboTubesProcessor::createEditor()
{
    return new TurboTubesEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new TurboTubesProcessor();
}
