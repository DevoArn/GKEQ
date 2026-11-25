#pragma once

#include "../Params.h"

namespace meq::dsp
{

/** Everything an EQ engine needs for one processing block. */
struct EngineInput
{
    const BandParams*   bands      = nullptr;   // kNumBands entries
    const GlobalParams* global     = nullptr;
    const float* const* sidechain  = nullptr;   // nullptr when no side-chain is connected
    int                 scChannels = 0;
    float*              dynOffsetsOut = nullptr; // kNumBands entries, written by the engine
    float*              detLevelsOut  = nullptr; // kNumBands detector levels in dB (-200 when inactive)
};

inline bool isMidSide (Placement p) noexcept { return p == Placement::Mid || p == Placement::Side; }

/** First soloed band, or -1. */
inline int findSoloBand (const BandParams* bands) noexcept
{
    for (int b = 0; b < kNumBands; ++b)
        if (bands[b].used && bands[b].solo)
            return b;
    return -1;
}

} // namespace meq::dsp
