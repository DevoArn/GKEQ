// GKEQ - 24-band dynamic EQ plug-in
// Copyright (C) 2026 Roman Gutekunst (GK)
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "../PluginProcessor.h"
#include "../dsp/FilterDesign.h"
#include "Spectrum.h"
#include "Theme.h"
#include <array>
#include <functional>

namespace meq::ui
{

/** The main graph: analysers, EQ curves and interactive band nodes. */
class EQDisplay : public juce::Component, private juce::Timer
{
public:
    explicit EQDisplay (ModernEQProcessor& p);
    ~EQDisplay() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    bool keyPressed (const juce::KeyPress&) override;

    // Selection
    const std::vector<int>& getSelection() const noexcept { return selection; }
    int  primarySelection() const noexcept { return selection.empty() ? -1 : selection.front(); }
    void setSelection (std::vector<int> s);
    void selectBand (int b) { setSelection (b >= 0 ? std::vector<int> { b } : std::vector<int> {}); }

    std::function<void()> onSelectionChanged;
    std::function<void()> onEdit;                       // user edit finished (undo commit)
    std::function<void (juce::Graphics&)> overlayPainter; // EQ match curves

    /** Floating panel positioned by the display (follows the selected band). */
    void setBandPanel (juce::Component* panel) { bandPanel = panel; layoutBandPanel(); }
    void layoutBandPanel();

    // Coordinate mapping
    juce::Rectangle<float> plotArea() const noexcept { return plot; }
    float  freqToX (double f) const noexcept;
    double xToFreq (float x) const noexcept;
    float  dbToY (double db) const noexcept;
    double yToDb (float y) const noexcept;
    float  specToY (float db) const noexcept;
    float  specFromY (float y) const noexcept;
    double displayRange() const;

    /** Band response on the analyser grid (dB), as currently displayed. */
    const float* bandCurve (int b) const noexcept { return cache[(size_t) b].db.data(); }

private:
    void timerCallback() override;
    void updateAnalyzers (double dt);
    void updateCurves();
    void computeBandCurve (const BandParams& p, double effGain, float* out) const;

    // Painting helpers
    void paintGrid (juce::Graphics&);
    void paintPianoRoll (juce::Graphics&);
    void paintSpectrum (juce::Graphics&, const float* lv, juce::Colour c, bool fill, float alpha);
    void paintCollisions (juce::Graphics&, const float* a, const float* b);
    void paintBandCurve (juce::Graphics&, int b, bool emphasised);
    void paintTotals (juce::Graphics&);
    void paintNodes (juce::Graphics&);
    void paintReadout (juce::Graphics&);
    juce::Path curvePath (const float* db) const;
    void paintBackground (juce::Graphics&);

    juce::Image bgCache;
    juce::String bgKey;

    // Interaction helpers
    juce::Point<float> nodePosition (int b) const;
    juce::Point<float> rangeHandlePosition (int b) const;
    juce::Point<float> thresholdHandlePosition (int b) const;
    int  hitTestThreshold (juce::Point<float> p) const;
    void paintThreshold (juce::Graphics&, int b, float dim);
    int  hitTestBand (juce::Point<float> p) const;
    int  hitTestRange (juce::Point<float> p) const;
    void findGrabPeak (juce::Point<float> p);
    void beginBandGestures();
    void endBandGestures();
    void setParamValue (int b, const char* id, float v);
    void showBandMenu (int b);
    void showBackgroundMenu (juce::Point<float> pos);
    void applyToTargets (int clicked, const std::function<void (int)>& fn);
    void createBandAt (juce::Point<float> pos);
    void deleteSelected();
    void notifyEdit();
    bool sketchMode() const;
    void addSketchPoint (juce::Point<float> p);
    void finishSketch();
    bool isUsed (int b) const;
    double gainScale() const;

    ModernEQProcessor& proc;
    juce::Component* bandPanel = nullptr;
    juce::Rectangle<float> plot;

    SpectrumAnalyzer pre, post, sc, inst;
    std::vector<float> instBuf;
    double lastTime = 0.0;

    struct BandCache
    {
        bool   valid = false;
        double key[10] {};
        std::array<float, kSpecPoints> db {};
        std::array<float, kSpecPoints> dbRange {};   // response at gain + range (dynamic bands)
    };
    std::array<BandCache, kNumBands> cache;
    std::array<std::array<float, kSpecPoints>, 5> totals {};
    std::array<bool, 5> placementInUse {};
    std::array<double, kSpecPoints> pointW {};      // rad/sample per point at the current rate
    std::array<dsp::Biquad, 1> dummy {};
    double curveRate = 0.0;
    BandParams bands[kNumBands];

    // Interaction state
    enum class Drag { None, Band, Range, Threshold, Rubber, Sketch };
    Drag drag = Drag::None;
    int  dragBand = -1;
    juce::Point<float> dragStart, mousePos;
    bool mouseInside = false;
    juce::Rectangle<float> rubber;
    std::vector<int> rubberBase;
    struct StartVals { double f = 1000, g = 0, q = 1, r = 0, t = -30; };
    std::array<StartVals, kNumBands> startVals;
    std::vector<int> gestureBands;
    int hoverBand = -1;
    int hoverRange = -1;
    int hoverThresh = -1;
    int grabPoint = -1;     // analyser point index of a grabbable peak
    std::vector<int> selection;
    int wheelCommitCountdown = 0;

    // EQ Sketch
    std::array<float, kSpecPoints> sketch {};
    int   sketchLastIdx = -1;
    float sketchLastDb = 0.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EQDisplay)
};

} // namespace meq::ui
