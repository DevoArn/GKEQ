#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "../PluginProcessor.h"
#include "Spectrum.h"
#include "Theme.h"

namespace meq::ui
{

class EQDisplay;

/** EQ Match: learns the average spectrum of this track (pre-EQ) and of a
    reference (side-chain or another GKEQ instance) and fits bands
    that make the track's tonal balance follow the reference. */
class EQMatchPanel : public juce::Component, private juce::Timer
{
public:
    EQMatchPanel (ModernEQProcessor& p, EQDisplay& d);
    ~EQMatchPanel() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    /** Draws the learned curves on the display. */
    void paintOverlay (juce::Graphics& g);

    std::function<void()> onEdit;
    std::function<void()> onClose;

private:
    void timerCallback() override;
    void refreshSources();
    void startLearning (bool on);
    void computeDifference();
    void doMatch();

    ModernEQProcessor& proc;
    EQDisplay& display;

    juce::ComboBox sourceBox, smoothBox;
    juce::TextButton learnBtn { "Learn" }, matchBtn { "Match" }, resetBtn { "Reset" }, closeBtn { "X" };
    juce::Slider bandsSlider, amountSlider;

    SpectrumAnalyzer refAna, curAna;
    std::vector<float> buf;
    std::vector<float> refDb, curDb, diffDb;
    bool learning = false;
    bool haveData = false;
    std::vector<int> sourceIds;    // 0 = side-chain, otherwise instance id
    std::vector<int> matchedBands;
    juce::String status { "Choose a reference, play both signals and press Learn." };
};

} // namespace meq::ui
