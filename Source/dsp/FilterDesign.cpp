#include "FilterDesign.h"

namespace meq::dsp
{

namespace
{
    // Pole angles above this are clamped (beyond it the matched-z mapping would
    // alias across Nyquist). Bands placed above it fall back to a three-point
    // magnitude fit against the true analog curve.
    constexpr double kThetaMax = 0.97 * kPi;
    constexpr double kMatchMax = 0.90 * kPi;

    struct Phi { double p0, p1, p2; };

    inline Phi phis (double w) noexcept
    {
        const double s  = std::sin (0.5 * w);
        const double p1 = s * s;
        const double p0 = 1.0 - p1;
        return { p0, p1, 4.0 * p0 * p1 };
    }

    struct Den
    {
        double a1 = 0.0, a2 = 0.0;
        double wEff = 0.0;
        bool   clamped = false;

        double A0() const noexcept { const double v = 1.0 + a1 + a2; return v * v; }
        double A1() const noexcept { const double v = 1.0 - a1 + a2; return v * v; }
        double A2() const noexcept { return -4.0 * a2; }
        double mag2 (const Phi& f) const noexcept { return A0() * f.p0 + A1() * f.p1 + A2() * f.p2; }
        double dMag2 (const Phi& f) const noexcept { return -A0() + A1() + 4.0 * (f.p0 - f.p1) * A2(); }
    };

    /** Impulse-invariant (matched-z) mapping of an analog pole pair. */
    Den matchedPoles2 (double wp, double Qp) noexcept
    {
        Den d;
        const double zeta = 1.0 / (2.0 * std::max (1.0e-6, Qp));
        if (zeta < 1.0)
        {
            double theta = wp * std::sqrt (1.0 - zeta * zeta);
            double wpe = wp;
            if (theta > kThetaMax)
            {
                wpe   = wp * kThetaMax / theta;
                theta = kThetaMax;
                d.clamped = true;
            }
            const double r = std::exp (-zeta * wpe);
            d.a1 = -2.0 * r * std::cos (theta);
            d.a2 = r * r;
            d.wEff = wpe;
        }
        else
        {
            const double s  = std::sqrt (zeta * zeta - 1.0);
            const double r1 = std::exp (-wp * (zeta - s));
            const double r2 = std::exp (-wp * (zeta + s));
            d.a1 = -(r1 + r2);
            d.a2 = r1 * r2;
            d.wEff = wp;
            d.clamped = wp > kThetaMax;
        }
        return d;
    }

    /** Builds b0,b1,b2 from the squared-magnitude coefficients
        |B|^2 = B0*phi0 + B1*phi1 + B2*phi2 (minimum-phase solution). */
    Biquad factor (double B0, double B1, double B2, const Den& d) noexcept
    {
        B0 = std::max (0.0, B0);
        B1 = std::max (0.0, B1);
        const double sB0 = std::sqrt (B0), sB1 = std::sqrt (B1);
        const double W = 0.5 * (sB0 + sB1);
        double disc = W * W + B2;
        if (disc < 0.0) { B2 = -W * W; disc = 0.0; }

        Biquad q;
        q.b0 = 0.5 * (W + std::sqrt (disc));
        q.b1 = 0.5 * (sB0 - sB1);
        q.b2 = q.b0 > 1.0e-300 ? -B2 / (4.0 * q.b0) : 0.0;
        q.a1 = d.a1;
        q.a2 = d.a2;
        return q;
    }

    /** Numerator fitted to the analog magnitude at DC, Nyquist and wm. */
    Biquad designGeneric2 (const AnalogSection& s, const Den& d, double wm) noexcept
    {
        wm = juce::jlimit (1.0e-7, kMatchMax, wm);
        const double B0 = s.mag2 (0.0) * d.A0();
        const double B1 = s.mag2 (kPi) * d.A1();
        const Phi f = phis (wm);
        const double T  = s.mag2 (wm) * d.mag2 (f);
        const double B2 = (T - B0 * f.p0 - B1 * f.p1) / f.p2;
        return factor (B0, B1, B2, d);
    }

