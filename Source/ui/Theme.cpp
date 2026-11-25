#include "Theme.h"

namespace meq::ui
{

juce::Colour bandColour (int band)
{
    const float hue = std::fmod (0.03f + (float) band * 0.61803398875f, 1.0f);
    return juce::Colour::fromHSV (hue, 0.62f, 0.98f, 1.0f);
}

juce::Colour placementColour (Placement p)
{
    switch (p)
    {
        case Placement::Left:  return juce::Colour (0xff5ad1ff);
        case Placement::Right: return juce::Colour (0xffff6fae);
        case Placement::Mid:   return juce::Colour (0xffffd166);
        case Placement::Side:  return juce::Colour (0xff7ee787);
        case Placement::Stereo:
        case Placement::Count:
        default:               return Colours::curve;
    }
}

juce::String placementShort (Placement p)
{
    switch (p)
    {
        case Placement::Left:  return "L";
        case Placement::Right: return "R";
        case Placement::Mid:   return "M";
        case Placement::Side:  return "S";
        case Placement::Stereo:
        case Placement::Count:
        default:               return "ST";
    }
}

juce::String filterShortName (FilterType t)
{
    return filterTypeNames()[(int) t];
}

juce::Path shapeIcon (FilterType t, juce::Rectangle<float> r)
{
    juce::Path p;
    const float x0 = r.getX(), x1 = r.getRight(), w = r.getWidth();
    const float yc = r.getCentreY(), top = r.getY(), bot = r.getBottom();
    auto lerpX = [&] (float f) { return x0 + f * w; };

    switch (t)
    {
        case FilterType::Bell:
            p.startNewSubPath (x0, yc);
            p.lineTo (lerpX (0.3f), yc);
            p.quadraticTo (lerpX (0.5f), top - (bot - top) * 0.3f, lerpX (0.7f), yc);
            p.lineTo (x1, yc);
            break;
        case FilterType::LowShelf:
            p.startNewSubPath (x0, top + 2);
            p.lineTo (lerpX (0.3f), top + 2);
            p.cubicTo (lerpX (0.5f), top + 2, lerpX (0.5f), yc, lerpX (0.7f), yc);
            p.lineTo (x1, yc);
            break;
        case FilterType::HighShelf:
            p.startNewSubPath (x0, yc);
            p.lineTo (lerpX (0.3f), yc);
            p.cubicTo (lerpX (0.5f), yc, lerpX (0.5f), top + 2, lerpX (0.7f), top + 2);
            p.lineTo (x1, top + 2);
            break;
        case FilterType::LowCut:
            p.startNewSubPath (lerpX (0.1f), bot);
            p.quadraticTo (lerpX (0.35f), yc, lerpX (0.5f), yc);
            p.lineTo (x1, yc);
            break;
        case FilterType::HighCut:
            p.startNewSubPath (x0, yc);
            p.lineTo (lerpX (0.5f), yc);
            p.quadraticTo (lerpX (0.65f), yc, lerpX (0.9f), bot);
            break;
        case FilterType::Notch:
            p.startNewSubPath (x0, top + 2);
            p.lineTo (lerpX (0.4f), top + 2);
            p.lineTo (lerpX (0.5f), bot);
            p.lineTo (lerpX (0.6f), top + 2);
            p.lineTo (x1, top + 2);
            break;
        case FilterType::BandPass:
            p.startNewSubPath (lerpX (0.1f), bot);
            p.quadraticTo (lerpX (0.5f), top - 4, lerpX (0.9f), bot);
            break;
        case FilterType::TiltShelf:
            p.startNewSubPath (x0, bot - 2);
            p.lineTo (lerpX (0.35f), bot - 2);
            p.cubicTo (lerpX (0.5f), bot - 2, lerpX (0.5f), top + 2, lerpX (0.65f), top + 2);
            p.lineTo (x1, top + 2);
            break;
        case FilterType::FlatTilt:
            p.startNewSubPath (x0, bot - 2);
            p.lineTo (x1, top + 2);
            break;
        case FilterType::AllPass:
            p.startNewSubPath (x0, yc);
            p.lineTo (x1, yc);
            p.addEllipse (lerpX (0.5f) - 3, yc - 3, 6, 6);
            break;
        case FilterType::Count:
        default:
            break;
    }
    return p;
}

// ---------------------------------------------------------------------------
LookAndFeel::LookAndFeel()
{
    setDefaultSansSerifTypefaceName ("Segoe UI");

    setColour (juce::ResizableWindow::backgroundColourId, Colours::bg0);
    setColour (juce::PopupMenu::backgroundColourId, Colours::bg2);
    setColour (juce::PopupMenu::textColourId, Colours::text);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, Colours::accent.withAlpha (0.25f));
    setColour (juce::PopupMenu::highlightedTextColourId, juce::Colours::white);
    setColour (juce::PopupMenu::headerTextColourId, Colours::textDim);
    setColour (juce::ComboBox::backgroundColourId, Colours::bg2);
    setColour (juce::ComboBox::textColourId, Colours::text);
    setColour (juce::ComboBox::outlineColourId, Colours::panelEdge);
    setColour (juce::ComboBox::arrowColourId, Colours::textDim);
    setColour (juce::TextButton::buttonColourId, Colours::bg2);
    setColour (juce::TextButton::buttonOnColourId, Colours::accent.withAlpha (0.35f));
    setColour (juce::TextButton::textColourOffId, Colours::text);
    setColour (juce::TextButton::textColourOnId, juce::Colours::white);
    setColour (juce::Slider::textBoxTextColourId, Colours::text);
    setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
    setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour (juce::Label::textColourId, Colours::text);
    setColour (juce::TextEditor::backgroundColourId, Colours::bg1);
    setColour (juce::TextEditor::textColourId, Colours::text);
    setColour (juce::TextEditor::outlineColourId, Colours::panelEdge);
    setColour (juce::TextEditor::focusedOutlineColourId, Colours::accent);
    setColour (juce::TextEditor::highlightColourId, Colours::accent.withAlpha (0.35f));
    setColour (juce::CaretComponent::caretColourId, Colours::accent);
    setColour (juce::TooltipWindow::backgroundColourId, Colours::bg2);
    setColour (juce::TooltipWindow::textColourId, Colours::text);
    setColour (juce::AlertWindow::backgroundColourId, Colours::bg2);
    setColour (juce::AlertWindow::textColourId, Colours::text);
    setColour (juce::AlertWindow::outlineColourId, Colours::panelEdge);
    setColour (juce::ToggleButton::textColourId, Colours::text);
    setColour (juce::ToggleButton::tickColourId, Colours::accent);
    setColour (juce::ToggleButton::tickDisabledColourId, Colours::textDim);
    setColour (juce::ScrollBar::thumbColourId, Colours::panelEdge);
}

