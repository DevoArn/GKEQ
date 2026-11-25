// GKEQ - 24-band dynamic EQ plug-in
// Copyright (C) 2026 Roman Gutekunst (GK)
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "../Params.h"
#include <cmath>

namespace meq::dsp
{

constexpr double kPi    = 3.14159265358979323846;
constexpr double kTwoPi = 2.0 * kPi;
constexpr int    kMaxSections = 16;

inline double dbToGain (double db) noexcept { return std::pow (10.0, db * 0.05); }
inline double gainToDb (double g)  noexcept { return 20.0 * std::log10 (std::max (g, 1.0e-20)); }

/** One analog section, H(s) = (n2 s^2 + n1 s + n0) / (d2 s^2 + d1 s + d0),
    with s expressed in radians per *sample* (i.e. analog frequency / fs).
    First-order sections have n2 == d2 == 0. */
struct AnalogSection
{
    enum class Kind : unsigned char { Generic, Peak, HighPass, LowPass, BandPass, Notch, AllPass };

    Kind   kind  = Kind::Generic;
    int    order = 2;
    double n0 = 1.0, n1 = 0.0, n2 = 0.0;
    double d0 = 1.0, d1 = 0.0, d2 = 0.0;
    double peakGain = 1.0;   // Peak: linear gain at the centre frequency

    double mag2 (double w) const noexcept
    {
        const double w2 = w * w;
        const double nr = n0 - n2 * w2, ni = n1 * w;
        const double dr = d0 - d2 * w2, di = d1 * w;
        return (nr * nr + ni * ni) / std::max (1.0e-300, dr * dr + di * di);
    }

    /** Pole frequency (rad/sample). */
    double poleFreq() const noexcept { return order == 2 ? std::sqrt (d0 / d2) : d0 / d1; }
    /** Pole quality factor (2nd order only). */
    double poleQ() const noexcept { return std::sqrt (d0 / d2) * d2 / std::max (1.0e-300, d1); }
    /** Zero frequency for sections with two finite zeros. */
    double zeroFreq() const noexcept { return n2 > 0.0 ? std::sqrt (std::abs (n0 / n2)) : poleFreq(); }
};

struct AnalogFilter
{
    enum class Brick : unsigned char { None, LowPass, HighPass };

    AnalogSection sec[kMaxSections];
    int    num  = 0;
    double gain = 1.0;            // overall linear scalar
    Brick  brick = Brick::None;   // ideal brickwall (linear-phase only)
    double brickW = 0.0;          // brickwall cut-off, rad/sample

    void clear() noexcept { num = 0; gain = 1.0; brick = Brick::None; }

    AnalogSection& add() noexcept
    {
        jassert (num < kMaxSections);
        auto& s = sec[juce::jmin (num, kMaxSections - 1)];
        s = AnalogSection();
        num = juce::jmin (num + 1, kMaxSections);
        return s;
    }

    double mag2 (double w) const noexcept
    {
        double m = gain * gain;
        for (int i = 0; i < num; ++i)
            m *= sec[i].mag2 (w);
        if (brick == Brick::LowPass  && w > brickW) m = 0.0;
        if (brick == Brick::HighPass && w < brickW) m = 0.0;
        return m;
    }

    double mag (double w) const noexcept { return std::sqrt (mag2 (w)); }
    bool isIdentity() const noexcept { return num == 0 && brick == Brick::None && std::abs (gain - 1.0) < 1.0e-12; }
};

struct Biquad
{
    double b0 = 1.0, b1 = 0.0, b2 = 0.0, a1 = 0.0, a2 = 0.0;

    double mag2 (double w) const noexcept
    {
        const double s  = std::sin (0.5 * w);
        const double p1 = s * s, p0 = 1.0 - p1, p2 = 4.0 * p0 * p1;
        const double bs = b0 + b1 + b2, bd = b0 - b1 + b2;
        const double as = 1.0 + a1 + a2, ad = 1.0 - a1 + a2;
        const double B = bs * bs * p0 + bd * bd * p1 - 4.0 * b0 * b2 * p2;
        const double A = as * as * p0 + ad * ad * p1 - 4.0 * a2 * p2;
        return std::max (0.0, B) / std::max (1.0e-300, A);
    }
};

struct DigitalFilter
{
    Biquad sec[kMaxSections];
    int    num  = 0;
    double gain = 1.0;

    double mag2 (double w) const noexcept
    {
        double m = gain * gain;
        for (int i = 0; i < num; ++i)
            m *= sec[i].mag2 (w);
        return m;
    }
};

struct DesignParams
{
    FilterType type     = FilterType::Bell;
    double     freq     = 1000.0;
    double     gainDb   = 0.0;
    double     q        = 1.0;
    int        slopeIdx = 1;
    bool       idealBrickwall = false;   // linear phase: brickwall as ideal response

    static DesignParams fromBand (const BandParams& b, double effGainDb, bool linearPhase) noexcept
    {
        DesignParams d;
        d.type = b.type; d.freq = b.freq; d.gainDb = effGainDb; d.q = b.q;
        d.slopeIdx = b.slopeIdx; d.idealBrickwall = linearPhase;
        return d;
    }
};

/** Builds the analog prototype of a band. */
AnalogFilter designAnalog (const DesignParams& p, double sampleRate);

/** Magnitude-matched digital realisation (matched-z poles, numerator fitted to
    the analog magnitude at DC, Nyquist and the section centre). This gives an
    analog-like response all the way up to Nyquist without oversampling. */
DigitalFilter designDigital (const AnalogFilter& a);

/** The filter used when a band is soloed: isolates the region the band acts on. */
AnalogFilter designSoloFilter (const DesignParams& p, double sampleRate);

/** Side-chain detection filter for a dynamic band (identity for broadband types). */
AnalogFilter designDetectorFilter (const DesignParams& p, double sampleRate);

} // namespace meq::dsp
