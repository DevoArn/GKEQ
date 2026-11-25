// GKEQ - 24-band dynamic EQ plug-in
// Copyright (C) 2026 Roman Gutekunst (GK)
// SPDX-License-Identifier: GPL-3.0-or-later

#include "Oversampler.h"
#include <algorithm>

namespace meq::dsp
{

namespace
{
    double besselI0 (double x) noexcept
    {
        double sum = 1.0, term = 1.0;
        const double y = 0.25 * x * x;
        for (int i = 1; i < 64; ++i)
        {
            term *= y / ((double) i * (double) i);
            sum += term;
            if (term < sum * 1.0e-17) break;
        }
        return sum;
    }
}

// ---------------------------------------------------------------------------
void HalfbandStage::design (int K, double beta)
{
    k = K;
    c.assign ((size_t) (K + 1), 0.0);
    const double half = 2.0 * K + 1.0;   // centre-to-edge distance in taps
    const double i0b = besselI0 (beta);
    const double pi = 3.14159265358979323846;

    double sum = 0.0;
    for (int j = 1; j <= K + 1; ++j)
    {
        const double o = 2.0 * j - 1.0;                       // odd offset from the centre tap
        const double sinc = ((j % 2 == 1) ? 1.0 : -1.0) / (pi * o);
        const double t = o / (half + 1.0);
        const double w = besselI0 (beta * std::sqrt (std::max (0.0, 1.0 - t * t))) / i0b;
        c[(size_t) (j - 1)] = sinc * w;
        sum += sinc * w;
    }
    // Exact unity DC gain: the side taps of a half-band filter sum to 1/4.
    for (auto& v : c) v *= 0.25 / sum;
    for (auto& v : c) v *= 2.0;   // interpolator gain of 2 folded in (c_j = 2 h_j)
}

void HalfbandStage::prepare (int numChannels)
{
    upHist.assign ((size_t) numChannels, Ring());
    dnEven.assign ((size_t) numChannels, Ring());
    dnOdd.assign ((size_t) numChannels, Ring());
    for (int ch = 0; ch < numChannels; ++ch)
    {
        upHist[(size_t) ch].init (2 * k + 2);
        dnEven[(size_t) ch].init (k + 1);
        dnOdd[(size_t) ch].init (2 * k + 2);
    }
}

void HalfbandStage::reset()
{
    for (auto& r : upHist) r.clear();
    for (auto& r : dnEven) r.clear();
    for (auto& r : dnOdd) r.clear();
}

void HalfbandStage::up (int ch, const float* x, float* y, int n) noexcept
{
    auto& ring = upHist[(size_t) ch];
    const int K = k;
    const double* cj = c.data();
    for (int i = 0; i < n; ++i)
    {
        const double* W = ring.push ((double) x[i]);
        double v = 0.0;
        for (int j = 1; j <= K + 1; ++j)
            v += cj[j - 1] * (W[K + j] + W[K + 1 - j]);
        y[2 * i]     = (float) W[K];
        y[2 * i + 1] = (float) v;
    }
}

void HalfbandStage::down (int ch, const float* w, float* y, int n) noexcept
{
    auto& re = dnEven[(size_t) ch];
    auto& ro = dnOdd[(size_t) ch];
    const int K = k;
    const double* cj = c.data();
    for (int i = 0; i < n; ++i)
    {
        const double* E = re.push ((double) w[2 * i]);
        const double* O = ro.push ((double) w[2 * i + 1]);
        double v = 0.0;
        for (int j = 1; j <= K + 1; ++j)
            v += cj[j - 1] * (O[K + j] + O[K + 1 - j]);
        y[i] = (float) (0.5 * E[0] + 0.5 * v);
    }
}

// ---------------------------------------------------------------------------
void LinearPhaseOversampler::prepare (int numChannels, int factorLog2, int maxBlockSize)
{
    channels = std::max (1, numChannels);
    log2f = std::clamp (factorLog2, 0, 2);

    // Stage A: flat to ~20 kHz at 44.1 kHz, ~95 dB image rejection.
    stageA.design (35, 9.5);
    // Stage B (2x -> 4x) has a very wide transition band: a short filter suffices.
    stageB.design (6, 9.5);
    stageA.prepare (channels);
    stageB.prepare (channels);

    const size_t maxN = (size_t) std::max (1, maxBlockSize);
    mid.assign ((size_t) channels, std::vector<float> (maxN * 2, 0.0f));
    os.assign ((size_t) channels, std::vector<float> (maxN * 4, 0.0f));
    osPtr.assign ((size_t) channels, nullptr);
    for (int ch = 0; ch < channels; ++ch)
        osPtr[(size_t) ch] = os[(size_t) ch].data();
    delay2x.assign ((size_t) channels, 0.0f);

    if (log2f == 0)      latency = 0;
    else if (log2f == 1) latency = 2 * stageA.K() + 1;
    else                 latency = 2 * stageA.K() + 1 + stageB.K() + 1;

    reset();
}

void LinearPhaseOversampler::reset()
{
    stageA.reset();
    stageB.reset();
    std::fill (delay2x.begin(), delay2x.end(), 0.0f);
}

float* const* LinearPhaseOversampler::up (const float* const* in, int numChannels, int n) noexcept
{
    const int nch = std::min (numChannels, channels);
    for (int ch = 0; ch < nch; ++ch)
    {
        float* o = os[(size_t) ch].data();
        if (log2f == 0)
        {
            std::copy (in[ch], in[ch] + n, o);
        }
        else if (log2f == 1)
        {
            stageA.up (ch, in[ch], o, n);
        }
        else
        {
            float* m = mid[(size_t) ch].data();
            stageA.up (ch, in[ch], m, n);
            stageB.up (ch, m, o, 2 * n);
        }
    }
    return osPtr.data();
}

void LinearPhaseOversampler::down (float* const* out, int numChannels, int n) noexcept
{
    const int nch = std::min (numChannels, channels);
    for (int ch = 0; ch < nch; ++ch)
    {
        float* o = os[(size_t) ch].data();
        if (log2f == 0)
        {
            std::copy (o, o + n, out[ch]);
        }
        else if (log2f == 1)
        {
            stageA.down (ch, o, out[ch], n);
        }
        else
        {
            float* m = mid[(size_t) ch].data();
            stageB.down (ch, o, m, 2 * n);
            // One sample of delay at 2x keeps the total latency integral at 1x.
            float d = delay2x[(size_t) ch];
            for (int i = 0; i < 2 * n; ++i)
                std::swap (d, m[i]);
            delay2x[(size_t) ch] = d;
            stageA.down (ch, m, out[ch], n);
        }
    }
}

} // namespace meq::dsp
