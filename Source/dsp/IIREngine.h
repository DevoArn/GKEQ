// GKEQ - 24-band dynamic EQ plug-in
// Copyright (C) 2026 Roman Gutekunst (GK)
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "FilterDesign.h"
#include "Dynamics.h"
#include "EngineTypes.h"
#include "Oversampler.h"
#include <juce_dsp/juce_dsp.h>

namespace meq::dsp
{

/** Minimum-phase EQ engine.
    - Zero Latency mode: runs at the host rate with magnitude-matched filters.
    - Natural Phase mode: runs oversampled (linear-phase half-band filters) so
      both magnitude *and* phase follow the analog prototypes up to 20 kHz. */
class IIREngine
{
public:
    void prepare (double hostRate, int maxBlockSize, int numChannels, int oversamplingLog2);
    void reset();

    int latencySamples() const noexcept { return latency; }
    double processingRate() const noexcept { return rate; }
    /** True when the Character stage runs inside this engine's oversampled domain. */
    bool handlesCharacter() const noexcept { return osFactor > 1; }

    void process (float* const* io, int numChannels, int numSamples, const EngineInput& in);

private:
    static constexpr int kSubBlock = 32;           // host-rate samples per control update
    static constexpr int kMaxOS    = 4;
    static constexpr int kMaxSub   = kSubBlock * kMaxOS;

    struct Chain
    {
        int    num = 0;
        Biquad cur[kMaxSections];
        Biquad tgt[kMaxSections];
        double gain = 1.0, gainTgt = 1.0;
        double z[kMaxSections][2][2] {};
        bool   moving = false;

        void resetState() noexcept;
        void setTarget (const DigitalFilter& d, bool snap) noexcept;
        void processChannel (int ch, double* x, int n) noexcept;
        void commit() noexcept;
    };

    struct Config
    {
        bool       active = false;
        FilterType type   = FilterType::Bell;
        int        slope  = 1;
        Placement  place  = Placement::Stereo;

        bool operator== (const Config& o) const noexcept
        {
            if (! active && ! o.active) return true;
            return active == o.active && type == o.type && slope == o.slope && place == o.place;
        }
        bool operator!= (const Config& o) const noexcept { return ! (*this == o); }
    };

    struct Band
    {
        bool   initialised = false;
        double logF = 0.0, gainDb = 0.0, logQ = 0.0;   // smoothed
        Config cfgA, cfgB;
        Chain  A, B;
        bool   fading = false;
        int    fadePos = 0, fadeLen = 1;
        double designedA[3] { -1.0e9, -1.0e9, -1.0e9 };
        double designedB[3] { -1.0e9, -1.0e9, -1.0e9 };

        EnvelopeDetector det;
        double detKey[4] { -1.0, -1.0, -1.0, -1.0 };
        double dynOffset = 0.0;
    };

    void updateBand (int index, const BandParams& p, const EngineInput& in, int hostOffset, int hostCount);
    void designChain (Chain& c, const Config& cfg, const Band& b, double effGain, double* designed, bool snap);
    void processChain (Chain& c, Placement place, double* L, double* R, int n) noexcept;
    void processSolo (const double* dryL, const double* dryR, double* outL, double* outR, int n) noexcept;

    double hostRate = 48000.0, rate = 48000.0;
    int osLog2 = 0, osFactor = 1, channels = 2, latency = 0;
    LinearPhaseOversampler oversampler;
    int maxBlock = 512;

    // Character stage (oversampled domain)
    void applyCharacter (double* L, double* R, int n, CharacterMode mode) noexcept;
    CharacterMode charMode = CharacterMode::Clean;
    double charMix = 0.0;
    double dcX[2] {}, dcY[2] {}, dcCoef = 0.9999;

    Band bands[kNumBands];
    double gainScale = 1.0;
    bool   gainScaleInit = false;

    // Solo
    Chain     soloChain;
    int       soloBand = -1;
    Placement soloPlace = Placement::Stereo;
    double    soloMix = 0.0;
    double    soloKey[5] { -1.0, -1.0, -1.0, -1.0, -1.0 };

    // Scratch (one sub-block at the processing rate)
    double bufL[kMaxSub] {}, bufR[kMaxSub] {};
    double tmpAL[kMaxSub] {}, tmpAR[kMaxSub] {}, tmpBL[kMaxSub] {}, tmpBR[kMaxSub] {};
    double dryL[kMaxSub] {}, dryR[kMaxSub] {}, solL[kMaxSub] {}, solR[kMaxSub] {};
    const float* hostIn[2] {};   // host-rate input of the current block (for detection)
};

} // namespace meq::dsp
