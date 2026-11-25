#include "LinearPhaseEngine.h"
#include <algorithm>

namespace meq::dsp
{

namespace
{
    constexpr int kBaseL[kNumLPRes] = { 2048, 4096, 8192, 16384, 32768 };
    constexpr double kKaiserBeta = 8.0;

    double besselI0 (double x) noexcept
    {
        double sum = 1.0, term = 1.0;
        const double y = 0.25 * x * x;
        for (int k = 1; k < 64; ++k)
        {
            term *= y / ((double) k * (double) k);
            sum += term;
            if (term < sum * 1.0e-16) break;
        }
        return sum;
    }
}

LinearPhaseEngine::LinearPhaseEngine() = default;
LinearPhaseEngine::~LinearPhaseEngine() = default;

int LinearPhaseEngine::latencyFor (int r) const noexcept
{
    return kBaseL[juce::jlimit (0, kNumLPRes - 1, r)] * rateFactor;
}

void LinearPhaseEngine::prepare (double sampleRate, int /*maxBlockSize*/, int numChannels)
{
    fs = sampleRate;
    channels = juce::jlimit (1, 2, numChannels);
    rateFactor = fs > 100000.0 ? 4 : (fs > 50000.0 ? 2 : 1);

    maxL = kBaseL[kNumLPRes - 1] * rateFactor;
    const int maxN = 2 * maxL;
    const int maxH = maxL / 4;
    const int maxBins = maxL / 2 + 1;

    fftN.clear();
    fftL.clear();
    hannTables.clear();
    kaiserTables.clear();
    for (int r = 0; r < kNumLPRes; ++r)
    {
        const int l = kBaseL[r] * rateFactor;
        fftN.push_back (std::make_unique<RealFFT> (2 * l));
        fftL.push_back (std::make_unique<RealFFT> (l));

        const int h = l / 4;
        std::vector<float> hw ((size_t) (2 * h));
        for (int n = 0; n < 2 * h; ++n)
            hw[(size_t) n] = (float) (0.5 - 0.5 * std::cos (kTwoPi * n / (2.0 * h)));
        hannTables.push_back (std::move (hw));

        std::vector<float> kw ((size_t) l);
        const double i0b = besselI0 (kKaiserBeta);
        for (int n = 0; n < l; ++n)
        {
            const double t = (n - 0.5 * l) / (0.5 * l);
            kw[(size_t) n] = (float) (besselI0 (kKaiserBeta * std::sqrt (std::max (0.0, 1.0 - t * t))) / i0b);
        }
        kaiserTables.push_back (std::move (kw));
    }

    for (int ch = 0; ch < 2; ++ch)
    {
        inBuf[ch].allocate ((size_t) (2 * maxH));
        acc[ch].allocate ((size_t) maxN);
        outReady[ch].allocate ((size_t) maxH);
        spec[ch].allocate ((size_t) maxN);
    }
    segment.allocate ((size_t) maxN);
    ySpec.allocate ((size_t) maxN);
    timeBuf.allocate ((size_t) maxN);
    kernelTime.allocate ((size_t) maxN);
    orderedSpec.allocate ((size_t) maxN);
    for (auto& k : K)
        k.allocate ((size_t) maxN);

    bandMag.assign ((size_t) kNumBands * (size_t) maxBins, 1.0f);
    for (auto& v : prod) v.assign ((size_t) maxBins, 1.0f);
    for (auto& v : mat)  v.assign ((size_t) maxBins, 0.0f);
    entry.assign ((size_t) maxBins, 0.0f);

    for (auto& d : det)
        d.prepare (fs);

    res = -1;
    setResolution (1);
}

void LinearPhaseEngine::setResolution (int r)
{
    r = juce::jlimit (0, kNumLPRes - 1, r);
    if (r == res || fftN.empty()) return;

    res  = r;
    L    = kBaseL[r] * rateFactor;
    H    = L / 4;
    N    = 2 * L;
    bins = L / 2 + 1;
    gridW0 = kTwoPi / (double) L;
    curN = fftN[(size_t) r].get();
    curL = fftL[(size_t) r].get();
    reset();
}

void LinearPhaseEngine::reset()
{
    for (int ch = 0; ch < 2; ++ch)
    {
        inBuf[ch].clear();
        acc[ch].clear();
        outReady[ch].clear();
    }
    hopPos = 0;
    kernelValid = false;
    for (auto& c : cache) c.valid = false;
    soloKey[0] = -2.0;
    for (int b = 0; b < kNumBands; ++b)
    {
        det[b].reset();
        dynOffset[b] = 0.0;
        detKey[b][0] = detKey[b][1] = detKey[b][2] = -1.0;
    }
}

// ---------------------------------------------------------------------------
void LinearPhaseEngine::computeBandMagnitude (int band, const DesignParams& d, float* out)
{
    juce::ignoreUnused (band);
    const AnalogFilter af = designAnalog (d, fs);
    for (int k = 0; k < bins; ++k)
        out[k] = (float) af.mag (k * gridW0);
}

void LinearPhaseEngine::designKernel (const float* mag, AlignedBuffer& kernelSpectrum)
{
    float* os = orderedSpec.data();
    const int half = L / 2;
    os[0] = mag[0];
    os[1] = mag[half];
    for (int k = 1; k < half; ++k)
    {
        os[2 * k]     = mag[k];
        os[2 * k + 1] = 0.0f;
    }
    curL->inverseOrdered (os, kernelTime.data());

    const auto& kw = kaiserTables[(size_t) res];
    const float scale = 1.0f / (float) L;
    float* t = timeBuf.data();
    const float* kt = kernelTime.data();
    for (int n = 0; n < L; ++n)
        t[n] = kt[(n + half) & (L - 1)] * kw[(size_t) n] * scale;
    std::memset (t + L, 0, sizeof (float) * (size_t) (N - L));

    curN->forward (t, kernelSpectrum.data());
}

bool LinearPhaseEngine::updateTargets (const EngineInput& in)
{
    const GlobalParams& g = *in.global;
    bool changed = ! kernelValid;

    for (int b = 0; b < kNumBands; ++b)
    {
        const BandParams& p = in.bands[b];
        auto& c = cache[b];

        if (! p.isActive())
        {
            if (! c.valid || c.active) changed = true;
            c.valid = true;
            c.active = false;
            continue;
        }

        const double eff = effectiveGainDb (p, g.gainScale, dynOffset[b]);
        const double key[6] { (double) p.type, p.freq, eff, p.q, (double) p.slopeIdx, 0.0 };
        bool same = c.valid && c.active;
        if (same)
        {
            for (int i = 0; i < 6 && same; ++i)
                same = (i == 2) ? std::abs (key[i] - c.key[i]) < 0.01 : key[i] == c.key[i];
        }

        if (! same)
        {
            DesignParams d = DesignParams::fromBand (p, eff, true);
            computeBandMagnitude (b, d, bandMag.data() + (size_t) b * (size_t) bins);
            std::copy (key, key + 6, c.key);
            changed = true;
        }
        if (c.place != p.placement) changed = true;
        c.place  = p.placement;
        c.valid  = true;
        c.active = true;
    }

    const int sb = findSoloBand (in.bands);
    double sk[6] { -1.0, 0.0, 0.0, 0.0, 0.0, 0.0 };
    if (sb >= 0)
    {
        const auto& sp = in.bands[sb];
        sk[0] = (double) sb; sk[1] = (double) sp.type; sk[2] = sp.freq;
        sk[3] = sp.q; sk[4] = (double) sp.placement; sk[5] = 1.0;
    }
    if (std::memcmp (sk, soloKey, sizeof (sk)) != 0)
    {
        std::memcpy (soloKey, sk, sizeof (sk));
        changed = true;
    }

    if (! changed)
        return false;

    const size_t nb = (size_t) bins;
    const bool stereo = channels > 1;

    if (sb >= 0)
    {
        const auto& sp = in.bands[sb];
        const AnalogFilter sf = designSoloFilter (DesignParams::fromBand (sp, 0.0, true), fs);
        for (int k = 0; k < bins; ++k)
            entry[(size_t) k] = (float) sf.mag (k * gridW0);

        const Placement pl = stereo ? sp.placement : Placement::Stereo;
        crossTerms = stereo && pl != Placement::Stereo;
        for (size_t k = 0; k < nb; ++k)
        {
            const float e = entry[k];
            switch (pl)
            {
                case Placement::Left:  mat[0][k] = e;        mat[1][k] = 0.0f;      mat[2][k] = e;        mat[3][k] = 0.0f;     break;
                case Placement::Right: mat[0][k] = 0.0f;     mat[1][k] = e;         mat[2][k] = 0.0f;     mat[3][k] = e;        break;
                case Placement::Mid:   mat[0][k] = 0.5f * e; mat[1][k] = 0.5f * e;  mat[2][k] = 0.5f * e; mat[3][k] = 0.5f * e; break;
                case Placement::Side:  mat[0][k] = 0.5f * e; mat[1][k] = -0.5f * e; mat[2][k] = -0.5f * e; mat[3][k] = 0.5f * e; break;
                case Placement::Stereo:
                case Placement::Count:
                default:               mat[0][k] = e;        mat[1][k] = 0.0f;      mat[2][k] = 0.0f;     mat[3][k] = e;        break;
            }
        }
        return true;
    }

    // Products per domain: prod[0]=L, prod[1]=R, prod[2]=M, prod[3]=S
    for (auto& v : prod) std::fill (v.begin(), v.begin() + (std::ptrdiff_t) nb, 1.0f);
    bool hasMS = false;

    for (int b = 0; b < kNumBands; ++b)
    {
        if (! cache[b].active) continue;
        const float* m = bandMag.data() + (size_t) b * nb;
        auto mul = [&] (int target) { float* d = prod[target].data(); for (size_t k = 0; k < nb; ++k) d[k] *= m[k]; };

        const Placement pl = cache[b].place;
        if (! stereo)
        {
            if (pl != Placement::Side) mul (0);
            continue;
        }
        switch (pl)
        {
            case Placement::Stereo: mul (0); mul (1); break;
            case Placement::Left:   mul (0); break;
            case Placement::Right:  mul (1); break;
            case Placement::Mid:    mul (2); hasMS = true; break;
            case Placement::Side:   mul (3); hasMS = true; break;
            case Placement::Count:
            default: break;
        }
    }

    crossTerms = hasMS;
    if (! hasMS)
    {
        std::copy (prod[0].begin(), prod[0].begin() + (std::ptrdiff_t) nb, mat[0].begin());
        std::copy (prod[1].begin(), prod[1].begin() + (std::ptrdiff_t) nb, mat[3].begin());
    }
    else
    {
        for (size_t k = 0; k < nb; ++k)
        {
            const float l = prod[0][k], r = prod[1][k], m = prod[2][k], s = prod[3][k];
            mat[0][k] = 0.5f * (m + s) * l;
            mat[1][k] = 0.5f * (m - s) * r;
            mat[2][k] = 0.5f * (m - s) * l;
            mat[3][k] = 0.5f * (m + s) * r;
        }
    }
    return true;
}

void LinearPhaseEngine::doHop (const EngineInput& in)
{
    const bool stereo = channels > 1;

    if (updateTargets (in) || ! kernelValid)
    {
        designKernel (mat[0].data(), K[0]);
        if (stereo)
        {
            designKernel (mat[3].data(), K[3]);
            if (crossTerms)
            {
                designKernel (mat[1].data(), K[1]);
                designKernel (mat[2].data(), K[2]);
            }
        }
        kernelValid = true;
    }

    const auto& hw = hannTables[(size_t) res];
    const int segLen = 2 * H;
    for (int ch = 0; ch < channels; ++ch)
    {
        float* s = segment.data();
        const float* x = inBuf[ch].data();
        for (int n = 0; n < segLen; ++n)
            s[n] = x[n] * hw[(size_t) n];
        std::memset (s + segLen, 0, sizeof (float) * (size_t) (N - segLen));
        curN->forward (s, spec[ch].data());
    }

    const float scale = 1.0f / (float) N;
    for (int o = 0; o < channels; ++o)
    {
        ySpec.clear ((size_t) N);
        if (! stereo)
        {
            curN->multiplyAccumulate (spec[0].data(), K[0].data(), ySpec.data(), scale);
        }
        else if (! crossTerms)
        {
            curN->multiplyAccumulate (spec[o].data(), K[o == 0 ? 0 : 3].data(), ySpec.data(), scale);
        }
        else if (o == 0)
        {
            curN->multiplyAccumulate (spec[0].data(), K[0].data(), ySpec.data(), scale);
            curN->multiplyAccumulate (spec[1].data(), K[1].data(), ySpec.data(), scale);
        }
        else
        {
            curN->multiplyAccumulate (spec[0].data(), K[2].data(), ySpec.data(), scale);
            curN->multiplyAccumulate (spec[1].data(), K[3].data(), ySpec.data(), scale);
        }

        curN->inverse (ySpec.data(), timeBuf.data());
        float* a = acc[o].data();
        const float* t = timeBuf.data();
        for (int n = 0; n < N; ++n)
            a[n] += t[n];
    }

    for (int ch = 0; ch < channels; ++ch)
    {
        float* a = acc[ch].data();
        std::memcpy (outReady[ch].data(), a, sizeof (float) * (size_t) H);
        std::memmove (a, a + H, sizeof (float) * (size_t) (N - H));
        std::memset (a + (N - H), 0, sizeof (float) * (size_t) H);

        float* x = inBuf[ch].data();
        std::memmove (x, x + H, sizeof (float) * (size_t) H);
    }
}

void LinearPhaseEngine::process (float* const* io, int numChannels, int numSamples, const EngineInput& in)
{
    const int nch = juce::jmin (numChannels, channels);
    if (nch <= 0 || curN == nullptr) return;

    int done = 0;
    while (done < numSamples)
    {
        const int todo = juce::jmin (numSamples - done, H - hopPos);

        // Dynamics detection on the (not yet delayed) input: acts as look-ahead.
        for (int b = 0; b < kNumBands; ++b)
        {
            const BandParams& p = in.bands[b];
            if (! (p.isActive() && p.isDynamic()))
            {
                dynOffset[b] = 0.0;
                if (in.dynOffsetsOut != nullptr) in.dynOffsetsOut[b] = 0.0f;
                if (in.detLevelsOut != nullptr) in.detLevelsOut[b] = -200.0f;
                continue;
            }
            if (detKey[b][0] != (double) p.type || detKey[b][1] != p.freq || detKey[b][2] != p.q)
            {
                det[b].setFilter (designDigital (designDetectorFilter (DesignParams::fromBand (p, 0.0, false), fs)));
                detKey[b][0] = (double) p.type; detKey[b][1] = p.freq; detKey[b][2] = p.q;
            }
            det[b].setTimes (p.attackMs, p.releaseMs);

            const float* dl = io[0] + done;
            const float* dr = nch > 1 ? io[1] + done : nullptr;
            if (p.dynSC && in.sidechain != nullptr && in.scChannels > 0)
            {
                dl = in.sidechain[0] + done;
                dr = in.scChannels > 1 ? in.sidechain[1] + done : nullptr;
            }
            det[b].process (dl, dr, todo, p.placement);
            dynOffset[b] = dynamicGainOffset (det[b].levelDb(), p.threshold, p.dynRange);
            if (in.dynOffsetsOut != nullptr) in.dynOffsetsOut[b] = (float) dynOffset[b];
            if (in.detLevelsOut != nullptr) in.detLevelsOut[b] = (float) det[b].levelDb();
        }

        for (int ch = 0; ch < nch; ++ch)
        {
            std::memcpy (inBuf[ch].data() + H + hopPos, io[ch] + done, sizeof (float) * (size_t) todo);
            std::memcpy (io[ch] + done, outReady[ch].data() + hopPos, sizeof (float) * (size_t) todo);
        }
        if (nch < channels)   // mono feed into a stereo-prepared engine
            std::memset (inBuf[1].data() + H + hopPos, 0, sizeof (float) * (size_t) todo);

        hopPos += todo;
        done   += todo;
        if (hopPos == H)
        {
            doHop (in);
            hopPos = 0;
        }
    }
}

} // namespace meq::dsp
