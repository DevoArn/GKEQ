// Numerical verification of the GKEQ DSP core.
#include "dsp/FilterDesign.h"
#include "dsp/IIREngine.h"
#include "dsp/LinearPhaseEngine.h"
#include "dsp/Character.h"

#include <chrono>
#include <complex>
#include <cstdio>
#include <vector>

using namespace meq;
using namespace meq::dsp;

static int failures = 0;
#define CHECK(cond, ...) do { if (! (cond)) { ++failures; std::printf ("  FAIL: "); std::printf (__VA_ARGS__); std::printf ("\n"); } } while (0)

static double clampDb (double db) { return std::max (-90.0, db); }

static double dftMagDb (const std::vector<float>& h, int start, double w)
{
    std::complex<double> sum = 0.0;
    const std::complex<double> step = std::polar (1.0, -w);
    std::complex<double> rot = 1.0;
    for (size_t n = (size_t) start; n < h.size(); ++n)
    {
        sum += (double) h[n] * rot;
        rot *= step;
        if ((n & 1023) == 0) rot /= std::abs (rot);
    }
    return gainToDb (std::abs (sum));
}

// ---------------------------------------------------------------------------
static void testDesigns()
{
    std::printf ("[1] Matched filter designs vs analog prototypes\n");
    const double rates[] = { 44100.0, 48000.0, 96000.0 };
    const double freqs[] = { 20.0, 100.0, 1000.0, 5000.0, 12000.0, 17000.0 };
    const double gains[] = { -18.0, -6.0, 6.0, 18.0 };
    const double qs[]    = { 0.3, kDefaultQ, 2.0, 8.0 };
    const int slopes[]   = { 0, 1, 3, 8, kBrickwallIdx };

    for (int t = 0; t < (int) FilterType::Count; ++t)
    {
        double worst = 0.0, worstBelow10k = 0.0;
        juce::String worstDesc, worstMidDesc;
        int unstable = 0;

        for (double fs : rates)
        for (double f : freqs)
        for (double g : gains)
        for (double q : qs)
        for (int sl : slopes)
        {
            if (! typeHasSlope ((FilterType) t) && sl != 1) continue;
            if (! typeHasGain ((FilterType) t) && g != 6.0) continue;

            DesignParams d;
            d.type = (FilterType) t; d.freq = f; d.gainDb = g; d.q = q; d.slopeIdx = sl;
            const auto a  = designAnalog (d, fs);
            const auto dg = designDigital (a);

            for (int s = 0; s < dg.num; ++s)
            {
                const auto& c = dg.sec[s];
                if (! (std::abs (c.a2) < 1.0 && std::abs (c.a1) < 1.0 + c.a2)) ++unstable;
                if (! std::isfinite (c.b0) || ! std::isfinite (c.b1) || ! std::isfinite (c.b2)) ++unstable;
            }

            // Compare up to 20 kHz (or 0.9 * Nyquist)
            const double fmax = std::min (20000.0, 0.45 * fs);
            for (int i = 0; i <= 200; ++i)
            {
                const double fr = 20.0 * std::pow (fmax / 20.0, i / 200.0);
                const double w  = kTwoPi * fr / fs;
                const double ea = clampDb (gainToDb (a.mag (w)));
                const double ed = clampDb (gainToDb (std::sqrt (dg.mag2 (w))));
                if (ea < -30.0) continue;   // deep stop-band: relative errors are meaningless
                const double err = std::abs (ea - ed);
                if (err > worst)
                {
                    worst = err;
                    worstDesc = juce::String (fs) + " Hz fs, f=" + juce::String (f) + ", g=" + juce::String (g)
                              + ", q=" + juce::String (q) + ", slope=" + juce::String (sl) + " @ " + juce::String (fr, 0) + " Hz";
                }
                if (fr < 10000.0 && f <= 0.2 * fs && err > worstBelow10k)
                {
                    worstBelow10k = err;
                    worstMidDesc = juce::String (fs) + " Hz fs, f=" + juce::String (f) + ", g=" + juce::String (g)
                                 + ", q=" + juce::String (q) + ", slope=" + juce::String (sl) + " @ " + juce::String (fr, 0) + " Hz"
                                 + " (analog " + juce::String (ea, 2) + " / digital " + juce::String (ed, 2) + ")";
                }
            }
        }

        std::printf ("  %-11s worst err %.3f dB (below 10k: %.3f dB) %s%s\n",
                     filterTypeNames()[t].toRawUTF8(), worst, worstBelow10k,
                     worst > 0.05 ? "[" : "", worst > 0.05 ? (worstDesc + "]").toRawUTF8() : "");
        if (worstBelow10k > 0.05) std::printf ("      mid-band worst: %s\n", worstMidDesc.toRawUTF8());
        CHECK (unstable == 0, "%s: %d unstable/non-finite sections", filterTypeNames()[t].toRawUTF8(), unstable);
        CHECK (worstBelow10k < 0.6, "%s: mid-band matching error %.3f dB", filterTypeNames()[t].toRawUTF8(), worstBelow10k);
    }
}