juce::Font LookAndFeel::font (float h, bool bold)
{
    return juce::Font ("Segoe UI", h, bold ? juce::Font::bold : juce::Font::plain);
}

void LookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos,
                                    float start, float end, juce::Slider& s)
{
    const auto bounds = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h).reduced (3.0f);
    const float radius = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f;
    const auto c = bounds.getCentre();
    const float angle = start + pos * (end - start);
    const auto accent = s.findColour (juce::Slider::rotarySliderFillColourId);
    const bool enabled = s.isEnabled();

    // Track
    juce::Path track;
    track.addCentredArc (c.x, c.y, radius - 2.0f, radius - 2.0f, 0.0f, start, end, true);
    g.setColour (Colours::panelEdge);
    g.strokePath (track, juce::PathStrokeType (3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    // Value arc (bipolar sliders start from the centre)
    const bool bipolar = s.getMinimum() < 0.0 && s.getMaximum() > 0.0;
    const float from = bipolar ? start + (float) s.valueToProportionOfLength (0.0) * (end - start) : start;
    juce::Path arc;
    arc.addCentredArc (c.x, c.y, radius - 2.0f, radius - 2.0f, 0.0f, juce::jmin (from, angle), juce::jmax (from, angle), true);
    g.setColour (enabled ? accent : Colours::textDim);
    g.strokePath (arc, juce::PathStrokeType (3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    // Body
    const float br = radius - 6.0f;
    juce::ColourGradient grad (juce::Colour (0xff2a3140), c.x, c.y - br, juce::Colour (0xff151a22), c.x, c.y + br, false);
    g.setGradientFill (grad);
    g.fillEllipse (c.x - br, c.y - br, br * 2.0f, br * 2.0f);
    g.setColour (juce::Colour (0x30ffffff));
    g.drawEllipse (c.x - br, c.y - br, br * 2.0f, br * 2.0f, 1.0f);

    // Pointer
    const float pr = br - 3.0f;
    const juce::Point<float> tip (c.x + pr * std::sin (angle), c.y - pr * std::cos (angle));
    const juce::Point<float> base (c.x + pr * 0.35f * std::sin (angle), c.y - pr * 0.35f * std::cos (angle));
    g.setColour (enabled ? Colours::text : Colours::textDim);
    g.drawLine ({ base, tip }, 2.0f);
}

void LookAndFeel::drawLinearSlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float, float,
                                    juce::Slider::SliderStyle style, juce::Slider& s)
{
    const auto r = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h);
    if (style == juce::Slider::LinearHorizontal)
    {
        const float cy = r.getCentreY();
        g.setColour (Colours::panelEdge);
        g.fillRoundedRectangle (r.getX(), cy - 2.0f, r.getWidth(), 4.0f, 2.0f);
        g.setColour (s.findColour (juce::Slider::rotarySliderFillColourId));
        g.fillRoundedRectangle (r.getX(), cy - 2.0f, pos - r.getX(), 4.0f, 2.0f);
        g.setColour (Colours::text);
        g.fillEllipse (pos - 6.0f, cy - 6.0f, 12.0f, 12.0f);
        return;
    }
    LookAndFeel_V4::drawLinearSlider (g, x, y, w, h, pos, 0, 0, style, s);
}

void LookAndFeel::drawComboBox (juce::Graphics& g, int w, int h, bool, int, int, int, int, juce::ComboBox& box)
{
    const auto r = juce::Rectangle<float> (0, 0, (float) w, (float) h).reduced (0.5f);
    g.setColour (box.findColour (juce::ComboBox::backgroundColourId));
    g.fillRoundedRectangle (r, 4.0f);
    g.setColour (box.hasKeyboardFocus (false) ? Colours::accent.withAlpha (0.6f) : Colours::panelEdge);
    g.drawRoundedRectangle (r, 4.0f, 1.0f);

    juce::Path arrow;
    const float ax = (float) w - 12.0f, ay = (float) h * 0.5f;
    arrow.addTriangle (ax - 4.0f, ay - 2.0f, ax + 4.0f, ay - 2.0f, ax, ay + 3.0f);
    g.setColour (box.isEnabled() ? Colours::textDim : Colours::textDim.withAlpha (0.4f));
    g.fillPath (arrow);
}

void LookAndFeel::positionComboBoxText (juce::ComboBox& box, juce::Label& label)
{
    label.setBounds (6, 1, box.getWidth() - 22, box.getHeight() - 2);
    label.setFont (getComboBoxFont (box));
}

juce::Font LookAndFeel::getComboBoxFont (juce::ComboBox& box)
{
    return font (juce::jlimit (11.0f, 15.0f, (float) box.getHeight() * 0.55f));
}

void LookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&, bool over, bool down)
{
    const auto r = b.getLocalBounds().toFloat().reduced (0.5f);
    const bool on = b.getToggleState();
    auto base = on ? b.findColour (juce::TextButton::buttonOnColourId) : b.findColour (juce::TextButton::buttonColourId);
    if (over) base = base.brighter (0.12f);
    if (down) base = base.brighter (0.2f);
    g.setColour (base);
    g.fillRoundedRectangle (r, 4.0f);
    g.setColour (on ? Colours::accent.withAlpha (0.7f) : Colours::panelEdge);
    g.drawRoundedRectangle (r, 4.0f, 1.0f);
}