    /** Vicanek's matched peaking EQ: matches DC, the centre gain and a zero
        slope at the centre frequency. */
    Biquad designPeak (const AnalogSection& s, const Den& d, double w0) noexcept
    {
        const double G2 = s.peakGain * s.peakGain;
        const Phi f = phis (w0);
        const double R1 = d.mag2 (f) * G2;
        const double R2 = d.dMag2 (f) * G2;
        const double B0 = d.A0() * s.mag2 (0.0);
        const double B2 = (R1 - R2 * f.p1 - B0) / (4.0 * f.p1 * f.p1);
        const double B1 = R2 + B0 + 4.0 * (f.p1 - f.p0) * B2;
        return factor (B0, B1, B2, d);
    }

    /** Vicanek's matched band-pass (zero at DC, matched centre value + slope). */
    Biquad designBandPass (const AnalogSection& s, const Den& d, double w0) noexcept
    {
        const double H2 = s.mag2 (w0);
        const Phi f = phis (w0);
        const double R1 = d.mag2 (f) * H2;
        const double R2 = d.dMag2 (f) * H2;
        const double B2 = (R1 - R2 * f.p1) / (4.0 * f.p1 * f.p1);
        const double B1 = R2 + 4.0 * (f.p1 - f.p0) * B2;

        Biquad q;
        q.b1 = -0.5 * std::sqrt (std::max (0.0, B1));
        q.b0 = 0.5 * (std::sqrt (std::max (0.0, B2 + q.b1 * q.b1)) - q.b1);
        q.b2 = -q.b0 - q.b1;
        q.a1 = d.a1;
        q.a2 = d.a2;
        return q;
    }

    /** d|H|^2/dw of an analog section (analytic). */
    double analogDMag2 (const AnalogSection& s, double w) noexcept
    {
        const double w2 = w * w;
        const double nr = s.n0 - s.n2 * w2, ni = s.n1 * w;
        const double dr = s.d0 - s.d2 * w2, di = s.d1 * w;
        const double N = nr * nr + ni * ni;
        const double D = std::max (1.0e-300, dr * dr + di * di);
        const double dN = 2.0 * nr * (-2.0 * s.n2 * w) + 2.0 * ni * s.n1;
        const double dD = 2.0 * dr * (-2.0 * s.d2 * w) + 2.0 * di * s.d1;
        return (dN * D - N * dD) / (D * D);
    }

    /** Numerator matched to the analog magnitude at DC plus value *and slope*
        at wm (generalises Vicanek's peaking design to any section). Good at
        pinning resonant dips/peaks of the numerator. */
    Biquad designDeriv (const AnalogSection& s, const Den& d, double wm) noexcept
    {
        wm = juce::jlimit (1.0e-6, 0.95 * kPi, wm);
        const Phi f = phis (wm);
        const double H2 = s.mag2 (wm);
        const double dH2dx = analogDMag2 (s, wm) / (0.5 * std::sin (wm));
        const double R1 = H2 * d.mag2 (f);
        const double R2 = dH2dx * d.mag2 (f) + H2 * d.dMag2 (f);
        const double B0 = s.mag2 (0.0) * d.A0();
        const double x  = f.p1;
        const double B2 = (R1 - R2 * x - B0) / (4.0 * x * x);
        const double B1 = R2 + B0 + 4.0 * (f.p1 - f.p0) * B2;
        return factor (B0, B1, B2, d);
    }

    Biquad withFixedZeros (const Den& d, double k, double c1, double c2) noexcept
    {
        Biquad q;
        q.b0 = k; q.b1 = k * c1; q.b2 = k * c2;
        q.a1 = d.a1; q.a2 = d.a2;
        return q;
    }

    /** Bilinear transform of the analog section, pre-warped at wWarp. Poles
        differ from the matched-z set (the biquad carries its own a1/a2). */
    Biquad designBilinear (const AnalogSection& s, double wWarp) noexcept
    {
        wWarp = juce::jlimit (1.0e-6, 0.9 * kPi, wWarp);
        const double K = wWarp / std::tan (0.5 * wWarp);
        const double K2 = K * K;
        const double a0 = s.d2 * K2 + s.d1 * K + s.d0;
        const double inv = 1.0 / a0;
        Biquad q;
        q.b0 = (s.n2 * K2 + s.n1 * K + s.n0) * inv;
        q.b1 = (2.0 * s.n0 - 2.0 * s.n2 * K2) * inv;
        q.b2 = (s.n2 * K2 - s.n1 * K + s.n0) * inv;
        q.a1 = (2.0 * s.d0 - 2.0 * s.d2 * K2) * inv;
        q.a2 = (s.d2 * K2 - s.d1 * K + s.d0) * inv;
        return q;
    }

