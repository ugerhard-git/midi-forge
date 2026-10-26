/**
 * @file ForgeLookAndFeel.h
 * @brief Custom drawing for knobs, power switches, buttons and menus.
 */
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace midiforge
{

/**
 * Knobs take their accent colour from juce::Slider::rotarySliderFillColourId and power switches from
 * juce::ToggleButton::tickColourId, so each slot can be coloured by its module category.
 */
class ForgeLookAndFeel final : public juce::LookAndFeel_V4
{
public:
    ForgeLookAndFeel();

    void drawRotarySlider (juce::Graphics& graphics, int x, int y, int width, int height, float sliderPosition,
                           float startAngle, float endAngle, juce::Slider& slider) override;

    void drawToggleButton (juce::Graphics& graphics, juce::ToggleButton& button, bool isHighlighted,
                           bool isDown) override;

    /** Horizontal sliders: a thin track filled from the left, or from the centre for bipolar sliders. */
    void drawLinearSlider (juce::Graphics& graphics, int x, int y, int width, int height, float sliderPosition,
                           float minSliderPosition, float maxSliderPosition, juce::Slider::SliderStyle style,
                           juce::Slider& slider) override;

    void drawButtonBackground (juce::Graphics& graphics, juce::Button& button, const juce::Colour& backgroundColour,
                               bool isHighlighted, bool isDown) override;
};

} // namespace midiforge
