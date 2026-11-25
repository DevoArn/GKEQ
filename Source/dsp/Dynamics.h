// GKEQ - 24-band dynamic EQ plug-in
// Copyright (C) 2026 Roman Gutekunst (GK)
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "FilterDesign.h"

namespace meq::dsp
{

/** Band-limited level detector for a dynamic EQ band. */
class EnvelopeDetector
{
public:
    void prepare (double sampleRate) noexcept
    {
        fs = sampleRate;
        reset();
    }

    void reset() noexcept
    {
        z[0][0] = z[0][1] = z[1][0] = z[1][1] = 0.0;
        env = 0.0;
    }

    void setFilter (const DigitalFilter& f) noexcept
    {
        hasFilter = f.num > 0;
        if (hasFilter) c = f.sec[0];
        g = f.gain;
    }

    void setTimes (double attackMs, double releaseMs) noexcept
    {
        atk = 1.0 - std::exp (-1.0 / (std::max (0.05, attackMs) * 0.001 * fs));
        rel = 1.0 - std::exp (-1.0 / (std::max (1.0, releaseMs) * 0.001 * fs));
    }

    /** l/r: detection signal (r may be nullptr for mono). */
    void process (const float* l, const float* r, int n, Placement p) noexcept
    {
        for (int i = 0; i < n; ++i)
        {
            double pw = 0.0;
            const double xl = l[i];
            const double xr = r != nullptr ? (double) r[i] : xl;
            switch (p)
            {
                case Placement::Stereo:
                {
                    const double a = filt (0, xl);
                    const double b = r != nullptr ? filt (1, xr) : a;
                    pw = std::max (a * a, b * b);
                    break;
                }
                case Placement::Left:  { const double a = filt (0, xl); pw = a * a; break; }
                case Placement::Right: { const double a = filt (1, xr); pw = a * a; break; }
                case Placement::Mid:   { const double a = filt (0, 0.5 * (xl + xr)); pw = a * a; break; }
                case Placement::Side:  { const double a = filt (0, 0.5 * (xl - xr)); pw = a * a; break; }
                case Placement::Count:
                default: break;
            }
            env += (pw > env ? atk : rel) * (pw - env);
        }
        if (env < 1.0e-30) env = 0.0;
    }

    double levelDb() const noexcept { return 10.0 * std::log10 (env + 1.0e-24); }

private:
    double filt (int ch, double x) noexcept
    {
        if (! hasFilter) return x * g;
        double* s = z[ch];
        const double y = c.b0 * x + s[0];
        s[0] = c.b1 * x - c.a1 * y + s[1];
        s[1] = c.b2 * x - c.a2 * y;
        return y * g;
    }

    double fs = 48000.0;
    Biquad c;
    double g = 1.0;
    bool hasFilter = false;
    double z[2][2] {};
    double env = 0.0;
    double atk = 0.1, rel = 0.01;
};

/** Gain computer: soft-knee, roughly 4:1 until the band's range is reached.
    A negative range compresses the band, a positive range expands it. */
inline double dynamicGainOffset (double levelDb, double thresholdDb, double rangeDb) noexcept
{
    if (std::abs (rangeDb) < 1.0e-6)
        return 0.0;

    constexpr double knee = 6.0;
    const double over = levelDb - thresholdDb;
    double o;
    if (over <= -0.5 * knee)      o = 0.0;
    else if (over < 0.5 * knee)   o = (over + 0.5 * knee) * (over + 0.5 * knee) / (2.0 * knee);
    else                          o = over;

    const double amount = std::min (1.0, o * 0.75 / std::abs (rangeDb));
    return rangeDb * amount;
}

} // namespace meq::dsp
