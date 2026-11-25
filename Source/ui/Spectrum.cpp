// GKEQ - 24-band dynamic EQ plug-in
// Copyright (C) 2026 Roman Gutekunst (GK)
// SPDX-License-Identifier: GPL-3.0-or-later

#include "Spectrum.h"
#include <algorithm>
#include <cmath>

namespace meq::ui
{

SpectrumAnalyzer::SpectrumAnalyzer()
    : rawDb ((size_t) kSpecPoints, -150.0f),
      smooth ((size_t) kSpecPoints, -150.0f),
      avgPow ((size_t) kSpecPoints, 0.0)
{
}

void SpectrumAnalyzer::configure (int resolution, double sampleRate)
{
    if (resolution == res && sampleRate == fs) return;
    res = resolution;
    fs = sampleRate > 0.0 ? sampleRate : 48000.0;

    const int base[] = { 2048, 4096, 8192, 16384 };
    int factor = 1;
    if (fs > 100000.0) factor = 4;
    else if (fs > 50000.0) factor = 2;
    N = base[std::clamp (resolution, 0, 3)] * factor;

    fft.init (N);
    in.allocate ((size_t) N);
    out.allocate ((size_t) N);
    window.assign ((size_t) N, 0.0f);
    double sum = 0.0;
    for (int i = 0; i < N; ++i)
    {
        const double t = 2.0 * 3.14159265358979323846 * i / (double) N;
        const double w = 0.35875 - 0.48829 * std::cos (t) + 0.14128 * std::cos (2.0 * t) - 0.01168 * std::cos (3.0 * t);
        window[(size_t) i] = (float) w;
        sum += w;
    }
    norm = (float) (2.0 / sum);
    binDb.assign ((size_t) (N / 2 + 1), -150.0f);
}

void SpectrumAnalyzer::reset()
{
    std::fill (smooth.begin(), smooth.end(), -150.0f);
    std::fill (rawDb.begin(), rawDb.end(), -150.0f);
    lastPos = 0;
}

void SpectrumAnalyzer::clearAverage()
{
    std::fill (avgPow.begin(), avgPow.end(), 0.0);
    avgFrames = 0;
}

std::vector<float> SpectrumAnalyzer::averageDb() const
{
    std::vector<float> r ((size_t) kSpecPoints, -150.0f);
    if (avgFrames == 0) return r;
    for (int i = 0; i < kSpecPoints; ++i)
        r[(size_t) i] = (float) (10.0 * std::log10 (avgPow[(size_t) i] / avgFrames + 1.0e-30));
    return r;
}

bool SpectrumAnalyzer::process (const SnapshotBuffer& src, double dt, int speed, double tilt)
{
    if (N <= 0) return false;
    const uint64_t pos = src.position();
    if (pos == lastPos)
    {
        decay (dt, speed);
        return false;
    }
    lastPos = pos;
    src.read (in.data(), N);
    compute (in.data(), dt, speed, tilt);
    return true;
}

void SpectrumAnalyzer::processSamples (const float* x, double dt, int speed, double tilt)
{
    if (N <= 0) return;
    std::copy (x, x + N, in.data());
    compute (in.data(), dt, speed, tilt);
}

void SpectrumAnalyzer::decay (double dt, int speed)
{
    const double relTau[] = { 1.2, 0.6, 0.3, 0.15, 0.07 };
    const float c = (float) (1.0 - std::exp (-dt / relTau[std::clamp (speed, 0, 4)]));
    for (auto& v : smooth)
        v += (-150.0f - v) * c;
}

void SpectrumAnalyzer::compute (const float* x, double dt, int speed, double tilt)
{
    float* buf = in.data();
    if (buf != x) std::copy (x, x + N, buf);
    for (int i = 0; i < N; ++i)
        buf[i] *= window[(size_t) i];

    fft.forwardOrdered (buf, out.data());
    const float* o = out.data();
    const int half = N / 2;
    binDb[0] = -150.0f;
    for (int k = 1; k < half; ++k)
    {
        const float re = o[2 * k], im = o[2 * k + 1];
        const float m = std::sqrt (re * re + im * im) * norm;
        binDb[(size_t) k] = 20.0f * std::log10 (m + 1.0e-9f);
    }
    binDb[(size_t) half] = 20.0f * std::log10 (std::abs (o[1]) * norm + 1.0e-9f);

    const double ratio = std::pow (kDispMaxF / kDispMinF, 1.0 / (kSpecPoints - 1));
    const double binHz = fs / N;

    for (int i = 0; i < kSpecPoints; ++i)
    {
        const double f = pointFreq (i);
        float db;
        if (f >= 0.5 * fs)
        {
            db = -150.0f;
        }
        else
        {
            const double b = f / binHz;
            const double span = b * (ratio - 1.0);
            if (span < 1.5)
            {
                // Interpolate between bins (in the magnitude domain)
                const int k0 = std::clamp ((int) std::floor (b), 1, half - 1);
                const double frac = std::clamp (b - k0, 0.0, 1.0);
                const double m0 = std::pow (10.0, binDb[(size_t) k0] / 20.0);
                const double m1 = std::pow (10.0, binDb[(size_t) (k0 + 1)] / 20.0);
                db = (float) (20.0 * std::log10 (m0 + (m1 - m0) * frac + 1.0e-12));
            }
            else
            {
                const int k0 = std::clamp ((int) std::floor (b - 0.5 * span), 1, half);
                const int k1 = std::clamp ((int) std::ceil (b + 0.5 * span), 1, half);
                float mx = -150.0f;
                for (int k = k0; k <= k1; ++k) mx = std::max (mx, binDb[(size_t) k]);
                db = mx;
            }
        }
        rawDb[(size_t) i] = db;
    }

    float frameMax = -150.0f;
    for (float v : rawDb) frameMax = std::max (frameMax, v);
    if (averaging && frameMax > -110.0f)   // ignore silence (stopped transport)
    {
        for (int i = 0; i < kSpecPoints; ++i)
            avgPow[(size_t) i] += std::pow (10.0, rawDb[(size_t) i] / 10.0);
        ++avgFrames;
    }

    const double atkTau[] = { 0.12, 0.06, 0.03, 0.015, 0.006 };
    const double relTau[] = { 1.2, 0.6, 0.3, 0.15, 0.07 };
    const int sp = std::clamp (speed, 0, 4);
    const float ca = (float) (1.0 - std::exp (-dt / atkTau[sp]));
    const float cr = (float) (1.0 - std::exp (-dt / relTau[sp]));

    float prev = rawDb[0];
    for (int i = 0; i < kSpecPoints; ++i)
    {
        // light spatial smoothing
        const float next = rawDb[(size_t) std::min (i + 1, kSpecPoints - 1)];
        const float cur = rawDb[(size_t) i];
        float v = 0.25f * prev + 0.5f * cur + 0.25f * next;
        prev = cur;
        if (cur <= -149.0f) v = -150.0f;
        else v += (float) (tilt * std::log2 (pointFreq (i) / 1000.0));

        float& s = smooth[(size_t) i];
        s += (v - s) * (v > s ? ca : cr);
    }
}

} // namespace meq::ui
