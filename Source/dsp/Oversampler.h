// GKEQ - 24-band dynamic EQ plug-in
// Copyright (C) 2026 Roman Gutekunst (GK)
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <vector>
#include <cmath>

namespace meq::dsp
{

/** One 2x stage of a linear-phase half-band (Kaiser windowed-sinc) resampler.
    Up- and down-sampling are polyphase; the round trip has an integer delay
    of 2K+1 samples at the lower rate. */
class HalfbandStage
{
public:
    void design (int K, double kaiserBeta);
    void prepare (int numChannels);
    void reset();

    int K() const noexcept { return k; }

    /** x: n samples -> y: 2n samples. */
    void up (int ch, const float* x, float* y, int n) noexcept;
    /** w: 2n samples -> y: n samples. */
    void down (int ch, const float* w, float* y, int n) noexcept;

private:
    struct Ring
    {
        std::vector<double> buf;
        int len = 0, pos = 0;
        void init (int l) { len = l; buf.assign ((size_t) (2 * l), 0.0); pos = 0; }
        void clear() { std::fill (buf.begin(), buf.end(), 0.0); pos = 0; }
        /** Pushes x and returns the window (oldest first, len samples). */
        const double* push (double x) noexcept
        {
            buf[(size_t) pos] = x;
            buf[(size_t) (pos + len)] = x;
            const double* w = buf.data() + pos + 1;
            if (++pos == len) pos = 0;
            return w;
        }
    };

    int k = 0;
    std::vector<double> c;           // interpolator side taps c_j (j = 1..K+1)
    std::vector<Ring> upHist, dnEven, dnOdd;
};

/** Linear-phase oversampler (1x, 2x or 4x) with exact integer latency. */
class LinearPhaseOversampler
{
public:
    void prepare (int numChannels, int factorLog2, int maxBlockSize);
    void reset();

    int factor() const noexcept { return 1 << log2f; }
    int latencySamples() const noexcept { return latency; }

    /** Upsamples into internal buffers and returns per-channel pointers. */
    float* const* up (const float* const* in, int numChannels, int n) noexcept;
    /** Downsamples the internal buffers into out. */
    void down (float* const* out, int numChannels, int n) noexcept;

private:
    int log2f = 0, channels = 2, latency = 0;
    HalfbandStage stageA, stageB;        // A: 1x<->2x, B: 2x<->4x
    std::vector<std::vector<float>> mid, os;
    std::vector<float*> osPtr;
    std::vector<float> delay2x;          // one 2x-rate sample of delay per channel (4x only)
};

} // namespace meq::dsp
