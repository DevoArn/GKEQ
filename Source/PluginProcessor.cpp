// GKEQ - 24-band dynamic EQ plug-in
// Copyright (C) 2026 Roman Gutekunst (GK)
// SPDX-License-Identifier: GPL-3.0-or-later

#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "InstanceRegistry.h"

namespace meq
{

ModernEQProcessor::ModernEQProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)
                          .withInput ("Sidechain", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMETERS", createParameterLayout())
{
    refs.init (apvts);
    defaultState = apvts.copyState();
    instanceId = InstanceRegistry::get().add (this);
    initUIDefaults();
    for (auto& d : dynOffsets) d.store (0.0f);
    for (auto& d : detLevels) d.store (-200.0f);
    for (auto& d : detLevelsRT) d = -200.0f;
}

ModernEQProcessor::~ModernEQProcessor()
{
    InstanceRegistry::get().remove (this);
}

void ModernEQProcessor::initUIDefaults()
{
    auto setDefault = [this] (const juce::Identifier& id, const juce::var& v)
    {
        if (! uiState.hasProperty (id)) uiState.setProperty (id, v, nullptr);
    };
    setDefault ("anaPre", true);
    setDefault ("anaPost", true);
    setDefault ("anaSC", false);
    setDefault ("anaRange", 90);
    setDefault ("anaRes", 2);        // 0 low .. 3 maximum
    setDefault ("anaSpeed", 2);      // 0 very slow .. 4 very fast
    setDefault ("anaTilt", 4.5);
    setDefault ("anaFreeze", false);
    setDefault ("grab", true);
    setDefault ("collisions", true);
    setDefault ("displayRange", 12);
    setDefault ("pianoRoll", false);
    setDefault ("instance", 0);      // id of another instance to overlay (0 = none)
    setDefault ("width", 1040);
    setDefault ("height", 620);
    setDefault ("gpu", true);

    // UI settings come from project files and may be malformed or hostile:
    // force every numeric value into its valid range (an absurd analyser range
    // would otherwise make the editor draw millions of grid labels).
    // Values restored from a project arrive as strings (XML attributes).
    auto number = [this] (const juce::Identifier& id, double def)
    {
        const auto v = uiState.getProperty (id);
        if (v.isVoid()) return def;
        const double d = v.isString() ? v.toString().getDoubleValue() : (double) v;
        return std::isfinite (d) ? d : def;
    };
    auto clampInt = [this, number] (const juce::Identifier& id, int lo, int hi, int def)
    {
        uiState.setProperty (id, (int) std::lround (juce::jlimit ((double) lo, (double) hi, number (id, def))), nullptr);
    };
    auto clampDouble = [this, number] (const juce::Identifier& id, double lo, double hi, double def)
    {
        uiState.setProperty (id, juce::jlimit (lo, hi, number (id, def)), nullptr);
    };
    {
        const int r = (int) uiState.getProperty ("anaRange", 90);
        uiState.setProperty ("anaRange", r <= 75 ? 60 : (r <= 105 ? 90 : 120), nullptr);
    }
    clampInt ("anaRes", 0, 3, 2);
    clampInt ("anaSpeed", 0, 4, 2);
    clampDouble ("anaTilt", 0.0, 6.0, 4.5);
    clampInt ("displayRange", 3, 30, 12);
    clampInt ("instance", 0, 1000000, 0);
    clampInt ("width", 880, 2560, 1040);
    clampInt ("height", 500, 1600, 620);
}

juce::String ModernEQProcessor::getInstanceName() const
{
    const juce::ScopedLock sl (trackLock);
    return trackName.isNotEmpty() ? trackName : "GKEQ " + juce::String (instanceId);
}

juce::Colour ModernEQProcessor::getTrackColour() const
{
    const juce::ScopedLock sl (trackLock);
    return trackColour;
}

void ModernEQProcessor::updateTrackProperties (const TrackProperties& p)
{
    const juce::ScopedLock sl (trackLock);
    trackName = p.name;
    trackColour = p.colour;
}

juce::AudioProcessorParameter* ModernEQProcessor::getBypassParameter() const
{
    return apvts.getParameter (ID::bypass);
}

bool ModernEQProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto in  = layouts.getMainInputChannelSet();
    const auto out = layouts.getMainOutputChannelSet();
    if (in != out) return false;
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo()) return false;

    if (layouts.inputBuses.size() > 1)
    {
        const auto sc = layouts.getChannelSet (true, 1);
        if (! sc.isDisabled() && sc != juce::AudioChannelSet::mono() && sc != juce::AudioChannelSet::stereo())
            return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
void ModernEQProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    currentRate.store (sampleRate);
    engineBlock = juce::jlimit (64, 8192, samplesPerBlock > 0 ? samplesPerBlock : 512);
    numChannels = juce::jlimit (1, 2, getMainBusNumOutputChannels());

    const int npOS = sampleRate <= 50000.0 ? 2 : (sampleRate <= 100000.0 ? 1 : 0);
    zlEngine.prepare (sampleRate, engineBlock, numChannels, 0);
    npEngine.prepare (sampleRate, engineBlock, numChannels, npOS);
    lpEngine.prepare (sampleRate, engineBlock, numChannels);
    character.prepare (sampleRate, engineBlock, numChannels);

    const int maxLatency = lpEngine.latencyFor (kNumLPRes - 1) + character.latencySamples (true) + npEngine.latencySamples();
    dryDelay.setSize (2, maxLatency + engineBlock + 16);
    dryDelay.clear();
    dryScratch.setSize (2, engineBlock);
    dryWrite = 0;

    outGainSm.reset (sampleRate, 0.02);
    panLSm.reset (sampleRate, 0.02);
    panRSm.reset (sampleRate, 0.02);
    const auto g = refs.readGlobal();
    outGainSm.setCurrentAndTargetValue ((float) dsp::dbToGain (g.outGainDb) * (g.phaseInvert ? -1.0f : 1.0f));
    panLSm.setCurrentAndTargetValue ((float) juce::jmin (1.0, 1.0 - g.outPan));
    panRSm.setCurrentAndTargetValue ((float) juce::jmin (1.0, 1.0 + g.outPan));

    switchStep = (float) (1.0 / (0.004 * sampleRate));
    bypassStep = (float) (1.0 / (0.010 * sampleRate));
    bypassMix  = g.bypass ? 1.0f : 0.0f;
    autoGainHash = -1.0;

    Config c;
    c.mode = g.mode; c.res = g.lpRes; c.charOn = g.character != CharacterMode::Clean;
    applyConfig (c);
    switchState = Switch::Idle;
    switchGain = 1.0f;
    prepared = true;
}

