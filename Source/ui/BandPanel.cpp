#include "BandPanel.h"

namespace meq::ui
{

// ---------------------------------------------------------------------------
Knob::Knob (const juce::String& c) : caption (c)
{
    slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 74, 16);
    slider.setRotaryParameters (juce::MathConstants<float>::pi * 1.2f, juce::MathConstants<float>::pi * 2.8f, true);
    slider.setColour (juce::Slider::rotarySliderFillColourId, Colours::accent);
    slider.setVelocityBasedMode (false);
    slider.setMouseDragSensitivity (220);
    slider.onDragEnd = [this] { if (onEdit) onEdit(); };
    addAndMakeVisible (slider);
}

void Knob::attach (juce::RangedAudioParameter* p)
{
    attachment.reset();
    if (p == nullptr) return;
    attachment = std::make_unique<juce::SliderParameterAttachment> (*p, slider, nullptr);
    slider.setDoubleClickReturnValue (true, p->convertFrom0to1 (p->getDefaultValue()));
}

void Knob::setAccent (juce::Colour c)
{
    slider.setColour (juce::Slider::rotarySliderFillColourId, c);
}

void Knob::resized()
{
    auto r = getLocalBounds();
    r.removeFromTop (13);
    slider.setBounds (r);
}

void Knob::paint (juce::Graphics& g)
{
    g.setColour (isEnabled() ? Colours::textDim : Colours::textDim.withAlpha (0.4f));
    g.setFont (LookAndFeel::font (10.5f, true));
    g.drawText (caption, getLocalBounds().removeFromTop (13), juce::Justification::centred);
}

// ---------------------------------------------------------------------------
MiniKnob::MiniKnob (const juce::String& c) : caption (c)
{
    slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    slider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    slider.setRotaryParameters (juce::MathConstants<float>::pi * 1.2f, juce::MathConstants<float>::pi * 2.8f, true);
    slider.setColour (juce::Slider::rotarySliderFillColourId, Colours::accent);
    slider.setMouseDragSensitivity (200);
    slider.onDragEnd = [this] { if (onEdit) onEdit(); };
    slider.onValueChange = [this] { repaint(); };
    addAndMakeVisible (slider);
}

void MiniKnob::attach (juce::RangedAudioParameter* p)
{
    param = p;
    attachment.reset();
    if (p == nullptr) return;
    attachment = std::make_unique<juce::SliderParameterAttachment> (*p, slider, nullptr);
    slider.setDoubleClickReturnValue (true, p->convertFrom0to1 (p->getDefaultValue()));
}

void MiniKnob::resized()
{
    slider.setBounds (getLocalBounds().removeFromLeft (getHeight()));
}

void MiniKnob::paint (juce::Graphics& g)
{
    auto r = getLocalBounds();
    r.removeFromLeft (getHeight() + 2);
    g.setFont (LookAndFeel::font (10.0f, true));
    g.setColour (Colours::textDim);
    g.drawText (caption, r.removeFromTop (r.getHeight() / 2), juce::Justification::bottomLeft);
    g.setFont (LookAndFeel::font (12.0f));
    g.setColour (Colours::text);
    g.drawText (slider.getTextFromValue (slider.getValue()), r, juce::Justification::topLeft);
}

