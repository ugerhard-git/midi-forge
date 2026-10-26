/**
 * @file ModuleDescriptionTests.cpp
 * @brief Checks the module table: consistent ranges, defaults, text formatting and parsing.
 */
#include "../TestFramework.h"
#include "ChainTestRig.h"

#include <cmath>

using namespace testing;

TEST_CASE ("descriptions: every used parameter has a sane range and a default inside it")
{
    for (int type = 0; type < numModuleTypes; ++type)
    {
        const auto& description = getModuleDescription (type);
        EXPECT (std::string (description.name).size() > 0);
        EXPECT ((type == 0) == (description.category == ModuleCategory::None));

        for (const auto& parameter : description.parameters)
        {
            if (! parameter.isUsed())
                continue;
            EXPECT (parameter.maximum > parameter.minimum);
            EXPECT (parameter.defaultValue >= parameter.minimum && parameter.defaultValue <= parameter.maximum);
            if (parameter.kind == ParameterKind::Choice)
                EXPECT (parameter.choiceLabels != nullptr);
        }
    }
}

TEST_CASE ("descriptions: formatting and parsing round-trip for every step of every parameter")
{
    for (int type = 0; type < numModuleTypes; ++type)
    {
        for (const auto& parameter : getModuleDescription (type).parameters)
        {
            if (! parameter.isUsed() || parameter.kind == ParameterKind::Continuous)
                continue;
            for (int step = 0; step < getNumberOfSteps (parameter); ++step)
            {
                const float normalised = plainValueToNormalised (parameter, parameter.minimum + (float) step);
                const std::string text = formatParameterValue (parameter, normalised);
                const float parsed = parseParameterText (parameter, text, -1.0f);
                EXPECT (std::abs (parsed - normalised) < 1.0e-4f);
            }
        }
    }
}

TEST_CASE ("descriptions: display text examples")
{
    const auto& arpeggiator = getModuleDescription (ModuleType::Arpeggiator);
    EXPECT (formatParameterValue (arpeggiator.parameters[0], getNormalisedDefault (arpeggiator.parameters[0])) ==
            "1/16");
    EXPECT (getNoteName (60) == "C3");
    EXPECT (getNoteName (0) == "C-2");
    EXPECT (getNoteName (127) == "G8");

    const auto& transpose = getModuleDescription (ModuleType::Transpose);
    EXPECT (formatParameterValue (transpose.parameters[0], parseParameterText (transpose.parameters[0], "7", 0.5f)) ==
            "+7 st");
    EXPECT (formatParameterValue (transpose.parameters[0],
                                  parseParameterText (transpose.parameters[0], "-3 st", 0.5f)) == "-3 st");

    const auto& velocityCurve = getModuleDescription (ModuleType::VelocityCurve);
    EXPECT (formatParameterValue (velocityCurve.parameters[3], 0.0f) == "Off");

    const auto& humanize = getModuleDescription (ModuleType::Humanize);
    EXPECT (formatParameterValue (humanize.parameters[0], 0.5f) == "25.0 ms");
}

TEST_CASE ("descriptions: hostile text input is rejected")
{
    const auto& transpose = getModuleDescription (ModuleType::Transpose).parameters[0];
    EXPECT (parseParameterText (transpose, "", 0.25f) == 0.25f);
    EXPECT (parseParameterText (transpose, "nan", 0.25f) == 0.25f);
    EXPECT (parseParameterText (transpose, "inf", 0.25f) == 0.25f);
    EXPECT (parseParameterText (transpose, "hello", 0.25f) == 0.25f);
    EXPECT (parseParameterText (transpose, "1e30", 0.25f) == 1.0f); // clamped to the range
}

TEST_CASE ("descriptions: out-of-range module types fall back to Empty")
{
    EXPECT (std::string (getModuleDescription (-1).name) == "Empty");
    EXPECT (std::string (getModuleDescription (numModuleTypes + 5).name) == "Empty");
}