void ModernEQProcessor::releaseResources() {}

int ModernEQProcessor::computeLatency (const Config& c) const
{
    switch (c.mode)
    {
        case ProcMode::ZeroLatency:  return 0;
        case ProcMode::NaturalPhase: return npEngine.latencySamples();
        case ProcMode::LinearPhase:  return lpEngine.latencyFor (c.res) + (c.charOn ? character.latencySamples (true) : 0);
        default: return 0;
    }
}

void ModernEQProcessor::applyConfig (const Config& c)
{
    active = c;
    switch (c.mode)
    {
        case ProcMode::ZeroLatency:  zlEngine.reset(); break;
        case ProcMode::NaturalPhase: npEngine.reset(); break;
        case ProcMode::LinearPhase:  lpEngine.setResolution (c.res); lpEngine.reset(); break;
        default: break;
    }
    character.reset();

    const int lat = computeLatency (c);
    activeLatency.store (lat);
    setLatencySamples (lat);
}

void ModernEQProcessor::updateAutoGain (const GlobalParams& g, int numSamples)
{
    // Hash of everything that shapes the static curve.
    double h = g.gainScale * 13.7;
    for (int b = 0; b < kNumBands; ++b)
    {
        const auto& p = bandsRT[b];
        if (! p.isActive()) continue;
        h += (b + 1) * (p.freq * 1.0e-3 + p.gain * 7.0 + p.q * 3.0 + (double) p.type * 11.0 + p.slopeIdx * 5.0 + (double) p.placement * 2.0);
    }

    autoGainCountdown -= numSamples;
    if (h == autoGainHash || autoGainCountdown > 0)
        return;
    autoGainHash = h;
    autoGainCountdown = (int) (0.02 * getCurrentSampleRate());

    const double fs = getCurrentSampleRate();
    constexpr int kPoints = 40;
    double sum = 0.0, wsum = 0.0;
    dsp::AnalogFilter filters[kNumBands];
    double placeWeight[kNumBands] {};
    for (int b = 0; b < kNumBands; ++b)
    {
        const auto& p = bandsRT[b];
        if (! p.isActive()) continue;
        filters[b] = dsp::designAnalog (dsp::DesignParams::fromBand (p, effectiveGainDb (p, g.gainScale, 0.0), true), fs);
        placeWeight[b] = (p.placement == Placement::Stereo || p.placement == Placement::Mid) ? 1.0 : 0.5;
    }
    for (int i = 0; i < kPoints; ++i)
    {
        const double f = 20.0 * std::pow (1000.0, i / (double) (kPoints - 1));
        if (f >= 0.49 * fs) break;
        const double w = dsp::kTwoPi * f / fs;
        double db = 0.0;
        for (int b = 0; b < kNumBands; ++b)
            if (placeWeight[b] > 0.0)
                db += placeWeight[b] * dsp::gainToDb (filters[b].mag (w));
        // Emphasise the region where loudness perception is most sensitive.
        const double lf = std::log10 (f);
        const double weight = lf < 2.3 ? 0.35 + 0.65 * (lf - 1.3) : (lf > 3.7 ? 1.0 - 0.6 * (lf - 3.7) / 0.6 : 1.0);
        sum += std::max (-60.0, db) * weight;
        wsum += weight;
    }
    autoGainDb = wsum > 0.0 ? juce::jlimit (-24.0, 24.0, -sum / wsum) : 0.0;
    autoGainDbUI.store ((float) autoGainDb);
}

void ModernEQProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    if (! prepared || n <= 0) return;

    auto mainBus = getBusBuffer (buffer, false, 0);
    const int nch = juce::jmin (2, mainBus.getNumChannels(), numChannels);

    // Side-chain
    const float* sc[2] { nullptr, nullptr };
    int scCh = 0;
    if (getBusCount (true) > 1)
    {
        if (auto* bus = getBus (true, 1); bus != nullptr && bus->isEnabled())
        {
            auto scBus = getBusBuffer (buffer, true, 1);
            scCh = juce::jmin (2, scBus.getNumChannels());
            for (int c = 0; c < scCh; ++c) sc[c] = scBus.getReadPointer (c);
        }
    }
    if (scCh > 0)
    {
        scTap.writeMix (sc[0], scCh > 1 ? sc[1] : nullptr, n);
        float mag = 0.0f;
        for (int c = 0; c < scCh; ++c) mag = juce::jmax (mag, juce::FloatVectorOperations::findMaximum (sc[c], n), -juce::FloatVectorOperations::findMinimum (sc[c], n));
        sidechainActive.store (mag > 1.0e-6f);
    }
    else
    {
        sidechainActive.store (false);
    }

    // Pre-EQ analyser tap
    preTap.writeMix (mainBus.getReadPointer (0), nch > 1 ? mainBus.getReadPointer (1) : nullptr, n);

    // Parameters
    for (int b = 0; b < kNumBands; ++b)
        bandsRT[b] = refs.readBand (b);
    const GlobalParams g = refs.readGlobal();
    updateAutoGain (g, n);

    Config want;
    want.mode = g.mode; want.res = g.lpRes; want.charOn = g.character != CharacterMode::Clean;
    if (want != active && switchState == Switch::Idle)
        switchState = Switch::FadingOut;

    // Output stage targets
    const double totalGainDb = g.outGainDb + (g.autoGain ? autoGainDb : 0.0);
    outGainSm.setTargetValue ((float) dsp::dbToGain (totalGainDb) * (g.phaseInvert ? -1.0f : 1.0f));
    panLSm.setTargetValue ((float) juce::jmin (1.0, 1.0 - g.outPan));
    panRSm.setTargetValue ((float) juce::jmin (1.0, 1.0 + g.outPan));
    const float bypassTarget = g.bypass ? 1.0f : 0.0f;

    float peak = 0.0f;

    for (int pos = 0; pos < n; pos += engineBlock)
    {
        const int len = juce::jmin (engineBlock, n - pos);
        float* io[2] { mainBus.getWritePointer (0) + pos, nch > 1 ? mainBus.getWritePointer (1) + pos : nullptr };
        const float* scp[2] { sc[0] != nullptr ? sc[0] + pos : nullptr, sc[1] != nullptr ? sc[1] + pos : nullptr };

        // Dry path delayed by the current latency (for bypass)
        {
            const int size = dryDelay.getNumSamples();
            const int lat  = activeLatency.load();
            for (int ch = 0; ch < nch; ++ch)
            {
                float* d = dryDelay.getWritePointer (ch);
                float* o = dryScratch.getWritePointer (ch);
                int w = dryWrite;
                int r = dryWrite - lat;
                while (r < 0) r += size;
                for (int i = 0; i < len; ++i)
                {
                    d[w] = io[ch][i];
                    o[i] = d[r];
                    if (++w == size) w = 0;
                    if (++r == size) r = 0;
                }
            }
            dryWrite = (dryWrite + len) % size;
        }

        dsp::EngineInput in;
        in.bands = bandsRT;
        in.global = &g;
        in.sidechain = scCh > 0 ? scp : nullptr;
        in.scChannels = scCh;
        in.dynOffsetsOut = dynOffsetsRT;
        in.detLevelsOut = detLevelsRT;

        switch (active.mode)
        {
            case ProcMode::ZeroLatency:  zlEngine.process (io, nch, len, in); break;
            case ProcMode::NaturalPhase: npEngine.process (io, nch, len, in); break;
            case ProcMode::LinearPhase:  lpEngine.process (io, nch, len, in); break;
            default: break;
        }

        if (active.charOn && ! (active.mode == ProcMode::NaturalPhase && npEngine.handlesCharacter()))
            character.process (io, nch, len, g.character, active.mode != ProcMode::ZeroLatency);

        // Output gain / pan / phase, mode-switch fade and bypass cross-fade
        for (int i = 0; i < len; ++i)
        {
            const float gain = outGainSm.getNextValue();
            const float pl = panLSm.getNextValue();
            const float pr = panRSm.getNextValue();

            if (switchState == Switch::FadingOut)
            {
                switchGain -= switchStep;
                if (switchGain <= 0.0f) switchGain = 0.0f;
            }
            else if (switchState == Switch::FadingIn)
            {
                switchGain += switchStep;
                if (switchGain >= 1.0f) { switchGain = 1.0f; switchState = Switch::Idle; }
            }

            if (bypassMix < bypassTarget)      bypassMix = juce::jmin (bypassTarget, bypassMix + bypassStep);
            else if (bypassMix > bypassTarget) bypassMix = juce::jmax (bypassTarget, bypassMix - bypassStep);

            for (int ch = 0; ch < nch; ++ch)
            {
                const float panG = nch > 1 ? (ch == 0 ? pl : pr) : 1.0f;
                const float wet = io[ch][i] * gain * panG;
                const float dry = dryScratch.getReadPointer (ch)[i];
                const float out = switchGain * (wet + bypassMix * (dry - wet));
                io[ch][i] = out;
                peak = juce::jmax (peak, std::abs (out));
            }
        }

        if (switchState == Switch::FadingOut && switchGain <= 0.0f)
        {
            applyConfig (want);
            switchState = Switch::FadingIn;
        }
    }

    postTap.writeMix (mainBus.getReadPointer (0), nch > 1 ? mainBus.getReadPointer (1) : nullptr, n);
    outPeakDb.store ((float) dsp::gainToDb (peak));

    for (int b = 0; b < kNumBands; ++b)
    {
        dynOffsets[(size_t) b].store (dynOffsetsRT[b], std::memory_order_relaxed);
        detLevels[(size_t) b].store (detLevelsRT[b], std::memory_order_relaxed);
    }
}

