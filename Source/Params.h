// GKEQ - 24-band dynamic EQ plug-in
// Copyright (C) 2026 Roman Gutekunst (GK)
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <array>
#include <atomic>

namespace meq
{

constexpr int    kNumBands = 24;
constexpr double kMinFreq  = 10.0;
constexpr double kMaxFreq  = 30000.0;
constexpr double kMinQ     = 0.025;
constexpr double kMaxQ     = 40.0;
constexpr double kMaxGain  = 30.0;
constexpr double kDefaultQ = 0.70710678118654752440;

enum class FilterType : int
{
    Bell = 0,
    LowShelf,
    LowCut,
    HighShelf,
    HighCut,
    Notch,
    BandPass,
    TiltShelf,
    FlatTilt,
    AllPass,
    Count
};

enum class Placement : int { Stereo = 0, Left, Right, Mid, Side, Count };
enum class ProcMode : int { ZeroLatency = 0, NaturalPhase, LinearPhase };
enum class CharacterMode : int { Clean = 0, Subtle, Warm };

constexpr int kNumSlopes    = 10;   // 6,12,18,24,30,36,48,72,96,Brickwall
constexpr int kBrickwallIdx = 9;
constexpr int kNumLPRes     = 5;    // Low, Medium, High, Very High, Maximum

inline int slopeDb (int idx) noexcept
{
    static constexpr int s[] = { 6, 12, 18, 24, 30, 36, 48, 72, 96, 0 };
    return s[juce::jlimit (0, kNumSlopes - 1, idx)];
}

/** Filter order (number of poles) used for a slope index. Brickwall maps to a
    very steep IIR in the minimum-phase modes and to an ideal response in
    linear-phase mode. */
inline int slopeOrder (int idx) noexcept
{
    if (idx >= kBrickwallIdx) return 24;
    return slopeDb (idx) / 6;
}

inline bool typeHasGain (FilterType t) noexcept
{
    return t == FilterType::Bell || t == FilterType::LowShelf || t == FilterType::HighShelf
        || t == FilterType::TiltShelf || t == FilterType::FlatTilt;
}

inline bool typeHasSlope (FilterType t) noexcept
{
    return t != FilterType::Bell && t != FilterType::FlatTilt;
}

inline bool typeHasQ (FilterType t) noexcept
{
    return t != FilterType::FlatTilt;
}

inline bool typeSupportsDynamics (FilterType t) noexcept { return typeHasGain (t); }

juce::StringArray filterTypeNames();
juce::StringArray slopeNames();
juce::StringArray placementNames();
juce::StringArray modeNames();
juce::StringArray lpResolutionNames();
juce::StringArray characterNames();

namespace ID
{
    juce::String band (int bandIndex, const char* suffix);

    inline constexpr const char* used      = "used";
    inline constexpr const char* enabled   = "on";
    inline constexpr const char* type      = "type";
    inline constexpr const char* freq      = "freq";
    inline constexpr const char* gain      = "gain";
    inline constexpr const char* q         = "q";
    inline constexpr const char* slope     = "slope";
    inline constexpr const char* placement = "place";
    inline constexpr const char* dynOn     = "dyn";
    inline constexpr const char* dynRange  = "range";
    inline constexpr const char* threshold = "thresh";
    inline constexpr const char* attack    = "attack";
    inline constexpr const char* release   = "release";
    inline constexpr const char* dynSC     = "sc";
    inline constexpr const char* solo      = "solo";

    inline constexpr const char* mode       = "mode";
    inline constexpr const char* lpRes      = "lpres";
    inline constexpr const char* character  = "character";
    inline constexpr const char* gainScale  = "gainscale";
    inline constexpr const char* outGain    = "outgain";
    inline constexpr const char* outPan     = "outpan";
    inline constexpr const char* autoGain   = "autogain";
    inline constexpr const char* phaseInv   = "phaseinv";
    inline constexpr const char* bypass     = "bypass";
}

struct BandParams
{
    bool       used      = false;
    bool       enabled   = true;
    FilterType type      = FilterType::Bell;
    double     freq      = 1000.0;
    double     gain      = 0.0;
    double     q         = 1.0;
    int        slopeIdx  = 1;
    Placement  placement = Placement::Stereo;
    bool       dynOn     = false;
    double     dynRange  = 0.0;
    double     threshold = -30.0;
    double     attackMs  = 10.0;
    double     releaseMs = 150.0;
    bool       dynSC     = false;
    bool       solo      = false;

    bool isActive() const noexcept { return used && enabled; }
    bool isDynamic() const noexcept { return dynOn && typeSupportsDynamics (type); }
};

struct GlobalParams
{
    ProcMode      mode        = ProcMode::ZeroLatency;
    int           lpRes       = 1;
    CharacterMode character   = CharacterMode::Clean;
    double        gainScale   = 1.0;   // linear factor (1 == 100 %)
    double        outGainDb   = 0.0;
    double        outPan      = 0.0;   // -1 .. +1
    bool          autoGain    = false;
    bool          phaseInvert = false;
    bool          bypass      = false;
};

/** The band's gain after gain-scale and dynamics have been applied. */
inline double effectiveGainDb (const BandParams& b, double gainScale, double dynOffsetDb) noexcept
{
    return juce::jlimit (-kMaxGain * 2.0, kMaxGain * 2.0, (b.gain + dynOffsetDb) * gainScale);
}

juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

/** Cached raw pointers into the APVTS for lock-free reads on any thread. */
class ParamRefs
{
public:
    void init (juce::AudioProcessorValueTreeState& state);

    BandParams   readBand (int b) const noexcept;
    GlobalParams readGlobal() const noexcept;

    struct Band
    {
        std::atomic<float>* used {}; std::atomic<float>* enabled {}; std::atomic<float>* type {};
        std::atomic<float>* freq {}; std::atomic<float>* gain {}; std::atomic<float>* q {};
        std::atomic<float>* slope {}; std::atomic<float>* placement {}; std::atomic<float>* dynOn {};
        std::atomic<float>* dynRange {}; std::atomic<float>* threshold {}; std::atomic<float>* attack {};
        std::atomic<float>* release {}; std::atomic<float>* dynSC {}; std::atomic<float>* solo {};
    };

    std::array<Band, kNumBands> bands;
    std::atomic<float>* mode {}; std::atomic<float>* lpRes {}; std::atomic<float>* character {};
    std::atomic<float>* gainScale {}; std::atomic<float>* outGain {}; std::atomic<float>* outPan {};
    std::atomic<float>* autoGain {}; std::atomic<float>* phaseInv {}; std::atomic<float>* bypass {};
};

// ---------------------------------------------------------------------------
// Formatting helpers shared by the UI and parameter text functions
// ---------------------------------------------------------------------------
juce::String formatFrequency (double hz, bool withUnit = true);
juce::String formatGain (double db);
juce::String formatQ (double q);
juce::String noteNameForFrequency (double hz);

} // namespace meq
