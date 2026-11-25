// GKEQ - 24-band dynamic EQ plug-in
// Copyright (C) 2026 Roman Gutekunst (GK)
// SPDX-License-Identifier: GPL-3.0-or-later

#include "Bars.h"
#include "../Presets.h"
#include "../InstanceRegistry.h"

namespace meq::ui
{

// ===========================================================================
// TopBar
// ===========================================================================
TopBar::TopBar (ModernEQProcessor& p) : proc (p)
{
    for (auto* b : { &undoBtn, &redoBtn, &aBtn, &bBtn, &copyBtn, &prevPreset, &nextPreset, &presetBtn,
                     &saveBtn, &matchBtn, &sketchBtn, &settingsBtn, &helpBtn })
        addAndMakeVisible (b);
    sketchBtn.setTooltip ("EQ Sketch: draw a curve on the display and matching bands are created");
    sketchBtn.setColour (juce::TextButton::buttonOnColourId, Colours::warn.withAlpha (0.4f));
    sketchBtn.onClick = [this]
    {
        auto& ui = proc.uiState;
        ui.setProperty ("sketch", ! (bool) ui.getProperty ("sketch", false), nullptr);
    };

    undoBtn.setTooltip ("Undo (Ctrl+Z)");
    redoBtn.setTooltip ("Redo (Ctrl+Y / Ctrl+Shift+Z)");
    aBtn.setTooltip ("A/B comparison: setting A");
    bBtn.setTooltip ("A/B comparison: setting B");
    copyBtn.setTooltip ("Copy the active setting to the other slot");
    presetBtn.setTooltip ("Presets");
    saveBtn.setTooltip ("Save the current setting as a user preset");
    matchBtn.setTooltip ("EQ Match: match this track's spectrum to a reference (side-chain or another instance)");
    matchBtn.setClickingTogglesState (false);

    undoBtn.onClick = [this] { proc.undo(); if (onStateLoaded) onStateLoaded(); };
    redoBtn.onClick = [this] { proc.redo(); if (onStateLoaded) onStateLoaded(); };
    aBtn.onClick = [this] { if (proc.getABSlot() != 0) { proc.switchAB(); if (onStateLoaded) onStateLoaded(); } };
    bBtn.onClick = [this] { if (proc.getABSlot() != 1) { proc.switchAB(); if (onStateLoaded) onStateLoaded(); } };
    copyBtn.onClick = [this] { proc.copyToOtherSlot(); };
    presetBtn.onClick = [this] { showPresetMenu(); };
    prevPreset.onClick = [this] { stepPreset (-1); };
    nextPreset.onClick = [this] { stepPreset (1); };
    saveBtn.onClick = [this] { savePreset(); };
    matchBtn.onClick = [this] { if (onToggleMatch) onToggleMatch(); };
    settingsBtn.onClick = [this] { showSettingsMenu(); };
    helpBtn.onClick = [this] { showHelp(); };

    startTimerHz (8);
}

TopBar::~TopBar()
{
    stopTimer();
}

void TopBar::timerCallback()
{
    undoBtn.setEnabled (proc.canUndo());
    redoBtn.setEnabled (proc.canRedo());
    aBtn.setToggleState (proc.getABSlot() == 0, juce::dontSendNotification);
    bBtn.setToggleState (proc.getABSlot() == 1, juce::dontSendNotification);
    copyBtn.setButtonText (proc.getABSlot() == 0 ? "Copy A>B" : "Copy B>A");
    sketchBtn.setToggleState ((bool) proc.uiState.getProperty ("sketch", false), juce::dontSendNotification);
    if (presetBtn.getButtonText() != proc.currentPresetName)
        presetBtn.setButtonText (proc.currentPresetName);
}

void TopBar::paint (juce::Graphics& g)
{
    g.fillAll (Colours::bg2);
    g.setColour (Colours::panelEdge);
    g.drawHorizontalLine (getHeight() - 1, 0.0f, (float) getWidth());

    auto r = getLocalBounds().reduced (12, 0);
    g.setFont (LookAndFeel::font (17.0f, true));
    g.setColour (Colours::text);
    g.drawText ("GK", r.removeFromLeft (g.getCurrentFont().getStringWidth ("GK") + 1), juce::Justification::centredLeft);
    g.setColour (Colours::accent);
    g.drawText ("EQ", r.removeFromLeft (30), juce::Justification::centredLeft);
}

void TopBar::resized()
{
    auto r = getLocalBounds().reduced (8, 5);
    r.removeFromLeft (110);

    undoBtn.setBounds (r.removeFromLeft (54));
    r.removeFromLeft (4);
    redoBtn.setBounds (r.removeFromLeft (54));
    r.removeFromLeft (16);
    aBtn.setBounds (r.removeFromLeft (28));
    r.removeFromLeft (2);
    bBtn.setBounds (r.removeFromLeft (28));
    r.removeFromLeft (4);
    copyBtn.setBounds (r.removeFromLeft (72));

    helpBtn.setBounds (r.removeFromRight (28));
    r.removeFromRight (4);
    settingsBtn.setBounds (r.removeFromRight (72));
    r.removeFromRight (4);
    sketchBtn.setBounds (r.removeFromRight (60));
    r.removeFromRight (4);
    matchBtn.setBounds (r.removeFromRight (80));
    r.removeFromRight (16);

    const int presetW = juce::jlimit (160, 320, r.getWidth() - 140);
    auto pr = r.withSizeKeepingCentre (presetW + 28 * 2 + 8 + 54, r.getHeight());
    prevPreset.setBounds (pr.removeFromLeft (28));
    pr.removeFromLeft (2);
    presetBtn.setBounds (pr.removeFromLeft (presetW));
    pr.removeFromLeft (2);
    nextPreset.setBounds (pr.removeFromLeft (28));
    pr.removeFromLeft (6);
    saveBtn.setBounds (pr.removeFromLeft (54));
}

std::vector<TopBar::PresetRef> TopBar::allPresets() const
{
    std::vector<PresetRef> v;
    const auto names = presets::factoryNames();
    for (int i = 0; i < names.size(); ++i)
        v.push_back ({ true, i, {}, names[i] });
    for (auto& f : presets::listUserPresets())
        v.push_back ({ false, 0, f, f.getFileNameWithoutExtension() });
    return v;
}

void TopBar::loadPreset (const PresetRef& r)
{
    juce::ValueTree state;
    if (r.factory) state = presets::makeFactory (r.index, proc.getDefaultParameterState());
    else           state = presets::loadUserPreset (r.file);
    if (! state.isValid()) return;
    proc.loadParameterState (state, true);
    proc.currentPresetName = r.name;
    presetBtn.setButtonText (r.name);
    if (onStateLoaded) onStateLoaded();
}

void TopBar::showPresetMenu()
{
    const auto list = allPresets();
    juce::PopupMenu m, factory, user;
    for (int i = 0; i < (int) list.size(); ++i)
    {
        if (list[(size_t) i].factory) factory.addItem (i + 1, list[(size_t) i].name, true, list[(size_t) i].name == proc.currentPresetName);
        else                          user.addItem (i + 1, list[(size_t) i].name, true, list[(size_t) i].name == proc.currentPresetName);
    }
    m.addSectionHeader ("Factory");
    for (juce::PopupMenu::MenuItemIterator it (factory); it.next();) m.addItem (it.getItem());
    m.addSeparator();
    m.addSectionHeader ("User");
    if (user.getNumItems() == 0) m.addItem (-1, "(no user presets yet)", false);
    for (juce::PopupMenu::MenuItemIterator it (user); it.next();) m.addItem (it.getItem());
    m.addSeparator();
    m.addItem (9000, "Save Preset...");
    m.addItem (9001, "Open Preset Folder");

    juce::Component::SafePointer<TopBar> safe (this);
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&presetBtn), [safe, list] (int r)
    {
        if (safe == nullptr || r == 0) return;
        if (r == 9000) { safe->savePreset(); return; }
        if (r == 9001) { presets::userFolder().startAsProcess(); return; }
        if (r >= 1 && r <= (int) list.size()) safe->loadPreset (list[(size_t) (r - 1)]);
    });
}

