#include "Character.h"

namespace meq::dsp
{

void CharacterProcessor::prepare (double sampleRate, int maxBlockSize, int numChannels)
{
    baseRate = sampleRate;
    rate     = sampleRate * 2.0;
    channels = juce::jlimit (1, 2, numChannels);

    using OS = juce::dsp::Oversampling<float>;
    osIIR = std::make_unique<OS> ((size_t) channels, 1, OS::filterHalfBandPolyphaseIIR, true, false);
    osIIR->initProcessing ((size_t) juce::jmax (1, maxBlockSize));
    osFIR.prepare (channels, 1, juce::jmax (1, maxBlockSize));

    dcCoef = std::exp (-2.0 * juce::MathConstants<double>::pi * 5.0 / baseRate);
    reset();
}

void CharacterProcessor::reset()
{
    if (osIIR) osIIR->reset();
    osFIR.reset();
    activeMode = CharacterMode::Clean;
    mix = 0.0f;
    dcX[0] = dcX[1] = dcY[0] = dcY[1] = 0.0;
}

int CharacterProcessor::latencySamples (bool linear) const noexcept
{
    return linear ? osFIR.latencySamples() : 0;
}

float CharacterProcessor::shape (CharacterMode m, float x) noexcept
{
    switch (m)
    {
        case CharacterMode::Subtle:
        {
            // Gentle symmetric saturation (mostly 3rd harmonic), unity small-signal gain.
            constexpr float d = 0.35f;
            return std::tanh (d * x) / d;
        }
        case CharacterMode::Warm:
        {
            // Asymmetric transfer: 2nd harmonic dominant plus soft 3rd. DC removed later.
            constexpr float d = 0.7f, b = 0.06f;
            return std::tanh (d * (x + b * x * x)) / d;
        }
        case CharacterMode::Clean:
        default:
            return x;
    }
}

void CharacterProcessor::process (float* const* io, int numChannels, int numSamples, CharacterMode mode, bool linear)
{
    const int nch = juce::jmin (numChannels, channels);
    if (nch <= 0 || numSamples <= 0 || osIIR == nullptr) return;

    juce::dsp::AudioBlock<float> block (io, (size_t) nch, (size_t) numSamples);
    juce::dsp::AudioBlock<float> up;
    if (linear)
    {
        float* const* p = osFIR.up (io, nch, numSamples);
        up = juce::dsp::AudioBlock<float> (p, (size_t) nch, (size_t) numSamples * 2);
    }
    else
    {
        up = osIIR->processSamplesUp (juce::dsp::AudioBlock<const float> (io, (size_t) nch, (size_t) numSamples));
    }

    const int n = (int) up.getNumSamples();
    const float step = (float) (1.0 / (0.01 * rate));
    const float startMix = mix;
    const CharacterMode startMode = activeMode;
    float endMix = mix;
    CharacterMode endMode = activeMode;

    for (int ch = 0; ch < nch; ++ch)
    {
        float* x = up.getChannelPointer ((size_t) ch);
        float m = startMix;
        CharacterMode am = startMode;
        for (int i = 0; i < n; ++i)
        {
            const float target = (am == mode && mode != CharacterMode::Clean) ? 1.0f : 0.0f;
            if (m < target)      m = juce::jmin (target, m + step);
            else if (m > target) m = juce::jmax (target, m - step);
            if (m <= 0.0f && am != mode) am = mode;

            const float in = x[i];
            x[i] = in + m * (shape (am, in) - in);
        }
        endMix = m;
        endMode = am;
    }
    mix = endMix;
    activeMode = endMode;

    if (linear) osFIR.down (io, nch, numSamples);
    else        osIIR->processSamplesDown (block);

    // DC blocker (the asymmetric curve generates a small offset).
    for (int ch = 0; ch < nch; ++ch)
    {
        float* x = io[ch];
        double px = dcX[ch], py = dcY[ch];
        for (int i = 0; i < numSamples; ++i)
        {
            const double in = x[i];
            const double y = in - px + dcCoef * py;
            px = in; py = y;
            x[i] = (float) y;
        }
        dcX[ch] = px; dcY[ch] = py;
    }
}

} // namespace meq::dsp