// ---------------------------------------------------------------------------
struct TestSetup
{
    BandParams bands[kNumBands];
    GlobalParams g;
    float offsets[kNumBands] {};
    EngineInput in;

    TestSetup()
    {
        in.bands = bands; in.global = &g; in.dynOffsetsOut = offsets;
    }

    void standardCurve()
    {
        auto& b0 = bands[0]; b0.used = true; b0.type = FilterType::Bell;      b0.freq = 1000;  b0.gain = 6;  b0.q = 1;
        auto& b1 = bands[1]; b1.used = true; b1.type = FilterType::LowCut;    b1.freq = 80;    b1.slopeIdx = 3; b1.q = kDefaultQ;
        auto& b2 = bands[2]; b2.used = true; b2.type = FilterType::HighShelf; b2.freq = 8000;  b2.gain = -4; b2.q = kDefaultQ;
        auto& b3 = bands[3]; b3.used = true; b3.type = FilterType::Bell;      b3.freq = 300;   b3.gain = -8; b3.q = 4;
    }

    double analogDb (double fs, double w) const
    {
        double db = 0.0;
        for (const auto& b : bands)
            if (b.isActive())
                db += gainToDb (designAnalog (DesignParams::fromBand (b, b.gain, true), fs).mag (w));
        return db;
    }
};

template <typename Engine>
static std::vector<float> impulseResponse (Engine& e, TestSetup& s, int length, int block, int channel = 0)
{
    std::vector<float> L ((size_t) length, 0.0f), R ((size_t) length, 0.0f);
    (channel == 0 ? L : R)[0] = 1.0f;
    for (int pos = 0; pos < length; pos += block)
    {
        const int n = std::min (block, length - pos);
        float* io[2] = { L.data() + pos, R.data() + pos };
        e.process (io, 2, n, s.in);
    }
    return channel == 0 ? L : R;
}

static void checkResponse (const char* name, const std::vector<float>& h, int latency, const TestSetup& s,
                           double fs, double tolDb, double fmin = 30.0)
{
    double worst = 0.0, worstF = 0.0;
    for (int i = 0; i <= 120; ++i)
    {
        const double fr = fmin * std::pow (std::min (20000.0, 0.45 * fs) / fmin, i / 120.0);
        const double w = kTwoPi * fr / fs;
        const double target = clampDb (s.analogDb (fs, w));
        if (target < -40.0) continue;
        const double got = clampDb (dftMagDb (h, latency > 0 ? 0 : 0, w));
        const double err = std::abs (target - got);
        if (err > worst) { worst = err; worstF = fr; }
    }
    std::printf ("  %-28s latency %6d  worst |err| %.3f dB @ %.0f Hz\n", name, latency, worst, worstF);
    CHECK (worst < tolDb, "%s response error %.3f dB > %.3f dB", name, worst, tolDb);
}

