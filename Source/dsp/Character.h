#pragma once

#include "../Params.h"
#include "Oversampler.h"
#include <juce_dsp/juce_dsp.h>

namespace meq::dsp
{

/** Analog-style colouration ("Subtle" / "Warm"), 2x oversampled.
    Uses a polyphase IIR oversampler in Zero Latency mode (no reported
    latency) and a linear-phase FIR oversampler in the other modes. */
class CharacterProcessor
{
public:
    void prepare (double sampleRate, int maxBlockSize, int numChannels);
    void reset();

    int latencySamples (bool linearPhaseOversampling) const noexcept;

    void process (float* const* io, int numChannels, int numSamples, CharacterMode mode, bool linearPhaseOversampling);

    static float shape (CharacterMode m, float x) noexcept;

private:
    std::unique_ptr<juce::dsp::Oversampling<float>> osIIR;
    LinearPhaseOversampler osFIR;
    double rate = 96000.0, baseRate = 48000.0;
    int channels = 2;
    CharacterMode activeMode = CharacterMode::Clean;
    float mix = 0.0f;
    double dcX[2] {}, dcY[2] {};
    double dcCoef = 0.9995;
};

} // namespace meq::dsp
