/**
 * @file ForgeLookAndFeel.h
 * @brief Custom drawing for knobs, power switches, buttons, menus and pop-ups (call-out boxes).
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

    /** The pop-ups of the module browser and the module info: dark panel with a thin outline. */
    void drawCallOutBoxBackground (juce::CallOutBox& box, juce::Graphics& graphics, const juce::Path& outline,
                                   juce::Image& cachedShadow) override;
};

} // namespace midiforge