void TopBar::stepPreset (int dir)
{
    const auto list = allPresets();
    if (list.empty()) return;
    int cur = -1;
    for (int i = 0; i < (int) list.size(); ++i)
        if (list[(size_t) i].name == proc.currentPresetName) cur = i;
    const int n = (int) list.size();
    const int next = cur < 0 ? 0 : (cur + dir + n) % n;
    loadPreset (list[(size_t) next]);
}

void TopBar::savePreset()
{
    saveDialog = std::make_unique<juce::AlertWindow> ("Save Preset", "Preset name:", juce::MessageBoxIconType::NoIcon, this);
    saveDialog->setLookAndFeel (&getLookAndFeel());
    saveDialog->addTextEditor ("name", proc.currentPresetName == "Default" ? juce::String ("My Preset") : proc.currentPresetName);
    saveDialog->addButton ("Save", 1, juce::KeyPress (juce::KeyPress::returnKey));
    saveDialog->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));

    juce::Component::SafePointer<TopBar> safe (this);
    saveDialog->enterModalState (true, juce::ModalCallbackFunction::create ([safe] (int result)
    {
        if (safe == nullptr || safe->saveDialog == nullptr) return;
        const auto name = safe->saveDialog->getTextEditorContents ("name").trim();
        if (result == 1 && name.isNotEmpty())
        {
            if (presets::saveUserPreset (safe->proc.apvts.copyState(), name))
                safe->proc.currentPresetName = juce::File::createLegalFileName (name);
        }
        juce::MessageManager::callAsync ([safe] { if (safe != nullptr) safe->saveDialog.reset(); });
    }), false);
}

