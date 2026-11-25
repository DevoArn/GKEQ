// GKEQ - 24-band dynamic EQ plug-in
// Copyright (C) 2026 Roman Gutekunst (GK)
// SPDX-License-Identifier: GPL-3.0-or-later

#include "PluginEditor.h"

namespace meq
{

ModernEQEditor::ModernEQEditor (ModernEQProcessor& p)
    : AudioProcessorEditor (p), proc (p), top (p), display (p), panel (p), bottom (p)
{
    setLookAndFeel (&lnf);

    addAndMakeVisible (top);
    addAndMakeVisible (display);
    addAndMakeVisible (bottom);
    display.addChildComponent (panel);
    display.setBandPanel (&panel);

    display.onSelectionChanged = [this]
    {
        panel.setBand (display.primarySelection());
        display.layoutBandPanel();
    };
    display.onEdit = [this] { commit(); };

    panel.onEdit = [this] { commit(); };
    panel.onSelectBand = [this] (int b) { display.selectBand (b); };
    panel.onClose = [this] { display.setSelection ({}); };

    top.onEdit = [this] { commit(); };
    top.onToggleMatch = [this] { toggleMatch(); };
    top.onGpuToggled = [this] { setGpu ((bool) proc.uiState.getProperty ("gpu", true)); };
    top.onResizeRequest = [this] (int w, int h) { setSize (w, h); };
    top.onStateLoaded = [this] { display.setSelection ({}); };

    bottom.onEdit = [this] { commit(); };

    addMouseListener (this, true);
    tooltip = std::make_unique<juce::TooltipWindow> (this, 700);

    setResizable (true, true);
    setResizeLimits (880, 500, 2560, 1600);
    setSize (juce::jlimit (880, 2560, (int) proc.uiState.getProperty ("width", 1040)),
             juce::jlimit (500, 1600, (int) proc.uiState.getProperty ("height", 620)));
    setWantsKeyboardFocus (true);

    proc.commitUndoPoint();   // establishes the undo baseline
    setGpu ((bool) proc.uiState.getProperty ("gpu", true));
}

ModernEQEditor::~ModernEQEditor()
{
    removeMouseListener (this);
    proc.clearAllSolos();
    display.overlayPainter = nullptr;
    match.reset();
    if (gl.isAttached()) gl.detach();
    tooltip.reset();
    setLookAndFeel (nullptr);
}

void ModernEQEditor::setGpu (bool on)
{
    if (on && ! gl.isAttached())
    {
        gl.setComponentPaintingEnabled (true);
        gl.setContinuousRepainting (false);
        gl.attachTo (*this);
    }
    else if (! on && gl.isAttached())
    {
        gl.detach();
    }
}

void ModernEQEditor::commit()
{
    proc.commitUndoPoint();
}

void ModernEQEditor::toggleMatch()
{
    if (match != nullptr)
    {
        display.overlayPainter = nullptr;
        removeChildComponent (match.get());
        match.reset();
        top.setMatchActive (false);
        return;
    }

    match = std::make_unique<ui::EQMatchPanel> (proc, display);
    addAndMakeVisible (*match);
    match->onEdit = [this] { commit(); };
    juce::Component::SafePointer<ModernEQEditor> safe (this);
    match->onClose = [safe]
    {
        juce::MessageManager::callAsync ([safe] { if (safe != nullptr && safe->match != nullptr) safe->toggleMatch(); });
    };
    display.overlayPainter = [this] (juce::Graphics& g) { if (match != nullptr) match->paintOverlay (g); };
    top.setMatchActive (true);
    resized();
}

void ModernEQEditor::paint (juce::Graphics& g)
{
    g.fillAll (ui::Colours::bg0);
}

void ModernEQEditor::resized()
{
    auto r = getLocalBounds();
    top.setBounds (r.removeFromTop (36));
    bottom.setBounds (r.removeFromBottom (44));
    display.setBounds (r);
    if (match != nullptr)
        match->setBounds (display.getX() + 8, display.getY() + 30, display.getWidth() - 16, 40);

    proc.uiState.setProperty ("width", getWidth(), nullptr);
    proc.uiState.setProperty ("height", getHeight(), nullptr);
}

bool ModernEQEditor::keyPressed (const juce::KeyPress& key)
{
    const auto mods = key.getModifiers();
    const int code = key.getKeyCode();
    if (mods.isCtrlDown() && (code == 'Z' || code == 'z'))
    {
        if (mods.isShiftDown()) proc.redo();
        else                    proc.undo();
        return true;
    }
    if (mods.isCtrlDown() && (code == 'Y' || code == 'y'))
    {
        proc.redo();
        return true;
    }
    return false;
}

void ModernEQEditor::mouseUp (const juce::MouseEvent&)
{
    commit();
}

} // namespace meq
