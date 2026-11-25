// GKEQ - 24-band dynamic EQ plug-in
// Copyright (C) 2026 Roman Gutekunst (GK)
// SPDX-License-Identifier: GPL-3.0-or-later

#include "CurveFit.h"
#include "Spectrum.h"
#include "../dsp/FilterDesign.h"

namespace meq::ui
{

std::vector<FittedBand> fitCurve (const std::vector<double>& target, int i0, int i1, int maxBands,
                                  double fs, double& residualRms, double tolerance)
{
    struct Fit { FittedBand b; std::vector<double> c; };

    auto evalCurve = [&] (const FittedBand& b, std::vector<double>& out)
    {
        dsp::DesignParams d;
        d.type = b.type; d.freq = b.freq; d.gainDb = b.gain; d.q = b.q; d.slopeIdx = 1;
        const auto a = dsp::designAnalog (d, fs);
        out.assign ((size_t) kSpecPoints, 0.0);
        for (int i = i0; i <= i1; ++i)
            out[(size_t) i] = 10.0 * std::log10 (a.mag2 (dsp::kTwoPi * pointFreq (i) / fs) + 1.0e-30);
    };
    auto sse = [&] (const std::vector<double>& r, const std::vector<double>* c)
    {
        double s = 0.0;
        for (int i = i0; i <= i1; ++i)
        {
            const double v = r[(size_t) i] - (c != nullptr ? (*c)[(size_t) i] : 0.0);
            s += v * v;
        }
        return s;
    };
    auto optimise = [&] (Fit& fit, const std::vector<double>& r)
    {
        // Gain by least squares (a band's dB response scales ~linearly with its gain).
        for (int pass = 0; pass < 2; ++pass)
        {
            evalCurve (fit.b, fit.c);
            double num = 0.0, den = 0.0;
            for (int i = i0; i <= i1; ++i) { num += r[(size_t) i] * fit.c[(size_t) i]; den += fit.c[(size_t) i] * fit.c[(size_t) i]; }
            if (den > 1.0e-9) fit.b.gain = juce::jlimit (-24.0, 24.0, fit.b.gain * num / den);
        }
        evalCurve (fit.b, fit.c);
        double best = sse (r, &fit.c);
        std::vector<double> tmp;
        for (int iter = 0; iter < 4; ++iter)
        {
            const double cand[6][3] { { std::pow (2.0, 1.0 / 12.0), 1.0, 1.0 }, { std::pow (2.0, -1.0 / 12.0), 1.0, 1.0 },
                                      { 1.0, 1.25, 1.0 }, { 1.0, 0.8, 1.0 }, { 1.0, 1.0, 1.08 }, { 1.0, 1.0, 0.92 } };
            bool improved = false;
            for (auto& c : cand)
            {
                FittedBand t = fit.b;
                t.freq = juce::jlimit (20.0, 20000.0, t.freq * c[0]);
                t.q    = juce::jlimit (0.3, 10.0, t.q * c[1]);
                t.gain = juce::jlimit (-24.0, 24.0, t.gain * c[2]);
                evalCurve (t, tmp);
                const double e = sse (r, &tmp);
                if (e < best) { best = e; fit.b = t; fit.c = tmp; improved = true; }
            }
            if (! improved) break;
        }
    };

    std::vector<double> R = target;
    R.resize ((size_t) kSpecPoints, 0.0);
    std::vector<Fit> fits;

    for (int k = 0; k < maxBands; ++k)
    {
        int best = -1;
        for (int i = i0; i <= i1; ++i)
            if (best < 0 || std::abs (R[(size_t) i]) > std::abs (R[(size_t) best])) best = i;
        if (best < 0 || std::abs (R[(size_t) best]) < tolerance) break;

        const double g = R[(size_t) best];
        const double sgn = g > 0 ? 1.0 : -1.0;
        int lo = best, hi = best;
        while (lo > i0 && R[(size_t) (lo - 1)] * sgn > std::abs (g) * 0.5) --lo;
        while (hi < i1 && R[(size_t) (hi + 1)] * sgn > std::abs (g) * 0.5) ++hi;
        const double bw = juce::jlimit (0.1, 6.0, std::log2 (pointFreq (hi) / pointFreq (lo)));

        Fit fit;
        fit.b = { FilterType::Bell, pointFreq (best), g,
                  juce::jlimit (0.3, 8.0, std::sqrt (std::pow (2.0, bw)) / (std::pow (2.0, bw) - 1.0)) };
        if (lo == i0 && pointFreq (best) < 150.0 && i0 > 0)
        {
            fit.b.type = FilterType::LowShelf;
            fit.b.freq = juce::jlimit (30.0, 400.0, pointFreq (hi));
            fit.b.q = kDefaultQ;
        }
        else if (hi == i1 && pointFreq (best) > 5000.0)
        {
            fit.b.type = FilterType::HighShelf;
            fit.b.freq = juce::jlimit (2500.0, 16000.0, pointFreq (lo));
            fit.b.q = kDefaultQ;
        }
        optimise (fit, R);
        for (int i = i0; i <= i1; ++i) R[(size_t) i] -= fit.c[(size_t) i];
        fits.push_back (std::move (fit));
    }

    for (int pass = 0; pass < 2; ++pass)
    {
        for (auto& fit : fits)
        {
            for (int i = i0; i <= i1; ++i) R[(size_t) i] += fit.c[(size_t) i];
            optimise (fit, R);
            for (int i = i0; i <= i1; ++i) R[(size_t) i] -= fit.c[(size_t) i];
        }
    }

    residualRms = std::sqrt (sse (R, nullptr) / std::max (1, i1 - i0 + 1));
    std::vector<FittedBand> out;
    for (auto& f : fits)
        if (std::abs (f.b.gain) >= 0.2) out.push_back (f.b);
    return out;
}

} // namespace meq::ui