    /** Error-evaluation grid for choosing between candidate designs. */
    struct EvalSet
    {
        static constexpr int kMax = 20;
        Phi    f[kMax];
        double ref[kMax];   // analog |H|^2, floored at -60 dB
        double wt[kMax];    // weighting: full below pi/2, easing to 0.3 at 0.9 pi
        int    n = 0;

        void add (const AnalogSection& s, double w) noexcept
        {
            if (n >= kMax || w <= 0.0 || w >= 0.97 * kPi) return;
            f[n] = phis (w);
            ref[n] = std::max (1.0e-6, s.mag2 (w));
            wt[n] = w <= 0.5 * kPi ? 1.0 : std::max (0.3, 1.0 - 0.7 * (w - 0.5 * kPi) / (0.4 * kPi));
            ++n;
        }

        double error (const Biquad& q) const noexcept
        {
            if (! (std::isfinite (q.b0) && std::isfinite (q.b1) && std::isfinite (q.b2)))
                return 1.0e30;
            const double bs = q.b0 + q.b1 + q.b2, bd = q.b0 - q.b1 + q.b2;
            const double as = 1.0 + q.a1 + q.a2, ad = 1.0 - q.a1 + q.a2;
            double worst = 0.0;
            for (int i = 0; i < n; ++i)
            {
                const double B = bs * bs * f[i].p0 + bd * bd * f[i].p1 - 4.0 * q.b0 * q.b2 * f[i].p2;
                const double A = as * as * f[i].p0 + ad * ad * f[i].p1 - 4.0 * q.a2 * f[i].p2;
                const double m = std::max (1.0e-6, std::max (0.0, B) / std::max (1.0e-300, A));
                worst = std::max (worst, wt[i] * std::abs (std::log (m / ref[i])));
            }
            return worst;
        }
    };

    Biquad pickBest (const EvalSet& e, const Biquad* c, int n) noexcept
    {
        int best = 0;
        double bestErr = e.error (c[0]);
        for (int i = 1; i < n; ++i)
        {
            const double err = e.error (c[i]);
            if (err < bestErr) { bestErr = err; best = i; }
        }
        return c[best];
    }

    Biquad designSection1 (const AnalogSection& s) noexcept
    {
        const double wp = s.poleFreq();
        Biquad q;
        q.a1 = -std::exp (-wp);
        q.a2 = 0.0;
        q.b2 = 0.0;

        using K = AnalogSection::Kind;
        if (s.kind == K::HighPass)
        {
            q.b0 = std::sqrt (s.mag2 (kPi)) * (1.0 - q.a1) * 0.5;
            q.b1 = -q.b0;
        }
        else if (s.kind == K::AllPass)
        {
            q.b0 = q.a1;
            q.b1 = 1.0;
        }
        else
        {
            const double sum  = std::sqrt (s.mag2 (0.0)) * (1.0 + q.a1);
            const double diff = std::sqrt (s.mag2 (kPi)) * (1.0 - q.a1);
            q.b0 = 0.5 * (sum + diff);
            q.b1 = 0.5 * (sum - diff);

            // Near Nyquist a first-order section cannot match DC, Nyquist and
            // the transition at once: also try DC + centre and bilinear fits.
            const double wz = std::abs (s.n1) > 0.0 ? std::abs (s.n0 / s.n1) : wp;
            const double wm = std::sqrt (wp * wz);
            if (wm < 0.9 * kPi)
            {
                EvalSet ev;
                const double lo = std::max (1.0e-4, 0.02 * std::min (wp, wz)), hi = 0.9 * kPi;
                for (int i = 0; i < 16; ++i)
                    ev.add (s, lo * std::pow (hi / lo, i / 15.0));
                ev.add (s, wm);

                Biquad c[5] { q, q, q, q, q };
                // DC exact + magnitude matched at a second frequency
                auto dcPlus = [&] (Biquad& out, double wx)
                {
                    const Phi f = phis (wx);
                    const double A = (1.0 + q.a1) * (1.0 + q.a1) * f.p0 + (1.0 - q.a1) * (1.0 - q.a1) * f.p1;
                    const double T = s.mag2 (wx) * A;
                    const double dd = std::sqrt (std::max (0.0, (T - sum * sum * f.p0) / std::max (1.0e-300, f.p1)));
                    out.b0 = 0.5 * (sum + dd);
                    out.b1 = 0.5 * (sum - dd);
                };
                dcPlus (c[1], wm);
                dcPlus (c[2], juce::jmin (0.9 * kPi, 4.0 * wm));
                dcPlus (c[3], 0.5 * kPi);
                {
                    const double K = wm / std::tan (0.5 * wm);
                    const double a0 = s.d1 * K + s.d0;
                    c[4].b0 = (s.n1 * K + s.n0) / a0;
                    c[4].b1 = (s.n0 - s.n1 * K) / a0;
                    c[4].a1 = (s.d0 - s.d1 * K) / a0;
                }
                return pickBest (ev, c, 5);
            }
        }
        return q;
    }

