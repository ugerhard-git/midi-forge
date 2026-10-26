/**
 * @file TransformModules.h
 * @brief Factory for the transform modules (Transpose, Scale Quantize, Velocity Curve, Channel Remap).
 */
#pragma once

#include "../MidiModule.h"

namespace midiforge
{

/** Creates the transform module of the given type, or nullptr if the type is not a transform. */
std::unique_ptr<MidiModule> createTransformModule (ModuleType type);

/**
 * Moves a note into a scale.
 * @param rootPitchClass  0 = C ... 11 = B.
 * @param semitoneMask    Scale::semitoneMask.
 * @param snapDirection   0 = nearest (ties go down), 1 = up, 2 = down.
 * @return The quantized note number (may lie outside 0..127 at the extremes).
 */
int quantizeToScale (int noteNumber, int rootPitchClass, unsigned short semitoneMask, int snapDirection);

/**
 * Applies the Velocity Curve module's shaping.
 * @param curve          -1 .. +1. Negative values make soft playing louder (more sensitive),
 *                       positive values make it quieter (you have to play harder), 0 = linear.
 * @param fixedVelocity  If above 0, every note gets exactly this velocity.
 * @return A velocity in 1..127.
 */
int shapeVelocity (int velocity, float curve, int outputMinimum, int outputMaximum, int fixedVelocity);

} // namespace midiforge
