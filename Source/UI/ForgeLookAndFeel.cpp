/**
 * @file ForgeLookAndFeel.cpp
 * @brief Implementation of ForgeLookAndFeel.
 */
#include "ForgeLookAndFeel.h"

#include "Theme.h"

namespace midiforge
{

ForgeLookAndFeel::ForgeLookAndFeel()
{
    using namespace Theme::Colours;
    setColour (juce::ComboBox::backgroundColourId, controlTrack.darker (0.35f));
    setColour (juce::ComboBox::outlineColourId, slotOutline);
    setColour (juce::ComboBox::textColourId, primaryText);
    setColour (juce::ComboBox::arrowColourId, secondaryText);
    setColour (juce::PopupMenu::backgroundColourId, popupBackground);
    setColour (juce::PopupMenu::textColourId, primaryText);
    setColour (juce::PopupMenu::headerTextColourId, secondaryText);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, popupHighlight);
    setColour (juce::PopupMenu::highlightedTextColourId, juce::Colours::white);
    setColour (juce::Label::textColourId, primaryText);
    setColour (juce::TextButton::textColourOffId, primaryText);
    setColour (juce::TextButton::textColourOnId, primaryText);
    setColour (juce::TooltipWindow::backgroundColourId, popupBackground.brighter (0.05f));
    setColour (juce::TooltipWindow::textColourId, primaryText);
    setColour (juce::TooltipWindow::outlineColourId, slotOutline);
    setColour (juce::ScrollBar::thumbColourId, controlTrack.brighter (0.25f));
}

void ForgeLookAndFeel::drawRotarySlider (juce::Graphics& graphics, int x, int y, int width, int height,
                                         float sliderPosition, float startAngle, float endAngle, juce::Slider& slider)
{
    const auto bounds = juce::Rectangle<int> (x, y, width, height).toFloat().reduced (3.0f);
    const auto centre = bounds.getCentre();
    constexpr float arcThickness = 3.0f;
    const float arcRadius = juce::jmin (bounds.getWidth(), bounds.getHeight()) / 2.0f - arcThickness;
    const float valueAngle = startAngle + sliderPosition * (endAngle - startAngle);
    const juce::PathStrokeType arcStroke (arcThickness, juce::PathStrokeType::curved, juce::PathStrokeType::rounded);

    // Background arc over the full range
    juce::Path fullRange;
    fullRange.addCentredArc (centre.x, centre.y, arcRadius, arcRadius, 0.0f, startAngle, endAngle, true);
    graphics.setColour (Theme::Colours::controlTrack);
    graphics.strokePath (fullRange, arcStroke);

    // Coloured arc up to the current value
    juce::Path valueArc;
    valueArc.addCentredArc (centre.x, centre.y, arcRadius, arcRadius, 0.0f, startAngle, valueAngle, true);
    graphics.setColour (slider.findColour (juce::Slider::rotarySliderFillColourId));
    graphics.strokePath (valueArc, arcStroke);

    // Knob body with a pointer line
    graphics.setColour (Theme::Colours::slotBackground.brighter (0.12f));
    graphics.fillEllipse (juce::Rectangle<float> (arcRadius * 1.3f, arcRadius * 1.3f).withCentre (centre));
    graphics.setColour (Theme::Colours::primaryText);
    graphics.drawLine ({ centre.getPointOnCircumference (arcRadius * 0.2f, valueAngle),
                         centre.getPointOnCircumference (arcRadius * 0.62f, valueAngle) },
                       2.0f);
}