    Biquad designSection2 (const AnalogSection& s) noexcept
    {
        const double wp = s.poleFreq();
        const double wz = s.zeroFreq();
        const Den d = matchedPoles2 (wp, s.poleQ());

        // Sections well below Nyquist are matched essentially perfectly by the
        // primary design. Higher up, several candidates are generated and the
        // one closest to the analog curve is chosen.
        const bool hasZeros = s.n2 > 0.0;
        const double zeroQ = (hasZeros && s.n1 > 0.0) ? wz * s.n2 / s.n1 : 0.0;
        const bool resonantZeros = zeroQ > 0.75;
        const bool refine = std::max (wp, hasZeros ? wz : 0.0) > 0.15 * kPi || d.clamped
                         || (resonantZeros && s.kind == AnalogSection::Kind::Generic);
        EvalSet ev;
        if (refine)
        {
            const double lo = std::max (1.0e-4, 0.02 * std::min (wp, hasZeros ? wz : wp));
            const double hi = 0.9 * kPi;
            for (int i = 0; i < 16; ++i)
                ev.add (s, lo * std::pow (hi / lo, i / 15.0));
            if (wp < hi) ev.add (s, wp);
            if (hasZeros && wz < hi) ev.add (s, wz);
        }

        Biquad c[8];
        int n = 0;

        using K = AnalogSection::Kind;
        switch (s.kind)
        {
            case K::Peak:
                if (! d.clamped && wp < kThetaMax)
                    c[n++] = designPeak (s, d, wp);
                c[n++] = designGeneric2 (s, d, d.clamped ? 0.8 * kThetaMax : wp);
                if (refine)
                {
                    c[n++] = designGeneric2 (s, d, 0.5 * juce::jmin (wp, kThetaMax));
                    if (wp < 0.9 * kPi) c[n++] = designBilinear (s, wp);
                }
                break;

            case K::BandPass:
                if (! d.clamped && wp < kMatchMax)
                    c[n++] = designBandPass (s, d, wp);
                c[n++] = designGeneric2 (s, d, d.clamped ? 0.8 * kThetaMax : wp);
                if (refine && wp < 0.9 * kPi) c[n++] = designBilinear (s, wp);
                break;

            case K::HighPass:
            {
                // Exact double zero at DC keeps the full stop-band slope; only
                // the gain is free, chosen at the best matching frequency.
                auto atW = [&] (double wm)
                {
                    wm = juce::jlimit (1.0e-6, kMatchMax, wm);
                    const Phi f = phis (wm);
                    return withFixedZeros (d, std::sqrt (s.mag2 (wm) * d.mag2 (f)) / (4.0 * f.p1), -2.0, 1.0);
                };
                c[n++] = atW (juce::jmin (d.wEff, kMatchMax));
                if (refine)
                {
                    c[n++] = atW (0.5 * (d.wEff + kPi));
                    c[n++] = atW (kMatchMax);
                    if (wp < 0.9 * kPi) c[n++] = designBilinear (s, wp);
                }
                break;
            }

            case K::LowPass:
                c[n++] = designGeneric2 (s, d, juce::jmin (d.wEff, kMatchMax));
                if (refine)
                {
                    c[n++] = designDeriv (s, d, juce::jmin (d.wEff, 0.9 * kPi));
                    c[n++] = designGeneric2 (s, d, 0.5 * d.wEff);
                    if (wp < 0.9 * kPi) c[n++] = designBilinear (s, wp);
                }
                break;

            case K::Notch:
            {
                if (! d.clamped && wp < 0.95 * kPi)
                {
                    const double cw = std::cos (wp);
                    const double kDC = std::sqrt (s.mag2 (0.0)) * (1.0 + d.a1 + d.a2) / (2.0 - 2.0 * cw);
                    c[n++] = withFixedZeros (d, kDC, -2.0 * cw, 1.0);
                    if (refine)
                    {
                        const double kNy = std::sqrt (s.mag2 (kPi)) * (1.0 - d.a1 + d.a2) / (2.0 + 2.0 * cw);
                        c[n++] = withFixedZeros (d, kNy, -2.0 * cw, 1.0);
                        c[n++] = withFixedZeros (d, std::sqrt (kDC * kNy), -2.0 * cw, 1.0);
                        c[n++] = designBilinear (s, wp);
                    }
                }
                else
                {
                    c[n++] = designGeneric2 (s, d, 0.8 * kThetaMax);
                }
                break;
            }

            case K::AllPass:
            {
                Biquad q;
                q.b0 = d.a2; q.b1 = d.a1; q.b2 = 1.0;
                q.a1 = d.a1; q.a2 = d.a2;
                return q;
            }

            case K::Generic:
            default:
            {
                // Shelf-type section.
                const double wmGeo = std::sqrt (wp * wz);
                if (resonantZeros && wz < 0.9 * kPi)
                    c[n++] = designDeriv (s, d, wz);   // pins a resonant dip/peak of the zeros
                c[n++] = designGeneric2 (s, d, juce::jmin (wmGeo, d.clamped ? 0.8 * kThetaMax : kMatchMax));
                if (refine)
                {
                    if (! resonantZeros && wz < 0.9 * kPi) c[n++] = designDeriv (s, d, wz);
                    if (wp < 0.9 * kPi && ! d.clamped) c[n++] = designDeriv (s, d, wp);
                    if (wz < kMatchMax) c[n++] = designGeneric2 (s, d, wz);
                    c[n++] = designGeneric2 (s, d, 0.5 * juce::jmin (wmGeo, kMatchMax));
                    if (wmGeo < 0.9 * kPi) c[n++] = designBilinear (s, wmGeo);
                }
                break;
            }
        }

        if (n == 1 || ! refine)
            return c[0];
        return pickBest (ev, c, n);
    }

