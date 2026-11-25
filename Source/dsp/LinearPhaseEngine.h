// GKEQ - 24-band dynamic EQ plug-in
// Copyright (C) 2026 Roman Gutekunst (GK)
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "FilterDesign.h"
#include "Dynamics.h"
#include "EngineTypes.h"
#include "PFFT.h"
#include <memory>
#include <vector>

namespace meq::dsp
{

/** Linear-phase EQ engine.

    The target magnitude (product of the analog band responses, no cramping)
    is turned into a windowed linear-phase FIR of length L every hop and
    applied with FFT convolution. Input segments are Hann-windowed with 50 %
    overlap, so kernel changes (automation, dynamics, solo) cross-fade
    smoothly. A 2x2 kernel matrix combines L/R- and M/S-placed bands in one
    pass. Latency is exactly L samples. */
class LinearPhaseEngine
{
public:
    LinearPhaseEngine();
    ~LinearPhaseEngine();

    void prepare (double sampleRate, int maxBlockSize, int numChannels);
    void setResolution (int resolutionIndex);   // real-time safe (no allocation)
    void reset();

    int resolution() const noexcept { return res; }
    int latencySamples() const noexcept { return L; }
    int latencyFor (int resolutionIndex) const noexcept;

    void process (float* const* io, int numChannels, int numSamples, const EngineInput& in);

private:
    void doHop (const EngineInput& in);
    bool updateTargets (const EngineInput& in);
    void designKernel (const float* mag, AlignedBuffer& kernelSpectrum);
    void computeBandMagnitude (int band, const DesignParams& d, float* out);

    double fs = 48000.0;
    int rateFactor = 1;
    int channels = 2;
    int res = -1;
    int L = 4096, H = 1024, N = 8192, bins = 2049;
    int maxL = 0;
    int hopPos = 0;

    std::vector<std::unique_ptr<RealFFT>> fftN, fftL;   // per resolution
    RealFFT* curN = nullptr;
    RealFFT* curL = nullptr;

    AlignedBuffer inBuf[2], acc[2], outReady[2], spec[2];
    AlignedBuffer segment, ySpec, timeBuf, kernelTime, orderedSpec;
    AlignedBuffer K[4];               // kernel spectra: 0=LL, 1=LR(R->L), 2=RL(L->R), 3=RR
    std::vector<std::vector<float>> hannTables, kaiserTables;  // per resolution: segment (2H) / kernel (L) windows

    // Target magnitudes on the L-point grid (bins = L/2 + 1)
    std::vector<float> bandMag;       // kNumBands * bins
    std::vector<float> prod[4];       // per-domain products: L, R, M, S
    std::vector<float> mat[4];        // kernel matrix targets: LL, R->L, L->R, RR
    std::vector<float> entry;
    bool kernelValid = false;
    bool crossTerms = false;

    struct BandCache
    {
        bool   valid = false;
        bool   active = false;
        double key[6] {};
        Placement place = Placement::Stereo;
    };
    BandCache cache[kNumBands];
    double soloKey[6] { -2.0, 0.0, 0.0, 0.0, 0.0, 0.0 };

    EnvelopeDetector det[kNumBands];
    double detKey[kNumBands][3] {};
    double dynOffset[kNumBands] {};
    double gridW0 = 0.0;   // rad/sample per bin
};

} // namespace meq::dsp