static void testEngines()
{
    std::printf ("[2] Engine impulse responses vs analog target\n");
    const double fs = 48000.0;
    const int len = 1 << 17;

    {
        TestSetup s; s.standardCurve();
        IIREngine e; e.prepare (fs, 512, 2, 0);
        auto h = impulseResponse (e, s, len, 512);
        checkResponse ("Zero Latency", h, e.latencySamples(), s, fs, 0.15);
    }
    {
        TestSetup s; s.standardCurve();
        IIREngine e; e.prepare (fs, 512, 2, 2);
        auto h = impulseResponse (e, s, len, 512);
        checkResponse ("Natural Phase (4x)", h, e.latencySamples(), s, fs, 0.15);
    }
    for (int r = 0; r < kNumLPRes; ++r)
    {
        TestSetup s; s.standardCurve();
        LinearPhaseEngine e; e.prepare (fs, 512, 2); e.setResolution (r);
        auto h = impulseResponse (e, s, len, 512);

        int peak = 0;
        for (int i = 1; i < (int) h.size(); ++i) if (std::abs (h[(size_t) i]) > std::abs (h[(size_t) peak])) peak = i;

        // Symmetry around the latency point => linear phase
        double asym = 0.0, energy = 0.0;
        const int lat = e.latencySamples();
        for (int k = 1; k < lat && lat + k < (int) h.size(); ++k)
        {
            asym   += std::abs ((double) h[(size_t) (lat + k)] - (double) h[(size_t) (lat - k)]);
            energy += std::abs ((double) h[(size_t) (lat + k)]);
        }
        const auto name = juce::String ("Linear Phase ") + lpResolutionNames()[r];
        const double tol[kNumLPRes]  = { 2.0, 1.0, 1.0, 0.4, 0.15 };
        const double fmin[kNumLPRes] = { 120.0, 120.0, 40.0, 40.0, 40.0 };
        checkResponse (name.toRawUTF8(), h, lat, s, fs, tol[r], fmin[r]);
        std::printf ("      peak at %d (latency %d), asymmetry %.2e\n", peak, lat, asym / std::max (1.0e-12, energy));
        CHECK (peak == lat, "LP peak %d != latency %d", peak, lat);
        CHECK (asym / std::max (1.0e-12, energy) < 1.0e-3, "LP response not symmetric");
    }
}

// ---------------------------------------------------------------------------
static void testNull()
{
    std::printf ("[3] Null tests (no bands => transparent)\n");
    const double fs = 44100.0;
    const int len = 1 << 15;
    // Band-limited test signal (oversampling filters legitimately roll off
    // the last ~1 kHz below Nyquist).
    juce::Random rng (42);
    std::vector<float> src ((size_t) len, 0.0f);
    for (int k = 0; k < 120; ++k)
    {
        const double f = 20.0 * std::pow (15000.0 / 20.0, rng.nextDouble());
        const double ph = rng.nextDouble() * kTwoPi;
        for (int i = 0; i < len; ++i)
            src[(size_t) i] += (float) (0.01 * std::sin (kTwoPi * f * i / fs + ph));
    }

    auto run = [&] (auto& e, int lat, const char* name)
    {
        TestSetup s;
        std::vector<float> L = src, R = src;
        for (int pos = 0; pos < len; pos += 333)
        {
            const int n = std::min (333, len - pos);
            float* io[2] = { L.data() + pos, R.data() + pos };
            e.process (io, 2, n, s.in);
        }
        double err = 0.0;
        for (int i = lat; i < len; ++i)
            err = std::max (err, (double) std::abs (L[(size_t) i] - src[(size_t) (i - lat)]));

        // Find the actual delay (best-matching lag)
        int bestLag = 0; double bestErr = 1.0e30;
        for (int lag = std::max (0, lat - 100); lag < lat + 100; ++lag)
        {
            double e2 = 0.0;
            for (int i = lat + 4096; i < lat + 8192; ++i) { const double d = L[(size_t) i] - src[(size_t) (i - lag)]; e2 += d * d; }
            if (e2 < bestErr) { bestErr = e2; bestLag = lag; }
        }
        std::printf ("  %-20s max deviation %.2e (%.1f dB), reported latency %d, measured %d\n",
                     name, err, gainToDb (err), lat, bestLag);
        CHECK (gainToDb (err) < -60.0, "%s not transparent", name);
        CHECK (bestLag == lat, "%s latency mismatch", name);
    };

    { IIREngine e; e.prepare (fs, 512, 2, 0); run (e, 0, "Zero Latency"); }
    { IIREngine e; e.prepare (fs, 512, 2, 2); run (e, e.latencySamples(), "Natural Phase"); }
    { LinearPhaseEngine e; e.prepare (fs, 512, 2); e.setResolution (1); run (e, e.latencySamples(), "Linear Phase"); }
}

