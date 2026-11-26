// GKEQ - 24-band dynamic EQ plug-in
// Copyright (C) 2026 Roman Gutekunst (GK)
// SPDX-License-Identifier: GPL-3.0-or-later

// Renders the real editor (in-process) to PNG files for visual inspection.
#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "ui/CurveFit.h"
#include "ui/Spectrum.h"
#include <cstdio>

using namespace meq;

static void feed (ModernEQProcessor& proc, int blocks, juce::Random& rng, float sideAmp = 0.0f)
{
    juce::AudioBuffer<float> buf (4, 512);
    juce::MidiBuffer midi;
    static float b0 = 0, b1 = 0, b2 = 0, c0 = 0;
    static double phase = 0.0;
    for (int blk = 0; blk < blocks; ++blk)
    {
        buf.clear();
        for (int i = 0; i < 512; ++i)
        {
            const float w = rng.nextFloat() * 2.0f - 1.0f;
            b0 = 0.99765f * b0 + w * 0.0990460f; b1 = 0.96300f * b1 + w * 0.2965164f; b2 = 0.57000f * b2 + w * 1.0526913f;
            phase += 2.0 * juce::MathConstants<double>::pi * 2700.0 / 48000.0;
            const float pink = 0.06f * (b0 + b1 + b2 + w * 0.1848f) + 0.02f * (float) std::sin (phase);
            buf.setSample (0, i, pink);
            buf.setSample (1, i, pink * 0.9f);
            c0 = 0.995f * c0 + 0.05f * (rng.nextFloat() - 0.5f);
            buf.setSample (2, i, sideAmp * c0);
            buf.setSample (3, i, sideAmp * c0);
        }
        proc.processBlock (buf, midi);
        juce::MessageManager::getInstance()->runDispatchLoopUntil (3);
    }
}

static void save (juce::Component& c, const juce::String& name)
{
    auto img = c.createComponentSnapshot (c.getLocalBounds(), true, 1.0f);
    auto f = juce::File::getCurrentWorkingDirectory().getChildFile (name);
    f.deleteFile();
    juce::FileOutputStream os (f);
    juce::PNGImageFormat().writeImageToStream (img, os);
    std::printf ("wrote %s\n", f.getFullPathName().toRawUTF8());
}