// ---------------------------------------------------------------------------
BandPanel::BandPanel (ModernEQProcessor& p) : proc (p)
{
    for (auto* b : { &prevBtn, &nextBtn, &enableBtn, &soloBtn, &deleteBtn, &dynBtn, &scBtn, &closeBtn })
        addAndMakeVisible (b);
    for (auto* b : { &enableBtn, &soloBtn, &dynBtn, &scBtn })
        b->setClickingTogglesState (true);

    enableBtn.setTooltip ("Enable / bypass this band (double-click the node)");
    soloBtn.setTooltip ("Solo: listen only to the region this band affects");
    deleteBtn.setTooltip ("Delete band (Alt+click the node)");
    dynBtn.setTooltip ("Dynamic EQ: gain moves towards Gain + Range when the band level exceeds the threshold");
    scBtn.setTooltip ("Trigger the dynamics from the external side-chain input");
    prevBtn.setTooltip ("Previous band");
    nextBtn.setTooltip ("Next band");

    shapeBox.addItemList (filterTypeNames(), 1);
    slopeBox.addItemList (slopeNames(), 1);
    placeBox.addItemList (placementNames(), 1);
    for (auto* c : { &shapeBox, &slopeBox, &placeBox })
    {
        addAndMakeVisible (c);
        c->onChange = [this] { edited(); };
    }
    shapeBox.setTooltip ("Filter shape");
    slopeBox.setTooltip ("Slope / steepness");
    placeBox.setTooltip ("Stereo placement: process both channels, left, right, mid or side");

    for (auto* k : { &freq, &gain, &q, &range, &thresh, &attack, &release })
    {
        addAndMakeVisible (k);
        k->onEdit = [this] { edited(); };
    }
    range.slider.setTooltip ("Dynamic range: maximum gain change applied by the dynamics");
    thresh.slider.setTooltip ("Level (of the band-filtered detector signal) above which the dynamics act");

    enableBtn.onClick = [this] { edited(); };
    dynBtn.onClick = [this]
    {
        // Turning dynamics on with a zero range would do nothing: start at -6 dB.
        if (band >= 0 && dynBtn.getToggleState() && std::abs (proc.refs.readBand (band).dynRange) < 0.01)
            proc.setParam (ID::band (band, ID::dynRange), -6.0f);
        refreshEnablement();
        edited();
    };
    scBtn.onClick = [this] { edited(); };
    soloBtn.onClick = [this]
    {
        if (band >= 0 && soloBtn.getToggleState())
            for (int o = 0; o < kNumBands; ++o)
                if (o != band && proc.refs.readBand (o).solo)
                    proc.setParam (ID::band (o, ID::solo), 0.0f);
        edited();
    };
    deleteBtn.onClick = [this]
    {
        if (band < 0) return;
        const int b = band;
        setBand (-1);
        proc.deleteBand (b);
        edited();
        if (onSelectBand) onSelectBand (-1);
    };
    prevBtn.onClick = [this] { stepBand (-1); };
    nextBtn.onClick = [this] { stepBand (1); };
    closeBtn.onClick = [this] { if (onClose) onClose(); };

    setSize (820, 112);
    setVisible (false);
    startTimerHz (15);
}

BandPanel::~BandPanel()
{
    stopTimer();
}

void BandPanel::edited()
{
    juce::Component::SafePointer<BandPanel> safe (this);
    juce::MessageManager::callAsync ([safe]
    {
        if (safe != nullptr && safe->onEdit) safe->onEdit();
    });
}

void BandPanel::stepBand (int dir)
{
    if (band < 0) return;
    for (int i = 1; i <= kNumBands; ++i)
    {
        const int b = (band + dir * i + kNumBands * 2) % kNumBands;
        if (proc.refs.readBand (b).used)
        {
            if (onSelectBand) onSelectBand (b);
            return;
        }
    }
}

void BandPanel::setBand (int b)
{
    if (b == band) return;
    band = b;

    shapeAtt.reset(); slopeAtt.reset(); placeAtt.reset();
    enableAtt.reset(); soloAtt.reset(); dynAtt.reset(); scAtt.reset();
    for (auto* k : { &freq, &gain, &q, &range, &thresh, &attack, &release })
        k->attach (nullptr);

    if (b < 0) { setVisible (false); return; }

    auto prm = [this, b] (const char* id) { return proc.param (ID::band (b, id)); };
    shapeAtt  = std::make_unique<juce::ComboBoxParameterAttachment> (*prm (ID::type), shapeBox, nullptr);
    slopeAtt  = std::make_unique<juce::ComboBoxParameterAttachment> (*prm (ID::slope), slopeBox, nullptr);
    placeAtt  = std::make_unique<juce::ComboBoxParameterAttachment> (*prm (ID::placement), placeBox, nullptr);
    enableAtt = std::make_unique<juce::ButtonParameterAttachment> (*prm (ID::enabled), enableBtn, nullptr);
    soloAtt   = std::make_unique<juce::ButtonParameterAttachment> (*prm (ID::solo), soloBtn, nullptr);
    dynAtt    = std::make_unique<juce::ButtonParameterAttachment> (*prm (ID::dynOn), dynBtn, nullptr);
    scAtt     = std::make_unique<juce::ButtonParameterAttachment> (*prm (ID::dynSC), scBtn, nullptr);

    freq.attach (prm (ID::freq));
    gain.attach (prm (ID::gain));
    q.attach (prm (ID::q));
    range.attach (prm (ID::dynRange));
    thresh.attach (prm (ID::threshold));
    attack.attach (prm (ID::attack));
    release.attach (prm (ID::release));

    const auto col = bandColour (b);
    for (auto* k : { &freq, &gain, &q, &range, &thresh, &attack, &release })
        k->setAccent (col);
    enableBtn.setColour (juce::TextButton::buttonOnColourId, Colours::on.withAlpha (0.35f));
    soloBtn.setColour (juce::TextButton::buttonOnColourId, Colours::warn.withAlpha (0.45f));
    dynBtn.setColour (juce::TextButton::buttonOnColourId, col.withAlpha (0.45f));

    lastType = -1;
    refreshEnablement();
    repaint();
}