void TopBar::showSettingsMenu()
{
    juce::PopupMenu m, sizes;
    const bool gpu = (bool) proc.uiState.getProperty ("gpu", true);
    m.addItem (1, "GPU Accelerated Drawing (OpenGL)", true, gpu);
    m.addItem (2, "Piano Roll Display", true, (bool) proc.uiState.getProperty ("pianoRoll", false));
    sizes.addItem (10, "Small (880 x 520)");
    sizes.addItem (11, "Medium (1040 x 620)");
    sizes.addItem (12, "Large (1320 x 780)");
    sizes.addItem (13, "Extra Large (1640 x 960)");
    m.addSubMenu ("Window Size", sizes);
    m.addSeparator();
    m.addItem (20, "Latency: " + juce::String (proc.getActiveLatency()) + " samples", false);

    juce::Component::SafePointer<TopBar> safe (this);
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&settingsBtn), [safe, gpu] (int r)
    {
        if (safe == nullptr || r == 0) return;
        auto& ui = safe->proc.uiState;
        if (r == 1) { ui.setProperty ("gpu", ! gpu, nullptr); if (safe->onGpuToggled) safe->onGpuToggled(); }
        if (r == 2) ui.setProperty ("pianoRoll", ! (bool) ui.getProperty ("pianoRoll", false), nullptr);
        const int w[] = { 880, 1040, 1320, 1640 }, h[] = { 520, 620, 780, 960 };
        if (r >= 10 && r <= 13 && safe->onResizeRequest) safe->onResizeRequest (w[r - 10], h[r - 10]);
    });
}