// ---------------------------------------------------------------------------
static double rmsDb (const float* x, int n)
{
    double s = 0.0;
    for (int i = 0; i < n; ++i) s += (double) x[i] * x[i];
    return 10.0 * std::log10 (s / n + 1.0e-30);
}

static void testDynamics()
{
    std::printf ("[4] Dynamic EQ\n");
    const double fs = 48000.0;
    const int len = 48000;

    auto run = [&] (float amp, auto& e) -> double
    {
        TestSetup s;
        auto& b = s.bands[0];
        b.used = true; b.type = FilterType::Bell; b.freq = 1000; b.gain = 0; b.q = 1;
        b.dynOn = true; b.dynRange = -12; b.threshold = -30; b.attackMs = 5; b.releaseMs = 50;
        std::vector<float> L ((size_t) len), R ((size_t) len);
        for (int i = 0; i < len; ++i) L[(size_t) i] = R[(size_t) i] = amp * (float) std::sin (kTwoPi * 1000.0 * i / fs);
        for (int pos = 0; pos < len; pos += 256)
        {
            float* io[2] = { L.data() + pos, R.data() + pos };
            e.process (io, 2, std::min (256, len - pos), s.in);
        }
        return rmsDb (L.data() + len / 2, len / 2) - rmsDb (R.data(), 1) * 0.0 - gainToDb (amp / std::sqrt (2.0));
    };

    {
        IIREngine q; q.prepare (fs, 256, 2, 0);
        const double quiet = run (0.003f, q);       // ~ -53 dBFS: below threshold
        IIREngine l; l.prepare (fs, 256, 2, 0);
        const double loud = run (0.5f, l);          // ~ -9 dBFS: far above
        std::printf ("  ZL: quiet signal gain %.2f dB, loud signal gain %.2f dB\n", quiet, loud);
        CHECK (std::abs (quiet) < 0.3, "quiet signal changed by %.2f dB", quiet);
        CHECK (loud < -10.0 && loud > -12.5, "loud signal reduction %.2f dB not within range", loud);
    }
    {
        LinearPhaseEngine l; l.prepare (fs, 256, 2); l.setResolution (1);
        const double loud = run (0.5f, l);
        std::printf ("  LP: loud signal gain %.2f dB\n", loud);
        CHECK (loud < -10.0 && loud > -12.5, "LP loud signal reduction %.2f dB", loud);
    }
}

