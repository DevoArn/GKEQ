#include "IIREngine.h"
#include "Character.h"
#include <cstring>

namespace meq::dsp
{

// ---------------------------------------------------------------------------
// Chain
// ---------------------------------------------------------------------------
void IIREngine::Chain::resetState() noexcept
{
    std::memset (z, 0, sizeof (z));
}

void IIREngine::Chain::setTarget (const DigitalFilter& d, bool snap) noexcept
{
    if (d.num != num)
    {
        // Structure changed (should only happen on a fresh chain): start clean.
        for (int s = num; s < d.num; ++s)
            z[s][0][0] = z[s][0][1] = z[s][1][0] = z[s][1][1] = 0.0;
        snap = true;
    }

    num = d.num;
    for (int s = 0; s < num; ++s)
        tgt[s] = d.sec[s];
    gainTgt = d.gain;

    if (snap)
    {
        for (int s = 0; s < num; ++s)
            cur[s] = tgt[s];
        gain = gainTgt;
        moving = false;
    }
    else
    {
        moving = true;
    }
}

void IIREngine::Chain::processChannel (int ch, double* x, int n) noexcept
{
    const double invN = 1.0 / (double) n;

    for (int s = 0; s < num; ++s)
    {
        double b0 = cur[s].b0, b1 = cur[s].b1, b2 = cur[s].b2, a1 = cur[s].a1, a2 = cur[s].a2;
        double* zz = z[s][ch];
        double z1 = zz[0], z2 = zz[1];

        if (moving)
        {
            const double db0 = (tgt[s].b0 - b0) * invN, db1 = (tgt[s].b1 - b1) * invN, db2 = (tgt[s].b2 - b2) * invN;
            const double da1 = (tgt[s].a1 - a1) * invN, da2 = (tgt[s].a2 - a2) * invN;
            for (int i = 0; i < n; ++i)
            {
                b0 += db0; b1 += db1; b2 += db2; a1 += da1; a2 += da2;
                const double xi = x[i];
                const double y  = b0 * xi + z1;
                z1 = b1 * xi - a1 * y + z2;
                z2 = b2 * xi - a2 * y;
                x[i] = y;
            }
        }
        else
        {
            for (int i = 0; i < n; ++i)
            {
                const double xi = x[i];
                const double y  = b0 * xi + z1;
                z1 = b1 * xi - a1 * y + z2;
                z2 = b2 * xi - a2 * y;
                x[i] = y;
            }
        }
        zz[0] = z1;
        zz[1] = z2;
    }

    if (moving && gain != gainTgt)
    {
        double g = gain;
        const double dg = (gainTgt - gain) * invN;
        for (int i = 0; i < n; ++i) { g += dg; x[i] *= g; }
    }
    else if (gain != 1.0)
    {
        for (int i = 0; i < n; ++i) x[i] *= gain;
    }
}

void IIREngine::Chain::commit() noexcept
{
    if (! moving) return;
    for (int s = 0; s < num; ++s)
        cur[s] = tgt[s];
    gain = gainTgt;
    moving = false;
}

// ---------------------------------------------------------------------------
// Engine
// ---------------------------------------------------------------------------
void IIREngine::prepare (double newHostRate, int maxBlockSize, int numChannels, int oversamplingLog2)
{
    hostRate = newHostRate;
    osLog2   = juce::jlimit (0, 2, oversamplingLog2);
    osFactor = 1 << osLog2;
    rate     = hostRate * osFactor;
    channels = juce::jlimit (1, 2, numChannels);

    maxBlock = juce::jmax (1, maxBlockSize);
    oversampler.prepare (channels, osLog2, maxBlock);
    latency = oversampler.latencySamples();
    dcCoef = std::exp (-kTwoPi * 5.0 / rate);

    for (auto& b : bands)
        b.det.prepare (hostRate);

    reset();
}

void IIREngine::reset()
{
    oversampler.reset();
    charMode = CharacterMode::Clean;
    charMix = 0.0;
    dcX[0] = dcX[1] = dcY[0] = dcY[1] = 0.0;

    for (auto& b : bands)
    {
        b.initialised = false;
        b.fading = false;
        b.A.resetState();
        b.B.resetState();
        b.A.num = b.B.num = 0;
        b.cfgA = b.cfgB = Config();
        for (auto& v : b.designedA) v = -1.0e9;
        for (auto& v : b.designedB) v = -1.0e9;
        for (auto& v : b.detKey) v = -1.0;
        b.det.reset();
        b.dynOffset = 0.0;
    }
    soloChain.resetState();
    soloChain.num = 0;
    soloBand = -1;
    soloMix  = 0.0;
    for (auto& v : soloKey) v = -1.0;
    gainScaleInit = false;
}

void IIREngine::designChain (Chain& c, const Config& cfg, const Band& b, double effGain, double* designed, bool snap)
{
    if (designed[0] == b.logF && designed[1] == effGain && designed[2] == b.logQ)
        return;

    const bool snapNow = snap || designed[0] < -1.0e8;

    DesignParams d;
    d.type     = cfg.type;
    d.freq     = std::exp (b.logF);
    d.gainDb   = effGain;
    d.q        = std::exp (b.logQ);
    d.slopeIdx = cfg.slope;
    d.idealBrickwall = false;

    c.setTarget (designDigital (designAnalog (d, rate)), snapNow);
    designed[0] = b.logF;
    designed[1] = effGain;
    designed[2] = b.logQ;
}

void IIREngine::updateBand (int index, const BandParams& p, const EngineInput& in, int hostOffset, int hostCount)
{
    Band& bd = bands[index];
    const Config want { p.isActive(), p.type, p.slopeIdx, p.placement };

    const double tF = std::log (p.freq), tG = p.gain, tQ = std::log (p.q);
    const double alpha = 1.0 - std::exp (-(hostCount / hostRate) / 0.02);

    if (! bd.initialised)
    {
        bd.logF = tF; bd.gainDb = tG; bd.logQ = tQ;
        bd.cfgA = want;
        bd.fading = false;
        bd.A.resetState();
        bd.A.num = 0;
        for (auto& v : bd.designedA) v = -1.0e9;
        bd.dynOffset = 0.0;
        bd.initialised = true;
    }
    else
    {
        auto smooth = [alpha] (double& v, double t, double eps)
        {
            v += alpha * (t - v);
            if (std::abs (t - v) < eps) v = t;
        };
        smooth (bd.logF, tF, 1.0e-5);
        smooth (bd.gainDb, tG, 1.0e-4);
        smooth (bd.logQ, tQ, 1.0e-5);

        const Config& current = bd.fading ? bd.cfgB : bd.cfgA;
        if (want != current)
        {
            if (bd.fading)
            {
                bd.A = bd.B;
                bd.cfgA = bd.cfgB;
                std::memcpy (bd.designedA, bd.designedB, sizeof (bd.designedA));
                bd.fading = false;
            }

            if (! bd.cfgA.active && ! want.active)
            {
                bd.cfgA = want;
            }
            else
            {
                bd.cfgB = want;
                bd.B.resetState();
                bd.B.num = 0;
                for (auto& v : bd.designedB) v = -1.0e9;
                bd.fading  = true;
                bd.fadePos = 0;
                bd.fadeLen = juce::jmax (1, (int) (0.012 * rate));
            }
        }
    }

    // --- Dynamics ------------------------------------------------------------
    const bool dyn = want.active && p.isDynamic();
    if (dyn)
    {
        if (bd.detKey[0] != (double) p.type || bd.detKey[1] != p.freq || bd.detKey[2] != p.q)
        {
            bd.det.setFilter (designDigital (designDetectorFilter (DesignParams::fromBand (p, 0.0, false), hostRate)));
            bd.detKey[0] = (double) p.type; bd.detKey[1] = p.freq; bd.detKey[2] = p.q;
        }
        bd.det.setTimes (p.attackMs, p.releaseMs);

        const float* dl = hostIn[0] + hostOffset;
        const float* dr = channels > 1 && hostIn[1] != nullptr ? hostIn[1] + hostOffset : nullptr;
        if (p.dynSC && in.sidechain != nullptr && in.scChannels > 0)
        {
            dl = in.sidechain[0] + hostOffset;
            dr = in.scChannels > 1 ? in.sidechain[1] + hostOffset : nullptr;
        }
        bd.det.process (dl, dr, hostCount, p.placement);
        bd.dynOffset = dynamicGainOffset (bd.det.levelDb(), p.threshold, p.dynRange);
        if (in.detLevelsOut != nullptr) in.detLevelsOut[index] = (float) bd.det.levelDb();
    }
    else
    {
        bd.dynOffset *= (1.0 - alpha);
        if (std::abs (bd.dynOffset) < 1.0e-4) bd.dynOffset = 0.0;
        if (in.detLevelsOut != nullptr) in.detLevelsOut[index] = -200.0f;
    }

    if (in.dynOffsetsOut != nullptr)
        in.dynOffsetsOut[index] = (float) bd.dynOffset;

    // --- Coefficients ----------------------------------------------------------
    const double effGain = juce::jlimit (-60.0, 60.0, (bd.gainDb + bd.dynOffset) * gainScale);
    if (bd.cfgA.active)
        designChain (bd.A, bd.cfgA, bd, effGain, bd.designedA, false);
    if (bd.fading && bd.cfgB.active)
        designChain (bd.B, bd.cfgB, bd, effGain, bd.designedB, false);
}

void IIREngine::processChain (Chain& c, Placement place, double* L, double* R, int n) noexcept
{
    const bool stereo = channels > 1;

    switch (place)
    {
        case Placement::Stereo:
            c.processChannel (0, L, n);
            if (stereo) c.processChannel (1, R, n);
            break;

        case Placement::Left:
            c.processChannel (0, L, n);
            break;

        case Placement::Right:
            if (stereo) c.processChannel (1, R, n);
            else        c.processChannel (0, L, n);
            break;

        case Placement::Mid:
        case Placement::Side:
        {
            if (! stereo)
            {
                if (place == Placement::Mid) c.processChannel (0, L, n);
                break;
            }
            for (int i = 0; i < n; ++i)
            {
                const double m = 0.5 * (L[i] + R[i]);
                const double s = 0.5 * (L[i] - R[i]);
                L[i] = m; R[i] = s;
            }
            if (place == Placement::Mid) c.processChannel (0, L, n);
            else                         c.processChannel (0, R, n);
            for (int i = 0; i < n; ++i)
            {
                const double m = L[i], s = R[i];
                L[i] = m + s; R[i] = m - s;
            }
            break;
        }

        case Placement::Count:
        default:
            break;
    }
    c.commit();
}

void IIREngine::processSolo (const double* dl, const double* dr, double* outL, double* outR, int n) noexcept
{
    const bool stereo = channels > 1;

    if (! stereo)
    {
        std::memcpy (outL, dl, sizeof (double) * (size_t) n);
        soloChain.processChannel (0, outL, n);
        soloChain.commit();
        return;
    }

    switch (soloPlace)
    {
        case Placement::Stereo:
            std::memcpy (outL, dl, sizeof (double) * (size_t) n);
            std::memcpy (outR, dr, sizeof (double) * (size_t) n);
            soloChain.processChannel (0, outL, n);
            soloChain.processChannel (1, outR, n);
            break;
        case Placement::Left:
            std::memcpy (outL, dl, sizeof (double) * (size_t) n);
            soloChain.processChannel (0, outL, n);
            std::memcpy (outR, outL, sizeof (double) * (size_t) n);
            break;
        case Placement::Right:
            std::memcpy (outL, dr, sizeof (double) * (size_t) n);
            soloChain.processChannel (0, outL, n);
            std::memcpy (outR, outL, sizeof (double) * (size_t) n);
            break;
        case Placement::Mid:
            for (int i = 0; i < n; ++i) outL[i] = 0.5 * (dl[i] + dr[i]);
            soloChain.processChannel (0, outL, n);
            std::memcpy (outR, outL, sizeof (double) * (size_t) n);
            break;
        case Placement::Side:
            for (int i = 0; i < n; ++i) outL[i] = 0.5 * (dl[i] - dr[i]);
            soloChain.processChannel (0, outL, n);
            for (int i = 0; i < n; ++i) outR[i] = -outL[i];
            break;
        case Placement::Count:
        default:
            break;
    }
    soloChain.commit();
}

void IIREngine::process (float* const* io, int numChannels, int numSamples, const EngineInput& in)
{
    const int nch = juce::jmin (numChannels, channels);
    if (nch <= 0 || numSamples <= 0) return;

    hostIn[0] = io[0];
    hostIn[1] = nch > 1 ? io[1] : nullptr;

    jassert (numSamples <= maxBlock);
    float* const* osData = osLog2 > 0 ? oversampler.up (io, nch, numSamples) : io;

    const GlobalParams& g = *in.global;

    for (int pos = 0; pos < numSamples; pos += kSubBlock)
    {
        const int hn = juce::jmin (kSubBlock, numSamples - pos);
        const int on = hn * osFactor;
        const int opos = pos * osFactor;

        // Global gain scale (smoothed)
        {
            const double alpha = 1.0 - std::exp (-(hn / hostRate) / 0.02);
            if (! gainScaleInit) { gainScale = g.gainScale; gainScaleInit = true; }
            gainScale += alpha * (g.gainScale - gainScale);
            if (std::abs (gainScale - g.gainScale) < 1.0e-6) gainScale = g.gainScale;
        }

        for (int b = 0; b < kNumBands; ++b)
            updateBand (b, in.bands[b], in, pos, hn);

        // Solo set-up
        const int sb = findSoloBand (in.bands);
        if (sb >= 0)
        {
            const BandParams& sp = in.bands[sb];
            const double key[5] { (double) sb, (double) sp.type, sp.freq, sp.q, (double) sp.placement };
            if (std::memcmp (key, soloKey, sizeof (key)) != 0)
            {
                const bool fresh = soloBand != sb;
                if (fresh) { soloChain.resetState(); soloChain.num = 0; }
                soloChain.setTarget (designDigital (designSoloFilter (DesignParams::fromBand (sp, 0.0, false), rate)), fresh);
                std::memcpy (soloKey, key, sizeof (key));
                soloBand  = sb;
                soloPlace = sp.placement;
            }
        }
        const bool needSolo = sb >= 0 || soloMix > 0.0;

        float* oL = osData[0] + opos;
        float* oR = nch > 1 ? osData[1] + opos : nullptr;
        for (int i = 0; i < on; ++i)
        {
            bufL[i] = oL[i];
            bufR[i] = oR != nullptr ? (double) oR[i] : 0.0;
        }

        if (needSolo)
        {
            std::memcpy (dryL, bufL, sizeof (double) * (size_t) on);
            std::memcpy (dryR, bufR, sizeof (double) * (size_t) on);
        }

        // Two passes: L/R-domain bands first, then Mid/Side bands.
        for (int pass = 0; pass < 2; ++pass)
        {
            for (int b = 0; b < kNumBands; ++b)
            {
                Band& bd = bands[b];
                if (! bd.fading && ! bd.cfgA.active)
                    continue;

                const Config& prim = bd.fading ? bd.cfgB : bd.cfgA;
                if ((isMidSide (prim.place) ? 1 : 0) != pass)
                    continue;

                if (! bd.fading)
                {
                    processChain (bd.A, bd.cfgA.place, bufL, bufR, on);
                    continue;
                }

                std::memcpy (tmpAL, bufL, sizeof (double) * (size_t) on);
                std::memcpy (tmpAR, bufR, sizeof (double) * (size_t) on);
                std::memcpy (tmpBL, bufL, sizeof (double) * (size_t) on);
                std::memcpy (tmpBR, bufR, sizeof (double) * (size_t) on);
                if (bd.cfgA.active) processChain (bd.A, bd.cfgA.place, tmpAL, tmpAR, on);
                if (bd.cfgB.active) processChain (bd.B, bd.cfgB.place, tmpBL, tmpBR, on);

                const double inv = 1.0 / (double) bd.fadeLen;
                for (int i = 0; i < on; ++i)
                {
                    const double a = juce::jmin (1.0, (bd.fadePos + i + 1) * inv);
                    bufL[i] = tmpAL[i] + a * (tmpBL[i] - tmpAL[i]);
                    bufR[i] = tmpAR[i] + a * (tmpBR[i] - tmpAR[i]);
                }
                bd.fadePos += on;
                if (bd.fadePos >= bd.fadeLen)
                {
                    bd.A = bd.B;
                    bd.cfgA = bd.cfgB;
                    std::memcpy (bd.designedA, bd.designedB, sizeof (bd.designedA));
                    bd.fading = false;
                }
            }
        }

        if (needSolo)
        {
            processSolo (dryL, dryR, solL, solR, on);
            const double target = sb >= 0 ? 1.0 : 0.0;
            const double step = 1.0 / (0.01 * rate);
            for (int i = 0; i < on; ++i)
            {
                if (soloMix < target)      soloMix = juce::jmin (target, soloMix + step);
                else if (soloMix > target) soloMix = juce::jmax (target, soloMix - step);
                bufL[i] += soloMix * (solL[i] - bufL[i]);
                bufR[i] += soloMix * (solR[i] - bufR[i]);
            }
            if (sb < 0 && soloMix <= 0.0)
            {
                soloBand = -1;
                for (auto& v : soloKey) v = -1.0;
            }
        }

        if (osFactor > 1)
            applyCharacter (bufL, bufR, on, g.character);

        for (int i = 0; i < on; ++i)
        {
            oL[i] = (float) bufL[i];
            if (oR != nullptr) oR[i] = (float) bufR[i];
        }
    }

    if (osLog2 > 0)
        oversampler.down (io, nch, numSamples);
}

void IIREngine::applyCharacter (double* L, double* R, int n, CharacterMode mode) noexcept
{
    if (mode == CharacterMode::Clean && charMix <= 0.0)
    {
        charMode = mode;
        return;
    }

    const double step = 1.0 / (0.01 * rate);
    const bool stereo = channels > 1;
    for (int i = 0; i < n; ++i)
    {
        const double target = (charMode == mode && mode != CharacterMode::Clean) ? 1.0 : 0.0;
        if (charMix < target)      charMix = juce::jmin (target, charMix + step);
        else if (charMix > target) charMix = juce::jmax (target, charMix - step);
        if (charMix <= 0.0 && charMode != mode) charMode = mode;

        for (int ch = 0; ch < (stereo ? 2 : 1); ++ch)
        {
            double* x = ch == 0 ? L : R;
            const double in = x[i];
            const double sh = (double) CharacterProcessor::shape (charMode, (float) in);
            const double wet = in + charMix * (sh - in);
            // DC blocker (the asymmetric curve generates a small offset)
            const double y = wet - dcX[ch] + dcCoef * dcY[ch];
            dcX[ch] = wet; dcY[ch] = y;
            x[i] = y;
        }
    }
}

} // namespace meq::dsp