void TopBar::showHelp()
{
    const juce::String text =
        "Display\n"
        "  Double-click empty area: add band (Bell; Low/High Cut near the edges)\n"
        "  Drag a node: frequency + gain (Q for cuts, notch, band pass)\n"
        "  Ctrl+drag: adjust Q      Shift+drag: fine adjustment\n"
        "  Mouse wheel on a node: Q      Ctrl+wheel: dynamic range\n"
        "  Double-click node: enable/bypass      Alt+click node: delete\n"
        "  Drag the small bar above/below a dynamic node: dynamic range\n"
        "  Drag the T marker of a dynamic band: threshold (the triangle is the\n"
        "    band's live level, orange when it is above the threshold)\n"
        "  Right-click: band menu (shape, slope, placement, solo, dynamics)\n"
        "  Drag on empty area: select multiple bands; then drag them together\n"
        "  Hover a spectrum peak and drag it: Spectrum Grab\n"
        "  Sketch button: draw a freehand curve, bands are created to match it\n"
        "\nKeyboard\n"
        "  Delete: remove selected bands     Ctrl+A: select all     Esc: deselect\n"
        "  Arrows: nudge frequency / gain (Shift = fine)\n"
        "  Ctrl+Z: undo      Ctrl+Y or Ctrl+Shift+Z: redo\n"
        "\nModes\n"
        "  Zero Latency: analog-matched minimum-phase filters, no latency\n"
        "  Natural Phase: oversampled, analog magnitude and phase, ~1.6 ms latency\n"
        "  Linear Phase: no phase shift; resolution sets latency and low-end precision\n";
    juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::NoIcon, "GKEQ - Quick Reference", text, "OK", this);
}

// ===========================================================================
// BottomBar
// ===========================================================================
BottomBar::BottomBar (ModernEQProcessor& p) : proc (p)
{
    modeBox.addItemList (modeNames(), 1);
    resBox.addItemList (lpResolutionNames(), 1);
    charBox.addItemList (characterNames(), 1);
    for (auto* c : { &modeBox, &resBox, &charBox })
    {
        addAndMakeVisible (c);
        c->onChange = [this]
        {
            juce::Component::SafePointer<BottomBar> safe (this);
            juce::MessageManager::callAsync ([safe] { if (safe != nullptr && safe->onEdit) safe->onEdit(); });
        };
    }
    modeAtt = std::make_unique<juce::ComboBoxParameterAttachment> (*proc.param (ID::mode), modeBox, nullptr);
    resAtt  = std::make_unique<juce::ComboBoxParameterAttachment> (*proc.param (ID::lpRes), resBox, nullptr);
    charAtt = std::make_unique<juce::ComboBoxParameterAttachment> (*proc.param (ID::character), charBox, nullptr);
    modeBox.setTooltip ("Processing mode");
    resBox.setTooltip ("Linear phase resolution: higher = more accurate low end, more latency");
    charBox.setTooltip ("Character: Clean, Subtle or Warm analog-style saturation");

    for (auto* b : { &analyzerBtn, &autoGainBtn, &phaseBtn, &bypassBtn })
        addAndMakeVisible (b);
    for (auto* b : { &autoGainBtn, &phaseBtn, &bypassBtn })
    {
        b->setClickingTogglesState (true);
        b->onClick = [this] { if (onEdit) onEdit(); };
    }
    autoAtt   = std::make_unique<juce::ButtonParameterAttachment> (*proc.param (ID::autoGain), autoGainBtn, nullptr);
    phaseAtt  = std::make_unique<juce::ButtonParameterAttachment> (*proc.param (ID::phaseInv), phaseBtn, nullptr);
    bypassAtt = std::make_unique<juce::ButtonParameterAttachment> (*proc.param (ID::bypass), bypassBtn, nullptr);
    bypassBtn.setColour (juce::TextButton::buttonOnColourId, Colours::warn.withAlpha (0.45f));
    autoGainBtn.setTooltip ("Auto Gain: compensates the loudness change of the EQ curve");
    analyzerBtn.setTooltip ("Spectrum analyser settings");
    analyzerBtn.onClick = [this] { showAnalyzerMenu(); };

    for (auto* k : { &scaleKnob, &panKnob, &outKnob })
    {
        addAndMakeVisible (k);
        k->onEdit = [this] { if (onEdit) onEdit(); };
    }
    scaleKnob.attach (proc.param (ID::gainScale));
    panKnob.attach (proc.param (ID::outPan));
    outKnob.attach (proc.param (ID::outGain));
    scaleKnob.slider.setTooltip ("Gain Scale: scales the gain of all bands");

    startTimerHz (10);
}

