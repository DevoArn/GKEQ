#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_opengl/juce_opengl.h>
#include "PluginProcessor.h"
#include "ui/Theme.h"
#include "ui/EQDisplay.h"
#include "ui/BandPanel.h"
#include "ui/Bars.h"
#include "ui/EQMatch.h"

namespace meq
{

class ModernEQEditor : public juce::AudioProcessorEditor
{
public:
    explicit ModernEQEditor (ModernEQProcessor& p);
    ~ModernEQEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;
    void mouseUp (const juce::MouseEvent&) override;

    ui::EQDisplay& getDisplay() noexcept { return display; }

private:
    void commit();
    void setGpu (bool on);
    void toggleMatch();

    ModernEQProcessor& proc;
    ui::LookAndFeel lnf;
    juce::OpenGLContext gl;

    ui::TopBar top;
    ui::EQDisplay display;
    ui::BandPanel panel;
    ui::BottomBar bottom;
    std::unique_ptr<ui::EQMatchPanel> match;
    std::unique_ptr<juce::TooltipWindow> tooltip;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ModernEQEditor)
};

} // namespace meq
