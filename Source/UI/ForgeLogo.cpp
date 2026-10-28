/**
 * @file ForgeLogo.cpp
 * @brief Implementation of the header's word mark, glow and ember line.
 */
#include "ForgeLogo.h"

#include "Theme.h"

namespace midiforge::ForgeLogo
{

namespace
{
const juce::Colour midiColour { 0xffb9bfcc };    ///< "MIDI": quiet grey (contrast > 9:1 on the window)
const juce::Colour metalWhiteHot { 0xfffff1d6 }; ///< Top of "FORGE"
const juce::Colour metalAmber { 0xffffc070 };    ///< Middle of "FORGE"
const juce::Colour metalOrange { 0xffff8a3d };   ///< Bottom of "FORGE" (contrast about 7:1 on the window)
const juce::Colour anvilSteel { 0xffaeb5c2 };
const juce::Colour emberGlow { 0xffff8030 };
const juce::Colour emberLine { 0xffff8c3c };

constexpr float fontHeight = 24.0f;
constexpr float gapAfterMidi = 7.0f;
constexpr float gapBeforeAnvil = 9.0f;
constexpr float anvilHeight = 22.0f;

/** @return The outline of a text as a path, its baseline at y = 0 and starting at x = 0. */
juce::Path textPath (const juce::String& text, const juce::Font& font)
{
    juce::GlyphArrangement glyphs;
    glyphs.addLineOfText (font, text, 0.0f, 0.0f);
    juce::Path path;
    glyphs.createPath (path);
    return path;
}

float textWidth (const juce::String& text, const juce::Font& font)
{
    juce::GlyphArrangement glyphs;
    glyphs.addLineOfText (font, text, 0.0f, 0.0f);
    return glyphs.getBoundingBox (0, -1, true).getRight();
}
} // namespace

juce::Path createAnvilPath()
{
    // Face, horn to the right, waist and foot, drawn as one outline (30 × 22 units).
    return juce::Drawable::parseSVGPath (
        "M2 3h20c0 3 3 5 6 5v2H20c-1 0-2 1-2 2v1c0 2 2 3 4 3v3H8v-3c2 0 4-1 4-3v-1c0-1-1-2-2-2H6C4 10 2 7 2 3z");
}

void paintHeaderBackground (juce::Graphics& graphics, juce::Rectangle<int> header)
{
    // A faint warm glow behind the logo: an ellipse 420 × 150 px around the top-left corner, at most 10 %.
    {
        juce::Graphics::ScopedSaveState savedState (graphics);
        const juce::Point<float> centre ((float) header.getX() + 104.0f, (float) header.getY());
        graphics.addTransform (juce::AffineTransform::scale (1.0f, 150.0f / 420.0f, centre.x, centre.y));
        graphics.setGradientFill (juce::ColourGradient (emberGlow.withAlpha (0.10f), centre, emberGlow.withAlpha (0.0f),
                                                        centre.translated (420.0f, 0.0f), true));
        graphics.fillRect (header.getX(), header.getY() - 420, 840, 840);
    }

    // The ember line along the bottom of the header, fading out to the right.
    const auto line = header.removeFromBottom (1).toFloat();
    graphics.setGradientFill (juce::ColourGradient (emberLine.withAlpha (0.55f), line.getX(), line.getY(),
                                                    emberLine.withAlpha (0.0f), line.getX() + line.getWidth() * 0.45f,
                                                    line.getY(), false));
    graphics.fillRect (line);
}

void paintWordMark (juce::Graphics& graphics, juce::Rectangle<int> area)
{
    const auto regularFont = Theme::font (fontHeight);
    const auto boldFont = Theme::font (fontHeight, true);
    const float baseline = (float) area.getCentreY() + fontHeight * 0.36f;
    float x = (float) area.getX();

    graphics.setColour (midiColour);
    graphics.setFont (regularFont);
    graphics.drawSingleLineText ("MIDI", juce::roundToInt (x), juce::roundToInt (baseline));
    x += textWidth ("MIDI", regularFont) + gapAfterMidi;

    // "FORGE" as glowing metal: white-hot at the top, amber in the middle, orange at the bottom.
    auto forge = textPath ("FORGE", boldFont);
    forge.applyTransform (juce::AffineTransform::translation (x, baseline));
    const auto bounds = forge.getBounds();
    juce::ColourGradient metal (metalWhiteHot, 0.0f, bounds.getY(), metalOrange, 0.0f, bounds.getBottom(), false);
    metal.addColour (0.55, metalAmber);
    graphics.setGradientFill (metal);
    graphics.fillPath (forge);
    x = bounds.getRight() + gapBeforeAnvil;

    // The anvil after the name, its base on the text's baseline.
    auto anvil = createAnvilPath();
    anvil.applyTransform (juce::AffineTransform::translation (x, baseline - anvilHeight + 3.0f));
    graphics.setColour (anvilSteel);
    graphics.strokePath (anvil,
                         juce::PathStrokeType (1.8f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

} // namespace midiforge::ForgeLogo