void LookAndFeel::drawButtonText (juce::Graphics& g, juce::TextButton& b, bool, bool)
{
    g.setFont (getTextButtonFont (b, b.getHeight()));
    g.setColour (b.findColour (b.getToggleState() ? juce::TextButton::textColourOnId : juce::TextButton::textColourOffId)
                     .withMultipliedAlpha (b.isEnabled() ? 1.0f : 0.4f));
    g.drawFittedText (b.getButtonText(), b.getLocalBounds().reduced (3, 0), juce::Justification::centred, 1);
}

juce::Font LookAndFeel::getTextButtonFont (juce::TextButton&, int h)
{
    return font (juce::jlimit (11.0f, 14.5f, (float) h * 0.52f));
}

void LookAndFeel::drawPopupMenuBackground (juce::Graphics& g, int w, int h)
{
    g.fillAll (Colours::bg2);
    g.setColour (Colours::panelEdge);
    g.drawRect (0, 0, w, h, 1);
}

juce::Font LookAndFeel::getPopupMenuFont() { return font (14.0f); }

void LookAndFeel::drawTooltip (juce::Graphics& g, const juce::String& text, int w, int h)
{
    g.fillAll (Colours::bg2);
    g.setColour (Colours::panelEdge);
    g.drawRect (0, 0, w, h, 1);
    g.setColour (Colours::text);
    g.setFont (font (13.0f));
    g.drawFittedText (text, 6, 2, w - 12, h - 4, juce::Justification::centredLeft, 4);
}

juce::Label* LookAndFeel::createSliderTextBox (juce::Slider& s)
{
    auto* l = LookAndFeel_V4::createSliderTextBox (s);
    l->setFont (font (12.5f));
    l->setJustificationType (juce::Justification::centred);
    l->setColour (juce::Label::outlineWhenEditingColourId, Colours::accent);
    l->setColour (juce::Label::backgroundWhenEditingColourId, Colours::bg1);
    return l;
}

void LookAndFeel::drawCallOutBoxBackground (juce::CallOutBox&, juce::Graphics& g, const juce::Path& path, juce::Image&)
{
    g.setColour (Colours::bg2);
    g.fillPath (path);
    g.setColour (Colours::panelEdge);
    g.strokePath (path, juce::PathStrokeType (1.0f));
}

} // namespace meq::ui