    // -----------------------------------------------------------------------
    int shelfOrder (int slopeIdx) noexcept
    {
        return slopeIdx >= kBrickwallIdx ? 16 : juce::jlimit (1, 16, slopeDb (slopeIdx) / 6);
    }

    int cascadeCount (int slopeIdx) noexcept
    {
        return slopeIdx >= kBrickwallIdx ? 8 : juce::jlimit (1, 8, slopeDb (slopeIdx) / 12);
    }

    double butterworthZeta (int n, int k) noexcept
    {
        return std::sin ((2.0 * k + 1.0) * kPi / (2.0 * n));
    }

    /** Butterworth-style shelf of order n: poles and zeros on circles whose
        radii are split around w0 by gain^(1/2n). */
    void addShelf (AnalogFilter& f, double w0, double G, int n, double q, bool high)
    {
        // Note: at 0 dB the sections are exact identities. They are kept so the
        // section structure never changes while gain is swept (click-free).
        const double Gs = high ? 1.0 / G : G;
        const double r  = std::pow (Gs, 1.0 / (2.0 * n));
        const double wp = w0 / r;
        const double wz = w0 * r;

        for (int k = 0; k < n / 2; ++k)
        {
            double zeta = butterworthZeta (n, k);
            if (k == 0) zeta *= kDefaultQ / q;
            auto& s = f.add();
            s.kind = AnalogSection::Kind::Generic;
            s.order = 2;
            s.n2 = 1.0; s.n1 = 2.0 * zeta * wz; s.n0 = wz * wz;
            s.d2 = 1.0; s.d1 = 2.0 * zeta * wp; s.d0 = wp * wp;
        }
        if (n % 2 == 1)
        {
            auto& s = f.add();
            s.kind = AnalogSection::Kind::Generic;
            s.order = 1;
            s.n1 = 1.0; s.n0 = wz;
            s.d1 = 1.0; s.d0 = wp;
        }
        if (high)
            f.gain *= G;
    }