void BandPanel::refreshEnablement()
{
    if (band < 0) return;
    const auto p = proc.refs.readBand (band);
    lastType = (int) p.type;
    lastSlope = p.slopeIdx;
    lastDyn = p.dynOn;

    gain.setEnabled (typeHasGain (p.type));
    slopeBox.setEnabled (typeHasSlope (p.type));
    const bool cutWithoutQ = (p.type == FilterType::LowCut || p.type == FilterType::HighCut)
                             && (p.slopeIdx == 0 || p.slopeIdx == kBrickwallIdx);
    q.setEnabled (typeHasQ (p.type) && ! cutWithoutQ);

    const bool dynOk = typeSupportsDynamics (p.type);
    dynBtn.setEnabled (dynOk);
    const bool dynActive = dynOk && p.dynOn;
    for (auto* k : { &range, &thresh, &attack, &release })
        k->setEnabled (dynActive);
    scBtn.setEnabled (dynActive);
    repaint();
}

void BandPanel::timerCallback()
{
    if (band < 0) return;
    const auto p = proc.refs.readBand (band);
    if ((int) p.type != lastType || p.slopeIdx != lastSlope || p.dynOn != lastDyn)
        refreshEnablement();
}

void BandPanel::paint (juce::Graphics& g)
{
    const auto r = getLocalBounds().toFloat().reduced (0.5f);
    g.setColour (Colours::panel);
    g.fillRoundedRectangle (r, 8.0f);
    g.setColour (Colours::panelEdge);
    g.drawRoundedRectangle (r, 8.0f, 1.0f);
    if (band < 0) return;

    const auto col = bandColour (band);
    g.setColour (col);
    g.fillRoundedRectangle (r.getX() + 12.0f, r.getY() + 1.0f, r.getWidth() - 24.0f, 2.5f, 1.0f);

    g.setFont (LookAndFeel::font (15.0f, true));
    g.setColour (Colours::text);
    g.drawText ("Band " + juce::String (band + 1), juce::Rectangle<float> (14.0f, 10.0f, 90.0f, 20.0f), juce::Justification::centredLeft);

    const auto p = proc.refs.readBand (band);
    g.setColour (col.withAlpha (0.9f));
    g.strokePath (shapeIcon (p.type, { 76.0f, 14.0f, 22.0f, 12.0f }), juce::PathStrokeType (1.6f));

    // Separator before the dynamics section
    const float sx = (float) dynBtn.getX() - 9.0f;
    g.setColour (Colours::panelEdge);
    g.drawVerticalLine ((int) sx, 12.0f, (float) getHeight() - 12.0f);
}

void BandPanel::resized()
{
    auto r = getLocalBounds().reduced (10, 8);
    closeBtn.setBounds (getWidth() - 26, 6, 20, 18);

    auto head = r.removeFromLeft (100);
    head.removeFromTop (28);
    auto row1 = head.removeFromTop (24);
    prevBtn.setBounds (row1.removeFromLeft (30));
    row1.removeFromLeft (4);
    nextBtn.setBounds (row1.removeFromLeft (30));
    row1.removeFromLeft (4);
    enableBtn.setBounds (row1);
    head.removeFromTop (6);
    auto row2 = head.removeFromTop (24);
    soloBtn.setBounds (row2.removeFromLeft (48));
    row2.removeFromLeft (4);
    deleteBtn.setBounds (row2);

    r.removeFromLeft (8);
    auto combos = r.removeFromLeft (124);
    combos.removeFromTop (6);
    shapeBox.setBounds (combos.removeFromTop (24));
    combos.removeFromTop (6);
    slopeBox.setBounds (combos.removeFromTop (24));
    combos.removeFromTop (6);
    placeBox.setBounds (combos.removeFromTop (24));

    r.removeFromLeft (6);
    for (auto* k : { &freq, &gain, &q })
        k->setBounds (r.removeFromLeft (70));

    r.removeFromLeft (14);
    auto dyn = r.removeFromLeft (62);
    dyn.removeFromTop (10);
    dynBtn.setBounds (dyn.removeFromTop (26));
    dyn.removeFromTop (8);
    scBtn.setBounds (dyn.removeFromTop (26));

    r.removeFromLeft (4);
    for (auto* k : { &range, &thresh, &attack, &release })
        k->setBounds (r.removeFromLeft (66));
}

} // namespace meq::ui