// ---------------------------------------------------------------------------
static void testMidSide()
{
    std::printf ("[5] Stereo placement\n");
    const double fs = 48000.0;
    const int len = 1 << 15;
    // Side-only cut: a mono (L==R) signal must pass untouched.
    auto run = [&] (auto& e, int lat, const char* name)
    {
        TestSetup s;
        auto& b = s.bands[0];
        b.used = true; b.type = FilterType::HighCut; b.freq = 200; b.slopeIdx = 3; b.placement = Placement::Side;
        juce::Random rng (7);
        std::vector<float> src ((size_t) len);
        for (auto& v : src) v = rng.nextFloat() - 0.5f;
        std::vector<float> L = src, R = src;
        for (int pos = 0; pos < len; pos += 512)
        {
            float* io[2] = { L.data() + pos, R.data() + pos };
            e.process (io, 2, std::min (512, len - pos), s.in);
        }
        double err = 0.0;
        for (int i = lat + 2048; i < len; ++i)
            err = std::max (err, (double) std::abs (L[(size_t) i] - src[(size_t) (i - lat)]));
        std::printf ("  %-14s mono through side-only band: max deviation %.1f dB\n", name, gainToDb (err));
        CHECK (gainToDb (err) < -60.0, "%s: side band affected mid content", name);
    };
    { IIREngine e; e.prepare (fs, 512, 2, 0); run (e, 0, "Zero Latency"); }
    { LinearPhaseEngine e; e.prepare (fs, 512, 2); e.setResolution (1); run (e, e.latencySamples(), "Linear Phase"); }
}

// ---------------------------------------------------------------------------
static void testPerformance()
{
    std::printf ("[6] CPU (8 bands, stereo, 48 kHz, 20 s of audio, 256-sample blocks)\n");
    const double fs = 48000.0;
    const int len = 48000 * 20;
    std::vector<float> L ((size_t) len), R ((size_t) len);
    juce::Random rng (1);
    for (int i = 0; i < len; ++i) { L[(size_t) i] = rng.nextFloat() - 0.5f; R[(size_t) i] = rng.nextFloat() - 0.5f; }

    auto setup = [] (TestSetup& s)
    {
        s.standardCurve();
        auto set = [&] (int i, FilterType t, double f, double g, double q, int sl, Placement p = Placement::Stereo)
        {
            auto& b = s.bands[i]; b.used = true; b.type = t; b.freq = f; b.gain = g; b.q = q; b.slopeIdx = sl; b.placement = p;
        };
        set (4, FilterType::HighCut, 18000, 0, kDefaultQ, 8);
        set (5, FilterType::Bell, 2500, 3, 2, 1, Placement::Mid);
        set (6, FilterType::LowShelf, 120, 2, kDefaultQ, 1, Placement::Side);
        set (7, FilterType::Bell, 6000, -3, 3, 1);
        s.bands[7].dynOn = true; s.bands[7].dynRange = -6;
    };

    auto bench = [&] (auto& e, const char* name)
    {
        TestSetup s; setup (s);
        auto t0 = std::chrono::high_resolution_clock::now();
        for (int pos = 0; pos < len; pos += 256)
        {
            float* io[2] = { L.data() + pos, R.data() + pos };
            e.process (io, 2, std::min (256, len - pos), s.in);
        }
        const double sec = std::chrono::duration<double> (std::chrono::high_resolution_clock::now() - t0).count();
        std::printf ("  %-26s %7.2f %% of one core (%.0fx real-time)\n", name, 100.0 * sec / 20.0, 20.0 / sec);
    };

    { IIREngine e; e.prepare (fs, 256, 2, 0); bench (e, "Zero Latency"); }
    { IIREngine e; e.prepare (fs, 256, 2, 2); bench (e, "Natural Phase"); }
    for (int r = 0; r < kNumLPRes; ++r)
    {
        LinearPhaseEngine e; e.prepare (fs, 256, 2); e.setResolution (r);
        bench (e, (juce::String ("Linear Phase ") + lpResolutionNames()[r]).toRawUTF8());
    }
}

int main()
{
    juce::ScopedNoDenormals noDenormals;
    testDesigns();
    testEngines();
    testNull();
    testDynamics();
    testMidSide();
    testPerformance();
    std::printf ("\n%s (%d failure%s)\n", failures == 0 ? "ALL TESTS PASSED" : "TESTS FAILED", failures, failures == 1 ? "" : "s");
    return failures == 0 ? 0 : 1;
}
