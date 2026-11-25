// GKEQ - 24-band dynamic EQ plug-in
// Copyright (C) 2026 Roman Gutekunst (GK)
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "../PluginProcessor.h"
#include "Theme.h"

namespace meq::ui
{

/** Rotary knob with a caption above and an editable value below. */
class Knob : public juce::Component
{
public:
    explicit Knob (const juce::String& caption);
    void resized() override;
    void paint (juce::Graphics&) override;

    void attach (juce::RangedAudioParameter* p);
    void setAccent (juce::Colour c);

    juce::Slider slider;
    std::function<void()> onEdit;

private:
    juce::String caption;
    std::unique_ptr<juce::SliderParameterAttachment> attachment;
};

/** Compact knob for the bottom bar: knob on the left, caption + value on the right. */
class MiniKnob : public juce::Component
{
public:
    explicit MiniKnob (const juce::String& caption);
    void resized() override;
    void paint (juce::Graphics&) override;
    void attach (juce::RangedAudioParameter* p);

    juce::Slider slider;
    std::function<void()> onEdit;

private:
    juce::String caption;
    juce::RangedAudioParameter* param = nullptr;
    std::unique_ptr<juce::SliderParameterAttachment> attachment;
};

class BandPanel : public juce::Component, private juce::Timer
{
public:
    explicit BandPanel (ModernEQProcessor& p);
    ~BandPanel() override;

    void setBand (int b);
    int  getBand() const noexcept { return band; }

    void paint (juce::Graphics&) override;
    void resized() override;

    std::function<void()> onEdit;
    std::function<void (int)> onSelectBand;
    std::function<void()> onClose;

private:
    void timerCallback() override;
    void refreshEnablement();
    void stepBand (int dir);
    void edited();

    ModernEQProcessor& proc;
    int band = -1;

    juce::TextButton prevBtn { "<" }, nextBtn { ">" }, enableBtn { "ON" }, soloBtn { "SOLO" },
                     deleteBtn { "DEL" }, dynBtn { "DYNAMIC" }, scBtn { "EXT SC" }, closeBtn { "X" };
    juce::ComboBox shapeBox, slopeBox, placeBox;
    Knob freq { "FREQ" }, gain { "GAIN" }, q { "Q" }, range { "RANGE" }, thresh { "THRESHOLD" },
         attack { "ATTACK" }, release { "RELEASE" };

    std::unique_ptr<juce::ComboBoxParameterAttachment> shapeAtt, slopeAtt, placeAtt;
    std::unique_ptr<juce::ButtonParameterAttachment> enableAtt, soloAtt, dynAtt, scAtt;

    int lastType = -1, lastSlope = -1;
    bool lastDyn = false;
};

} // namespace meq::ui