    void addButterworth (AnalogFilter& f, double w0, int n, double q, bool highPass)
    {
        for (int k = 0; k < n / 2; ++k)
        {
            double zeta = butterworthZeta (n, k);
            if (k == 0) zeta *= kDefaultQ / q;
            auto& s = f.add();
            s.order = 2;
            s.kind = highPass ? AnalogSection::Kind::HighPass : AnalogSection::Kind::LowPass;
            s.n2 = highPass ? 1.0 : 0.0;
            s.n1 = 0.0;
            s.n0 = highPass ? 0.0 : w0 * w0;
            s.d2 = 1.0; s.d1 = 2.0 * zeta * w0; s.d0 = w0 * w0;
        }
        if (n % 2 == 1)
        {
            auto& s = f.add();
            s.order = 1;
            s.kind = highPass ? AnalogSection::Kind::HighPass : AnalogSection::Kind::LowPass;
            s.n1 = highPass ? 1.0 : 0.0;
            s.n0 = highPass ? 0.0 : w0;
            s.d1 = 1.0; s.d0 = w0;
        }
    }

    void addBandPass2 (AnalogFilter& f, double w0, double q)
    {
        auto& s = f.add();
        s.kind = AnalogSection::Kind::BandPass;
        s.order = 2;
        s.n2 = 0.0; s.n1 = w0 / q; s.n0 = 0.0;
        s.d2 = 1.0; s.d1 = w0 / q; s.d0 = w0 * w0;
    }

