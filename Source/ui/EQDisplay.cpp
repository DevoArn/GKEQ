#include "EQDisplay.h"
#include "../InstanceRegistry.h"
#include "CurveFit.h"
#include <limits>

namespace meq::ui
{

namespace
{
    constexpr float kNodeR = 7.5f;
    const double kLogSpan = std::log (kDispMaxF / kDispMinF);

    int pointIndexForFreq (double f)
    {
        const double t = std::log (f / kDispMinF) / kLogSpan;
        return juce::jlimit (0, kSpecPoints - 1, (int) std::lround (t * (kSpecPoints - 1)));
    }

    juce::String shortFreq (double f)
    {
        if (f >= 1000.0)
        {
            const double k = f / 1000.0;
            const bool whole = std::abs (k - std::round (k)) < 1.0e-6;
            return (whole ? juce::String ((int) std::lround (k)) : juce::String (k, 1)) + "k";
        }
        return juce::String ((int) std::lround (f));
    }
}

EQDisplay::EQDisplay (ModernEQProcessor& p) : proc (p)
{
    setWantsKeyboardFocus (true);
    setOpaque (true);
    instBuf.resize ((size_t) SnapshotBuffer::kSize);
    for (auto& t : totals) t.fill (0.0f);
    lastTime = juce::Time::getMillisecondCounterHiRes();
    startTimerHz (60);
}

EQDisplay::~EQDisplay()
{
    stopTimer();
}

void EQDisplay::resized()
{
    plot = getLocalBounds().toFloat();
    layoutBandPanel();
}

// ---------------------------------------------------------------------------
// Mapping
// ---------------------------------------------------------------------------
double EQDisplay::displayRange() const
{
    return juce::jlimit (3.0, 30.0, (double) proc.uiState.getProperty ("displayRange", 12));
}

double EQDisplay::gainScale() const
{
    return proc.getParam (ID::gainScale) / 100.0;
}

static float pianoHeight (const ModernEQProcessor& p)
{
    return (bool) p.uiState.getProperty ("pianoRoll", false) ? 18.0f : 0.0f;
}

float EQDisplay::freqToX (double f) const noexcept
{
    return plot.getX() + plot.getWidth() * (float) (std::log (std::max (1.0, f) / kDispMinF) / kLogSpan);
}

double EQDisplay::xToFreq (float x) const noexcept
{
    const double t = (x - plot.getX()) / std::max (1.0f, plot.getWidth());
    return juce::jlimit (kMinFreq, kMaxFreq, kDispMinF * std::exp (t * kLogSpan));
}

float EQDisplay::dbToY (double db) const noexcept
{
    const float pad = 16.0f;
    const float h = plot.getHeight() - 2.0f * pad - pianoHeight (proc);
    const float centre = plot.getY() + pad + h * 0.5f;
    return centre - (float) (db / displayRange()) * h * 0.5f;
}

double EQDisplay::yToDb (float y) const noexcept
{
    const float pad = 16.0f;
    const float h = plot.getHeight() - 2.0f * pad - pianoHeight (proc);
    const float centre = plot.getY() + pad + h * 0.5f;
    return (centre - y) / (h * 0.5f) * displayRange();
}

float EQDisplay::specToY (float db) const noexcept
{
    const float range = (float) (int) proc.uiState.getProperty ("anaRange", 90);
    const float h = plot.getHeight() - pianoHeight (proc);
    return plot.getY() + juce::jlimit (0.0f, h, (-db / range) * h);
}

// ---------------------------------------------------------------------------
// Timer: analysers + curves
// ---------------------------------------------------------------------------
void EQDisplay::timerCallback()
{
    const double now = juce::Time::getMillisecondCounterHiRes();
    const double dt = juce::jlimit (0.001, 0.1, (now - lastTime) * 0.001);
    lastTime = now;

    updateAnalyzers (dt);
    updateCurves();
    layoutBandPanel();

    if (wheelCommitCountdown > 0 && --wheelCommitCountdown == 0)
        notifyEdit();

    // 60 fps with GPU drawing, 40 fps with the software renderer
    const int hz = (bool) proc.uiState.getProperty ("gpu", true) ? 60 : 40;
    if (getTimerInterval() != 1000 / hz)
        startTimerHz (hz);

    repaint();
}

void EQDisplay::updateAnalyzers (double dt)
{
    const auto& ui = proc.uiState;
    const double fs = proc.getCurrentSampleRate();
    const int res = (int) ui.getProperty ("anaRes", 2);
    const int speed = (int) ui.getProperty ("anaSpeed", 2);
    const double tilt = (double) ui.getProperty ("anaTilt", 4.5);
    const bool freeze = (bool) ui.getProperty ("anaFreeze", false);

    pre.configure (res, fs);
    post.configure (res, fs);
    sc.configure (res, fs);

    if (freeze) return;

    if ((bool) ui.getProperty ("anaPre", true))
        pre.process (proc.preTap, dt, speed, tilt);

    const bool needPost = (bool) ui.getProperty ("anaPost", true) || (bool) ui.getProperty ("grab", true)
                       || (bool) ui.getProperty ("collisions", true);
    if (needPost)
        post.process (proc.postTap, dt, speed, tilt);

    if (proc.isSidechainActive())
        sc.process (proc.scTap, dt, speed, tilt);
    else
        sc.decay (dt, speed);

    const int instId = (int) ui.getProperty ("instance", 0);
    if (instId > 0 && instId != proc.getInstanceId())
    {
        double ifs = fs;
        inst.configure (res, fs);
        if (InstanceRegistry::get().readSpectrumSource (instId, instBuf.data(), inst.fftSize(), ifs))
        {
            inst.configure (res, ifs);
            if (InstanceRegistry::get().readSpectrumSource (instId, instBuf.data(), inst.fftSize(), ifs))
                inst.processSamples (instBuf.data(), dt, speed, tilt);
        }
        else
        {
            inst.decay (dt, speed);
        }
    }
}

void EQDisplay::computeBandCurve (const BandParams& p, double effGain, float* out) const
{
    const auto mode = (ProcMode) (int) proc.getParam (ID::mode);
    const auto d = dsp::DesignParams::fromBand (p, effGain, mode == ProcMode::LinearPhase);
    const double fs = curveRate;
    const auto analog = dsp::designAnalog (d, fs);

    float last = 0.0f;
    if (mode == ProcMode::ZeroLatency)
    {
        const auto dig = dsp::designDigital (analog);
        for (int i = 0; i < kSpecPoints; ++i)
        {
            if (pointFreq (i) < 0.5 * fs)
                last = (float) (10.0 * std::log10 (dig.mag2 (pointW[(size_t) i]) + 1.0e-30));
            out[i] = juce::jlimit (-200.0f, 100.0f, last);
        }
    }
    else
    {
        for (int i = 0; i < kSpecPoints; ++i)
        {
            if (pointFreq (i) < 0.5 * fs)
                last = (float) (10.0 * std::log10 (analog.mag2 (pointW[(size_t) i]) + 1.0e-30));
            out[i] = juce::jlimit (-200.0f, 100.0f, last);
        }
    }
}

void EQDisplay::updateCurves()
{
    const double fs = proc.getCurrentSampleRate();
    if (fs != curveRate)
    {
        curveRate = fs;
        for (int i = 0; i < kSpecPoints; ++i)
            pointW[(size_t) i] = dsp::kTwoPi * pointFreq (i) / fs;
        for (auto& c : cache) c.valid = false;
    }

    const double mode = proc.getParam (ID::mode);
    const double scale = gainScale();

    for (int b = 0; b < kNumBands; ++b)
        bands[b] = proc.refs.readBand (b);

    for (int b = 0; b < kNumBands; ++b)
    {
        const auto& p = bands[b];
        auto& c = cache[(size_t) b];
        if (! p.used) { c.valid = false; continue; }

        const double dyn = (p.isDynamic() && p.enabled) ? (double) proc.getDynamicOffset (b) : 0.0;
        const double eff = effectiveGainDb (p, scale, dyn);
        const double key[10] { (double) p.type, p.freq, std::round (eff * 200.0) / 200.0, p.q, (double) p.slopeIdx,
                               mode, p.enabled ? 1.0 : 0.0, p.isDynamic() ? p.dynRange : 0.0, scale, (double) p.gain };
        if (c.valid && std::equal (key, key + 10, c.key))
            continue;

        computeBandCurve (p, eff, c.db.data());
        if (p.isDynamic())
            computeBandCurve (p, effectiveGainDb (p, scale, p.dynRange), c.dbRange.data());
        std::copy (key, key + 10, c.key);
        c.valid = true;
    }

    for (auto& t : totals) t.fill (0.0f);
    placementInUse.fill (false);
    for (int b = 0; b < kNumBands; ++b)
    {
        const auto& p = bands[b];
        if (! p.used || ! p.enabled || ! cache[(size_t) b].valid) continue;
        const int pl = (int) p.placement;
        const auto& db = cache[(size_t) b].db;
        if (p.placement == Placement::Stereo)
        {
            for (auto& t : totals)
                for (int i = 0; i < kSpecPoints; ++i) t[(size_t) i] += db[(size_t) i];
        }
        else
        {
            placementInUse[(size_t) pl] = true;
            for (int i = 0; i < kSpecPoints; ++i) totals[(size_t) pl][(size_t) i] += db[(size_t) i];
        }
    }

    // Drop selection entries for bands that no longer exist (e.g. after undo)
    std::vector<int> valid;
    for (int b : selection)
        if (bands[b].used) valid.push_back (b);
    if (valid.size() != selection.size())
        setSelection (valid);
}

// ---------------------------------------------------------------------------
// Painting
// ---------------------------------------------------------------------------
juce::Path EQDisplay::curvePath (const float* db) const
{
    juce::Path p;
    const float lo = plot.getY() - 40.0f, hi = plot.getBottom() + 40.0f;
    for (int i = 0; i < kSpecPoints; ++i)
    {
        const float x = freqToX (pointFreq (i));
        const float y = juce::jlimit (lo, hi, dbToY (db[i]));
        if (i == 0) p.startNewSubPath (x, y);
        else        p.lineTo (x, y);
    }
    return p;
}

void EQDisplay::paint (juce::Graphics& g)
{
    paintBackground (g);

    const auto& ui = proc.uiState;
    const bool showPre  = (bool) ui.getProperty ("anaPre", true);
    const bool showPost = (bool) ui.getProperty ("anaPost", true);
    const bool showSC   = (bool) ui.getProperty ("anaSC", false) && proc.isSidechainActive();
    const int  instId   = (int) ui.getProperty ("instance", 0);
    const bool showInst = instId > 0 && instId != proc.getInstanceId();

    if (showPre)  paintSpectrum (g, pre.levels(), Colours::preSpec, true, 0.9f);
    if (showPost) paintSpectrum (g, post.levels(), Colours::postSpec, true, 0.6f);
    if (showSC)   paintSpectrum (g, sc.levels(), Colours::scSpec, false, 0.85f);
    if (showInst) paintSpectrum (g, inst.levels(), Colours::instSpec, false, 0.85f);

    if ((bool) ui.getProperty ("collisions", true) && (showSC || showInst))
        paintCollisions (g, post.levels(), showSC ? sc.levels() : inst.levels());

    if (overlayPainter)
        overlayPainter (g);

    // Individual curves of hovered / selected bands
    for (int b = 0; b < kNumBands; ++b)
    {
        const bool sel = std::find (selection.begin(), selection.end(), b) != selection.end();
        if (bands[b].used && cache[(size_t) b].valid && (sel || b == hoverBand))
            paintBandCurve (g, b, sel);
    }

    paintTotals (g);
    paintNodes (g);
    paintReadout (g);

    if (drag == Drag::Sketch)
    {
        juce::Path sp;
        bool started = false;
        for (int i = 0; i < kSpecPoints; ++i)
        {
            if (std::isnan (sketch[(size_t) i])) { started = false; continue; }
            const float x = freqToX (pointFreq (i)), y = dbToY (sketch[(size_t) i]);
            if (! started) { sp.startNewSubPath (x, y); started = true; } else sp.lineTo (x, y);
        }
        g.setColour (Colours::warn.withAlpha (0.95f));
        g.strokePath (sp, juce::PathStrokeType (2.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }
    else if (sketchMode() && mouseInside && drag == Drag::None)
    {
        g.setColour (Colours::warn.withAlpha (0.85f));
        g.setFont (LookAndFeel::font (12.0f, true));
        g.drawText ("SKETCH: draw a curve - bands are created when you release the mouse",
                    juce::Rectangle<float> (plot.getX() + 40.0f, plot.getY() + 6.0f, 460.0f, 18.0f), juce::Justification::centredLeft);
    }

    if (drag == Drag::Rubber && ! rubber.isEmpty())
    {
        g.setColour (Colours::accent.withAlpha (0.12f));
        g.fillRect (rubber);
        g.setColour (Colours::accent.withAlpha (0.6f));
        g.drawRect (rubber, 1.0f);
    }
}

void EQDisplay::paintBackground (juce::Graphics& g)
{
    // Static layers (gradient, grid, labels, piano roll) are rendered once into
    // an image at the physical pixel scale and re-used every frame.
    const float scale = juce::jmax (1.0f, g.getInternalContext().getPhysicalPixelScaleFactor());
    const auto& ui = proc.uiState;
    const juce::String key = juce::String (getWidth()) + "x" + juce::String (getHeight()) + "@" + juce::String (scale, 2)
                           + "|" + ui.getProperty ("displayRange", 12).toString() + "|" + ui.getProperty ("anaRange", 90).toString()
                           + "|" + ui.getProperty ("pianoRoll", false).toString();
    if (key != bgKey || ! bgCache.isValid())
    {
        bgKey = key;
        bgCache = juce::Image (juce::Image::RGB, juce::jmax (1, juce::roundToInt (getWidth() * scale)),
                               juce::jmax (1, juce::roundToInt (getHeight() * scale)), false);
        juce::Graphics ig (bgCache);
        ig.addTransform (juce::AffineTransform::scale (scale));
        juce::ColourGradient bg (Colours::bg1, 0.0f, plot.getY(), Colours::bg0, 0.0f, plot.getBottom(), false);
        ig.setGradientFill (bg);
        ig.fillRect (getLocalBounds());
        paintGrid (ig);
        if ((bool) ui.getProperty ("pianoRoll", false))
            paintPianoRoll (ig);
    }
    g.drawImage (bgCache, getLocalBounds().toFloat());
}

void EQDisplay::paintGrid (juce::Graphics& g)
{
    const float top = plot.getY(), bottom = plot.getBottom() - pianoHeight (proc);

    for (double decade = 10.0; decade <= 10000.0; decade *= 10.0)
    {
        for (int m = 1; m <= 9; ++m)
        {
            const double f = decade * m;
            if (f > kDispMaxF) break;
            const float x = freqToX (f);
            g.setColour (m == 1 ? Colours::gridStrong : Colours::grid);
            g.drawVerticalLine ((int) std::round (x), top, bottom);
        }
    }

    g.setFont (LookAndFeel::font (11.0f));
    const double labels[] = { 20, 50, 100, 200, 500, 1000, 2000, 5000, 10000, 20000 };
    for (double f : labels)
    {
        const float x = freqToX (f);
        g.setColour (Colours::textDim);
        g.drawText (shortFreq (f), juce::Rectangle<float> (x - 20.0f, bottom - 16.0f, 40.0f, 14.0f), juce::Justification::centred);
    }

    // EQ scale
    const double range = displayRange();
    const double step = range <= 3.0 ? 1.0 : range <= 6.0 ? 2.0 : range <= 12.0 ? 3.0 : range <= 24.0 ? 6.0 : 10.0;
    for (double db = -range; db <= range + 1.0e-6; db += step)
    {
        const float y = dbToY (db);
        const bool zero = std::abs (db) < 1.0e-6;
        g.setColour (zero ? Colours::gridStrong.withMultipliedAlpha (1.6f) : Colours::grid);
        g.drawHorizontalLine ((int) std::round (y), plot.getX(), plot.getRight());
        g.setColour (zero ? Colours::text.withAlpha (0.8f) : Colours::textDim);
        const auto txt = (db > 0 ? "+" : "") + juce::String ((int) std::lround (db));
        g.drawText (txt, juce::Rectangle<float> (plot.getX() + 4.0f, y - 7.0f, 30.0f, 14.0f), juce::Justification::centredLeft);
    }

    // Analyser scale (right edge)
    const int anaRange = (int) proc.uiState.getProperty ("anaRange", 90);
    const int astep = anaRange >= 90 ? 20 : 10;
    g.setColour (Colours::textDim.withAlpha (0.6f));
    for (int db = -astep; db > -anaRange; db -= astep)
    {
        const float y = specToY ((float) db);
        g.drawText (juce::String (db), juce::Rectangle<float> (plot.getRight() - 34.0f, y - 7.0f, 30.0f, 14.0f), juce::Justification::centredRight);
    }

    // Display range pill
    const auto pill = juce::Rectangle<float> (plot.getRight() - 70.0f, plot.getY() + 6.0f, 62.0f, 18.0f);
    g.setColour (Colours::bg2.withAlpha (0.9f));
    g.fillRoundedRectangle (pill, 9.0f);
    g.setColour (Colours::panelEdge);
    g.drawRoundedRectangle (pill, 9.0f, 1.0f);
    g.setColour (Colours::text);
    g.drawText (juce::String::fromUTF8 ("\xc2\xb1") + juce::String ((int) range) + " dB", pill, juce::Justification::centred);
}

void EQDisplay::paintPianoRoll (juce::Graphics& g)
{
    const float h = pianoHeight (proc);
    const float y = plot.getBottom() - h;
    g.setColour (juce::Colour (0xffc4c9d3));
    g.fillRect (plot.getX(), y, plot.getWidth(), h);

    for (int midi = 12; midi <= 135; ++midi)
    {
        const double f0 = 440.0 * std::pow (2.0, (midi - 69 - 0.5) / 12.0);
        const double f1 = 440.0 * std::pow (2.0, (midi - 69 + 0.5) / 12.0);
        if (f1 < kDispMinF || f0 > kDispMaxF) continue;
        const float x0 = freqToX (f0), x1 = freqToX (f1);
        const int pc = midi % 12;
        const bool black = pc == 1 || pc == 3 || pc == 6 || pc == 8 || pc == 10;
        if (black)
        {
            g.setColour (juce::Colour (0xff1b2029));
            g.fillRect (x0, y, juce::jmax (0.6f, x1 - x0), h * 0.6f);
        }
        if (pc == 0 || pc == 5)   // boundaries between adjacent white keys (B|C and E|F)
        {
            g.setColour (juce::Colour (0xff7d8491));
            g.drawVerticalLine ((int) std::round (x0), y, y + h);
        }
        if (pc == 0 && x1 - x0 > 2.5f)
        {
            g.setColour (juce::Colour (0xff2a303b));
            g.setFont (LookAndFeel::font (9.0f, true));
            g.drawText ("C" + juce::String (midi / 12 - 1), juce::Rectangle<float> (x0 + 1.0f, y + h * 0.55f, 24.0f, h * 0.45f),
                        juce::Justification::centredLeft);
        }
    }
    g.setColour (Colours::panelEdge);
    g.drawHorizontalLine ((int) y, plot.getX(), plot.getRight());
}

void EQDisplay::paintSpectrum (juce::Graphics& g, const float* lv, juce::Colour c, bool fill, float alpha)
{
    juce::Path p;
    const float bottom = plot.getBottom() - pianoHeight (proc);
    for (int i = 0; i < kSpecPoints; ++i)
    {
        const float x = freqToX (pointFreq (i));
        const float y = juce::jmin (bottom, specToY (lv[i]));
        if (i == 0) p.startNewSubPath (x, y);
        else        p.lineTo (x, y);
    }

    if (fill)
    {
        juce::Path f (p);
        f.lineTo (plot.getRight(), bottom);
        f.lineTo (plot.getX(), bottom);
        f.closeSubPath();
        g.setColour (c.withAlpha (0.38f * alpha));
        g.fillPath (f);
    }
    g.setColour (c.withAlpha (alpha * (fill ? 0.7f : 0.95f)));
    g.strokePath (p, juce::PathStrokeType (fill ? 1.0f : 1.4f));
}

void EQDisplay::paintCollisions (juce::Graphics& g, const float* a, const float* b)
{
    const float range = (float) (int) proc.uiState.getProperty ("anaRange", 90);
    const float thr = -range * 0.62f;
    const float bottom = plot.getBottom() - pianoHeight (proc);
    for (int i = 0; i < kSpecPoints - 1; ++i)
    {
        const float lo = juce::jmin (a[i], b[i]);
        if (lo < thr) continue;
        const float close = juce::jlimit (0.0f, 1.0f, 1.0f - std::abs (a[i] - b[i]) / 15.0f);
        const float strength = juce::jlimit (0.0f, 1.0f, (lo - thr) / 15.0f) * close;
        if (strength < 0.05f) continue;
        const float x0 = freqToX (pointFreq (i)), x1 = freqToX (pointFreq (i + 1));
        const float y = specToY (lo);
        g.setColour (Colours::collision.withAlpha (0.42f * strength));
        g.fillRect (x0, y, x1 - x0 + 0.6f, bottom - y);
    }
}

void EQDisplay::paintBandCurve (juce::Graphics& g, int b, bool emphasised)
{
    const auto col = bandColour (b);
    const auto& c = cache[(size_t) b];
    const auto line = curvePath (c.db.data());
    const float y0 = dbToY (0.0);

    juce::Path fill (line);
    fill.lineTo (plot.getRight(), y0);
    fill.lineTo (plot.getX(), y0);
    fill.closeSubPath();
    g.setColour (col.withAlpha (emphasised ? 0.17f : 0.10f));
    g.fillPath (fill);

    if (bands[b].isDynamic())
    {
        const auto rangeLine = curvePath (c.dbRange.data());
        // Region between the static curve and the full-range curve
        juce::Path region;
        for (int i = 0; i < kSpecPoints; ++i)
        {
            const float x = freqToX (pointFreq (i));
            const float y = dbToY (c.db[(size_t) i]);
            if (i == 0) region.startNewSubPath (x, y); else region.lineTo (x, y);
        }
        for (int i = kSpecPoints - 1; i >= 0; --i)
            region.lineTo (freqToX (pointFreq (i)), dbToY (c.dbRange[(size_t) i]));
        region.closeSubPath();
        g.setColour (col.withAlpha (0.12f));
        g.fillPath (region);

        g.setColour (col.withAlpha (0.45f));
        g.strokePath (rangeLine, juce::PathStrokeType (1.0f));
    }

    g.setColour (col.withAlpha (emphasised ? 0.95f : 0.7f));
    g.strokePath (line, juce::PathStrokeType (1.4f));
}

void EQDisplay::paintTotals (juce::Graphics& g)
{
    for (int p = 1; p < 5; ++p)
    {
        if (! placementInUse[(size_t) p]) continue;
        const auto path = curvePath (totals[(size_t) p].data());
        const auto col = placementColour ((Placement) p);
        g.setColour (col.withAlpha (0.85f));
        g.strokePath (path, juce::PathStrokeType (1.6f));
        const float yEnd = juce::jlimit (plot.getY() + 8.0f, plot.getBottom() - 30.0f, dbToY (totals[(size_t) p][(size_t) pointIndexForFreq (22000.0)]));
        g.setFont (LookAndFeel::font (11.0f, true));
        g.drawText (placementShort ((Placement) p), juce::Rectangle<float> (freqToX (24000.0), yEnd - 14.0f, 20.0f, 12.0f), juce::Justification::centredLeft);
    }

    const auto main = curvePath (totals[0].data());
    juce::Path fill (main);
    const float y0 = dbToY (0.0);
    fill.lineTo (plot.getRight(), y0);
    fill.lineTo (plot.getX(), y0);
    fill.closeSubPath();
    g.setColour (Colours::curve.withAlpha (0.045f));
    g.fillPath (fill);

    if ((bool) proc.uiState.getProperty ("gpu", true))
    {
        g.setColour (Colours::curve.withAlpha (0.10f));
        g.strokePath (main, juce::PathStrokeType (6.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }
    g.setColour (Colours::curve.withAlpha (0.95f));
    g.strokePath (main, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

juce::Point<float> EQDisplay::nodePosition (int b) const
{
    const auto& p = bands[b];
    const float x = freqToX (p.freq);
    const float y = typeHasGain (p.type) ? dbToY (p.gain * gainScale()) : dbToY (0.0);
    return { x, y };
}

juce::Point<float> EQDisplay::rangeHandlePosition (int b) const
{
    // The handle sits at gain + range. When the range is (close to) zero it is
    // kept just outside the node so it can always be grabbed.
    const auto& p = bands[b];
    const auto node = nodePosition (b);
    float y = dbToY ((p.gain + p.dynRange) * gainScale());
    constexpr float minOffset = 17.0f;
    if (std::abs (y - node.y) < minOffset)
        y = node.y + (p.dynRange > 0.0 ? -minOffset : minOffset);
    return { node.x, y };
}

float EQDisplay::specFromY (float y) const noexcept
{
    const float range = (float) (int) proc.uiState.getProperty ("anaRange", 90);
    const float h = juce::jmax (1.0f, plot.getHeight() - pianoHeight (proc));
    return -(y - plot.getY()) / h * range;
}

static float tiltAt (const ModernEQProcessor& p, double freq)
{
    return (float) ((double) p.uiState.getProperty ("anaTilt", 4.5) * std::log2 (freq / 1000.0));
}

juce::Point<float> EQDisplay::thresholdHandlePosition (int b) const
{
    // Drawn in the analyser's level scale (with its tilt) so it can be judged
    // against the spectrum and the band's live detector level.
    const auto& p = bands[b];
    return { freqToX (p.freq), specToY ((float) p.threshold + tiltAt (proc, p.freq)) };
}

int EQDisplay::hitTestThreshold (juce::Point<float> p) const
{
    for (int b = 0; b < kNumBands; ++b)
    {
        if (! bands[b].used || ! bands[b].isDynamic()) continue;
        const auto tp = thresholdHandlePosition (b);
        if (std::abs (tp.x - p.x) <= 26.0f && std::abs (tp.y - p.y) <= 6.0f && nodePosition (b).getDistanceFrom (p) > kNodeR + 1.0f)
            return b;
    }
    return -1;
}

void EQDisplay::paintThreshold (juce::Graphics& g, int b, float dim)
{
    const auto& p = bands[b];
    const auto col = bandColour (b);
    const auto tp = thresholdHandlePosition (b);
    const bool active = b == hoverThresh || (drag == Drag::Threshold && dragBand == b);
    const bool sel = std::find (selection.begin(), selection.end(), b) != selection.end();
    const float alpha = (active ? 1.0f : (sel || b == hoverBand ? 0.85f : 0.5f)) * dim;

    // Live detector level: a small triangle, orange while above the threshold.
    const float level = proc.getDetectorLevel (b);
    const float anaRange = (float) (int) proc.uiState.getProperty ("anaRange", 90);
    const bool over = level > (float) p.threshold;
    if (level > -anaRange)
    {
        const float ly = specToY (level + tiltAt (proc, p.freq));
        juce::Path tri;
        tri.addTriangle (tp.x - 30.0f, ly - 4.5f, tp.x - 30.0f, ly + 4.5f, tp.x - 23.0f, ly);
        g.setColour ((over ? Colours::warn : col).withAlpha (0.9f * dim));
        g.fillPath (tri);
    }

    g.setColour (col.withAlpha (alpha));
    g.drawLine (tp.x - 20.0f, tp.y, tp.x + 20.0f, tp.y, active ? 2.0f : 1.4f);
    const auto tab = juce::Rectangle<float> (tp.x + 20.0f, tp.y - 6.0f, 12.0f, 12.0f);
    g.fillRoundedRectangle (tab, 3.0f);
    g.setColour (juce::Colours::black.withAlpha (0.85f * dim));
    g.setFont (LookAndFeel::font (9.5f, true));
    g.drawText ("T", tab, juce::Justification::centred);

    if (active)
    {
        juce::String txt = "Threshold " + formatGain (p.threshold).replace ("+", "");
        if (level > -150.0f) txt << "   level " << juce::String (level, 1) << " dB";
        const auto box = juce::Rectangle<float> (tp.x + 36.0f, tp.y - 9.0f, 190.0f, 18.0f);
        g.setColour (Colours::bg2.withAlpha (0.92f));
        g.fillRoundedRectangle (box, 4.0f);
        g.setColour (over ? Colours::warn : Colours::text);
        g.setFont (LookAndFeel::font (12.0f));
        g.drawText (txt, box.reduced (6.0f, 0.0f), juce::Justification::centredLeft);
    }
}

void EQDisplay::paintNodes (juce::Graphics& g)
{
    const int solo = dsp::findSoloBand (bands);
    std::vector<int> order;
    for (int b = 0; b < kNumBands; ++b)
        if (bands[b].used) order.push_back (b);
    std::stable_sort (order.begin(), order.end(), [this] (int a, int b)
    {
        auto rank = [this] (int x) { return (std::find (selection.begin(), selection.end(), x) != selection.end() ? 2 : 0) + (x == hoverBand ? 1 : 0); };
        return rank (a) < rank (b);
    });

    for (int b : order)
    {
        const auto& p = bands[b];
        const auto col = bandColour (b);
        const auto pos = nodePosition (b);
        const bool sel = std::find (selection.begin(), selection.end(), b) != selection.end();
        const bool hov = b == hoverBand;
        const float dim = (solo >= 0 && solo != b) ? 0.35f : 1.0f;
        const float r = kNodeR * (hov ? 1.18f : 1.0f);

        if (p.isDynamic())
        {
            paintThreshold (g, b, dim);
            const auto hp = rangeHandlePosition (b);
            g.setColour (col.withAlpha (0.55f * dim));
            g.drawLine ({ pos, hp }, 1.2f);
            const bool hr = b == hoverRange;
            g.setColour (col.withAlpha ((hr ? 1.0f : 0.8f) * dim));
            g.fillRoundedRectangle (hp.x - 7.0f, hp.y - 2.5f, 14.0f, 5.0f, 2.5f);

            // live dynamic gain
            const float live = dbToY ((p.gain + proc.getDynamicOffset (b)) * gainScale());
            if (std::abs (live - pos.y) > 1.0f)
            {
                g.setColour (col.withAlpha (0.9f * dim));
                g.drawEllipse (pos.x - 4.0f, live - 4.0f, 8.0f, 8.0f, 1.5f);
            }
        }

        if (sel)
        {
            g.setColour (juce::Colours::white.withAlpha (0.9f * dim));
            g.drawEllipse (pos.x - r - 3.0f, pos.y - r - 3.0f, (r + 3.0f) * 2.0f, (r + 3.0f) * 2.0f, 1.5f);
        }

        if (p.enabled)
        {
            g.setColour (col.withAlpha (dim));
            g.fillEllipse (pos.x - r, pos.y - r, r * 2.0f, r * 2.0f);
            g.setColour (juce::Colours::black.withAlpha (0.8f * dim));
        }
        else
        {
            g.setColour (Colours::bg2.withAlpha (0.9f));
            g.fillEllipse (pos.x - r, pos.y - r, r * 2.0f, r * 2.0f);
            g.setColour (col.withAlpha (0.7f * dim));
            g.drawEllipse (pos.x - r, pos.y - r, r * 2.0f, r * 2.0f, 1.5f);
        }
        g.setFont (LookAndFeel::font (10.5f, true));
        g.drawText (juce::String (b + 1), juce::Rectangle<float> (pos.x - r, pos.y - r, r * 2.0f, r * 2.0f), juce::Justification::centred);

        if (p.solo)
        {
            g.setColour (Colours::warn);
            g.setFont (LookAndFeel::font (10.0f, true));
            g.drawText ("SOLO", juce::Rectangle<float> (pos.x - 20.0f, pos.y - r - 16.0f, 40.0f, 12.0f), juce::Justification::centred);
        }
    }
}

void EQDisplay::paintReadout (juce::Graphics& g)
{
    g.setFont (LookAndFeel::font (12.0f));
    const int b = drag == Drag::Band ? dragBand : (hoverBand >= 0 ? hoverBand : hoverRange);

    if (b >= 0 && bands[b].used)
    {
        const auto& p = bands[b];
        juce::String line1 = filterTypeNames()[(int) p.type];
        if (typeHasSlope (p.type)) line1 << "  " << slopeNames()[p.slopeIdx];
        if (p.placement != Placement::Stereo) line1 << "  [" << placementNames()[(int) p.placement] << "]";
        juce::String line2 = formatFrequency (p.freq) + "  " + noteNameForFrequency (p.freq);
        juce::String line3;
        if (typeHasGain (p.type)) line3 << formatGain (p.gain) << "   ";
        if (typeHasQ (p.type)) line3 << "Q " << formatQ (p.q);
        if (p.isDynamic()) line3 << "   Dyn " << formatGain (p.dynRange);

        const auto pos = nodePosition (b);
        const float w = 190.0f, h = 52.0f;
        auto box = juce::Rectangle<float> (pos.x + 14.0f, pos.y - h - 10.0f, w, h);
        if (box.getRight() > plot.getRight() - 4.0f) box.setX (pos.x - w - 14.0f);
        if (box.getY() < plot.getY() + 4.0f) box.setY (pos.y + 14.0f);
        g.setColour (Colours::bg2.withAlpha (0.92f));
        g.fillRoundedRectangle (box, 5.0f);
        g.setColour (bandColour (b).withAlpha (0.7f));
        g.drawRoundedRectangle (box, 5.0f, 1.0f);
        g.setColour (Colours::text);
        auto t = box.reduced (8.0f, 4.0f);
        g.setFont (LookAndFeel::font (12.0f, true));
        g.drawText (line1, t.removeFromTop (15.0f), juce::Justification::centredLeft);
        g.setFont (LookAndFeel::font (12.0f));
        g.drawText (line2, t.removeFromTop (15.0f), juce::Justification::centredLeft);
        g.drawText (line3, t.removeFromTop (15.0f), juce::Justification::centredLeft);
        return;
    }

    if (grabPoint >= 0)
    {
        const float* lv = (bool) proc.uiState.getProperty ("anaPost", true) ? post.levels() : pre.levels();
        const float x = freqToX (pointFreq (grabPoint));
        const float y = specToY (lv[grabPoint]);
        g.setColour (Colours::accent);
        g.drawEllipse (x - 6.0f, y - 6.0f, 12.0f, 12.0f, 1.5f);
        g.drawLine (x - 3.0f, y, x + 3.0f, y, 1.5f);
        g.drawLine (x, y - 3.0f, x, y + 3.0f, 1.5f);
        g.setColour (Colours::text);
        g.drawText (formatFrequency (pointFreq (grabPoint)) + "  drag to cut/boost",
                    juce::Rectangle<float> (x + 10.0f, y - 20.0f, 200.0f, 14.0f), juce::Justification::centredLeft);
    }

    if (mouseInside && drag == Drag::None)
    {
        const double f = xToFreq (mousePos.x);
        const double db = yToDb (mousePos.y);
        const auto txt = formatFrequency (f) + "   " + noteNameForFrequency (f) + "   " + formatGain (db);
        g.setColour (Colours::textDim);
        g.drawText (txt, juce::Rectangle<float> (plot.getRight() - 300.0f, plot.getY() + 6.0f, 222.0f, 18.0f), juce::Justification::centredRight);
    }
}

// ---------------------------------------------------------------------------
// Band panel placement
// ---------------------------------------------------------------------------
void EQDisplay::layoutBandPanel()
{
    if (bandPanel == nullptr) return;
    const int b = primarySelection();
    if (b < 0 || ! proc.refs.readBand (b).used || getWidth() < 100)
    {
        if (bandPanel->isVisible()) bandPanel->setVisible (false);
        return;
    }
    const int w = bandPanel->getWidth(), h = bandPanel->getHeight();
    const float fx = freqToX (proc.refs.readBand (b).freq);
    const int x = juce::jlimit (8, juce::jmax (8, getWidth() - w - 8), (int) std::round (fx - w * 0.5f));
    const int y = getHeight() - h - 10 - (int) pianoHeight (proc);
    if (bandPanel->getX() != x || bandPanel->getY() != y)
        bandPanel->setTopLeftPosition (x, y);
    if (! bandPanel->isVisible()) bandPanel->setVisible (true);
}

// ---------------------------------------------------------------------------
// Interaction
// ---------------------------------------------------------------------------
bool EQDisplay::isUsed (int b) const
{
    return b >= 0 && b < kNumBands && proc.refs.readBand (b).used;
}

void EQDisplay::setSelection (std::vector<int> s)
{
    std::vector<int> clean;
    for (int b : s)
        if (isUsed (b) && std::find (clean.begin(), clean.end(), b) == clean.end())
            clean.push_back (b);
    if (clean == selection) return;
    selection = clean;
    layoutBandPanel();
    if (onSelectionChanged) onSelectionChanged();
}

int EQDisplay::hitTestBand (juce::Point<float> p) const
{
    int best = -1;
    float bestD = kNodeR + 4.0f;
    for (int b = 0; b < kNumBands; ++b)
    {
        if (! bands[b].used) continue;
        const float d = nodePosition (b).getDistanceFrom (p);
        const bool sel = std::find (selection.begin(), selection.end(), b) != selection.end();
        if (d < bestD || (sel && d <= bestD + 0.5f && d < kNodeR + 4.0f))
        {
            bestD = d;
            best = b;
        }
    }
    return best;
}

int EQDisplay::hitTestRange (juce::Point<float> p) const
{
    for (int b = 0; b < kNumBands; ++b)
    {
        if (! bands[b].used || ! bands[b].isDynamic()) continue;
        const auto hp = rangeHandlePosition (b);
        if (std::abs (hp.x - p.x) <= 9.0f && std::abs (hp.y - p.y) <= 6.0f && nodePosition (b).getDistanceFrom (p) > kNodeR + 1.0f)
            return b;
    }
    return -1;
}

void EQDisplay::findGrabPeak (juce::Point<float> p)
{
    grabPoint = -1;
    const auto& ui = proc.uiState;
    if (! (bool) ui.getProperty ("grab", true)) return;
    const bool usePost = (bool) ui.getProperty ("anaPost", true);
    if (! usePost && ! (bool) ui.getProperty ("anaPre", true)) return;
    const float* lv = usePost ? post.levels() : pre.levels();
    const float range = (float) (int) ui.getProperty ("anaRange", 90);

    const int i0 = pointIndexForFreq (xToFreq (p.x - 14.0f));
    const int i1 = pointIndexForFreq (xToFreq (p.x + 14.0f));
    int best = -1;
    for (int i = juce::jmax (3, i0); i <= juce::jmin (kSpecPoints - 4, i1); ++i)
        if (best < 0 || lv[i] > lv[best]) best = i;
    if (best < 0) return;
    if (pointFreq (best) >= 0.5 * proc.getCurrentSampleRate()) return;

    const float v = lv[best];
    const bool isPeak = v >= lv[best - 1] && v >= lv[best + 1] && v - juce::jmin (lv[best - 3], lv[best + 3]) > 1.5f;
    if (! isPeak || v < -range + 8.0f) return;
    if (std::abs (specToY (v) - p.y) > 36.0f) return;
    grabPoint = best;
}

void EQDisplay::setParamValue (int b, const char* id, float v)
{
    if (auto* prm = proc.param (ID::band (b, id)))
        prm->setValueNotifyingHost (prm->convertTo0to1 (v));
}

void EQDisplay::beginBandGestures()
{
    gestureBands = selection;
    if (std::find (gestureBands.begin(), gestureBands.end(), dragBand) == gestureBands.end())
        gestureBands.push_back (dragBand);
    for (int b : gestureBands)
    {
        const auto p = proc.refs.readBand (b);
        startVals[(size_t) b] = { p.freq, p.gain, p.q, p.dynRange };
        for (auto* id : { ID::freq, ID::gain, ID::q })
            if (auto* prm = proc.param (ID::band (b, id))) prm->beginChangeGesture();
    }
}

void EQDisplay::endBandGestures()
{
    for (int b : gestureBands)
        for (auto* id : { ID::freq, ID::gain, ID::q })
            if (auto* prm = proc.param (ID::band (b, id))) prm->endChangeGesture();
    gestureBands.clear();
}

void EQDisplay::notifyEdit()
{
    if (onEdit) onEdit();
}

void EQDisplay::mouseMove (const juce::MouseEvent& e)
{
    mousePos = e.position;
    mouseInside = true;
    hoverRange = hitTestRange (e.position);
    hoverBand = hoverRange >= 0 ? -1 : hitTestBand (e.position);
    hoverThresh = (hoverRange < 0 && hoverBand < 0) ? hitTestThreshold (e.position) : -1;
    if (hoverBand < 0 && hoverRange < 0 && hoverThresh < 0) findGrabPeak (e.position);
    else grabPoint = -1;

    if (hoverThresh >= 0)
    {
        setMouseCursor (juce::MouseCursor::UpDownResizeCursor);
        return;
    }

    if (sketchMode() && hoverBand < 0 && hoverRange < 0)
    {
        grabPoint = -1;
        setMouseCursor (juce::MouseCursor::CrosshairCursor);
        return;
    }
    setMouseCursor (hoverBand >= 0 || hoverRange >= 0 ? juce::MouseCursor::DraggingHandCursor
                    : grabPoint >= 0 ? juce::MouseCursor::CrosshairCursor
                                     : juce::MouseCursor::NormalCursor);
}

void EQDisplay::mouseExit (const juce::MouseEvent&)
{
    mouseInside = false;
    hoverBand = hoverRange = hoverThresh = grabPoint = -1;
}

void EQDisplay::mouseDown (const juce::MouseEvent& e)
{
    grabKeyboardFocus();
    mousePos = dragStart = e.position;

    const auto pill = juce::Rectangle<float> (plot.getRight() - 70.0f, plot.getY() + 6.0f, 62.0f, 18.0f);
    if (pill.contains (e.position))
    {
        juce::PopupMenu m;
        const int ranges[] = { 3, 6, 12, 24, 30 };
        const int cur = (int) displayRange();
        for (int r : ranges)
            m.addItem (r, juce::String::fromUTF8 ("\xc2\xb1") + juce::String (r) + " dB", true, r == cur);
        juce::Component::SafePointer<EQDisplay> safe (this);
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withMousePosition(), [safe] (int r)
        {
            if (safe != nullptr && r > 0) safe->proc.uiState.setProperty ("displayRange", r, nullptr);
        });
        return;
    }

    const int hr = hitTestRange (e.position);
    const int hb = hr >= 0 ? -1 : hitTestBand (e.position);
    const int ht = (hr < 0 && hb < 0) ? hitTestThreshold (e.position) : -1;

    if (e.mods.isPopupMenu())
    {
        if (hb >= 0)
        {
            if (std::find (selection.begin(), selection.end(), hb) == selection.end())
                selectBand (hb);
            showBandMenu (hb);
        }
        else
        {
            showBackgroundMenu (e.position);
        }
        return;
    }

    if (hr >= 0)
    {
        drag = Drag::Range;
        dragBand = hr;
        startVals[(size_t) hr].r = proc.refs.readBand (hr).dynRange;
        if (auto* prm = proc.param (ID::band (hr, ID::dynRange))) prm->beginChangeGesture();
        return;
    }

    if (ht >= 0)
    {
        drag = Drag::Threshold;
        dragBand = ht;
        startVals[(size_t) ht].t = proc.refs.readBand (ht).threshold;
        if (auto* prm = proc.param (ID::band (ht, ID::threshold))) prm->beginChangeGesture();
        return;
    }

    if (hb >= 0)
    {
        if (e.mods.isAltDown())
        {
            proc.deleteBand (hb);
            selection.erase (std::remove (selection.begin(), selection.end(), hb), selection.end());
            if (onSelectionChanged) onSelectionChanged();
            notifyEdit();
            return;
        }

        const bool inSel = std::find (selection.begin(), selection.end(), hb) != selection.end();
        if (e.mods.isShiftDown() && ! inSel)
        {
            auto s = selection;
            s.insert (s.begin(), hb);
            setSelection (s);
        }
        else if (! inSel && ! e.mods.isCtrlDown())
        {
            selectBand (hb);
        }
        else if (inSel)
        {
            // make the clicked band the primary one
            auto s = selection;
            s.erase (std::remove (s.begin(), s.end(), hb), s.end());
            s.insert (s.begin(), hb);
            setSelection (s);
        }

        drag = Drag::Band;
        dragBand = hb;
        beginBandGestures();
        return;
    }

    if (sketchMode())
    {
        drag = Drag::Sketch;
        sketch.fill (std::numeric_limits<float>::quiet_NaN());
        sketchLastIdx = -1;
        addSketchPoint (e.position);
        return;
    }

    if (grabPoint >= 0)
    {
        // Spectrum grab: create a bell on the peak and drag its gain.
        const float* lv = (bool) proc.uiState.getProperty ("anaPost", true) ? post.levels() : pre.levels();
        const int pk = grabPoint;
        int lo = pk, hi = pk;
        while (lo > 0 && lv[lo] > lv[pk] - 3.0f) --lo;
        while (hi < kSpecPoints - 1 && lv[hi] > lv[pk] - 3.0f) ++hi;
        const double bwOct = std::max (0.05, std::log2 (pointFreq (hi) / pointFreq (lo)));
        const double q = juce::jlimit (0.7, 12.0, std::sqrt (std::pow (2.0, bwOct)) / (std::pow (2.0, bwOct) - 1.0));
        const int b = proc.createBand (FilterType::Bell, pointFreq (pk), 0.0, q, 1, Placement::Stereo);
        grabPoint = -1;
        if (b >= 0)
        {
            bands[b] = proc.refs.readBand (b);
            selectBand (b);
            drag = Drag::Band;
            dragBand = b;
            beginBandGestures();
        }
        return;
    }

    drag = Drag::Rubber;
    rubber = {};
    rubberBase = (e.mods.isShiftDown() || e.mods.isCtrlDown()) ? selection : std::vector<int> {};
    if (rubberBase.empty()) setSelection ({});
}

void EQDisplay::mouseDrag (const juce::MouseEvent& e)
{
    mousePos = e.position;
    const double scale = gainScale();
    const double safeScale = std::abs (scale) < 0.05 ? (scale < 0 ? -0.05 : 0.05) : scale;

    if (drag == Drag::Band)
    {
        const float k = e.mods.isShiftDown() ? 0.15f : 1.0f;
        const float dx = (e.position.x - dragStart.x) * k;
        const float dy = (e.position.y - dragStart.y) * k;
        const bool qMode = e.mods.isCtrlDown();

        for (int b : gestureBands)
        {
            const auto& sv = startVals[(size_t) b];
            const auto type = proc.refs.readBand (b).type;
            setParamValue (b, ID::freq, (float) juce::jlimit (kMinFreq, kMaxFreq, xToFreq (freqToX (sv.f) + dx)));
            if (qMode || ! typeHasGain (type))
            {
                setParamValue (b, ID::q, (float) juce::jlimit (kMinQ, kMaxQ, sv.q * std::pow (2.0, -dy / 40.0)));
            }
            else
            {
                const double disp = yToDb (dbToY (sv.g * scale) + dy);
                setParamValue (b, ID::gain, (float) juce::jlimit (-kMaxGain, kMaxGain, disp / safeScale));
            }
        }
        return;
    }

    if (drag == Drag::Range)
    {
        // Relative: grabbing the handle never makes the value jump.
        const double k = e.mods.isShiftDown() ? 0.2 : 1.0;
        const double delta = (yToDb (e.position.y) - yToDb (dragStart.y)) * k / safeScale;
        setParamValue (dragBand, ID::dynRange, (float) juce::jlimit (-30.0, 30.0, startVals[(size_t) dragBand].r + delta));
        return;
    }

    if (drag == Drag::Threshold)
    {
        const double k = e.mods.isShiftDown() ? 0.2 : 1.0;
        const double delta = (specFromY (e.position.y) - specFromY (dragStart.y)) * k;
        setParamValue (dragBand, ID::threshold, (float) juce::jlimit (-80.0, 6.0, startVals[(size_t) dragBand].t + delta));
        return;
    }

    if (drag == Drag::Sketch)
    {
        addSketchPoint (e.position);
        return;
    }

    if (drag == Drag::Rubber)
    {
        rubber = juce::Rectangle<float> (dragStart, e.position);
        auto s = rubberBase;
        for (int b = 0; b < kNumBands; ++b)
            if (bands[b].used && rubber.contains (nodePosition (b)) && std::find (s.begin(), s.end(), b) == s.end())
                s.push_back (b);
        setSelection (s);
    }
}

void EQDisplay::mouseUp (const juce::MouseEvent& e)
{
    if (drag == Drag::Band)
    {
        endBandGestures();
        if (e.mods.isCtrlDown() && e.getDistanceFromDragStart() < 3)
        {
            auto s = selection;
            auto it = std::find (s.begin(), s.end(), dragBand);
            if (it != s.end() && s.size() > 1) s.erase (it);
            else if (it == s.end()) s.push_back (dragBand);
            setSelection (s);
        }
        notifyEdit();
    }
    else if (drag == Drag::Range)
    {
        if (auto* prm = proc.param (ID::band (dragBand, ID::dynRange))) prm->endChangeGesture();
        notifyEdit();
    }
    else if (drag == Drag::Threshold)
    {
        if (auto* prm = proc.param (ID::band (dragBand, ID::threshold))) prm->endChangeGesture();
        notifyEdit();
    }
    else if (drag == Drag::Sketch)
    {
        drag = Drag::None;
        finishSketch();
    }
    drag = Drag::None;
    rubber = {};
}

void EQDisplay::mouseDoubleClick (const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu()) return;
    const int hb = hitTestBand (e.position);
    if (hb >= 0)
    {
        const auto p = proc.refs.readBand (hb);
        proc.setParam (ID::band (hb, ID::enabled), p.enabled ? 0.0f : 1.0f);
        notifyEdit();
        return;
    }
    if (hitTestRange (e.position) < 0 && hitTestThreshold (e.position) < 0)
        createBandAt (e.position);
}

void EQDisplay::createBandAt (juce::Point<float> pos)
{
    const double f = xToFreq (pos.x);
    const double scale = gainScale();
    const double disp = yToDb (pos.y) / (std::abs (scale) < 0.05 ? 1.0 : scale);
    FilterType type = FilterType::Bell;
    if (f < 30.0) type = FilterType::LowCut;
    else if (f > 16000.0) type = FilterType::HighCut;

    const bool bell = type == FilterType::Bell;
    const int b = proc.createBand (type, f, bell ? juce::jlimit (-kMaxGain, kMaxGain, disp) : 0.0,
                                   bell ? 1.0 : kDefaultQ, bell ? 1 : 3, Placement::Stereo);
    if (b >= 0)
    {
        bands[b] = proc.refs.readBand (b);
        selectBand (b);
        notifyEdit();
    }
}

void EQDisplay::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    const int hb = hitTestBand (e.position);
    if (hb < 0) return;

    float delta = w.deltaY;
    if (w.isReversed) delta = -delta;
    const bool fine = e.mods.isShiftDown();

    applyToTargets (hb, [&] (int b)
    {
        const auto p = proc.refs.readBand (b);
        if (e.mods.isCtrlDown())
        {
            if (! typeSupportsDynamics (p.type)) return;
            if (! p.dynOn) proc.setParam (ID::band (b, ID::dynOn), 1.0f);   // range set below
            proc.setParam (ID::band (b, ID::dynRange), (float) juce::jlimit (-30.0, 30.0, p.dynRange + delta * (fine ? 2.0 : 12.0)));
        }
        else if (typeHasQ (p.type))
        {
            proc.setParam (ID::band (b, ID::q), (float) juce::jlimit (kMinQ, kMaxQ, p.q * std::pow (2.0, delta * (fine ? 0.4 : 2.0))));
        }
    });
    wheelCommitCountdown = 20;
}

void EQDisplay::applyToTargets (int clicked, const std::function<void (int)>& fn)
{
    const bool inSel = std::find (selection.begin(), selection.end(), clicked) != selection.end();
    if (inSel) for (int b : selection) fn (b);
    else       fn (clicked);
}

void EQDisplay::deleteSelected()
{
    for (int b : selection)
        proc.deleteBand (b);
    selection.clear();
    if (onSelectionChanged) onSelectionChanged();
    notifyEdit();
}

bool EQDisplay::keyPressed (const juce::KeyPress& key)
{
    if (key == juce::KeyPress::deleteKey || key == juce::KeyPress::backspaceKey)
    {
        if (selection.empty()) return false;
        deleteSelected();
        return true;
    }
    if (key == juce::KeyPress ('a', juce::ModifierKeys::ctrlModifier, 0))
    {
        std::vector<int> all;
        for (int b = 0; b < kNumBands; ++b) if (bands[b].used) all.push_back (b);
        setSelection (all);
        return true;
    }
    if (key == juce::KeyPress::escapeKey)
    {
        setSelection ({});
        return true;
    }

    const bool fine = key.getModifiers().isShiftDown();
    const int code = key.getKeyCode();
    if (! selection.empty() && (code == juce::KeyPress::leftKey || code == juce::KeyPress::rightKey
                                || code == juce::KeyPress::upKey || code == juce::KeyPress::downKey))
    {
        for (int b : selection)
        {
            const auto p = proc.refs.readBand (b);
            if (code == juce::KeyPress::leftKey || code == juce::KeyPress::rightKey)
            {
                const double step = (code == juce::KeyPress::rightKey ? 1.0 : -1.0) / (fine ? 48.0 : 12.0);
                proc.setParam (ID::band (b, ID::freq), (float) juce::jlimit (kMinFreq, kMaxFreq, p.freq * std::pow (2.0, step)));
            }
            else
            {
                const double dir = code == juce::KeyPress::upKey ? 1.0 : -1.0;
                if (typeHasGain (p.type))
                    proc.setParam (ID::band (b, ID::gain), (float) juce::jlimit (-kMaxGain, kMaxGain, p.gain + dir * (fine ? 0.1 : 0.5)));
                else
                    proc.setParam (ID::band (b, ID::q), (float) juce::jlimit (kMinQ, kMaxQ, p.q * std::pow (2.0, dir * (fine ? 0.05 : 0.25))));
            }
        }
        notifyEdit();
        return true;
    }
    return false;
}

// ---------------------------------------------------------------------------
// EQ Sketch
// ---------------------------------------------------------------------------
bool EQDisplay::sketchMode() const
{
    return (bool) proc.uiState.getProperty ("sketch", false);
}

void EQDisplay::addSketchPoint (juce::Point<float> p)
{
    const int idx = pointIndexForFreq (xToFreq (p.x));
    const float db = (float) juce::jlimit (-displayRange(), displayRange(), yToDb (p.y));
    if (sketchLastIdx < 0 || sketchLastIdx == idx)
    {
        sketch[(size_t) idx] = db;
    }
    else
    {
        const int a = juce::jmin (sketchLastIdx, idx), b = juce::jmax (sketchLastIdx, idx);
        for (int i = a; i <= b; ++i)
        {
            const float t = (float) (i - sketchLastIdx) / (float) (idx - sketchLastIdx);
            sketch[(size_t) i] = sketchLastDb + t * (db - sketchLastDb);
        }
    }
    sketchLastIdx = idx;
    sketchLastDb = db;
}

void EQDisplay::finishSketch()
{
    int i0 = -1, i1 = -1;
    for (int i = 0; i < kSpecPoints; ++i)
        if (! std::isnan (sketch[(size_t) i])) { if (i0 < 0) i0 = i; i1 = i; }
    if (i0 < 0 || i1 - i0 < 4) return;

    const double fs = curveRate > 0.0 ? curveRate : 48000.0;
    while (i1 > i0 && pointFreq (i1) > 0.45 * fs) --i1;

    // Fit what has to be added on top of the existing curve. Half an octave of
    // flat target on either side keeps the new bands contained.
    const int margin = 22;
    int e1 = juce::jmin (kSpecPoints - 1, i1 + margin);
    while (e1 > i1 && pointFreq (e1) > 0.45 * fs) --e1;
    const int e0 = juce::jmax (0, i0 - margin);
    std::vector<double> target ((size_t) kSpecPoints, 0.0);
    for (int i = i0; i <= i1; ++i)
        target[(size_t) i] = sketch[(size_t) i] - totals[0][(size_t) i];

    int freeBands = 0;
    for (int b = 0; b < kNumBands; ++b) if (! bands[b].used) ++freeBands;
    double rms = 0.0;
    const auto fits = fitCurve (target, e0, e1, juce::jmin (12, freeBands), fs, rms, 0.3);

    const double scale = gainScale();
    const double safeScale = std::abs (scale) < 0.05 ? 1.0 : scale;
    std::vector<int> created;
    for (const auto& f : fits)
    {
        const int b = proc.createBand (f.type, f.freq, juce::jlimit (-kMaxGain, kMaxGain, f.gain / safeScale), f.q, 1, Placement::Stereo);
        if (b >= 0) created.push_back (b);
    }
    for (int b : created) bands[b] = proc.refs.readBand (b);
    setSelection (created);
    sketch.fill (std::numeric_limits<float>::quiet_NaN());
    notifyEdit();
}

// ---------------------------------------------------------------------------
// Context menus
// ---------------------------------------------------------------------------
void EQDisplay::showBandMenu (int b)
{
    const auto p = proc.refs.readBand (b);
    juce::PopupMenu m, shapes, slopes, places;
    const auto tn = filterTypeNames();
    for (int t = 0; t < (int) FilterType::Count; ++t)
        shapes.addItem (100 + t, tn[t], true, (int) p.type == t);
    const auto sn = slopeNames();
    for (int s = 0; s < kNumSlopes; ++s)
        slopes.addItem (200 + s, sn[s], true, p.slopeIdx == s);
    const auto pn = placementNames();
    for (int s = 0; s < (int) Placement::Count; ++s)
        places.addItem (300 + s, pn[s], true, (int) p.placement == s);

    m.addSectionHeader ("Band " + juce::String (b + 1));
    m.addSubMenu ("Shape", shapes);
    if (typeHasSlope (p.type)) m.addSubMenu ("Slope", slopes);
    m.addSubMenu ("Stereo Placement", places);
    m.addSeparator();
    m.addItem (1, "Enabled", true, p.enabled);
    m.addItem (2, "Solo", true, p.solo);
    if (typeSupportsDynamics (p.type))
    {
        m.addItem (3, "Dynamic EQ", true, p.dynOn);
        m.addItem (4, "External Side-chain Trigger", p.dynOn, p.dynSC);
    }
    m.addSeparator();
    if (typeHasGain (p.type))
    {
        m.addItem (5, "Invert Gain");
        m.addItem (6, "Reset Gain");
    }
    m.addItem (7, "Delete Band");

    juce::Component::SafePointer<EQDisplay> safe (this);
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withMousePosition(), [safe, b] (int r)
    {
        if (safe == nullptr || r == 0) return;
        auto& pr = safe->proc;
        safe->applyToTargets (b, [&] (int t)
        {
            const auto cur = pr.refs.readBand (t);
            if (r >= 100 && r < 200)      pr.setParam (ID::band (t, ID::type), (float) (r - 100));
            else if (r >= 200 && r < 300) pr.setParam (ID::band (t, ID::slope), (float) (r - 200));
            else if (r >= 300 && r < 400) pr.setParam (ID::band (t, ID::placement), (float) (r - 300));
            else if (r == 1) pr.setParam (ID::band (t, ID::enabled), cur.enabled ? 0.0f : 1.0f);
            else if (r == 3)
            {
                pr.setParam (ID::band (t, ID::dynOn), cur.dynOn ? 0.0f : 1.0f);
                if (! cur.dynOn && std::abs (cur.dynRange) < 0.01) pr.setParam (ID::band (t, ID::dynRange), -6.0f);
            }
            else if (r == 4) pr.setParam (ID::band (t, ID::dynSC), cur.dynSC ? 0.0f : 1.0f);
            else if (r == 5) pr.setParam (ID::band (t, ID::gain), (float) -cur.gain);
            else if (r == 6) pr.setParam (ID::band (t, ID::gain), 0.0f);
        });
        if (r == 2)
        {
            const bool newSolo = ! pr.refs.readBand (b).solo;
            for (int o = 0; o < kNumBands; ++o)
                if (pr.refs.readBand (o).solo) pr.setParam (ID::band (o, ID::solo), 0.0f);
            if (newSolo) pr.setParam (ID::band (b, ID::solo), 1.0f);
        }
        if (r == 7)
        {
            safe->deleteSelected();
            if (pr.refs.readBand (b).used) { pr.deleteBand (b); }
        }
        safe->notifyEdit();
    });
}

void EQDisplay::showBackgroundMenu (juce::Point<float> pos)
{
    juce::PopupMenu m, add, range;
    const auto tn = filterTypeNames();
    for (int t = 0; t < (int) FilterType::Count; ++t)
        add.addItem (100 + t, tn[t]);
    m.addSubMenu ("Add Band Here", add, proc.findFreeBand() >= 0);
    m.addSeparator();
    m.addItem (1, "Select All Bands");
    m.addItem (2, "Invert All Gains");
    m.addItem (3, "Reset All Gains");
    m.addItem (4, "Bypass All Bands");
    m.addItem (5, "Enable All Bands");
    m.addSeparator();
    m.addItem (6, "Delete All Bands");

    const double f = xToFreq (pos.x);
    const double db = yToDb (pos.y);
    juce::Component::SafePointer<EQDisplay> safe (this);
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withMousePosition(), [safe, f, db] (int r)
    {
        if (safe == nullptr || r == 0) return;
        auto& pr = safe->proc;
        if (r >= 100)
        {
            const auto t = (FilterType) (r - 100);
            const bool cut = t == FilterType::LowCut || t == FilterType::HighCut;
            const int b = pr.createBand (t, f, typeHasGain (t) ? juce::jlimit (-kMaxGain, kMaxGain, db) : 0.0,
                                         (t == FilterType::Bell || t == FilterType::Notch || t == FilterType::BandPass) ? 1.0 : kDefaultQ,
                                         cut ? 3 : 1, Placement::Stereo);
            if (b >= 0) safe->selectBand (b);
        }
        for (int b = 0; b < kNumBands; ++b)
        {
            const auto p = pr.refs.readBand (b);
            if (! p.used) continue;
            if (r == 2) pr.setParam (ID::band (b, ID::gain), (float) -p.gain);
            if (r == 3) pr.setParam (ID::band (b, ID::gain), 0.0f);
            if (r == 4) pr.setParam (ID::band (b, ID::enabled), 0.0f);
            if (r == 5) pr.setParam (ID::band (b, ID::enabled), 1.0f);
            if (r == 6) pr.deleteBand (b);
        }
        if (r == 1)
        {
            std::vector<int> all;
            for (int b = 0; b < kNumBands; ++b) if (pr.refs.readBand (b).used) all.push_back (b);
            safe->setSelection (all);
        }
        if (r == 6) safe->setSelection ({});
        safe->notifyEdit();
    });
}

} // namespace meq::ui