BottomBar::~BottomBar()
{
    stopTimer();
}

void BottomBar::timerCallback()
{
    resBox.setEnabled ((int) proc.getParam (ID::mode) == (int) ProcMode::LinearPhase);
    repaint (infoArea);
}

void BottomBar::paint (juce::Graphics& g)
{
    g.fillAll (Colours::bg2);
    g.setColour (Colours::panelEdge);
    g.drawHorizontalLine (0, 0.0f, (float) getWidth());

    g.setFont (LookAndFeel::font (11.5f));
    auto r = infoArea;
    const int lat = proc.getActiveLatency();
    const double fs = proc.getCurrentSampleRate();
    g.setColour (Colours::textDim);
    juce::String info = "Latency " + juce::String (lat);
    if (lat > 0 && fs > 0.0 && r.getWidth() > 150) info << " smp (" << juce::String (1000.0 * lat / fs, 1) << " ms)";
    else if (lat > 0 && fs > 0.0) info = "Lat. " + juce::String (1000.0 * lat / fs, 1) + " ms";
    g.drawText (info, r.removeFromTop (r.getHeight() / 2), juce::Justification::centredLeft);

    juce::String line2;
    if ((bool) proc.getParam (ID::autoGain))
        line2 << "Auto " << formatGain (proc.getAutoGainDb()) << "   ";
    line2 << "Peak " << (proc.getOutputPeakDb() < -99.0f ? juce::String ("-inf") : juce::String (proc.getOutputPeakDb(), 1));
    g.setColour (proc.getOutputPeakDb() > -0.1f ? Colours::collision : Colours::textDim);
    g.drawText (line2, r, juce::Justification::centredLeft);

    if (proc.isSidechainActive())
    {
        auto sc = analyzerBtn.getBounds().translated (analyzerBtn.getWidth() + 6, 0).withWidth (26);
        g.setColour (Colours::scSpec);
        g.drawRoundedRectangle (sc.toFloat().reduced (1.0f), 4.0f, 1.0f);
        g.setFont (LookAndFeel::font (11.0f, true));
        g.drawText ("SC", sc, juce::Justification::centred);
    }
}

void BottomBar::resized()
{
    auto r = getLocalBounds().reduced (8, 6);
    const bool compact = getWidth() < 1180;
    modeBox.setBounds (r.removeFromLeft (compact ? 118 : 130));
    r.removeFromLeft (4);
    resBox.setBounds (r.removeFromLeft (compact ? 88 : 96));
    r.removeFromLeft (6);
    charBox.setBounds (r.removeFromLeft (compact ? 76 : 90));
    r.removeFromLeft (8);
    analyzerBtn.setBounds (r.removeFromLeft (compact ? 70 : 80));
    r.removeFromLeft (34);

    bypassBtn.setBounds (r.removeFromRight (58));
    r.removeFromRight (4);
    phaseBtn.setBounds (r.removeFromRight (compact ? 46 : 72));
    phaseBtn.setButtonText (compact ? "Phase" : "Phase Inv");
    r.removeFromRight (6);
    outKnob.setBounds (r.removeFromRight (compact ? 88 : 100));
    panKnob.setBounds (r.removeFromRight (compact ? 78 : 92));
    scaleKnob.setBounds (r.removeFromRight (compact ? 96 : 106));
    r.removeFromRight (4);
    autoGainBtn.setBounds (r.removeFromRight (compact ? 70 : 80));
    r.removeFromRight (6);
    infoArea = r;
}

