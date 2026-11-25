// GKEQ - 24-band dynamic EQ plug-in
// Copyright (C) 2026 Roman Gutekunst (GK)
// SPDX-License-Identifier: GPL-3.0-or-later

#include "EQMatch.h"
#include "EQDisplay.h"
#include "CurveFit.h"
#include "../InstanceRegistry.h"
#include "../dsp/FilterDesign.h"

namespace meq::ui
{

namespace
{
    constexpr double kPointsPerOct = (kSpecPoints - 1) / 11.550746785383243;   // log2(30000/10)
}

EQMatchPanel::EQMatchPanel (ModernEQProcessor& p, EQDisplay& d) : proc (p), display (d)
{
    for (auto* c : { (juce::Component*) &sourceBox, (juce::Component*) &smoothBox, (juce::Component*) &learnBtn,
                     (juce::Component*) &matchBtn, (juce::Component*) &resetBtn, (juce::Component*) &closeBtn,
                     (juce::Component*) &bandsSlider, (juce::Component*) &amountSlider })
        addAndMakeVisible (c);

    smoothBox.addItem ("1 oct smoothing", 1);
    smoothBox.addItem ("1/3 oct smoothing", 2);
    smoothBox.addItem ("1/6 oct smoothing", 3);
    smoothBox.setSelectedId (2, juce::dontSendNotification);
    smoothBox.onChange = [this] { computeDifference(); };

    learnBtn.setClickingTogglesState (true);
    learnBtn.setColour (juce::TextButton::buttonOnColourId, Colours::collision.withAlpha (0.45f));
    learnBtn.onClick = [this] { startLearning (learnBtn.getToggleState()); };
    matchBtn.onClick = [this] { doMatch(); };
    resetBtn.onClick = [this]
    {
        refAna.clearAverage();
        curAna.clearAverage();
        haveData = false;
        status = "Cleared.";
        repaint();
    };
    closeBtn.onClick = [this] { if (onClose) onClose(); };

    for (auto* s : { &bandsSlider, &amountSlider })
    {
        s->setSliderStyle (juce::Slider::LinearHorizontal);
        s->setTextBoxStyle (juce::Slider::TextBoxRight, false, 64, 20);
        s->setColour (juce::Slider::rotarySliderFillColourId, Colours::accent);
    }
    bandsSlider.setRange (1.0, 24.0, 1.0);
    bandsSlider.setValue (8.0);
    bandsSlider.textFromValueFunction = [] (double v) { return juce::String ((int) v) + " bands"; };
    bandsSlider.updateText();
    amountSlider.setRange (0.0, 150.0, 1.0);
    amountSlider.setValue (100.0);
    amountSlider.textFromValueFunction = [] (double v) { return juce::String ((int) v) + " %"; };
    amountSlider.updateText();

    sourceBox.setTooltip ("Reference signal: the external side-chain input or another GKEQ instance");
    learnBtn.setTooltip ("Play the material: the average spectra of this track and the reference are learned");
    matchBtn.setTooltip ("Create bands that match this track to the reference (replaces bands from the last match)");

    refreshSources();
    buf.resize ((size_t) SnapshotBuffer::kSize);
    refAna.setAveraging (true);
    curAna.setAveraging (true);
    startTimerHz (15);
}

EQMatchPanel::~EQMatchPanel()
{
    stopTimer();
}

void EQMatchPanel::refreshSources()
{
    const int prevId = sourceBox.getSelectedItemIndex() >= 0 && sourceBox.getSelectedItemIndex() < (int) sourceIds.size()
                           ? sourceIds[(size_t) sourceBox.getSelectedItemIndex()] : 0;
    sourceBox.clear (juce::dontSendNotification);
    sourceIds.clear();
    sourceIds.push_back (0);
    sourceBox.addItem ("Side-chain input", 1);
    for (const auto& info : InstanceRegistry::get().list())
    {
        if (info.id == proc.getInstanceId()) continue;
        sourceIds.push_back (info.id);
        sourceBox.addItem (info.name, (int) sourceIds.size());
    }
    int sel = 0;
    for (int i = 0; i < (int) sourceIds.size(); ++i)
        if (sourceIds[(size_t) i] == prevId) sel = i;
    sourceBox.setSelectedItemIndex (sel, juce::dontSendNotification);
}

void EQMatchPanel::startLearning (bool on)
{
    learning = on;
    if (on)
    {
        const double fs = proc.getCurrentSampleRate();
        refAna.configure (3, fs);
        curAna.configure (3, fs);
        status = "Learning...";
    }
    else
    {
        computeDifference();
        status = haveData ? "Learned. Press Match to create the bands." : "Not enough signal learned yet.";
    }
    repaint();
}

void EQMatchPanel::timerCallback()
{
    static int refreshCounter = 0;
    if (++refreshCounter % 30 == 0 && ! sourceBox.isPopupActive() && ! learning)
        refreshSources();

    if (! learning) return;

    const double fs = proc.getCurrentSampleRate();
    const double dt = 1.0 / 15.0;
    curAna.process (proc.preTap, dt, 2, 0.0);

    const int idx = sourceBox.getSelectedItemIndex();
    const int sid = idx >= 0 && idx < (int) sourceIds.size() ? sourceIds[(size_t) idx] : 0;
    if (sid == 0)
    {
        if (proc.isSidechainActive())
            refAna.process (proc.scTap, dt, 2, 0.0);
    }
    else
    {
        double ifs = fs;
        if (InstanceRegistry::get().readSpectrumSource (sid, buf.data(), refAna.fftSize(), ifs))
            refAna.processSamples (buf.data(), dt, 2, 0.0);
    }

    const int frames = juce::jmin (refAna.averageFrames(), curAna.averageFrames());
    status = "Learning... " + juce::String (frames / 15.0, 1) + " s";
    if (sid == 0 && ! proc.isSidechainActive())
        status << "  (no side-chain signal)";
    computeDifference();
    repaint();
}

void EQMatchPanel::computeDifference()
{
    if (juce::jmin (refAna.averageFrames(), curAna.averageFrames()) < 8)
    {
        haveData = false;
        return;
    }

    const auto refRaw = refAna.averageDb();
    const auto curRaw = curAna.averageDb();
    const double widthOct = smoothBox.getSelectedId() == 1 ? 1.0 : smoothBox.getSelectedId() == 3 ? 1.0 / 6.0 : 1.0 / 3.0;
    const int half = juce::jmax (1, (int) std::round (widthOct * kPointsPerOct * 0.5));

    auto smoothPow = [half] (const std::vector<float>& in)
    {
        std::vector<float> out (in.size(), -150.0f);
        for (int i = 0; i < kSpecPoints; ++i)
        {
            double s = 0.0; int n = 0;
            for (int k = i - half; k <= i + half; ++k)
            {
                if (k < 0 || k >= kSpecPoints || in[(size_t) k] < -140.0f) continue;
                s += std::pow (10.0, in[(size_t) k] / 10.0);
                ++n;
            }
            out[(size_t) i] = n > 0 ? (float) (10.0 * std::log10 (s / n + 1.0e-30)) : -150.0f;
        }
        return out;
    };

    refDb = smoothPow (refRaw);
    curDb = smoothPow (curRaw);
    diffDb.assign ((size_t) kSpecPoints, 0.0f);

    const double fs = proc.getCurrentSampleRate();
    const double fmax = std::min (18000.0, 0.45 * fs);
    double sum = 0.0, wsum = 0.0;
    for (int i = 0; i < kSpecPoints; ++i)
    {
        const double f = pointFreq (i);
        if (f < 25.0 || f > fmax || refDb[(size_t) i] < -120.0f || curDb[(size_t) i] < -120.0f) continue;
        const double d = refDb[(size_t) i] - curDb[(size_t) i];
        diffDb[(size_t) i] = (float) d;
        const double w = (f > 100.0 && f < 8000.0) ? 1.0 : 0.5;
        sum += d * w;
        wsum += w;
    }
    if (wsum <= 0.0) { haveData = false; return; }

    const float mean = (float) (sum / wsum);
    for (int i = 0; i < kSpecPoints; ++i)
    {
        const double f = pointFreq (i);
        if (f < 25.0 || f > fmax || refDb[(size_t) i] < -120.0f || curDb[(size_t) i] < -120.0f) { diffDb[(size_t) i] = 0.0f; continue; }
        diffDb[(size_t) i] = juce::jlimit (-18.0f, 18.0f, diffDb[(size_t) i] - mean);
    }
    haveData = true;
}

void EQMatchPanel::doMatch()
{
    computeDifference();
    if (! haveData)
    {
        status = "Nothing learned yet: press Learn while both signals play.";
        repaint();
        return;
    }

    for (int b : matchedBands)
        if (proc.refs.readBand (b).used) proc.deleteBand (b);
    matchedBands.clear();

    const double fs = proc.getCurrentSampleRate();
    const double amount = amountSlider.getValue() / 100.0;
    const double fmax = std::min (18000.0, 0.45 * fs);
    int i0 = 0, i1 = kSpecPoints - 1;
    while (i0 < kSpecPoints - 1 && pointFreq (i0) < 25.0) ++i0;
    while (i1 > i0 && pointFreq (i1) > fmax) --i1;

    // Existing (non-matched) EQ curve, so the match accounts for it.
    std::vector<double> target ((size_t) kSpecPoints, 0.0);
    std::vector<dsp::AnalogFilter> existing;
    const double scale = proc.getParam (ID::gainScale) / 100.0;
    for (int b = 0; b < kNumBands; ++b)
    {
        const auto p = proc.refs.readBand (b);
        if (! p.isActive() || ! (p.placement == Placement::Stereo || p.placement == Placement::Mid)) continue;
        existing.push_back (dsp::designAnalog (dsp::DesignParams::fromBand (p, effectiveGainDb (p, scale, 0.0), true), fs));
    }
    for (int i = i0; i <= i1; ++i)
    {
        const double w = dsp::kTwoPi * pointFreq (i) / fs;
        double ex = 0.0;
        for (auto& a : existing) ex += dsp::gainToDb (a.mag (w));
        target[(size_t) i] = diffDb[(size_t) i] * amount - ex;
    }

    int freeBands = 0;
    for (int b = 0; b < kNumBands; ++b) if (! proc.refs.readBand (b).used) ++freeBands;
    const int maxBands = juce::jmin ((int) bandsSlider.getValue(), freeBands);

    double rms = 0.0;
    const auto fits = fitCurve (target, i0, i1, maxBands, fs, rms);

    int created = 0;
    for (const auto& fit : fits)
    {
        const int b = proc.createBand (fit.type, fit.freq, juce::jlimit (-18.0, 18.0, fit.gain), fit.q, 1, Placement::Stereo);
        if (b >= 0) { matchedBands.push_back (b); ++created; }
    }

    status = "Matched with " + juce::String (created) + " bands (residual " + juce::String (rms, 2) + " dB RMS).";
    if (onEdit) onEdit();
    repaint();
}

void EQMatchPanel::paintOverlay (juce::Graphics& g)
{
    if (! haveData) return;
    const double tilt = (double) proc.uiState.getProperty ("anaTilt", 4.5);

    auto specPath = [&] (const std::vector<float>& db)
    {
        juce::Path p;
        bool started = false;
        for (int i = 0; i < kSpecPoints; ++i)
        {
            if (db[(size_t) i] < -140.0f) { started = false; continue; }
            const float v = db[(size_t) i] + (float) (tilt * std::log2 (pointFreq (i) / 1000.0));
            const float x = display.freqToX (pointFreq (i)), y = display.specToY (v);
            if (! started) { p.startNewSubPath (x, y); started = true; }
            else p.lineTo (x, y);
        }
        return p;
    };

    g.setColour (Colours::scSpec.withAlpha (0.9f));
    g.strokePath (specPath (refDb), juce::PathStrokeType (1.5f));
    g.setColour (Colours::accent.withAlpha (0.9f));
    g.strokePath (specPath (curDb), juce::PathStrokeType (1.5f));

    juce::Path diff;
    for (int i = 0; i < kSpecPoints; ++i)
    {
        const float x = display.freqToX (pointFreq (i)), y = display.dbToY (diffDb[(size_t) i]);
        if (i == 0) diff.startNewSubPath (x, y); else diff.lineTo (x, y);
    }
    juce::Path dashed;
    const float dashes[] = { 5.0f, 4.0f };
    juce::PathStrokeType (1.6f).createDashedStroke (dashed, diff, dashes, 2);
    g.setColour (Colours::warn.withAlpha (0.9f));
    g.fillPath (dashed);
}

void EQMatchPanel::paint (juce::Graphics& g)
{
    const auto r = getLocalBounds().toFloat().reduced (0.5f);
    g.setColour (Colours::panel);
    g.fillRoundedRectangle (r, 6.0f);
    g.setColour (Colours::panelEdge);
    g.drawRoundedRectangle (r, 6.0f, 1.0f);

    g.setFont (LookAndFeel::font (13.0f, true));
    g.setColour (Colours::text);
    g.drawText ("EQ MATCH", juce::Rectangle<int> (10, 0, 80, getHeight()), juce::Justification::centredLeft);

    auto legend = juce::Rectangle<int> (getWidth() - 270, 4, 236, getHeight() / 2 - 2);
    g.setFont (LookAndFeel::font (11.0f));
    g.setColour (Colours::scSpec);   g.drawText ("Reference", legend.removeFromLeft (70), juce::Justification::centredLeft);
    g.setColour (Colours::accent);   g.drawText ("This track", legend.removeFromLeft (70), juce::Justification::centredLeft);
    g.setColour (Colours::warn);     g.drawText ("Difference", legend.removeFromLeft (80), juce::Justification::centredLeft);
    g.setColour (Colours::textDim);
    g.drawText (status, juce::Rectangle<int> (getWidth() - 270, getHeight() / 2, 236, getHeight() / 2 - 4), juce::Justification::centredLeft);
}

void EQMatchPanel::resized()
{
    auto r = getLocalBounds().reduced (6, 7);
    r.removeFromLeft (84);
    closeBtn.setBounds (getWidth() - 28, 8, 20, 20);
    r.removeFromRight (276);
    sourceBox.setBounds (r.removeFromLeft (170));
    r.removeFromLeft (6);
    learnBtn.setBounds (r.removeFromLeft (64));
    r.removeFromLeft (6);
    smoothBox.setBounds (r.removeFromLeft (130));
    r.removeFromLeft (8);
    const int sw = juce::jmax (120, (r.getWidth() - 140) / 2);
    bandsSlider.setBounds (r.removeFromLeft (sw));
    r.removeFromLeft (4);
    amountSlider.setBounds (r.removeFromLeft (sw));
    r.removeFromLeft (6);
    matchBtn.setBounds (r.removeFromLeft (64));
    r.removeFromLeft (4);
    resetBtn.setBounds (r.removeFromLeft (56));
}

} // namespace meq::ui
