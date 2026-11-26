// GKEQ - 24-band dynamic EQ plug-in
// Copyright (C) 2026 Roman Gutekunst (GK)
// SPDX-License-Identifier: GPL-3.0-or-later

#include "Params.h"

namespace meq
{

juce::StringArray filterTypeNames()
{
    return { "Bell", "Low Shelf", "Low Cut", "High Shelf", "High Cut",
             "Notch", "Band Pass", "Tilt Shelf", "Flat Tilt", "All Pass" };
}

juce::StringArray slopeNames()
{
    return { "6 dB/oct", "12 dB/oct", "18 dB/oct", "24 dB/oct", "30 dB/oct",
             "36 dB/oct", "48 dB/oct", "72 dB/oct", "96 dB/oct", "Brickwall" };
}

juce::StringArray placementNames() { return { "Stereo", "Left", "Right", "Mid", "Side" }; }
juce::StringArray modeNames()      { return { "Zero Latency", "Natural Phase", "Linear Phase" }; }
juce::StringArray lpResolutionNames() { return { "Low", "Medium", "High", "Very High", "Maximum" }; }
juce::StringArray characterNames() { return { "Clean", "Subtle", "Warm" }; }

juce::String ID::band (int bandIndex, const char* suffix)
{
    return "b" + juce::String (bandIndex + 1) + "_" + suffix;
}

// ---------------------------------------------------------------------------
juce::String formatFrequency (double hz, bool withUnit)
{
    if (hz >= 1000.0)
    {
        const double k = hz / 1000.0;
        auto s = formatNumber (k, k >= 10.0 ? 1 : 2);
        return withUnit ? s + " kHz" : s + "k";
    }
    auto s = formatNumber (hz, hz >= 100.0 ? 0 : 1);
    return withUnit ? s + " Hz" : s;
}

juce::String formatNumber (double v, int decimals)
{
    if (! std::isfinite (v)) v = 0.0;
    if (decimals <= 0)
    {
        const auto r = (long long) std::llround (v);
        return juce::String (r == 0 ? 0LL : r);   // no "-0"
    }
    return juce::String (v, decimals);
}

juce::String formatGain (double db)
{
    if (std::abs (db) < 0.005) db = 0.0;
    return (db > 0.0 ? "+" : "") + juce::String (db, 1) + " dB";
}

juce::String formatQ (double q)
{
    return juce::String (q, q < 1.0 ? 3 : (q < 10.0 ? 2 : 1));
}

juce::String noteNameForFrequency (double hz)
{
    if (hz <= 0.0) return {};
    static const char* names[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    const double midi  = 69.0 + 12.0 * std::log2 (hz / 440.0);
    const int    note  = (int) std::round (midi);
    const int    cents = (int) std::round ((midi - note) * 100.0);
    const int    idx   = ((note % 12) + 12) % 12;
    const int    oct   = note / 12 - 1 - ((note < 0 && note % 12 != 0) ? 1 : 0);
    juce::String s = juce::String (names[idx]) + juce::String (oct);
    if (cents != 0) s << (cents > 0 ? " +" : " ") << cents << "c";
    return s;
}

static double parseFrequency (const juce::String& text)
{
    auto t = text.trim().toLowerCase();
    double mult = 1.0;
    if (t.containsChar ('k')) mult = 1000.0;
    return t.retainCharacters ("0123456789.-").getDoubleValue() * mult;
}

// ---------------------------------------------------------------------------
static juce::NormalisableRange<float> logRange (float lo, float hi)
{
    return juce::NormalisableRange<float> (
        lo, hi,
        [] (float s, float e, float x) { return s * std::pow (e / s, x); },
        [] (float s, float e, float v) { return std::log (v / s) / std::log (e / s); },
        [] (float s, float e, float v) { return juce::jlimit (s, e, v); });
}

juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout layout;

    auto freqAttr = AudioParameterFloatAttributes()
                        .withStringFromValueFunction ([] (float v, int) { return formatFrequency (v); })
                        .withValueFromStringFunction ([] (const String& s) { return (float) parseFrequency (s); });
    auto gainAttr = AudioParameterFloatAttributes()
                        .withStringFromValueFunction ([] (float v, int) { return formatGain (v); })
                        .withValueFromStringFunction ([] (const String& s) { return s.getFloatValue(); });
    auto qAttr = AudioParameterFloatAttributes()
                     .withStringFromValueFunction ([] (float v, int) { return formatQ (v); })
                     .withValueFromStringFunction ([] (const String& s) { return s.getFloatValue(); });
    auto msAttr = AudioParameterFloatAttributes()
                      .withStringFromValueFunction ([] (float v, int) { return formatNumber (v, v < 10.0f ? 1 : 0) + " ms"; })
                      .withValueFromStringFunction ([] (const String& s) { return s.getFloatValue(); });

    for (int b = 0; b < kNumBands; ++b)
    {
        const auto prefix = "Band " + String (b + 1) + " ";
        auto group = std::make_unique<AudioProcessorParameterGroup> ("band" + String (b + 1), prefix.trim(), "|");
        auto pid = [b] (const char* s) { return ParameterID { ID::band (b, s), 1 }; };

        // Spread default frequencies so freshly-enabled slots are distinguishable.
        const float defFreq = (float) (40.0 * std::pow (400.0, b / (double) (kNumBands - 1)));

        group->addChild (std::make_unique<AudioParameterBool> (pid (ID::used), prefix + "Used", false));
        group->addChild (std::make_unique<AudioParameterBool> (pid (ID::enabled), prefix + "Enabled", true));
        group->addChild (std::make_unique<AudioParameterChoice> (pid (ID::type), prefix + "Shape", filterTypeNames(), 0));
        group->addChild (std::make_unique<AudioParameterFloat> (pid (ID::freq), prefix + "Frequency",
                                                                logRange ((float) kMinFreq, (float) kMaxFreq), defFreq, freqAttr));
        group->addChild (std::make_unique<AudioParameterFloat> (pid (ID::gain), prefix + "Gain",
                                                                NormalisableRange<float> (-30.0f, 30.0f, 0.0f), 0.0f, gainAttr));
        group->addChild (std::make_unique<AudioParameterFloat> (pid (ID::q), prefix + "Q",
                                                                logRange ((float) kMinQ, (float) kMaxQ), 1.0f, qAttr));
        group->addChild (std::make_unique<AudioParameterChoice> (pid (ID::slope), prefix + "Slope", slopeNames(), 1));
        group->addChild (std::make_unique<AudioParameterChoice> (pid (ID::placement), prefix + "Stereo Placement", placementNames(), 0));
        group->addChild (std::make_unique<AudioParameterBool> (pid (ID::dynOn), prefix + "Dynamics", false));
        group->addChild (std::make_unique<AudioParameterFloat> (pid (ID::dynRange), prefix + "Dynamic Range",
                                                                NormalisableRange<float> (-30.0f, 30.0f, 0.0f), 0.0f, gainAttr));
        group->addChild (std::make_unique<AudioParameterFloat> (pid (ID::threshold), prefix + "Threshold",
                                                                NormalisableRange<float> (-80.0f, 6.0f, 0.0f), -30.0f, gainAttr));
        group->addChild (std::make_unique<AudioParameterFloat> (pid (ID::attack), prefix + "Attack",
                                                                logRange (0.1f, 200.0f), 10.0f, msAttr));
        group->addChild (std::make_unique<AudioParameterFloat> (pid (ID::release), prefix + "Release",
                                                                logRange (5.0f, 3000.0f), 150.0f, msAttr));
        group->addChild (std::make_unique<AudioParameterBool> (pid (ID::dynSC), prefix + "External Side-chain", false));
        group->addChild (std::make_unique<AudioParameterBool> (pid (ID::solo), prefix + "Solo", false));
        layout.add (std::move (group));
    }

    auto pctAttr = AudioParameterFloatAttributes()
                       .withStringFromValueFunction ([] (float v, int) { return formatNumber (v, 0) + " %"; })
                       .withValueFromStringFunction ([] (const String& s) { return s.getFloatValue(); });
    auto panAttr = AudioParameterFloatAttributes()
                       .withStringFromValueFunction ([] (float v, int)
                       {
                           if (std::abs (v) < 0.5f) return String ("Center");
                           return formatNumber (std::abs (v), 0) + (v < 0 ? " L" : " R");
                       });

    auto global = std::make_unique<AudioProcessorParameterGroup> ("global", "Global", "|");
    global->addChild (std::make_unique<AudioParameterChoice> (ParameterID { ID::mode, 1 }, "Processing Mode", modeNames(), 0));
    global->addChild (std::make_unique<AudioParameterChoice> (ParameterID { ID::lpRes, 1 }, "Linear Phase Resolution", lpResolutionNames(), 1));
    global->addChild (std::make_unique<AudioParameterChoice> (ParameterID { ID::character, 1 }, "Character", characterNames(), 0));
    global->addChild (std::make_unique<AudioParameterFloat> (ParameterID { ID::gainScale, 1 }, "Gain Scale",
                                                             NormalisableRange<float> (-100.0f, 200.0f, 0.0f), 100.0f, pctAttr));
    global->addChild (std::make_unique<AudioParameterFloat> (ParameterID { ID::outGain, 1 }, "Output Gain",
                                                             NormalisableRange<float> (-36.0f, 36.0f, 0.0f), 0.0f, gainAttr));
    global->addChild (std::make_unique<AudioParameterFloat> (ParameterID { ID::outPan, 1 }, "Output Pan",
                                                             NormalisableRange<float> (-100.0f, 100.0f, 0.0f), 0.0f, panAttr));
    global->addChild (std::make_unique<AudioParameterBool> (ParameterID { ID::autoGain, 1 }, "Auto Gain", false));
    global->addChild (std::make_unique<AudioParameterBool> (ParameterID { ID::phaseInv, 1 }, "Phase Invert", false));
    global->addChild (std::make_unique<AudioParameterBool> (ParameterID { ID::bypass, 1 }, "Bypass", false));
    layout.add (std::move (global));

    return layout;
}

// ---------------------------------------------------------------------------
void ParamRefs::init (juce::AudioProcessorValueTreeState& s)
{
    auto get = [&s] (const juce::String& id)
    {
        auto* p = s.getRawParameterValue (id);
        jassert (p != nullptr);
        return p;
    };

    for (int b = 0; b < kNumBands; ++b)
    {
        auto& r = bands[(size_t) b];
        r.used      = get (ID::band (b, ID::used));
        r.enabled   = get (ID::band (b, ID::enabled));
        r.type      = get (ID::band (b, ID::type));
        r.freq      = get (ID::band (b, ID::freq));
        r.gain      = get (ID::band (b, ID::gain));
        r.q         = get (ID::band (b, ID::q));
        r.slope     = get (ID::band (b, ID::slope));
        r.placement = get (ID::band (b, ID::placement));
        r.dynOn     = get (ID::band (b, ID::dynOn));
        r.dynRange  = get (ID::band (b, ID::dynRange));
        r.threshold = get (ID::band (b, ID::threshold));
        r.attack    = get (ID::band (b, ID::attack));
        r.release   = get (ID::band (b, ID::release));
        r.dynSC     = get (ID::band (b, ID::dynSC));
        r.solo      = get (ID::band (b, ID::solo));
    }

    mode      = get (ID::mode);
    lpRes     = get (ID::lpRes);
    character = get (ID::character);
    gainScale = get (ID::gainScale);
    outGain   = get (ID::outGain);
    outPan    = get (ID::outPan);
    autoGain  = get (ID::autoGain);
    phaseInv  = get (ID::phaseInv);
    bypass    = get (ID::bypass);
}

BandParams ParamRefs::readBand (int b) const noexcept
{
    const auto& r = bands[(size_t) b];
    BandParams p;
    p.used      = r.used->load (std::memory_order_relaxed) > 0.5f;
    p.enabled   = r.enabled->load (std::memory_order_relaxed) > 0.5f;
    p.type      = (FilterType) juce::jlimit (0, (int) FilterType::Count - 1, (int) std::lround (r.type->load (std::memory_order_relaxed)));
    p.freq      = juce::jlimit (kMinFreq, kMaxFreq, (double) r.freq->load (std::memory_order_relaxed));
    p.gain      = (double) r.gain->load (std::memory_order_relaxed);
    p.q         = juce::jlimit (kMinQ, kMaxQ, (double) r.q->load (std::memory_order_relaxed));
    p.slopeIdx  = juce::jlimit (0, kNumSlopes - 1, (int) std::lround (r.slope->load (std::memory_order_relaxed)));
    p.placement = (Placement) juce::jlimit (0, (int) Placement::Count - 1, (int) std::lround (r.placement->load (std::memory_order_relaxed)));
    p.dynOn     = r.dynOn->load (std::memory_order_relaxed) > 0.5f;
    p.dynRange  = (double) r.dynRange->load (std::memory_order_relaxed);
    p.threshold = (double) r.threshold->load (std::memory_order_relaxed);
    p.attackMs  = (double) r.attack->load (std::memory_order_relaxed);
    p.releaseMs = (double) r.release->load (std::memory_order_relaxed);
    p.dynSC     = r.dynSC->load (std::memory_order_relaxed) > 0.5f;
    p.solo      = r.solo->load (std::memory_order_relaxed) > 0.5f;
    return p;
}

GlobalParams ParamRefs::readGlobal() const noexcept
{
    GlobalParams g;
    g.mode        = (ProcMode) juce::jlimit (0, 2, (int) std::lround (mode->load (std::memory_order_relaxed)));
    g.lpRes       = juce::jlimit (0, kNumLPRes - 1, (int) std::lround (lpRes->load (std::memory_order_relaxed)));
    g.character   = (CharacterMode) juce::jlimit (0, 2, (int) std::lround (character->load (std::memory_order_relaxed)));
    g.gainScale   = (double) gainScale->load (std::memory_order_relaxed) / 100.0;
    g.outGainDb   = (double) outGain->load (std::memory_order_relaxed);
    g.outPan      = (double) outPan->load (std::memory_order_relaxed) / 100.0;
    g.autoGain    = autoGain->load (std::memory_order_relaxed) > 0.5f;
    g.phaseInvert = phaseInv->load (std::memory_order_relaxed) > 0.5f;
    g.bypass      = bypass->load (std::memory_order_relaxed) > 0.5f;
    return g;
}

} // namespace meq
