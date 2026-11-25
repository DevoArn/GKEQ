// GKEQ - 24-band dynamic EQ plug-in
// Copyright (C) 2026 Roman Gutekunst (GK)
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "Params.h"
#include "SnapshotBuffer.h"
#include "dsp/IIREngine.h"
#include "dsp/LinearPhaseEngine.h"
#include "dsp/Character.h"

namespace meq
{

class ModernEQProcessor : public juce::AudioProcessor
{
public:
    ModernEQProcessor();
    ~ModernEQProcessor() override;

    // --- AudioProcessor ------------------------------------------------------
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.25; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return currentPresetName; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorParameter* getBypassParameter() const override;
    void updateTrackProperties (const TrackProperties& properties) override;

    // --- Shared state ----------------------------------------------------------
    juce::AudioProcessorValueTreeState apvts;
    ParamRefs refs;
    juce::ValueTree uiState { "UI" };   // analyser / display settings (message thread)
    juce::String currentPresetName { "Default" };

    SnapshotBuffer preTap, postTap, scTap;

    double getCurrentSampleRate() const noexcept { return currentRate.load(); }
    float  getDynamicOffset (int band) const noexcept { return dynOffsets[(size_t) band].load (std::memory_order_relaxed); }
    /** Current level of a dynamic band's detector (dB), -200 if not dynamic. */
    float  getDetectorLevel (int band) const noexcept { return detLevels[(size_t) band].load (std::memory_order_relaxed); }
    bool   isSidechainActive() const noexcept { return sidechainActive.load(); }
    int    getActiveLatency() const noexcept { return activeLatency.load(); }
    float  getAutoGainDb() const noexcept { return autoGainDbUI.load(); }
    float  getOutputPeakDb() const noexcept { return outPeakDb.load(); }

    int          getInstanceId() const noexcept { return instanceId; }
    juce::String getInstanceName() const;
    juce::Colour getTrackColour() const;

    // --- Editing helpers (message thread) -----------------------------------
    juce::RangedAudioParameter* param (const juce::String& id) const { return apvts.getParameter (id); }
    void  setParam (const juce::String& id, float value);           // wrapped in a host gesture
    float getParam (const juce::String& id) const;
    int   findFreeBand() const;
    int   createBand (FilterType type, double freq, double gain, double q, int slopeIdx, Placement placement);
    void  deleteBand (int band);
    void  resetBand (int band);
    /** Solo is a monitoring aid, never part of a saved setting. */
    void  clearAllSolos();

    // Undo / redo (snapshot based). The editor calls commitUndoPoint() after
    // each user action (mouse-up, menu or keyboard command).
    void commitUndoPoint();
    void resetUndoBaseline();
    bool canUndo() const noexcept { return ! undoStack.empty(); }
    bool canRedo() const noexcept { return ! redoStack.empty(); }
    void undo();
    void redo();

    // A/B comparison
    int  getABSlot() const noexcept { return abSlot; }
    void switchAB();
    void copyToOtherSlot();

    /** Loads a parameter tree (preset / A-B / undo). */
    void loadParameterState (const juce::ValueTree& params, bool recordUndo);
    juce::ValueTree getDefaultParameterState() const { return defaultState.createCopy(); }

private:
    struct Config
    {
        ProcMode mode = ProcMode::ZeroLatency;
        int      res  = 1;
        bool     charOn = false;
        bool operator== (const Config& o) const noexcept { return mode == o.mode && (mode != ProcMode::LinearPhase || res == o.res) && charOn == o.charOn; }
        bool operator!= (const Config& o) const noexcept { return ! (*this == o); }
    };

    int  computeLatency (const Config& c) const;
    void applyConfig (const Config& c);
    void updateAutoGain (const GlobalParams& g, int numSamples);
    void initUIDefaults();

    dsp::IIREngine zlEngine, npEngine;
    dsp::LinearPhaseEngine lpEngine;
    dsp::CharacterProcessor character;

    BandParams   bandsRT[kNumBands];
    float        dynOffsetsRT[kNumBands] {};
    std::array<std::atomic<float>, kNumBands> dynOffsets {};
    float        detLevelsRT[kNumBands] {};
    std::array<std::atomic<float>, kNumBands> detLevels {};

    Config active;
    bool   prepared = false;
    int    engineBlock = 512;
    int    numChannels = 2;

    // Mode-switch fade (latency changes are inaudible thanks to a short mute)
    enum class Switch { Idle, FadingOut, FadingIn };
    Switch switchState = Switch::Idle;
    float  switchGain = 1.0f;
    float  switchStep = 1.0f / 256.0f;

    // Bypass with latency-compensated dry path
    juce::AudioBuffer<float> dryDelay, dryScratch;
    int   dryWrite = 0;
    float bypassMix = 0.0f;
    float bypassStep = 1.0f / 512.0f;

    // Output stage
    juce::SmoothedValue<float> outGainSm, panLSm, panRSm;
    double autoGainDb = 0.0;
    double autoGainHash = -1.0;
    int    autoGainCountdown = 0;

    std::atomic<double> currentRate { 48000.0 };
    std::atomic<bool>   sidechainActive { false };
    std::atomic<int>    activeLatency { 0 };
    std::atomic<float>  autoGainDbUI { 0.0f };
    std::atomic<float>  outPeakDb { -100.0f };

    // Undo / A-B (message thread)
    std::vector<juce::ValueTree> undoStack, redoStack;
    juce::ValueTree undoBaseline;
    juce::ValueTree abSlots[2];
    int abSlot = 0;
    juce::ValueTree defaultState;

    int instanceId = 0;
    mutable juce::CriticalSection trackLock;
    juce::String trackName;
    juce::Colour trackColour;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ModernEQProcessor)
};

} // namespace meq