// ---------------------------------------------------------------------------
juce::AudioProcessorEditor* ModernEQProcessor::createEditor()
{
    return new ModernEQEditor (*this);
}

void ModernEQProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    juce::ValueTree root ("MODERN_EQ");
    root.setProperty ("version", 1, nullptr);
    root.setProperty ("preset", currentPresetName, nullptr);
    root.addChild (apvts.copyState(), -1, nullptr);
    root.addChild (uiState.createCopy(), -1, nullptr);

    juce::ValueTree ab ("AB");
    ab.setProperty ("slot", abSlot, nullptr);
    for (int i = 0; i < 2; ++i)
    {
        if (abSlots[i].isValid())
        {
            juce::ValueTree s (i == 0 ? "A" : "B");
            s.addChild (abSlots[i].createCopy(), -1, nullptr);
            ab.addChild (s, -1, nullptr);
        }
    }
    root.addChild (ab, -1, nullptr);

    if (auto xml = root.createXml())
        copyXmlToBinary (*xml, destData);
}

void ModernEQProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    auto xml = getXmlFromBinary (data, sizeInBytes);
    if (xml == nullptr) return;
    auto root = juce::ValueTree::fromXml (*xml);
    if (! root.isValid()) return;

    if (root.hasType ("MODERN_EQ"))
    {
        currentPresetName = root.getProperty ("preset", "Default").toString();
        auto params = root.getChildWithName (apvts.state.getType());
        if (params.isValid())
            apvts.replaceState (params);

        auto ui = root.getChildWithName ("UI");
        if (ui.isValid())
            uiState.copyPropertiesFrom (ui, nullptr);
        initUIDefaults();

        auto ab = root.getChildWithName ("AB");
        abSlot = juce::jlimit (0, 1, (int) ab.getProperty ("slot", 0));
        for (int i = 0; i < 2; ++i)
        {
            auto s = ab.getChildWithName (i == 0 ? "A" : "B");
            abSlots[i] = s.isValid() && s.getNumChildren() > 0 ? s.getChild (0).createCopy() : juce::ValueTree();
        }
    }
    else if (root.hasType (apvts.state.getType()))
    {
        apvts.replaceState (root);
    }
    undoStack.clear();
    redoStack.clear();
    undoBaseline = {};
    clearAllSolos();
}