    /** Constant dB/octave tilt over 20 Hz - 20 kHz, built from first-order
        shelves spaced one octave apart; 0 dB at the band frequency. */
    void addFlatTilt (AnalogFilter& f, double w0, double gainDb, double fs)
    {
        constexpr int N = 10;
        const double per = gainDb / N;
        for (int i = 0; i < N; ++i)
        {
            const double fc = 20.0 * std::pow (1000.0, (i + 0.5) / N);
            const double wc = kTwoPi * fc / fs;
            const double Gi = dbToGain (per);
            const double r  = std::sqrt (1.0 / Gi);
            auto& s = f.add();
            s.kind = AnalogSection::Kind::Generic;
            s.order = 1;
            s.n1 = 1.0; s.n0 = wc * r;
            s.d1 = 1.0; s.d0 = wc / r;
            f.gain *= Gi;
        }
        const double atCentre = f.mag (w0);
        if (atCentre > 0.0)
            f.gain /= atCentre;
    }
}

// ---------------------------------------------------------------------------
AnalogFilter designAnalog (const DesignParams& p, double fs)
{
    AnalogFilter f;
    fs = std::max (1000.0, fs);
    const double w0 = kTwoPi * juce::jlimit (kMinFreq, kMaxFreq, p.freq) / fs;
    const double q  = juce::jlimit (kMinQ, kMaxQ, p.q);
    const double G  = dbToGain (p.gainDb);
    const bool brick = p.slopeIdx >= kBrickwallIdx;

    switch (p.type)
    {
        case FilterType::Bell:
        {
            const double A = std::sqrt (G);
            auto& s = f.add();
            s.kind = AnalogSection::Kind::Peak;
            s.order = 2;
            s.n2 = 1.0; s.n1 = w0 * A / q;   s.n0 = w0 * w0;
            s.d2 = 1.0; s.d1 = w0 / (A * q); s.d0 = w0 * w0;
            s.peakGain = G;
            break;
        }

        case FilterType::LowShelf:  addShelf (f, w0, G, shelfOrder (p.slopeIdx), q, false); break;
        case FilterType::HighShelf: addShelf (f, w0, G, shelfOrder (p.slopeIdx), q, true); break;

        case FilterType::TiltShelf:
            addShelf (f, w0, G, shelfOrder (p.slopeIdx), q, true);
            f.gain /= std::sqrt (G);
            break;

        case FilterType::FlatTilt:
            addFlatTilt (f, w0, p.gainDb, fs);
            break;

        case FilterType::LowCut:
            if (brick && p.idealBrickwall) { f.brick = AnalogFilter::Brick::HighPass; f.brickW = w0; break; }
            addButterworth (f, w0, slopeOrder (p.slopeIdx), brick ? kDefaultQ : q, true);
            break;

        case FilterType::HighCut:
            if (brick && p.idealBrickwall) { f.brick = AnalogFilter::Brick::LowPass; f.brickW = w0; break; }
            addButterworth (f, w0, slopeOrder (p.slopeIdx), brick ? kDefaultQ : q, false);
            break;

        case FilterType::Notch:
        {
            const int k = cascadeCount (p.slopeIdx);
            const double qs = q / std::sqrt (std::pow (2.0, 1.0 / k) - 1.0);
            for (int i = 0; i < k; ++i)
            {
                auto& s = f.add();
                s.kind = AnalogSection::Kind::Notch;
                s.order = 2;
                s.n2 = 1.0; s.n1 = 0.0;     s.n0 = w0 * w0;
                s.d2 = 1.0; s.d1 = w0 / qs; s.d0 = w0 * w0;
            }
            break;
        }

        case FilterType::BandPass:
        {
            const int k = cascadeCount (p.slopeIdx);
            const double qs = q * std::sqrt (std::pow (2.0, 1.0 / k) - 1.0);
            for (int i = 0; i < k; ++i)
                addBandPass2 (f, w0, qs);
            break;
        }

        case FilterType::AllPass:
        {
            const int n = shelfOrder (p.slopeIdx);
            for (int i = 0; i < n / 2; ++i)
            {
                auto& s = f.add();
                s.kind = AnalogSection::Kind::AllPass;
                s.order = 2;
                s.n2 = 1.0; s.n1 = -w0 / q; s.n0 = w0 * w0;
                s.d2 = 1.0; s.d1 =  w0 / q; s.d0 = w0 * w0;
            }
            if (n % 2 == 1)
            {
                auto& s = f.add();
                s.kind = AnalogSection::Kind::AllPass;
                s.order = 1;
                s.n1 = 1.0; s.n0 = -w0;
                s.d1 = 1.0; s.d0 = w0;
            }
            break;
        }

        case FilterType::Count:
        default:
            break;
    }
    return f;
}

DigitalFilter designDigital (const AnalogFilter& a)
{
    DigitalFilter d;
    d.num = a.num;
    for (int i = 0; i < a.num; ++i)
        d.sec[i] = a.sec[i].order == 2 ? designSection2 (a.sec[i]) : designSection1 (a.sec[i]);

    if (d.num > 0)
    {
        d.sec[0].b0 *= a.gain;
        d.sec[0].b1 *= a.gain;
        d.sec[0].b2 *= a.gain;
        d.gain = 1.0;
    }
    else
    {
        d.gain = a.gain;
    }
    return d;
}

AnalogFilter designSoloFilter (const DesignParams& p, double fs)
{
    AnalogFilter f;
    const double w0 = kTwoPi * juce::jlimit (kMinFreq, kMaxFreq, p.freq) / std::max (1000.0, fs);
    const double q  = juce::jlimit (0.1, kMaxQ, p.q);

    switch (p.type)
    {
        case FilterType::LowShelf:
        case FilterType::LowCut:
            addButterworth (f, w0, 2, kDefaultQ, false);
            break;
        case FilterType::HighShelf:
        case FilterType::HighCut:
        case FilterType::TiltShelf:
            addButterworth (f, w0, 2, kDefaultQ, true);
            break;
        case FilterType::FlatTilt:
            break;
        case FilterType::Bell:
        case FilterType::Notch:
        case FilterType::BandPass:
        case FilterType::AllPass:
        case FilterType::Count:
        default:
            addBandPass2 (f, w0, q);
            break;
    }
    return f;
}

AnalogFilter designDetectorFilter (const DesignParams& p, double fs)
{
    AnalogFilter f;
    const double w0 = kTwoPi * juce::jlimit (kMinFreq, kMaxFreq, p.freq) / std::max (1000.0, fs);

    switch (p.type)
    {
        case FilterType::LowShelf:
            addButterworth (f, w0, 2, kDefaultQ, false);
            break;
        case FilterType::HighShelf:
        case FilterType::TiltShelf:
            addButterworth (f, w0, 2, kDefaultQ, true);
            break;
        case FilterType::FlatTilt:
            break;
        default:
            addBandPass2 (f, w0, juce::jlimit (0.2, 20.0, p.q));
            break;
    }
    return f;
}

} // namespace meq::dsp
