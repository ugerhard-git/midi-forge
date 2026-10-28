/**
 * @file ForgeLogo.h
 * @brief The "forge" look of the header: the word mark (MIDI in grey, FORGE in glowing metal, an anvil after
 *        it), a faint warm glow behind it and a thin ember line under the header.
 *
 * Everything is drawn with paths and gradients (no image files), so it stays sharp at any display scale.
 * It is decoration only: it never sits behind small text, uses no yellow (reserved for modulated values) and
 * does not move.
 */
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace midiforge::ForgeLogo
{

/** Width the word mark needs, including the anvil, at the header's size. */
constexpr int width = 206;

/**
 * Draws the warm glow behind the logo and the ember line along the bottom of the header.
 * @param header The header area (the whole width of the editor, the header's height).
 */
void paintHeaderBackground (juce::Graphics& graphics, juce::Rectangle<int> header);

/**
 * Draws the word mark, left-aligned and vertically centred in an area of at least `width` × 30 px.
 */
void paintWordMark (juce::Graphics& graphics, juce::Rectangle<int> area);

/** @return The anvil outline, 30 × 22 units, horn to the right. */
juce::Path createAnvilPath();

} // namespace midiforge::ForgeLogo