void ModernEQProcessor::clearAllSolos()
{
    for (int b = 0; b < kNumBands; ++b)
        if (refs.bands[(size_t) b].solo->load() > 0.5f)
            if (auto* p = apvts.getParameter (ID::band (b, ID::solo)))
                p->setValueNotifyingHost (0.0f);
}

// ---------------------------------------------------------------------------
void ModernEQProcessor::setParam (const juce::String& id, float value)
{
    if (auto* p = apvts.getParameter (id))
    {
        p->beginChangeGesture();
        p->setValueNotifyingHost (p->convertTo0to1 (value));
        p->endChangeGesture();
    }
}

float ModernEQProcessor::getParam (const juce::String& id) const
{
    if (auto* p = apvts.getRawParameterValue (id))
        return p->load();
    return 0.0f;
}

int ModernEQProcessor::findFreeBand() const
{
    for (int b = 0; b < kNumBands; ++b)
        if (refs.bands[(size_t) b].used->load() < 0.5f)
            return b;
    return -1;
}

void ModernEQProcessor::resetBand (int b)
{
    setParam (ID::band (b, ID::dynOn), 0.0f);
    setParam (ID::band (b, ID::dynRange), 0.0f);
    setParam (ID::band (b, ID::threshold), -30.0f);
    setParam (ID::band (b, ID::attack), 10.0f);
    setParam (ID::band (b, ID::release), 150.0f);
    setParam (ID::band (b, ID::dynSC), 0.0f);
    setParam (ID::band (b, ID::solo), 0.0f);
    setParam (ID::band (b, ID::enabled), 1.0f);
}

