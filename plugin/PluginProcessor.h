// TURBO TUBES — plugin/PluginProcessor.h

#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "../dsp/TurboTubesEngine.h"
#include "Parameters.h"

class TurboTubesProcessor : public juce::AudioProcessor,
                            private juce::AsyncUpdater
{
public:
    TurboTubesProcessor();
    ~TurboTubesProcessor() override = default;

    //==========================================================================
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    using AudioProcessor::processBlock;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Turbo Tubes"; }
    bool acceptsMidi() const override  { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override;
    juce::AudioProcessorParameter* getBypassParameter() const override;

    //==========================================================================
    int getNumPrograms() override;
    int getCurrentProgram() override { return currentProgram; }
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int, const juce::String&) override {}

    //==========================================================================
    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    //==========================================================================
    // A/B/C/D snapshots (persisted in plugin state, undoable)
    void switchToSnapshot (int index);
    void copyActiveSnapshotTo (int index);
    int getActiveSnapshot() const noexcept { return activeSnapshot; }

    void reseedDrift();

    juce::AudioProcessorValueTreeState apvts;
    juce::UndoManager undoManager;
    tt::TurboTubesEngine engine;

    std::atomic<uint32_t> driftSeed { 0x54554245u };

private:
    void handleAsyncUpdate() override;   // latency reporting on message thread
    tt::EngineParams makeEngineParams() const noexcept;

    // cached raw parameter pointers (audio-thread reads)
    std::atomic<float>* pInTrim, * pDrive, * pTube, * pTurbo, * pBias, * pSag,
                      * pInertia, * pLowCut, * pTilt, * pHighCut, * pMix,
                      * pOutTrim, * pAutoGain, * pDelta, * pOs, * pOsQ,
                      * pProcMode, * pWidth, * pMsBal, * pVariance, * pScExt,
                      * pScHp, * pScLp, * pBypass;

    juce::ValueTree snapshots[4];
    int activeSnapshot = 0;
    int currentProgram = 0;
    int lastReportedLatency = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TurboTubesProcessor)
};
