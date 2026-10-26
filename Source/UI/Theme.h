/**
 * @file Theme.h
 * @brief Colours, fonts and sizes of the editor, in one place.
 */
#pragma once

#include "Engine/ModuleDescriptions.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace midiforge::Theme
{

namespace Colours
{
inline const juce::Colour windowBackground { 0xff15171c };
inline const juce::Colour slotBackground { 0xff1e2128 };
inline const juce::Colour slotOutline { 0xff2a2e37 };
inline const juce::Colour primaryText { 0xffe6e8ee };
inline const juce::Colour secondaryText { 0xff8a90a0 };
inline const juce::Colour controlTrack { 0xff343944 };
inline const juce::Colour popupBackground { 0xff22262e };
inline const juce::Colour popupHighlight { 0xff343a46 };
inline const juce::Colour panicButton { 0xffb8434e };
inline const juce::Colour modulation { 0xfff2f2a0 }; ///< LFO rings, waveform previews and route sliders
inline const juce::Colour selectedTab { 0xff2c313b };
} // namespace Colours

/** The accent colour of a module category, used for the slot strip, knobs and power switch. */
inline juce::Colour accentColourFor (ModuleCategory category)
{
    switch (category)
    {
        case ModuleCategory::Filter: return juce::Colour (0xff4ea1ff);     // blue
        case ModuleCategory::Transform: return juce::Colour (0xff3ecf8e);  // green
        case ModuleCategory::Generator: return juce::Colour (0xffffa24a);  // orange
        case ModuleCategory::Randomizer: return juce::Colour (0xffb68cff); // purple
        case ModuleCategory::None: break;
    }
    return juce::Colour (0xff5a606e); // grey for empty slots
}

/** Slider property: when true, ForgeLookAndFeel fills a linear slider from its centre (for -100..+100 values). */
inline const juce::Identifier bipolarSliderProperty { "bipolar" };

/** The editor's font at a given size. */
inline juce::Font font (float height, bool isBold = false)
{
    return juce::Font (juce::FontOptions (height, isBold ? juce::Font::bold : juce::Font::plain));
}

namespace Layout
{
constexpr int editorWidth = 860;
constexpr int headerHeight = 64;
constexpr int slotHeight = 68;
constexpr int gapBetweenSlots = 6;
constexpr int footerHeight = 30;
constexpr int outerMargin = 14;
} // namespace Layout

} // namespace midiforge::Theme