/** Application icon (installer + standalone), rendered with JUCE's anti-aliased vector drawing. */
static void renderIcon (const juce::File& out)
{
    constexpr float S = 512.0f;
    juce::Image img (juce::Image::ARGB, (int) S, (int) S, true);
    juce::Graphics g (img);
    g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);

    const juce::Rectangle<float> r (20.0f, 20.0f, S - 40.0f, S - 40.0f);
    juce::ColourGradient bg (juce::Colour (0xff222b3b), 0.0f, r.getY(), juce::Colour (0xff0b0e13), 0.0f, r.getBottom(), false);
    g.setGradientFill (bg);
    g.fillRoundedRectangle (r, 104.0f);
    g.setColour (juce::Colour (0x33ffffff));
    g.drawRoundedRectangle (r.reduced (2.0f), 102.0f, 4.0f);

    // faint grid
    g.setColour (juce::Colour (0x14ffffff));
    for (int i = 1; i < 4; ++i)
        g.fillRect (r.getX() + r.getWidth() * i / 4.0f - 2.0f, r.getY() + 70.0f, 4.0f, r.getHeight() - 140.0f);
    g.fillRect (r.getX() + 60.0f, S * 0.5f - 2.0f, r.getWidth() - 120.0f, 4.0f);

    auto curveY = [S] (float t)
    {
        return S * 0.5f - 118.0f * std::exp (-std::pow ((t - 0.30f) / 0.12f, 2.0f))
                        + 96.0f * std::exp (-std::pow ((t - 0.66f) / 0.065f, 2.0f))
                        - 46.0f / (1.0f + std::exp (-(t - 0.88f) / 0.03f));
    };
    const float x0 = 62.0f, x1 = S - 62.0f;
    juce::Path curve;
    for (int i = 0; i <= 200; ++i)
    {
        const float t = i / 200.0f;
        const float x = x0 + t * (x1 - x0);
        if (i == 0) curve.startNewSubPath (x, curveY (t)); else curve.lineTo (x, curveY (t));
    }
    juce::Path fill (curve);
    fill.lineTo (x1, S * 0.5f);
    fill.lineTo (x0, S * 0.5f);
    fill.closeSubPath();
    g.setColour (juce::Colour (0x554fc3ff));
    g.fillPath (fill);
    g.setColour (juce::Colour (0x40ffffff));
    g.strokePath (curve, juce::PathStrokeType (34.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    g.setColour (juce::Colours::white);
    g.strokePath (curve, juce::PathStrokeType (18.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    auto node = [&] (float t, juce::Colour c)
    {
        const float x = x0 + t * (x1 - x0), y = curveY (t);
        g.setColour (juce::Colour (0xff0b0e13));
        g.fillEllipse (x - 34.0f, y - 34.0f, 68.0f, 68.0f);
        g.setColour (c);
        g.fillEllipse (x - 26.0f, y - 26.0f, 52.0f, 52.0f);
    };
    node (0.30f, juce::Colour (0xffff8a4c));
    node (0.66f, juce::Colour (0xff4fc3ff));

    out.getParentDirectory().createDirectory();
    out.deleteFile();
    juce::FileOutputStream os (out);
    juce::PNGImageFormat().writeImageToStream (img, os);
    std::printf ("wrote %s\n", out.getFullPathName().toRawUTF8());
}

static int failures = 0;
static void expect (bool ok, const char* what)
{
    if (! ok)
    {
        ++failures;
        std::printf ("  FAIL: %s\n", what);
    }
}

int main()
{
    juce::ScopedJuceInitialiser_GUI gui;
    renderIcon (juce::File::getCurrentWorkingDirectory().getChildFile ("Resources/icon.png"));

    {   // Curve fitter sanity check: recover a known 3-band curve
        const double fs = 48000.0;
        std::vector<double> target ((size_t) ui::kSpecPoints, 0.0);
        const dsp::DesignParams ds[3] = { { FilterType::Bell, 250.0, -4.0, 1.5, 1, false },
                                          { FilterType::Bell, 3000.0, 3.0, 0.8, 1, false },
                                          { FilterType::HighShelf, 10000.0, 2.0, kDefaultQ, 1, false } };
        int i0 = 0, i1 = ui::kSpecPoints - 1;
        while (ui::pointFreq (i0) < 25.0) ++i0;
        while (ui::pointFreq (i1) > 18000.0) --i1;
        for (auto& d : ds)
        {
            const auto a = dsp::designAnalog (d, fs);
            for (int i = i0; i <= i1; ++i)
                target[(size_t) i] += 10.0 * std::log10 (a.mag2 (dsp::kTwoPi * ui::pointFreq (i) / fs));
        }
        double rms = 0.0;
        const auto fit = ui::fitCurve (target, i0, i1, 8, fs, rms);
        std::printf ("curve fit: %d bands, residual %.3f dB RMS\n", (int) fit.size(), rms);
        expect (rms < 0.3, "curve fit residual");
        for (auto& f : fit)
            std::printf ("   %-10s %8.1f Hz  %+5.2f dB  Q %.2f\n", filterTypeNames()[(int) f.type].toRawUTF8(), f.freq, f.gain, f.q);
    }

    {   // Robustness: hostile and corrupt project state must not hang or crash the plug-in
        ModernEQProcessor p;
        p.setPlayConfigDetails (4, 2, 48000.0, 512);
        p.prepareToPlay (48000.0, 512);
        // Legitimate settings must survive a save / reload unchanged.
        p.uiState.setProperty ("anaRange", 60, nullptr);
        p.uiState.setProperty ("anaTilt", 3.0, nullptr);
        p.uiState.setProperty ("width", 1320, nullptr);
        p.uiState.setProperty ("displayRange", 24, nullptr);
        {
            juce::MemoryBlock saved;
            p.getStateInformation (saved);
            p.uiState.setProperty ("anaRange", 90, nullptr);
            p.setStateInformation (saved.getData(), (int) saved.getSize());
            const auto& u = p.uiState;
            const bool kept = (int) u["anaRange"] == 60 && std::abs ((double) u["anaTilt"] - 3.0) < 1e-9
                           && (int) u["width"] == 1320 && (int) u["displayRange"] == 24;
            std::printf ("normal UI settings survive save/reload: %s\n", kept ? "yes" : "NO");
            expect (kept, "UI settings round trip");
        }
        {   // The editor must open at the stored size (setResizeLimits must not overwrite it).
            p.uiState.setProperty ("width", 1320, nullptr);
            p.uiState.setProperty ("height", 760, nullptr);
            std::unique_ptr<juce::AudioProcessorEditor> e (p.createEditor());
            std::printf ("editor opens at %d x %d (stored 1320 x 760)\n", e->getWidth(), e->getHeight());
            expect (e->getWidth() == 1320 && e->getHeight() == 760, "editor restores its stored size");
        }
        {   // Value texts are rounded (juce::String (x, 0) would print every decimal).
            auto text = [&p] (const juce::String& name, float v) -> juce::String
            {
                for (auto* prm : p.getParameters())
                    if (prm->getName (100) == name)
                        if (auto* r = dynamic_cast<juce::RangedAudioParameter*> (prm))
                            return r->getText (r->convertTo0to1 (v), 64);
                return "(missing)";
            };
            const auto f = text ("Band 1 Frequency", 150.347f), a = text ("Band 1 Attack", 12.34f);
            std::printf ("value texts: '%s', '%s'\n", f.toRawUTF8(), a.toRawUTF8());
            expect (f == "150 Hz" && a == "12 ms", "parameter value texts are rounded");
        }

        juce::MemoryBlock state;
        p.getStateInformation (state);
        auto xml = juce::AudioProcessor::getXmlFromBinary (state.getData(), (int) state.getSize());
        if (auto* ui = xml != nullptr ? xml->getChildByName ("UI") : nullptr)
        {
            ui->setAttribute ("anaRange", 2000000000);
            ui->setAttribute ("anaTilt", "nan");
            ui->setAttribute ("width", -5);
            ui->setAttribute ("displayRange", 1.0e30);
            ui->setAttribute ("anaRes", 99);
        }
        juce::MemoryBlock evil;
        juce::AudioProcessor::copyXmlToBinary (*xml, evil);
        p.setStateInformation (evil.getData(), (int) evil.getSize());
        const auto& u = p.uiState;
        const bool clamped = (int) u["anaRange"] == 120 && (double) u["anaTilt"] >= 0.0 && (double) u["anaTilt"] <= 6.0
                          && (int) u["width"] == 880 && (int) u["displayRange"] == 30 && (int) u["anaRes"] == 3;
        expect (clamped, "hostile UI state clamped");
        std::printf ("hostile UI state clamped: %s (range %d, tilt %.1f, width %d, displayRange %d, anaRes %d)\n",
                     clamped ? "yes" : "NO", (int) u["anaRange"], (double) u["anaTilt"], (int) u["width"],
                     (int) u["displayRange"], (int) u["anaRes"]);
        {
            std::unique_ptr<juce::AudioProcessorEditor> e (p.createEditor());
            e->setSize (1040, 620);
            juce::Image img (juce::Image::ARGB, 1040, 620, true);
            juce::Graphics g (img);
            const double t0 = juce::Time::getMillisecondCounterHiRes();
            e->paintEntireComponent (g, true);
            std::printf ("editor paint with the hostile state: %.1f ms\n", juce::Time::getMillisecondCounterHiRes() - t0);
        }

        juce::Random rng (99);
        juce::AudioBuffer<float> buf (4, 512);
        juce::MidiBuffer midi;
        for (int i = 0; i < 500; ++i)
        {
            juce::MemoryBlock junk ((size_t) (9 + rng.nextInt (4000)));
            for (size_t k = 0; k < junk.getSize(); ++k) junk[k] = (char) rng.nextInt (256);
            if (i % 2 == 0)   // valid JUCE binary-XML header, garbage body
            {
                const juce::uint32 magic = 0x21324356, len = (juce::uint32) junk.getSize() - 9;
                std::memcpy (junk.getData(), &magic, 4);
                std::memcpy (static_cast<char*> (junk.getData()) + 4, &len, 4);
            }
            p.setStateInformation (junk.getData(), (int) junk.getSize());
            buf.clear();
            p.processBlock (buf, midi);
        }
        std::printf ("survived 500 random / corrupt state blobs while processing\n");
    }

    ModernEQProcessor proc;
    proc.uiState.setProperty ("gpu", false, nullptr);
    proc.setPlayConfigDetails (4, 2, 48000.0, 512);
    proc.prepareToPlay (48000.0, 512);

    proc.createBand (FilterType::LowCut, 35.0, 0.0, kDefaultQ, 3, Placement::Stereo);
    proc.createBand (FilterType::Bell, 180.0, 2.5, 0.9, 1, Placement::Stereo);
    const int dynBand = proc.createBand (FilterType::Bell, 2700.0, 0.0, 4.0, 1, Placement::Stereo);
    proc.setParam (ID::band (dynBand, ID::dynOn), 1.0f);
    proc.setParam (ID::band (dynBand, ID::dynRange), -8.0f);
    proc.setParam (ID::band (dynBand, ID::threshold), -45.0f);
    proc.createBand (FilterType::HighShelf, 9000.0, 3.0, kDefaultQ, 1, Placement::Stereo);
    proc.createBand (FilterType::Notch, 600.0, 0.0, 5.0, 1, Placement::Stereo);
    proc.createBand (FilterType::LowShelf, 120.0, -3.0, kDefaultQ, 1, Placement::Side);

    juce::Random rng (5);
    std::unique_ptr<ModernEQEditor> ed (dynamic_cast<ModernEQEditor*> (proc.createEditor()));
    ed->setSize (1040, 620);
    feed (proc, 250, rng);
    save (*ed, "build/ui_main.png");

    ed->getDisplay().selectBand (dynBand);
    feed (proc, 40, rng);
    save (*ed, "build/ui_selected.png");

    {   // Software-renderer cost of one full editor repaint
        juce::Image img (juce::Image::ARGB, ed->getWidth(), ed->getHeight(), true);
        const double t0 = juce::Time::getMillisecondCounterHiRes();
        for (int i = 0; i < 30; ++i)
        {
            juce::Graphics g (img);
            ed->paintEntireComponent (g, true);
        }
        std::printf ("software repaint 1040x620: %.2f ms per frame\n", (juce::Time::getMillisecondCounterHiRes() - t0) / 30.0);

        auto timeIt = [&] (const char* what, std::function<void (juce::Graphics&)> fn)
        {
            const double s0 = juce::Time::getMillisecondCounterHiRes();
            for (int i = 0; i < 30; ++i) { juce::Graphics g (img); fn (g); }
            std::printf ("   %-34s %.2f ms\n", what, (juce::Time::getMillisecondCounterHiRes() - s0) / 30.0);
        };
        auto& d = ed->getDisplay();
        timeIt ("display incl. band panel", [&] (juce::Graphics& g) { d.paintEntireComponent (g, true); });
        timeIt ("display paint() only", [&] (juce::Graphics& g) { d.paint (g); });
        auto& u = proc.uiState;
        u.setProperty ("anaPre", false, nullptr); u.setProperty ("anaPost", false, nullptr);
        timeIt ("display paint() without analysers", [&] (juce::Graphics& g) { d.paint (g); });
        u.setProperty ("anaPre", true, nullptr); u.setProperty ("anaPost", true, nullptr);
        d.selectBand (-1);
        timeIt ("display paint() no selection", [&] (juce::Graphics& g) { d.paint (g); });
        d.selectBand (dynBand);
    }

    {   // Interaction test: dynamics handles on a freshly enabled band (range 0)
        auto& d = ed->getDisplay();
        const int tb = proc.createBand (FilterType::Bell, 1000.0, 0.0, 1.0, 1, Placement::Stereo);
        proc.setParam (ID::band (tb, ID::dynOn), 1.0f);
        feed (proc, 10, rng);

        auto ev = [&] (juce::Point<float> pos, juce::Point<float> down, bool dragged)
        {
            return juce::MouseEvent (juce::Desktop::getInstance().getMainMouseSource(), pos,
                                     juce::ModifierKeys (juce::ModifierKeys::leftButtonModifier),
                                     juce::MouseInputSource::defaultPressure, juce::MouseInputSource::defaultOrientation,
                                     juce::MouseInputSource::defaultRotation, juce::MouseInputSource::defaultTiltX,
                                     juce::MouseInputSource::defaultTiltY, &d, &d, juce::Time::getCurrentTime(), down,
                                     juce::Time::getCurrentTime(), 1, dragged);
        };
        auto dragFromTo = [&] (juce::Point<float> a, juce::Point<float> b)
        {
            d.mouseDown (ev (a, a, false));
            d.mouseDrag (ev ({ a.x, (a.y + b.y) * 0.5f }, a, true));
            d.mouseDrag (ev (b, a, true));
            d.mouseUp (ev (b, a, true));
        };

        const float x = d.freqToX (1000.0);
        const float nodeY = d.dbToY (0.0);
        dragFromTo ({ x, nodeY + 17.0f }, { x, nodeY + 17.0f + 120.0f });
        const auto afterRange = proc.refs.readBand (tb);
        const double expectRange = d.yToDb (nodeY + 120.0f);
        std::printf ("range drag on a fresh dynamic band: range %.2f dB (expected %.2f), gain %.2f dB\n",
                     afterRange.dynRange, expectRange, afterRange.gain);
        expect (std::abs (afterRange.dynRange - expectRange) < 0.05 && std::abs (afterRange.gain) < 1.0e-6, "range handle drag");

        const float thrY = d.specToY (-30.0f);   // tilt is 0 dB at 1 kHz
        dragFromTo ({ x + 12.0f, thrY }, { x + 12.0f, thrY - 50.0f });
        const auto afterThr = proc.refs.readBand (tb);
        std::printf ("threshold drag: threshold %.2f dB (expected %.2f), range %.2f dB\n",
                     afterThr.threshold, -30.0 + (d.specFromY (thrY - 50.0f) - d.specFromY (thrY)), afterThr.dynRange);
        expect (std::abs (afterThr.threshold - (-30.0 + (d.specFromY (thrY - 50.0f) - d.specFromY (thrY)))) < 0.05, "threshold handle drag");
        feed (proc, 30, rng);
        d.selectBand (tb);
        feed (proc, 5, rng);
        save (*ed, "build/ui_dynamic.png");
        proc.deleteBand (tb);
        feed (proc, 5, rng);
    }

    proc.uiState.setProperty ("pianoRoll", true, nullptr);
    proc.uiState.setProperty ("displayRange", 24, nullptr);
    proc.setParam (ID::mode, 2.0f);
    ed->setSize (1320, 780);
    feed (proc, 120, rng);
    save (*ed, "build/ui_large.png");

    ed.reset();
    proc.releaseResources();
    std::printf ("\n%s (%d failure%s)\n", failures == 0 ? "UI TEST PASSED" : "UI TEST FAILED", failures, failures == 1 ? "" : "s");
    return failures == 0 ? 0 : 1;
}
