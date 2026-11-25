// GKEQ - 24-band dynamic EQ plug-in
// Copyright (C) 2026 Roman Gutekunst (GK)
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "../SnapshotBuffer.h"
#include "../dsp/PFFT.h"
#include <vector>
#include <cmath>

namespace meq::ui
{

constexpr int    kSpecPoints = 512;
constexpr double kDispMinF   = 10.0;
constexpr double kDispMaxF   = 30000.0;

inline double pointFreq (int i) noexcept
{
    return kDispMinF * std::pow (kDispMaxF / kDispMinF, i / (double) (kSpecPoints - 1));
}

/** Real-time spectrum analyser producing dB levels on a fixed log-frequency grid. */
class SpectrumAnalyzer
{
public:
    SpectrumAnalyzer();

    /** resolution: 0 = low .. 3 = maximum. */
    void configure (int resolution, double sampleRate);

    /** Reads the newest window from src. Returns false if there was no new audio. */
    bool process (const SnapshotBuffer& src, double dtSeconds, int speed, double tiltDbPerOct);

    /** Analyses externally supplied samples (n >= fftSize()). */
    void processSamples (const float* x, double dtSeconds, int speed, double tiltDbPerOct);

    void decay (double dtSeconds, int speed);
    void reset();

    const float* levels() const noexcept { return smooth.data(); }   // displayed (tilted, smoothed)
    const float* rawLevels() const noexcept { return rawDb.data(); } // un-tilted, unsmoothed frame
    int fftSize() const noexcept { return N; }

    // Long-term average (for EQ Match)
    void setAveraging (bool on) noexcept { averaging = on; }
    void clearAverage();
    int  averageFrames() const noexcept { return avgFrames; }
    /** Average level in dB per point (un-tilted). */
    std::vector<float> averageDb() const;

private:
    void compute (const float* x, double dtSeconds, int speed, double tilt);

    int res = -1;
    double fs = 0.0;
    int N = 0;
    dsp::RealFFT fft;
    dsp::AlignedBuffer in, out;
    std::vector<float> window;
    float norm = 1.0f;
    std::vector<float> binDb, rawDb, smooth;
    std::vector<double> avgPow;
    int avgFrames = 0;
    bool averaging = false;
    uint64_t lastPos = 0;
};

} // namespace meq::ui
