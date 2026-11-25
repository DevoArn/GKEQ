// GKEQ - 24-band dynamic EQ plug-in
// Copyright (C) 2026 Roman Gutekunst (GK)
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "../PluginProcessor.h"
#include "BandPanel.h"
#include "Theme.h"

namespace meq::ui
{

class TopBar : public juce::Component, private juce::Timer
{
public:
    explicit TopBar (ModernEQProcessor& p);
    ~TopBar() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    void setMatchActive (bool on) { matchBtn.setToggleState (on, juce::dontSendNotification); }

    std::function<void()> onEdit;
    std::function<void()> onToggleMatch;
    std::function<void()> onGpuToggled;
    std::function<void (int, int)> onResizeRequest;
    std::function<void()> onStateLoaded;

private:
    struct PresetRef
    {
        bool factory = true;
        int index = 0;
        juce::File file;
        juce::String name;
    };

    void timerCallback() override;
    std::vector<PresetRef> allPresets() const;
    void loadPreset (const PresetRef& r);
    void showPresetMenu();
    void stepPreset (int dir);
    void savePreset();
    void showSettingsMenu();
    void showHelp();

    ModernEQProcessor& proc;
    juce::TextButton undoBtn { "Undo" }, redoBtn { "Redo" }, aBtn { "A" }, bBtn { "B" }, copyBtn { "Copy A>B" },
                     prevPreset { "<" }, nextPreset { ">" }, presetBtn { "Default" }, saveBtn { "Save" },
                     matchBtn { "EQ Match" }, sketchBtn { "Sketch" }, settingsBtn { "Settings" }, helpBtn { "?" };
    std::unique_ptr<juce::AlertWindow> saveDialog;
};

class BottomBar : public juce::Component, private juce::Timer
{
public:
    explicit BottomBar (ModernEQProcessor& p);
    ~BottomBar() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    std::function<void()> onEdit;

private:
    void timerCallback() override;
    void showAnalyzerMenu();

    ModernEQProcessor& proc;
    juce::ComboBox modeBox, resBox, charBox;
    std::unique_ptr<juce::ComboBoxParameterAttachment> modeAtt, resAtt, charAtt;
    juce::TextButton analyzerBtn { "Analyzer" }, autoGainBtn { "Auto Gain" }, phaseBtn { "Phase Inv" }, bypassBtn { "Bypass" };
    std::unique_ptr<juce::ButtonParameterAttachment> autoAtt, phaseAtt, bypassAtt;
    MiniKnob scaleKnob { "GAIN SCALE" }, panKnob { "PAN" }, outKnob { "OUTPUT" };
    juce::Rectangle<int> infoArea;
};

} // namespace meq::ui