void ForgeLookAndFeel::drawToggleButton (juce::Graphics& graphics, juce::ToggleButton& button, bool isHighlighted,
                                         bool /*isDown*/)
{
    // A pill-shaped power switch that lights up in the slot's accent colour when on.
    const auto pill = button.getLocalBounds().toFloat().withSizeKeepingCentre (30.0f, 16.0f);
    const bool isOn = button.getToggleState();
    const float cornerRadius = pill.getHeight() / 2.0f;

    graphics.setColour (isOn ? button.findColour (juce::ToggleButton::tickColourId).withAlpha (0.9f)
                             : Theme::Colours::controlTrack);
    graphics.fillRoundedRectangle (pill, cornerRadius);

    const float thumbDiameter = pill.getHeight() - 4.0f;
    const float thumbCentreX =
        isOn ? pill.getRight() - thumbDiameter / 2.0f - 2.0f : pill.getX() + thumbDiameter / 2.0f + 2.0f;
    graphics.setColour (isOn ? juce::Colours::white : Theme::Colours::secondaryText);
    graphics.fillEllipse (
        juce::Rectangle<float> (thumbDiameter, thumbDiameter).withCentre ({ thumbCentreX, pill.getCentreY() }));

    if (isHighlighted)
    {
        graphics.setColour (juce::Colours::white.withAlpha (0.15f));
        graphics.drawRoundedRectangle (pill, cornerRadius, 1.0f);
    }
}

void ForgeLookAndFeel::drawLinearSlider (juce::Graphics& graphics, int x, int y, int width, int height,
                                         float sliderPosition, float minSliderPosition, float maxSliderPosition,
                                         juce::Slider::SliderStyle style, juce::Slider& slider)
{
    if (style != juce::Slider::LinearHorizontal)
    {
        LookAndFeel_V4::drawLinearSlider (graphics, x, y, width, height, sliderPosition, minSliderPosition,
                                          maxSliderPosition, style, slider);
        return;
    }

    const auto area = juce::Rectangle<int> (x, y, width, height).toFloat();
    constexpr float trackThickness = 4.0f;
    const auto track = area.withSizeKeepingCentre (area.getWidth(), trackThickness);
    graphics.setColour (Theme::Colours::controlTrack);
    graphics.fillRoundedRectangle (track, trackThickness / 2.0f);

    const bool isBipolar = (bool) slider.getProperties().getWithDefault (Theme::bipolarSliderProperty, false);
    const float fillStart = isBipolar ? track.getCentreX() : track.getX();
    const auto fill =
        juce::Rectangle<float>::leftTopRightBottom (juce::jmin (fillStart, sliderPosition), track.getY(),
                                                    juce::jmax (fillStart, sliderPosition), track.getBottom());
    graphics.setColour (slider.isEnabled() ? Theme::Colours::modulation : Theme::Colours::secondaryText);
    graphics.fillRoundedRectangle (fill, trackThickness / 2.0f);

    if (isBipolar)
    {
        graphics.setColour (Theme::Colours::secondaryText);
        graphics.fillRect (juce::Rectangle<float> (1.0f, 10.0f).withCentre (track.getCentre()));
    }

    graphics.setColour (juce::Colours::white);
    graphics.fillEllipse (juce::Rectangle<float> (11.0f, 11.0f).withCentre ({ sliderPosition, track.getCentreY() }));
}

void ForgeLookAndFeel::drawButtonBackground (juce::Graphics& graphics, juce::Button& button,
                                             const juce::Colour& backgroundColour, bool isHighlighted, bool isDown)
{
    auto colour = backgroundColour;
    if (! button.isEnabled())
        colour = colour.withAlpha (0.4f);
    else if (isDown)
        colour = colour.brighter (0.2f);
    else if (isHighlighted)
        colour = colour.brighter (0.1f);

    graphics.setColour (colour);
    graphics.fillRoundedRectangle (button.getLocalBounds().toFloat().reduced (1.0f), 5.0f);
}

void ForgeLookAndFeel::drawCallOutBoxBackground (juce::CallOutBox& box, juce::Graphics& graphics,
                                                 const juce::Path& outline, juce::Image& cachedShadow)
{
    if (cachedShadow.isNull())
    {
        cachedShadow = juce::Image (juce::Image::ARGB, box.getWidth(), box.getHeight(), true);
        juce::Graphics shadowGraphics (cachedShadow);
        juce::DropShadow (juce::Colours::black.withAlpha (0.6f), 10, { 0, 3 }).drawForPath (shadowGraphics, outline);
    }
    graphics.drawImageAt (cachedShadow, 0, 0);

    graphics.setColour (Theme::Colours::popupBackground);
    graphics.fillPath (outline);
    graphics.setColour (Theme::Colours::slotOutline.brighter (0.3f));
    graphics.strokePath (outline, juce::PathStrokeType (1.0f));
}

} // namespace midiforge