int ModernEQProcessor::createBand (FilterType type, double freq, double gain, double q, int slopeIdx, Placement placement)
{
    const int b = findFreeBand();
    if (b < 0) return -1;

    resetBand (b);
    setParam (ID::band (b, ID::type), (float) (int) type);
    setParam (ID::band (b, ID::freq), (float) freq);
    setParam (ID::band (b, ID::gain), (float) gain);
    setParam (ID::band (b, ID::q), (float) q);
    setParam (ID::band (b, ID::slope), (float) slopeIdx);
    setParam (ID::band (b, ID::placement), (float) (int) placement);
    setParam (ID::band (b, ID::used), 1.0f);   // last: the band appears fully configured
    return b;
}

void ModernEQProcessor::deleteBand (int b)
{
    if (b < 0 || b >= kNumBands) return;
    setParam (ID::band (b, ID::used), 0.0f);
    setParam (ID::band (b, ID::solo), 0.0f);
}

void ModernEQProcessor::commitUndoPoint()
{
    auto now = apvts.copyState();
    if (! undoBaseline.isValid())
    {
        undoBaseline = now;
        return;
    }
    if (! now.isEquivalentTo (undoBaseline))
    {
        undoStack.push_back (undoBaseline);
        if (undoStack.size() > 200) undoStack.erase (undoStack.begin());
        redoStack.clear();
        undoBaseline = now;
    }
}

void ModernEQProcessor::resetUndoBaseline()
{
    undoBaseline = apvts.copyState();
}

void ModernEQProcessor::undo()
{
    commitUndoPoint();
    if (undoStack.empty()) return;
    redoStack.push_back (apvts.copyState());
    auto s = undoStack.back();
    undoStack.pop_back();
    apvts.replaceState (s.createCopy());
    undoBaseline = s;
}

void ModernEQProcessor::redo()
{
    commitUndoPoint();
    if (redoStack.empty()) return;
    undoStack.push_back (apvts.copyState());
    auto s = redoStack.back();
    redoStack.pop_back();
    apvts.replaceState (s.createCopy());
    undoBaseline = s;
}

void ModernEQProcessor::switchAB()
{
    commitUndoPoint();
    abSlots[abSlot] = apvts.copyState();
    abSlot ^= 1;
    if (abSlots[abSlot].isValid())
        apvts.replaceState (abSlots[abSlot].createCopy());
    commitUndoPoint();
}

void ModernEQProcessor::copyToOtherSlot()
{
    abSlots[abSlot ^ 1] = apvts.copyState();
}

void ModernEQProcessor::loadParameterState (const juce::ValueTree& params, bool recordUndo)
{
    if (! params.isValid()) return;
    if (recordUndo) commitUndoPoint();
    apvts.replaceState (params.createCopy());
    if (recordUndo) commitUndoPoint();
    else            resetUndoBaseline();
}

} // namespace meq

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new meq::ModernEQProcessor();
}