void BottomBar::showAnalyzerMenu()
{
    auto& ui = proc.uiState;
    juce::PopupMenu m, range, res, speed, tilt, inst;

    m.addItem (1, "Pre-EQ Spectrum", true, (bool) ui.getProperty ("anaPre", true));
    m.addItem (2, "Post-EQ Spectrum", true, (bool) ui.getProperty ("anaPost", true));
    m.addItem (3, "Side-chain Spectrum", true, (bool) ui.getProperty ("anaSC", false));
    m.addSeparator();

    const int ranges[] = { 60, 90, 120 };
    for (int i = 0; i < 3; ++i) range.addItem (10 + i, juce::String (ranges[i]) + " dB", true, (int) ui.getProperty ("anaRange", 90) == ranges[i]);
    const char* resNames[] = { "Low", "Medium", "High", "Maximum" };
    for (int i = 0; i < 4; ++i) res.addItem (20 + i, resNames[i], true, (int) ui.getProperty ("anaRes", 2) == i);
    const char* speedNames[] = { "Very Slow", "Slow", "Medium", "Fast", "Very Fast" };
    for (int i = 0; i < 5; ++i) speed.addItem (30 + i, speedNames[i], true, (int) ui.getProperty ("anaSpeed", 2) == i);
    const double tilts[] = { 0.0, 1.5, 3.0, 4.5, 6.0 };
    for (int i = 0; i < 5; ++i) tilt.addItem (40 + i, juce::String (tilts[i], 1) + " dB/oct", true, std::abs ((double) ui.getProperty ("anaTilt", 4.5) - tilts[i]) < 0.01);

    m.addSubMenu ("Range", range);
    m.addSubMenu ("Resolution", res);
    m.addSubMenu ("Speed", speed);
    m.addSubMenu ("Tilt", tilt);
    m.addSeparator();
    m.addItem (4, "Freeze", true, (bool) ui.getProperty ("anaFreeze", false));
    m.addItem (5, "Spectrum Grab", true, (bool) ui.getProperty ("grab", true));
    m.addItem (6, "Collision Detection", true, (bool) ui.getProperty ("collisions", true));
    m.addItem (7, "Piano Roll", true, (bool) ui.getProperty ("pianoRoll", false));

    const int curInst = (int) ui.getProperty ("instance", 0);
    inst.addItem (100, "None", true, curInst == 0);
    for (const auto& info : InstanceRegistry::get().list())
        if (info.id != proc.getInstanceId())
            inst.addItem (100 + info.id, info.name, true, curInst == info.id);
    m.addSubMenu ("Show Other Instance", inst);

    juce::Component::SafePointer<BottomBar> safe (this);
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&analyzerBtn), [safe] (int r)
    {
        if (safe == nullptr || r == 0) return;
        auto& u = safe->proc.uiState;
        auto flip = [&u] (const char* id, bool def) { u.setProperty (id, ! (bool) u.getProperty (id, def), nullptr); };
        const int ranges[] = { 60, 90, 120 };
        const double tilts[] = { 0.0, 1.5, 3.0, 4.5, 6.0 };
        switch (r)
        {
            case 1: flip ("anaPre", true); break;
            case 2: flip ("anaPost", true); break;
            case 3: flip ("anaSC", false); break;
            case 4: flip ("anaFreeze", false); break;
            case 5: flip ("grab", true); break;
            case 6: flip ("collisions", true); break;
            case 7: flip ("pianoRoll", false); break;
            default: break;
        }
        if (r >= 10 && r < 13) u.setProperty ("anaRange", ranges[r - 10], nullptr);
        if (r >= 20 && r < 24) u.setProperty ("anaRes", r - 20, nullptr);
        if (r >= 30 && r < 35) u.setProperty ("anaSpeed", r - 30, nullptr);
        if (r >= 40 && r < 45) u.setProperty ("anaTilt", tilts[r - 40], nullptr);
        if (r >= 100) u.setProperty ("instance", r - 100, nullptr);
    });
}

} // namespace meq::ui
