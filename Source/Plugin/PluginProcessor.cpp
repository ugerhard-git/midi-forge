/**
 * @file PluginProcessor.cpp
 * @brief Implementation of MidiForgeProcessor.
 */
#include "PluginProcessor.h"

#include "FactoryPresets.h"
#include "ParameterIds.h"
#include "UI/PluginEditor.h"

#include <cmath>

namespace midiforge
{

namespace
{
constexpr size_t reservedEngineEvents = 8192;
constexpr int reservedLongMessageBytes = 4096;
constexpr int largestShortMessage = 3;
constexpr juce::uint8 sysexStart = 0xF0;

const juce::StringArray syncModeNames { "Free", "Tempo" };
constexpr int tempoSyncIndex = 1;
const juce::String middleDot = juce::String::fromUTF8 (" \xc2\xb7 ");

juce::StringArray namesOf (const char* const* names, int count)
{
    juce::StringArray result;
    for (int index = 0; index < count; ++index)
        result.add (names[index]);
    return result;
}

juce::StringArray lfoDivisionNames()
{
    juce::StringArray result;
    for (int index = 0; index < numLfoDivisions; ++index)
        result.add (lfoDivisions[index].name);
    return result;
}

/**
 * Fixed host-side names of the modulation targets, in target-number order: "None", "Slot 1 A" ...,
 * then "Slot 1 On/Off" ...
 */
juce::StringArray fixedModulationTargetNames()
{
    juce::StringArray result { "None" };
    for (int targetIndex = 1; targetIndex < numModulationTargets; ++targetIndex)
    {
        ModulationRoute route;
        route.targetIndex = targetIndex;
        const juce::String slotName = "Slot " + juce::String (route.targetSlotIndex() + 1) + " ";
        if (route.targetsSlotSwitch())
            result.add (slotName + "On/Off");
        else
            result.add (slotName +
                        juce::String (ParameterIds::valueSuffixes[route.targetParameterIndex()]).toUpperCase());
    }
    return result;
}

/** Turns the shown knob page of every slot into text for saving, e.g. "0,1,0,0,0,0,0,0". */
juce::String pagesToText (const std::array<std::atomic<int>, numSlots>& pages)
{
    juce::StringArray texts;
    for (const auto& page : pages)
        texts.add (juce::String (page.load()));
    return texts.joinIntoString (",");
}
} // namespace

MidiForgeProcessor::MidiForgeProcessor()
    : AudioProcessor (BusesProperties()), // MIDI only: no audio inputs or outputs
      parameters (*this, nullptr, "MidiForge", createParameterLayout())
{
    // Seed the randomizer modules and LFOs differently in every session, then build the engine.
    setRandomSeedForNewModules ((unsigned) juce::Time::currentTimeMillis());
    engine = std::make_unique<MidiForgeEngine>();

    for (int slotIndex = 0; slotIndex < numSlots; ++slotIndex)
    {
        auto& values = slotParameterValues[(size_t) slotIndex];
        values.moduleType = parameters.getRawParameterValue (ParameterIds::moduleType (slotIndex));
        values.isEnabled = parameters.getRawParameterValue (ParameterIds::slotEnabled (slotIndex));
        for (int parameterIndex = 0; parameterIndex < numParametersPerSlot; ++parameterIndex)
            values.values[parameterIndex] =
                parameters.getRawParameterValue (ParameterIds::moduleParameter (slotIndex, parameterIndex));
    }

    for (int lfoIndex = 0; lfoIndex < numLfos; ++lfoIndex)
    {
        auto& values = lfoParameterValues[(size_t) lfoIndex];
        values.waveform = parameters.getRawParameterValue (ParameterIds::lfoWaveform (lfoIndex));
        values.syncMode = parameters.getRawParameterValue (ParameterIds::lfoSyncMode (lfoIndex));
        values.rateHz = parameters.getRawParameterValue (ParameterIds::lfoRate (lfoIndex));
        values.division = parameters.getRawParameterValue (ParameterIds::lfoDivision (lfoIndex));
        values.phaseDegrees = parameters.getRawParameterValue (ParameterIds::lfoPhase (lfoIndex));
        values.pulseWidthPercent = parameters.getRawParameterValue (ParameterIds::lfoPulseWidth (lfoIndex));
        values.restartsOnKey = parameters.getRawParameterValue (ParameterIds::lfoRestartOnKey (lfoIndex));
    }

    for (int routeIndex = 0; routeIndex < numModulationRoutes; ++routeIndex)
    {
        auto& values = routeParameterValues[(size_t) routeIndex];
        values.source = parameters.getRawParameterValue (ParameterIds::routeSource (routeIndex));
        values.target = parameters.getRawParameterValue (ParameterIds::routeTarget (routeIndex));
        values.amountPercent = parameters.getRawParameterValue (ParameterIds::routeAmount (routeIndex));
        values.polarity = parameters.getRawParameterValue (ParameterIds::routePolarity (routeIndex));
    }

    for (int randomIndex = 0; randomIndex < numNoteRandoms; ++randomIndex)
    {
        auto& values = noteRandomParameterValues[(size_t) randomIndex];
        values.chordWindowMilliseconds = parameters.getRawParameterValue (ParameterIds::noteRandomWindow (randomIndex));
        values.trigger = parameters.getRawParameterValue (ParameterIds::noteRandomTrigger (randomIndex));
        values.steps = parameters.getRawParameterValue (ParameterIds::noteRandomSteps (randomIndex));
    }

    for (int stepIndex = 0; stepIndex < numStepModulators; ++stepIndex)
    {
        auto& values = stepModulatorParameterValues[(size_t) stepIndex];
        values.numSteps = parameters.getRawParameterValue (ParameterIds::stepCount (stepIndex));
        values.division = parameters.getRawParameterValue (ParameterIds::stepRate (stepIndex));
        values.glidePercent = parameters.getRawParameterValue (ParameterIds::stepGlide (stepIndex));
        values.restartsOnKey = parameters.getRawParameterValue (ParameterIds::stepRestartOnKey (stepIndex));
        for (int step = 0; step < maximumSteps; ++step)
            values.stepValuesPercent[step] =
                parameters.getRawParameterValue (ParameterIds::stepValue (stepIndex, step));
        for (int step = 1; step < maximumSteps; ++step)
            values.isTied[step] = parameters.getRawParameterValue (ParameterIds::stepTie (stepIndex, step));
    }
    for (auto& readout : stepModulatorValueReadouts)
        readout.store (0.0f);
    for (auto& readout : currentStepReadouts)
        readout.store (0);

    for (auto& readout : modulatedValueReadouts)
        readout.store (0.0f);
    for (auto& readout : modulatedSwitchReadouts)
        readout.store (true);
    for (auto& readout : lfoValueReadouts)
        readout.store (0.0f);
    for (auto& readout : lfoPhaseReadouts)
        readout.store (0.0f);
    for (auto& readout : noteRandomValueReadouts)
        readout.store (0.0f);
    for (auto& page : shownPages)
        page.store (0);

    rememberValuesOfCurrentPreset(); // the "Init" state everything starts with

    engineEvents.reserve (reservedEngineEvents);
    longMessagesPassedThrough.ensureSize (reservedLongMessageBytes);
}

juce::AudioProcessorValueTreeState::ParameterLayout MidiForgeProcessor::createParameterLayout()
{
    // The order matters to hosts that list parameters by index: new parameters are only ever appended.
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    addSlotParameters (layout);
    addLfoParameters (layout);
    addRouteParameters (layout);
    addNoteRandomAndPolarityParameters (layout);
    addSecondPageSlotParameters (layout);
    addStepModulatorParameters (layout);
    addStepTieParameters (layout);
    return layout;
}

void MidiForgeProcessor::addSlotParameters (juce::AudioProcessorValueTreeState::ParameterLayout& layout)
{
    juce::StringArray moduleNames;
    for (int type = 0; type < numModuleTypes; ++type)
        moduleNames.add (getModuleDescription (type).name);

    for (int slotIndex = 0; slotIndex < numSlots; ++slotIndex)
    {
        const juce::String slotName = "Slot " + juce::String (slotIndex + 1);
        auto group = std::make_unique<juce::AudioProcessorParameterGroup> ("slot" + juce::String (slotIndex + 1),
                                                                           slotName, " | ");

        group->addChild (std::make_unique<juce::AudioParameterChoice> (
            juce::ParameterID { ParameterIds::moduleType (slotIndex), ParameterIds::slotParameterVersion },
            slotName + " Module", moduleNames, 0));

        group->addChild (std::make_unique<juce::AudioParameterBool> (
            juce::ParameterID { ParameterIds::slotEnabled (slotIndex), ParameterIds::slotParameterVersion },
            slotName + " On", true));

        for (int parameterIndex = 0; parameterIndex < numParametersPerPage; ++parameterIndex)
            group->addChild (createModuleParameter (slotIndex, parameterIndex, ParameterIds::slotParameterVersion));
        layout.add (std::move (group));
    }
}

std::unique_ptr<juce::AudioParameterFloat> MidiForgeProcessor::createModuleParameter (int slotIndex, int parameterIndex,
                                                                                      int version)
{
    // The host shows these values in the units of whichever module is loaded in the slot.
    auto attributes = juce::AudioParameterFloatAttributes()
                          .withStringFromValueFunction (
                              [this, slotIndex, parameterIndex] (float normalisedValue, int)
                              {
                                  const auto& description =
                                      getModuleDescription (getModuleType (slotIndex)).parameters[parameterIndex];
                                  return juce::String (formatParameterValue (description, normalisedValue));
                              })
                          .withValueFromStringFunction (
                              [this, slotIndex, parameterIndex] (const juce::String& text)
                              {
                                  const auto& description =
                                      getModuleDescription (getModuleType (slotIndex)).parameters[parameterIndex];
                                  return parseParameterText (description, text.trim().toStdString(),
                                                             getParameterValue (slotIndex, parameterIndex));
                              });

    return std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParameterIds::moduleParameter (slotIndex, parameterIndex), version },
        "Slot " + juce::String (slotIndex + 1) + " " +
            juce::String (ParameterIds::valueSuffixes[parameterIndex]).toUpperCase(),
        juce::NormalisableRange<float> (0.0f, 1.0f), 0.0f, attributes);
}

void MidiForgeProcessor::addSecondPageSlotParameters (juce::AudioProcessorValueTreeState::ParameterLayout& layout)
{
    for (int slotIndex = 0; slotIndex < numSlots; ++slotIndex)
    {
        auto group = std::make_unique<juce::AudioProcessorParameterGroup> (
            "slot" + juce::String (slotIndex + 1) + "page2", "Slot " + juce::String (slotIndex + 1) + " Page 2", " | ");
        for (int parameterIndex = numParametersPerPage; parameterIndex < numParametersPerSlot; ++parameterIndex)
            group->addChild (
                createModuleParameter (slotIndex, parameterIndex, ParameterIds::secondPageParameterVersion));
        layout.add (std::move (group));
    }
}

void MidiForgeProcessor::addLfoParameters (juce::AudioProcessorValueTreeState::ParameterLayout& layout)
{
    constexpr int version = ParameterIds::modulationParameterVersion;
    for (int lfoIndex = 0; lfoIndex < numLfos; ++lfoIndex)
    {
        const juce::String lfoName = "LFO " + juce::String (lfoIndex + 1);
        auto group =
            std::make_unique<juce::AudioProcessorParameterGroup> ("lfo" + juce::String (lfoIndex + 1), lfoName, " | ");

        group->addChild (std::make_unique<juce::AudioParameterChoice> (
            juce::ParameterID { ParameterIds::lfoWaveform (lfoIndex), version }, lfoName + " Wave",
            namesOf (lfoWaveformNames, numLfoWaveforms), 0));

        group->addChild (std::make_unique<juce::AudioParameterChoice> (
            juce::ParameterID { ParameterIds::lfoSyncMode (lfoIndex), version }, lfoName + " Sync", syncModeNames,
            tempoSyncIndex));

        juce::NormalisableRange<float> rateRange (0.01f, 20.0f);
        rateRange.setSkewForCentre (1.0f); // half the knob covers 0.01-1 Hz, the other half 1-20 Hz
        group->addChild (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { ParameterIds::lfoRate (lfoIndex), version }, lfoName + " Rate", rateRange, 1.0f,
            juce::AudioParameterFloatAttributes().withLabel ("Hz").withStringFromValueFunction (
                [] (float hertz, int) { return juce::String (hertz, hertz < 1.0f ? 2 : 1) + " Hz"; })));

        group->addChild (std::make_unique<juce::AudioParameterChoice> (
            juce::ParameterID { ParameterIds::lfoDivision (lfoIndex), version }, lfoName + " Division",
            lfoDivisionNames(), defaultLfoDivisionIndex));

        group->addChild (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { ParameterIds::lfoPhase (lfoIndex), version }, lfoName + " Phase",
            juce::NormalisableRange<float> (0.0f, 360.0f, 1.0f), 0.0f,
            juce::AudioParameterFloatAttributes().withStringFromValueFunction (
                [] (float degrees, int)
                { return juce::String (juce::roundToInt (degrees)) + juce::String::fromUTF8 ("\xc2\xb0"); })));

        group->addChild (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { ParameterIds::lfoPulseWidth (lfoIndex), version }, lfoName + " Pulse Width",
            juce::NormalisableRange<float> (5.0f, 95.0f, 1.0f), 50.0f,
            juce::AudioParameterFloatAttributes().withStringFromValueFunction (
                [] (float percent, int) { return juce::String (juce::roundToInt (percent)) + "%"; })));

        group->addChild (std::make_unique<juce::AudioParameterBool> (
            juce::ParameterID { ParameterIds::lfoRestartOnKey (lfoIndex), version }, lfoName + " Restart On Key",
            false));

        layout.add (std::move (group));
    }
}

void MidiForgeProcessor::addRouteParameters (juce::AudioProcessorValueTreeState::ParameterLayout& layout)
{
    constexpr int version = ParameterIds::modulationParameterVersion;
    // LFO 1..4 first, then Random 1..4 (added in 0.6.0, appended so stored indices keep their meaning).
    juce::StringArray sourceNames;
    for (int lfoIndex = 0; lfoIndex < numLfos; ++lfoIndex)
        sourceNames.add ("LFO " + juce::String (lfoIndex + 1));
    for (int randomIndex = 0; randomIndex < numNoteRandoms; ++randomIndex)
        sourceNames.add ("Random " + juce::String (randomIndex + 1));
    for (int stepIndex = 0; stepIndex < numStepModulators; ++stepIndex) // added in 0.14.0
        sourceNames.add ("Steps " + juce::String (stepIndex + 1));
    const juce::StringArray targetNames = fixedModulationTargetNames();

    for (int routeIndex = 0; routeIndex < numModulationRoutes; ++routeIndex)
    {
        const juce::String routeName = "Route " + juce::String (routeIndex + 1);
        auto group = std::make_unique<juce::AudioProcessorParameterGroup> ("route" + juce::String (routeIndex + 1),
                                                                           routeName, " | ");

        group->addChild (std::make_unique<juce::AudioParameterChoice> (
            juce::ParameterID { ParameterIds::routeSource (routeIndex), version }, routeName + " Source", sourceNames,
            0));

        // The host shows the target with the name of the module parameter, e.g. "Slot 2 · Gate".
        auto targetAttributes =
            juce::AudioParameterChoiceAttributes()
                .withStringFromValueFunction ([this] (int targetIndex, int)
                                              { return getModulationTargetName (targetIndex); })
                .withValueFromStringFunction (
                    [this, targetNames] (const juce::String& text)
                    {
                        for (int targetIndex = 0; targetIndex < numModulationTargets; ++targetIndex)
                            if (text == targetNames[targetIndex] || text == getModulationTargetName (targetIndex))
                                return targetIndex;
                        return 0;
                    });
        group->addChild (std::make_unique<juce::AudioParameterChoice> (
            juce::ParameterID { ParameterIds::routeTarget (routeIndex), version }, routeName + " Target", targetNames,
            0, targetAttributes));

        group->addChild (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { ParameterIds::routeAmount (routeIndex), version }, routeName + " Amount",
            juce::NormalisableRange<float> (-100.0f, 100.0f, 1.0f), 50.0f,
            juce::AudioParameterFloatAttributes().withStringFromValueFunction (
                [] (float percent, int)
                {
                    const int rounded = juce::roundToInt (percent);
                    return (rounded > 0 ? "+" : "") + juce::String (rounded) + "%";
                })));

        layout.add (std::move (group));
    }
}

void MidiForgeProcessor::addNoteRandomAndPolarityParameters (
    juce::AudioProcessorValueTreeState::ParameterLayout& layout)
{
    constexpr int version = ParameterIds::noteRandomParameterVersion;
    juce::StringArray stepNames { "Smooth" };
    for (int steps = minimumNoteRandomSteps; steps <= maximumNoteRandomSteps; ++steps)
        stepNames.add (juce::String (steps));

    for (int randomIndex = 0; randomIndex < numNoteRandoms; ++randomIndex)
    {
        const juce::String randomName = "Random " + juce::String (randomIndex + 1);
        auto group = std::make_unique<juce::AudioProcessorParameterGroup> ("random" + juce::String (randomIndex + 1),
                                                                           randomName, " | ");
        group->addChild (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { ParameterIds::noteRandomWindow (randomIndex), version }, randomName + " Chord Window",
            juce::NormalisableRange<float> (0.0f, (float) maximumChordWindowMilliseconds, 1.0f), 30.0f,
            juce::AudioParameterFloatAttributes().withStringFromValueFunction (
                [] (float milliseconds, int) { return juce::String (juce::roundToInt (milliseconds)) + " ms"; })));
        group->addChild (std::make_unique<juce::AudioParameterChoice> (
            juce::ParameterID { ParameterIds::noteRandomTrigger (randomIndex), version }, randomName + " New Value On",
            juce::StringArray { "Every note", "Phrase" }, 0));
        group->addChild (std::make_unique<juce::AudioParameterChoice> (
            juce::ParameterID { ParameterIds::noteRandomSteps (randomIndex), version }, randomName + " Steps",
            stepNames, 0));
        layout.add (std::move (group));
    }

    auto polarityGroup = std::make_unique<juce::AudioProcessorParameterGroup> ("polarity", "Route Polarity", " | ");
    for (int routeIndex = 0; routeIndex < numModulationRoutes; ++routeIndex)
        polarityGroup->addChild (std::make_unique<juce::AudioParameterChoice> (
            juce::ParameterID { ParameterIds::routePolarity (routeIndex), version },
            "Route " + juce::String (routeIndex + 1) + " Polarity", juce::StringArray { "Bipolar", "Unipolar" }, 0));
    layout.add (std::move (polarityGroup));
}

void MidiForgeProcessor::addStepModulatorParameters (juce::AudioProcessorValueTreeState::ParameterLayout& layout)
{
    constexpr int version = ParameterIds::stepModulatorParameterVersion;
    juce::StringArray rateNames;
    for (int divisionIndex = 0; divisionIndex < numStepDivisions; ++divisionIndex)
        rateNames.add (stepDivisions[divisionIndex].name);
    const auto percentText = [] (float percent, int)
    {
        const int rounded = juce::roundToInt (percent);
        return (rounded > 0 ? "+" : "") + juce::String (rounded);
    };

    for (int stepIndex = 0; stepIndex < numStepModulators; ++stepIndex)
    {
        const juce::String name = "Steps " + juce::String (stepIndex + 1);
        auto group =
            std::make_unique<juce::AudioProcessorParameterGroup> ("steps" + juce::String (stepIndex + 1), name, " | ");
        group->addChild (std::make_unique<juce::AudioParameterInt> (
            juce::ParameterID { ParameterIds::stepCount (stepIndex), version }, name + " Count", minimumSteps,
            maximumSteps, maximumSteps));
        group->addChild (std::make_unique<juce::AudioParameterChoice> (
            juce::ParameterID { ParameterIds::stepRate (stepIndex), version }, name + " Rate", rateNames,
            defaultStepDivisionIndex));
        group->addChild (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { ParameterIds::stepGlide (stepIndex), version }, name + " Glide",
            juce::NormalisableRange<float> (0.0f, 100.0f, 1.0f), 0.0f,
            juce::AudioParameterFloatAttributes().withStringFromValueFunction (
                [] (float percent, int)
                { return percent < 0.5f ? juce::String ("Off") : juce::String (juce::roundToInt (percent)) + "%"; })));
        group->addChild (std::make_unique<juce::AudioParameterBool> (
            juce::ParameterID { ParameterIds::stepRestartOnKey (stepIndex), version }, name + " Restart On Key",
            false));
        for (int step = 0; step < maximumSteps; ++step)
            group->addChild (std::make_unique<juce::AudioParameterFloat> (
                juce::ParameterID { ParameterIds::stepValue (stepIndex, step), version },
                name + " Step " + juce::String (step + 1), juce::NormalisableRange<float> (-100.0f, 100.0f, 1.0f), 0.0f,
                juce::AudioParameterFloatAttributes().withStringFromValueFunction (percentText)));
        layout.add (std::move (group));
    }
}

void MidiForgeProcessor::addStepTieParameters (juce::AudioProcessorValueTreeState::ParameterLayout& layout)
{
    constexpr int version = ParameterIds::stepTieParameterVersion;
    for (int stepIndex = 0; stepIndex < numStepModulators; ++stepIndex)
    {
        const juce::String name = "Steps " + juce::String (stepIndex + 1);
        auto group = std::make_unique<juce::AudioProcessorParameterGroup> ("stepties" + juce::String (stepIndex + 1),
                                                                           name + " Ties", " | ");
        for (int step = 1; step < maximumSteps; ++step)
            group->addChild (std::make_unique<juce::AudioParameterBool> (
                juce::ParameterID { ParameterIds::stepTie (stepIndex, step), version },
                name + " Tie " + juce::String (step + 1), false));
        layout.add (std::move (group));
    }
}

//==============================================================================
// Audio thread

void MidiForgeProcessor::prepareToPlay (double sampleRate, int /*maximumBlockSize*/)
{
    engine->prepare (sampleRate);
}

void MidiForgeProcessor::releaseResources()
{
    engine->reset();
}

bool MidiForgeProcessor::isBusesLayoutSupported (const BusesLayout& /*layouts*/) const
{
    return true; // no audio is processed, so any layout a host insists on is fine
}

void MidiForgeProcessor::reportLatencyOfModules (double sampleRate)
{
    // Modules that delay everything by a fixed time: Groove (Early) and Strum (Window). Delays of several
    // switched-on modules add up.
    int totalDelayMilliseconds = 0;
    for (const auto& slot : engineSettings.slots)
    {
        if (! slot.isEnabled)
            continue;
        int parameterIndex = -1;
        if (slot.moduleType == (int) ModuleType::Groove)
            parameterIndex = grooveEarlyParameterIndex;
        else if (slot.moduleType == (int) ModuleType::Strum)
            parameterIndex = strumWindowParameterIndex;
        if (parameterIndex >= 0)
            totalDelayMilliseconds +=
                normalisedToInteger (getModuleDescription (slot.moduleType).parameters[parameterIndex],
                                     slot.parameterValues[parameterIndex]);
    }

    const int requiredLatency = (int) std::lround (totalDelayMilliseconds / 1000.0 * sampleRate);
    if (requiredLatency != getLatencySamples())
        setLatencySamples (requiredLatency); // the host is told asynchronously and compensates the track
}

void MidiForgeProcessor::readEngineSettings()
{
    for (int slotIndex = 0; slotIndex < numSlots; ++slotIndex)
    {
        const auto& values = slotParameterValues[(size_t) slotIndex];
        auto& settings = engineSettings.slots[slotIndex];
        settings.moduleType = getModuleType (slotIndex);
        settings.isEnabled = values.isEnabled->load() >= 0.5f;
        for (int parameterIndex = 0; parameterIndex < numParametersPerSlot; ++parameterIndex)
            settings.parameterValues[parameterIndex] = juce::jlimit (0.0f, 1.0f, values.values[parameterIndex]->load());
    }

    for (int lfoIndex = 0; lfoIndex < numLfos; ++lfoIndex)
        engineSettings.lfos[lfoIndex] = getLfoSettings (lfoIndex);
    for (int randomIndex = 0; randomIndex < numNoteRandoms; ++randomIndex)
        engineSettings.noteRandoms[randomIndex] = getNoteRandomSettings (randomIndex);
    for (int stepIndex = 0; stepIndex < numStepModulators; ++stepIndex)
        engineSettings.stepModulators[stepIndex] = getStepModulatorSettings (stepIndex);

    for (int routeIndex = 0; routeIndex < numModulationRoutes; ++routeIndex)
    {
        const auto& values = routeParameterValues[(size_t) routeIndex];
        auto& route = engineSettings.routes[routeIndex];
        route.sourceIndex = juce::jlimit (0, numModulationSources - 1, (int) std::lround (values.source->load()));
        route.isUnipolar = values.polarity->load() >= 0.5f;
        route.targetIndex = juce::jlimit (0, numModulationTargets - 1, (int) std::lround (values.target->load()));
        route.amount = juce::jlimit (-1.0f, 1.0f, values.amountPercent->load() / 100.0f);
    }
}

BlockContext MidiForgeProcessor::readBlockContext (int numSamples)
{
    BlockContext context;
    context.sampleRate = getSampleRate() > 0.0 ? getSampleRate() : 44100.0;
    context.numSamples = std::max (1, numSamples);

    if (auto* hostPlayHead = getPlayHead())
    {
        if (auto position = hostPlayHead->getPosition())
        {
            if (auto bpm = position->getBpm(); bpm && std::isfinite (*bpm) && *bpm > 0.0)
                context.tempoBpm = juce::jlimit (10.0, 1000.0, *bpm); // guard against nonsense from hosts
            context.isPlaying = position->getIsPlaying();
            if (auto ppq = position->getPpqPosition(); ppq && std::isfinite (*ppq))
            {
                context.hasPpqPosition = true;
                context.ppqPositionAtBlockStart = *ppq;
            }
        }
    }
    return context;
}

void MidiForgeProcessor::processBlock (juce::AudioBuffer<float>& audio, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    audio.clear();
    lastBlockTimeMilliseconds.store (juce::Time::getMillisecondCounter(), std::memory_order_relaxed);

    readEngineSettings();
    const BlockContext context = readBlockContext (audio.getNumSamples());
    reportLatencyOfModules (context.sampleRate);

    // Split the incoming MIDI: short messages go through the engine, long ones pass untouched.
    engineEvents.clear();
    longMessagesPassedThrough.clear();
    for (const auto metadata : midi)
    {
        const int numBytes = metadata.numBytes;
        if (numBytes >= 1 && numBytes <= largestShortMessage && metadata.data[0] != sysexStart &&
            engineEvents.size() < engineEvents.capacity())
        {
            MidiEvent event;
            event.samplePosition = juce::jlimit (0, context.numSamples - 1, metadata.samplePosition);
            event.status = metadata.data[0];
            event.data1 = numBytes > 1 ? metadata.data[1] : 0;
            event.data2 = numBytes > 2 ? metadata.data[2] : 0;
            event.numBytes = (uint8_t) numBytes;
            engineEvents.push_back (event);
        }
        else
        {
            longMessagesPassedThrough.addEvent (metadata.data, numBytes, metadata.samplePosition);
        }
    }

    engine->process (engineEvents, context, engineSettings);
    publishReadoutsForEditor();

    midi.clear();
    for (const auto& event : engineEvents)
    {
        const juce::uint8 bytes[largestShortMessage] = { event.status, event.data1, event.data2 };
        midi.addEvent (bytes, event.numBytes, event.samplePosition);
    }
    midi.addEvents (longMessagesPassedThrough, 0, -1, 0);
}

void MidiForgeProcessor::publishReadoutsForEditor()
{
    const SlotSettings* modulatedSlots = engine->getLastModulatedSlots();
    for (int slotIndex = 0; slotIndex < numSlots; ++slotIndex)
        for (int parameterIndex = 0; parameterIndex < numParametersPerSlot; ++parameterIndex)
            modulatedValueReadouts[(size_t) (slotIndex * numParametersPerSlot + parameterIndex)].store (
                modulatedSlots[slotIndex].parameterValues[parameterIndex], std::memory_order_relaxed);
    for (int slotIndex = 0; slotIndex < numSlots; ++slotIndex)
        modulatedSwitchReadouts[(size_t) slotIndex].store (modulatedSlots[slotIndex].isEnabled,
                                                           std::memory_order_relaxed);

    for (int lfoIndex = 0; lfoIndex < numLfos; ++lfoIndex)
    {
        lfoValueReadouts[(size_t) lfoIndex].store (engine->getLastLfoValues()[lfoIndex], std::memory_order_relaxed);
        lfoPhaseReadouts[(size_t) lfoIndex].store ((float) engine->getLastLfoPhases()[lfoIndex],
                                                   std::memory_order_relaxed);
    }
    for (int randomIndex = 0; randomIndex < numNoteRandoms; ++randomIndex)
        noteRandomValueReadouts[(size_t) randomIndex].store (engine->getNoteRandomValue (randomIndex),
                                                             std::memory_order_relaxed);
    for (int stepIndex = 0; stepIndex < numStepModulators; ++stepIndex)
    {
        stepModulatorValueReadouts[(size_t) stepIndex].store (engine->getLastStepModulatorValues()[stepIndex],
                                                              std::memory_order_relaxed);
        currentStepReadouts[(size_t) stepIndex].store (engine->getLastSteps()[stepIndex], std::memory_order_relaxed);
    }
}

//==============================================================================
// Modulation access

LfoSettings MidiForgeProcessor::getLfoSettings (int lfoIndex) const noexcept
{
    const auto& values = lfoParameterValues[(size_t) lfoIndex];
    LfoSettings settings;
    settings.waveform =
        (LfoWaveform) juce::jlimit (0, numLfoWaveforms - 1, (int) std::lround (values.waveform->load()));
    settings.isTempoSynced = std::lround (values.syncMode->load()) == tempoSyncIndex;
    settings.rateHz = juce::jlimit (0.01, 20.0, (double) values.rateHz->load());
    settings.divisionIndex = juce::jlimit (0, numLfoDivisions - 1, (int) std::lround (values.division->load()));
    settings.phaseDegrees = juce::jlimit (0.0, 360.0, (double) values.phaseDegrees->load());
    settings.pulseWidth = juce::jlimit (0.05, 0.95, (double) values.pulseWidthPercent->load() / 100.0);
    settings.restartsOnFirstKey = values.restartsOnKey->load() >= 0.5f;
    return settings;
}

NoteRandomSettings MidiForgeProcessor::getNoteRandomSettings (int randomIndex) const noexcept
{
    const auto& values = noteRandomParameterValues[(size_t) randomIndex];
    NoteRandomSettings settings;
    settings.chordWindowMilliseconds =
        juce::jlimit (0.0, maximumChordWindowMilliseconds, (double) values.chordWindowMilliseconds->load());
    settings.trigger =
        std::lround (values.trigger->load()) == 1 ? NoteRandomTrigger::FirstNoteOfPhrase : NoteRandomTrigger::EveryNote;
    // Choice 0 is "Smooth", choice n is n + 1 steps.
    const int stepsChoice = juce::jlimit (0, maximumNoteRandomSteps - 1, (int) std::lround (values.steps->load()));
    settings.numSteps = stepsChoice == 0 ? 0 : stepsChoice + 1;
    return settings;
}

StepModulatorSettings MidiForgeProcessor::getStepModulatorSettings (int stepModulatorIndex) const noexcept
{
    const auto& values = stepModulatorParameterValues[(size_t) stepModulatorIndex];
    StepModulatorSettings settings;
    settings.numSteps = juce::jlimit (minimumSteps, maximumSteps, (int) std::lround (values.numSteps->load()));
    settings.divisionIndex = juce::jlimit (0, numStepDivisions - 1, (int) std::lround (values.division->load()));
    settings.glide = juce::jlimit (0.0, 1.0, (double) values.glidePercent->load() / 100.0);
    settings.restartsOnFirstKey = values.restartsOnKey->load() >= 0.5f;
    for (int step = 0; step < maximumSteps; ++step)
        settings.stepValues[step] = juce::jlimit (-1.0f, 1.0f, values.stepValuesPercent[step]->load() / 100.0f);
    for (int step = 1; step < maximumSteps; ++step)
        settings.isTied[step] = values.isTied[step]->load() >= 0.5f;
    return settings;
}

bool MidiForgeProcessor::isSourceInUse (int sourceIndex) const noexcept
{
    for (const auto& values : routeParameterValues)
    {
        const int target = (int) std::lround (values.target->load());
        if ((int) std::lround (values.source->load()) == sourceIndex && target > 0 &&
            std::abs (values.amountPercent->load()) / 100.0f >= ModulationRoute::smallestEffectiveAmount)
            return true;
    }
    return false;
}

bool MidiForgeProcessor::isSlotSwitchModulated (int slotIndex) const noexcept
{
    return isParameterModulated (slotIndex, slotSwitchParameterIndex);
}

bool MidiForgeProcessor::isSlotEnabledAfterModulation (int slotIndex) const noexcept
{
    return modulatedSwitchReadouts[(size_t) slotIndex].load (std::memory_order_relaxed);
}

bool MidiForgeProcessor::isParameterModulated (int slotIndex, int parameterIndex) const noexcept
{
    const int targetIndex = modulationTargetIndex (slotIndex, parameterIndex);
    for (const auto& values : routeParameterValues)
        if ((int) std::lround (values.target->load()) == targetIndex &&
            std::abs (values.amountPercent->load()) / 100.0f >= ModulationRoute::smallestEffectiveAmount)
            return true;
    return false;
}

float MidiForgeProcessor::getModulatedParameterValue (int slotIndex, int parameterIndex) const noexcept
{
    return modulatedValueReadouts[(size_t) (slotIndex * numParametersPerSlot + parameterIndex)].load (
        std::memory_order_relaxed);
}

juce::String MidiForgeProcessor::getModulationTargetName (int targetIndex) const
{
    if (targetIndex <= 0 || targetIndex >= numModulationTargets)
        return "None";

    ModulationRoute route;
    route.targetIndex = targetIndex;
    const int slotIndex = route.targetSlotIndex();
    const int parameterIndex = route.targetParameterIndex();
    const juce::String slotName = "Slot " + juce::String (slotIndex + 1);
    if (route.targetsSlotSwitch())
        return slotName + middleDot + "On/Off";

    const auto& description = getModuleDescription (getModuleType (slotIndex));
    const auto& parameter = description.parameters[parameterIndex];
    if (parameter.isUsed())
        return slotName + middleDot + parameter.name;

    const juce::String letter = juce::String (ParameterIds::valueSuffixes[parameterIndex]).toUpperCase();
    return slotName + middleDot + letter + " (unused)";
}

//==============================================================================
// Slot access

int MidiForgeProcessor::getModuleType (int slotIndex) const noexcept
{
    const float rawValue = slotParameterValues[(size_t) slotIndex].moduleType->load();
    return juce::jlimit (0, numModuleTypes - 1, (int) std::lround (rawValue));
}

bool MidiForgeProcessor::isSlotEnabled (int slotIndex) const noexcept
{
    return slotParameterValues[(size_t) slotIndex].isEnabled->load() >= 0.5f;
}

float MidiForgeProcessor::getParameterValue (int slotIndex, int parameterIndex) const noexcept
{
    return slotParameterValues[(size_t) slotIndex].values[parameterIndex]->load();
}

void MidiForgeProcessor::setParameterAsGesture (const juce::String& parameterId, float normalisedValue)
{
    if (auto* parameter = parameters.getParameter (parameterId))
    {
        parameter->beginChangeGesture();
        parameter->setValueNotifyingHost (juce::jlimit (0.0f, 1.0f, normalisedValue));
        parameter->endChangeGesture();
    }
}

void MidiForgeProcessor::loadModule (int slotIndex, int moduleType)
{
    moduleType = juce::jlimit (0, numModuleTypes - 1, moduleType);
    if (auto* typeParameter = parameters.getParameter (ParameterIds::moduleType (slotIndex)))
        setParameterAsGesture (ParameterIds::moduleType (slotIndex), typeParameter->convertTo0to1 ((float) moduleType));

    const auto& description = getModuleDescription (moduleType);
    for (int parameterIndex = 0; parameterIndex < numParametersPerSlot; ++parameterIndex)
        setParameterAsGesture (ParameterIds::moduleParameter (slotIndex, parameterIndex),
                               getNormalisedDefault (description.parameters[parameterIndex]));
    shownPages[(size_t) slotIndex].store (0); // a new module starts on its first page
}

MidiForgeProcessor::SlotContents MidiForgeProcessor::copySlotContents (int slotIndex) const
{
    SlotContents contents;
    contents.moduleTypeValue = parameters.getParameter (ParameterIds::moduleType (slotIndex))->getValue();
    contents.enabledValue = parameters.getParameter (ParameterIds::slotEnabled (slotIndex))->getValue();
    for (int parameterIndex = 0; parameterIndex < numParametersPerSlot; ++parameterIndex)
        contents.parameterValues[(size_t) parameterIndex] =
            parameters.getParameter (ParameterIds::moduleParameter (slotIndex, parameterIndex))->getValue();
    contents.shownPage = shownPages[(size_t) slotIndex].load();
    return contents;
}

void MidiForgeProcessor::setSlotContents (int slotIndex, const SlotContents& contents)
{
    setParameterAsGesture (ParameterIds::moduleType (slotIndex), contents.moduleTypeValue);
    setParameterAsGesture (ParameterIds::slotEnabled (slotIndex), contents.enabledValue);
    for (int parameterIndex = 0; parameterIndex < numParametersPerSlot; ++parameterIndex)
        setParameterAsGesture (ParameterIds::moduleParameter (slotIndex, parameterIndex),
                               contents.parameterValues[(size_t) parameterIndex]);
    shownPages[(size_t) slotIndex].store (contents.shownPage);
}

int MidiForgeProcessor::getRouteTarget (int routeIndex) const noexcept
{
    return (int) std::lround (parameters.getRawParameterValue (ParameterIds::routeTarget (routeIndex))->load());
}

void MidiForgeProcessor::moveRouteTargets (const std::array<int, numSlots>& newSlotOfOldSlot)
{
    for (int routeIndex = 0; routeIndex < numModulationRoutes; ++routeIndex)
    {
        ModulationRoute route;
        route.targetIndex = getRouteTarget (routeIndex);
        if (route.targetIndex <= 0 || route.targetIndex >= numModulationTargets)
            continue;
        const int newSlot = newSlotOfOldSlot[(size_t) route.targetSlotIndex()];
        const int newTarget = newSlot < 0 ? 0 : modulationTargetIndex (newSlot, route.targetParameterIndex());
        if (newTarget != route.targetIndex)
            setPlainParameterValue (ParameterIds::routeTarget (routeIndex), (float) newTarget);
    }
}

void MidiForgeProcessor::swapSlots (int firstSlotIndex, int secondSlotIndex)
{
    const auto isValid = [] (int slotIndex) { return slotIndex >= 0 && slotIndex < numSlots; };
    if (firstSlotIndex == secondSlotIndex || ! isValid (firstSlotIndex) || ! isValid (secondSlotIndex))
        return;

    const auto firstContents = copySlotContents (firstSlotIndex);
    setSlotContents (firstSlotIndex, copySlotContents (secondSlotIndex));
    setSlotContents (secondSlotIndex, firstContents);

    std::array<int, numSlots> newSlotOfOldSlot {};
    for (int slotIndex = 0; slotIndex < numSlots; ++slotIndex)
        newSlotOfOldSlot[(size_t) slotIndex] = slotIndex;
    std::swap (newSlotOfOldSlot[(size_t) firstSlotIndex], newSlotOfOldSlot[(size_t) secondSlotIndex]);
    moveRouteTargets (newSlotOfOldSlot);
}

void MidiForgeProcessor::removeModule (int slotIndex)
{
    if (slotIndex < 0 || slotIndex >= numSlots || getModuleType (slotIndex) == 0)
        return;

    RemovedModule removed;
    removed.slotIndex = slotIndex;
    removed.contents = copySlotContents (slotIndex);
    for (int routeIndex = 0; routeIndex < numModulationRoutes; ++routeIndex)
    {
        ModulationRoute route;
        route.targetIndex = getRouteTarget (routeIndex);
        const bool targetsRemovedSlot =
            route.targetIndex > 0 && route.targetIndex < numModulationTargets && route.targetSlotIndex() == slotIndex;
        removed.targetsOfRoutesToIt[(size_t) routeIndex] = targetsRemovedSlot ? route.targetIndex : 0;
    }

    // Close the gap: every module below moves up one slot; the last slot becomes empty.
    for (int targetSlot = slotIndex; targetSlot < numSlots - 1; ++targetSlot)
        setSlotContents (targetSlot, copySlotContents (targetSlot + 1));
    clearSlot (numSlots - 1);

    std::array<int, numSlots> newSlotOfOldSlot {};
    for (int oldSlot = 0; oldSlot < numSlots; ++oldSlot)
        newSlotOfOldSlot[(size_t) oldSlot] = oldSlot < slotIndex ? oldSlot : (oldSlot == slotIndex ? -1 : oldSlot - 1);
    moveRouteTargets (newSlotOfOldSlot);

    lastRemovedModule = removed;
    ++moduleRemovalCount;
}

bool MidiForgeProcessor::canUndoModuleRemoval() const noexcept
{
    return lastRemovedModule.has_value() && getModuleType (numSlots - 1) == 0;
}

void MidiForgeProcessor::undoModuleRemoval()
{
    if (! canUndoModuleRemoval())
        return;
    const auto removed = *lastRemovedModule;
    lastRemovedModule.reset();

    // Make room again: the modules from the removed slot on move down one slot (the last slot is empty).
    for (int targetSlot = numSlots - 1; targetSlot > removed.slotIndex; --targetSlot)
        setSlotContents (targetSlot, copySlotContents (targetSlot - 1));
    setSlotContents (removed.slotIndex, removed.contents);

    std::array<int, numSlots> newSlotOfOldSlot {};
    for (int oldSlot = 0; oldSlot < numSlots; ++oldSlot)
        newSlotOfOldSlot[(size_t) oldSlot] =
            oldSlot < removed.slotIndex ? oldSlot : (oldSlot == numSlots - 1 ? -1 : oldSlot + 1);
    moveRouteTargets (newSlotOfOldSlot);

    for (int routeIndex = 0; routeIndex < numModulationRoutes; ++routeIndex)
        if (removed.targetsOfRoutesToIt[(size_t) routeIndex] > 0)
            setPlainParameterValue (ParameterIds::routeTarget (routeIndex),
                                    (float) removed.targetsOfRoutesToIt[(size_t) routeIndex]);
}

juce::String MidiForgeProcessor::getLastRemovedModuleName() const
{
    if (! lastRemovedModule)
        return {};
    const auto* typeParameter = parameters.getParameter (ParameterIds::moduleType (0));
    const int type = (int) std::lround (typeParameter->convertFrom0to1 (lastRemovedModule->contents.moduleTypeValue));
    return getModuleDescription (type).name;
}

int MidiForgeProcessor::getShownPage (int slotIndex) const noexcept
{
    const int lastPage = getNumberOfParameterPages (getModuleDescription (getModuleType (slotIndex))) - 1;
    return juce::jlimit (0, lastPage, shownPages[(size_t) slotIndex].load());
}

void MidiForgeProcessor::setShownPage (int slotIndex, int page) noexcept
{
    shownPages[(size_t) slotIndex].store (juce::jlimit (0, numParameterPages - 1, page));
}

void MidiForgeProcessor::restoreShownPages (const juce::ValueTree& state)
{
    const auto texts = juce::StringArray::fromTokens (state.getProperty (shownPagesProperty).toString(), ",", "");
    for (int slotIndex = 0; slotIndex < numSlots; ++slotIndex)
        setShownPage (slotIndex, slotIndex < texts.size() ? texts[slotIndex].getIntValue() : 0);
}

juce::ValueTree MidiForgeProcessor::copyStateWithPages()
{
    auto state = parameters.copyState();
    state.setProperty (shownPagesProperty, pagesToText (shownPages), nullptr);
    state.setProperty (knobLayoutProperty, currentKnobLayoutVersion, nullptr);
    return state;
}

int MidiForgeProcessor::getKnobLayoutVersionOf (const juce::ValueTree& state)
{
    if (state.hasProperty (knobLayoutProperty))
        return juce::jlimit (0, currentKnobLayoutVersion, (int) state.getProperty (knobLayoutProperty));
    // 0.7.0 stored the shown pages and the parameters E-H, but no layout version yet.
    const bool isFrom070 = state.hasProperty (shownPagesProperty) ||
                           state.getChildWithProperty ("id", ParameterIds::moduleParameter (0, 4)).isValid();
    return isFrom070 ? 1 : 0;
}

void MidiForgeProcessor::resetKnobsAddedAfter (int savedKnobLayoutVersion)
{
    for (int slotIndex = 0; slotIndex < numSlots; ++slotIndex)
    {
        const auto& description = getModuleDescription (getModuleType (slotIndex));
        for (int parameterIndex = 0; parameterIndex < numParametersPerSlot; ++parameterIndex)
        {
            const auto& parameter = description.parameters[parameterIndex];
            if (parameter.isUsed() && parameter.addedInKnobLayout > savedKnobLayoutVersion)
                setParameterAsGesture (ParameterIds::moduleParameter (slotIndex, parameterIndex),
                                       getNormalisedDefault (parameter));
        }
    }
}

void MidiForgeProcessor::clearSlot (int slotIndex)
{
    loadModule (slotIndex, 0);
    setParameterAsGesture (ParameterIds::slotEnabled (slotIndex), 1.0f);
}

//==============================================================================
// Presets and state

int MidiForgeProcessor::getNumPrograms()
{
    return (int) getFactoryPresets().size();
}

const juce::String MidiForgeProcessor::getProgramName (int presetIndex)
{
    const auto& presets = getFactoryPresets();
    return juce::isPositiveAndBelow (presetIndex, (int) presets.size()) ? presets[(size_t) presetIndex].name : "";
}

void MidiForgeProcessor::setCurrentProgram (int presetIndex)
{
    const auto& presets = getFactoryPresets();
    if (! juce::isPositiveAndBelow (presetIndex, (int) presets.size()))
        return;

    currentPresetIndex = presetIndex;
    const auto& preset = presets[(size_t) presetIndex];
    applyPresetModulation (preset);
    for (int slotIndex = 0; slotIndex < numSlots; ++slotIndex)
    {
        if (slotIndex >= (int) preset.slots.size())
        {
            clearSlot (slotIndex);
            continue;
        }

        const auto& presetSlot = preset.slots[(size_t) slotIndex];
        loadModule (slotIndex, (int) presetSlot.moduleType);
        const auto& description = getModuleDescription (presetSlot.moduleType);
        for (int parameterIndex = 0; parameterIndex < numParametersPerSlot; ++parameterIndex)
            if (! std::isnan (presetSlot.valueOf (parameterIndex)))
                setParameterAsGesture (ParameterIds::moduleParameter (slotIndex, parameterIndex),
                                       plainValueToNormalised (description.parameters[parameterIndex],
                                                               presetSlot.valueOf (parameterIndex)));
        setParameterAsGesture (ParameterIds::slotEnabled (slotIndex), 1.0f);
    }

    currentPresetKind = PresetKind::factory;
    currentUserPresetName.clear();
    rememberValuesOfCurrentPreset();
}

void MidiForgeProcessor::resetModulationToDefaults()
{
    juce::StringArray ids;
    for (int lfoIndex = 0; lfoIndex < numLfos; ++lfoIndex)
        ids.addArray ({ ParameterIds::lfoWaveform (lfoIndex), ParameterIds::lfoSyncMode (lfoIndex),
                        ParameterIds::lfoRate (lfoIndex), ParameterIds::lfoDivision (lfoIndex),
                        ParameterIds::lfoPhase (lfoIndex), ParameterIds::lfoPulseWidth (lfoIndex),
                        ParameterIds::lfoRestartOnKey (lfoIndex) });
    for (int routeIndex = 0; routeIndex < numModulationRoutes; ++routeIndex)
        ids.addArray ({ ParameterIds::routeSource (routeIndex), ParameterIds::routeTarget (routeIndex),
                        ParameterIds::routeAmount (routeIndex), ParameterIds::routePolarity (routeIndex) });
    for (int randomIndex = 0; randomIndex < numNoteRandoms; ++randomIndex)
        ids.addArray ({ ParameterIds::noteRandomWindow (randomIndex), ParameterIds::noteRandomTrigger (randomIndex),
                        ParameterIds::noteRandomSteps (randomIndex) });
    for (int stepIndex = 0; stepIndex < numStepModulators; ++stepIndex)
    {
        ids.addArray ({ ParameterIds::stepCount (stepIndex), ParameterIds::stepRate (stepIndex),
                        ParameterIds::stepGlide (stepIndex), ParameterIds::stepRestartOnKey (stepIndex) });
        for (int step = 0; step < maximumSteps; ++step)
            ids.add (ParameterIds::stepValue (stepIndex, step));
        for (int step = 1; step < maximumSteps; ++step)
            ids.add (ParameterIds::stepTie (stepIndex, step));
    }

    for (const auto& id : ids)
        if (auto* parameter = parameters.getParameter (id))
            setParameterAsGesture (id, parameter->getDefaultValue());
}

void MidiForgeProcessor::setPlainParameterValue (const juce::String& parameterId, float plainValue)
{
    if (auto* parameter = parameters.getParameter (parameterId))
        setParameterAsGesture (parameterId, parameter->convertTo0to1 (plainValue));
}

void MidiForgeProcessor::applyPresetModulation (const FactoryPreset& preset)
{
    resetModulationToDefaults();

    for (const auto& presetLfo : preset.lfos)
    {
        const int lfoIndex = presetLfo.lfoIndex;
        const auto& settings = presetLfo.settings;
        setPlainParameterValue (ParameterIds::lfoWaveform (lfoIndex), (float) settings.waveform);
        setPlainParameterValue (ParameterIds::lfoSyncMode (lfoIndex),
                                settings.isTempoSynced ? (float) tempoSyncIndex : 0.0f);
        setPlainParameterValue (ParameterIds::lfoRate (lfoIndex), (float) settings.rateHz);
        setPlainParameterValue (ParameterIds::lfoDivision (lfoIndex), (float) settings.divisionIndex);
        setPlainParameterValue (ParameterIds::lfoPhase (lfoIndex), (float) settings.phaseDegrees);
        setPlainParameterValue (ParameterIds::lfoPulseWidth (lfoIndex), (float) settings.pulseWidth * 100.0f);
        setPlainParameterValue (ParameterIds::lfoRestartOnKey (lfoIndex), settings.restartsOnFirstKey ? 1.0f : 0.0f);
    }

    for (int routeIndex = 0; routeIndex < (int) preset.routes.size() && routeIndex < numModulationRoutes; ++routeIndex)
    {
        const auto& presetRoute = preset.routes[(size_t) routeIndex];
        setPlainParameterValue (ParameterIds::routeSource (routeIndex), (float) presetRoute.sourceIndex);
        setPlainParameterValue (ParameterIds::routePolarity (routeIndex), presetRoute.isUnipolar ? 1.0f : 0.0f);
        setPlainParameterValue (ParameterIds::routeTarget (routeIndex),
                                (float) modulationTargetIndex (presetRoute.slotIndex, presetRoute.parameterIndex));
        setPlainParameterValue (ParameterIds::routeAmount (routeIndex), presetRoute.amountPercent);
    }

    for (const auto& presetRandom : preset.noteRandoms)
    {
        const int randomIndex = presetRandom.randomIndex;
        const auto& settings = presetRandom.settings;
        setPlainParameterValue (ParameterIds::noteRandomWindow (randomIndex), (float) settings.chordWindowMilliseconds);
        setPlainParameterValue (ParameterIds::noteRandomTrigger (randomIndex),
                                settings.trigger == NoteRandomTrigger::FirstNoteOfPhrase ? 1.0f : 0.0f);
        setPlainParameterValue (ParameterIds::noteRandomSteps (randomIndex),
                                settings.numSteps < minimumNoteRandomSteps ? 0.0f : (float) (settings.numSteps - 1));
    }

    for (const auto& presetSteps : preset.stepModulators)
    {
        const int stepIndex = presetSteps.stepModulatorIndex;
        const auto& settings = presetSteps.settings;
        setPlainParameterValue (ParameterIds::stepCount (stepIndex), (float) settings.numSteps);
        setPlainParameterValue (ParameterIds::stepRate (stepIndex), (float) settings.divisionIndex);
        setPlainParameterValue (ParameterIds::stepGlide (stepIndex), (float) settings.glide * 100.0f);
        setPlainParameterValue (ParameterIds::stepRestartOnKey (stepIndex), settings.restartsOnFirstKey ? 1.0f : 0.0f);
        for (int step = 0; step < maximumSteps; ++step)
            setPlainParameterValue (ParameterIds::stepValue (stepIndex, step), settings.stepValues[step] * 100.0f);
        for (int step = 1; step < maximumSteps; ++step)
            setPlainParameterValue (ParameterIds::stepTie (stepIndex, step), settings.isTied[step] ? 1.0f : 0.0f);
    }
}

void MidiForgeProcessor::getStateInformation (juce::MemoryBlock& destination)
{
    auto state = copyStateWithPages();
    state.setProperty ("preset", currentPresetIndex, nullptr);
    state.setProperty ("presetKind", (int) currentPresetKind, nullptr);
    state.setProperty ("userPresetName", currentUserPresetName, nullptr);
    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destination);
}

void MidiForgeProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    // State comes from project files, which may be damaged or crafted: accept only our own tree type.
    auto xml = getXmlFromBinary (data, sizeInBytes);
    if (xml == nullptr || ! xml->hasTagName (parameters.state.getType()))
        return;

    auto state = juce::ValueTree::fromXml (*xml);
    currentPresetIndex = juce::jlimit (0, getNumPrograms() - 1, (int) state.getProperty ("preset", 0));
    currentPresetKind =
        (PresetKind) juce::jlimit (0, 2, (int) state.getProperty ("presetKind", (int) PresetKind::factory));
    currentUserPresetName = UserPresetLibrary::cleanName (state.getProperty ("userPresetName").toString());
    if (currentPresetKind == PresetKind::user && currentUserPresetName.isEmpty())
        currentPresetKind = PresetKind::none;
    // Read before replaceState(), which adds the missing parameters to the (shared) tree.
    const int savedKnobLayoutVersion = getKnobLayoutVersionOf (state);
    parameters.replaceState (state);
    resetKnobsAddedAfter (savedKnobLayoutVersion);
    restoreShownPages (state);
    rememberValuesOfCurrentPreset();
}

//==============================================================================
// User presets

void MidiForgeProcessor::rememberValuesOfCurrentPreset()
{
    valuesOfCurrentPreset.clear();
    for (auto* parameter : getParameters())
        valuesOfCurrentPreset.push_back (parameter->getValue());
}

bool MidiForgeProcessor::isCurrentPresetModified() const
{
    const auto& allParameters = getParameters();
    if ((size_t) allParameters.size() != valuesOfCurrentPreset.size())
        return true;
    for (int index = 0; index < allParameters.size(); ++index)
        if (std::abs (allParameters[index]->getValue() - valuesOfCurrentPreset[(size_t) index]) > 1.0e-4f)
            return true;
    return false;
}

juce::String MidiForgeProcessor::getCurrentPresetName() const
{
    switch (currentPresetKind)
    {
        case PresetKind::factory: return getFactoryPresets()[(size_t) currentPresetIndex].name;
        case PresetKind::user: return currentUserPresetName;
        case PresetKind::none: break;
    }
    return "Untitled";
}

void MidiForgeProcessor::applyStateValues (const juce::ValueTree& state)
{
    for (auto* parameter : getParameters())
    {
        auto* rangedParameter = dynamic_cast<juce::RangedAudioParameter*> (parameter);
        if (rangedParameter == nullptr)
            continue;

        float normalisedValue = rangedParameter->getDefaultValue();
        const auto savedParameter = state.getChildWithProperty ("id", rangedParameter->paramID);
        if (savedParameter.isValid() && savedParameter.hasProperty ("value"))
        {
            const double plainValue = savedParameter.getProperty ("value");
            if (std::isfinite (plainValue))
            {
                const auto& range = rangedParameter->getNormalisableRange();
                normalisedValue =
                    rangedParameter->convertTo0to1 (juce::jlimit (range.start, range.end, (float) plainValue));
            }
        }
        setParameterAsGesture (rangedParameter->paramID, normalisedValue);
    }
}

bool MidiForgeProcessor::loadUserPreset (const juce::String& name)
{
    const auto state = userPresets.load (name);
    if (! state.has_value())
        return false;

    applyStateValues (*state);
    resetKnobsAddedAfter (getKnobLayoutVersionOf (*state));
    restoreShownPages (*state);
    currentPresetKind = PresetKind::user;
    currentUserPresetName = UserPresetLibrary::cleanName (name);
    rememberValuesOfCurrentPreset();
    return true;
}

juce::String MidiForgeProcessor::saveUserPreset (const juce::String& name)
{
    const auto error = userPresets.save (name, copyStateWithPages());
    if (error.isNotEmpty())
        return error;

    currentPresetKind = PresetKind::user;
    currentUserPresetName = UserPresetLibrary::cleanName (name);
    rememberValuesOfCurrentPreset();
    return {};
}

juce::String MidiForgeProcessor::renameCurrentUserPreset (const juce::String& newName)
{
    if (currentPresetKind != PresetKind::user)
        return "Only your own presets can be renamed.";

    const auto error = userPresets.rename (currentUserPresetName, newName);
    if (error.isEmpty())
        currentUserPresetName = UserPresetLibrary::cleanName (newName);
    return error;
}

bool MidiForgeProcessor::deleteCurrentUserPreset()
{
    if (currentPresetKind != PresetKind::user || ! userPresets.remove (currentUserPresetName))
        return false;

    currentPresetKind = PresetKind::none;
    currentUserPresetName.clear();
    return true;
}

juce::String MidiForgeProcessor::importUserPreset (const juce::File& file)
{
    juce::String importedName;
    const auto error = userPresets.importFile (file, importedName);
    if (error.isNotEmpty())
        return error;
    loadUserPreset (importedName);
    return {};
}

void MidiForgeProcessor::stepPreset (int direction)
{
    const int numFactoryPresets = getNumPrograms();
    const auto userPresetNames = userPresets.getPresetNames();
    const int numPresets = numFactoryPresets + userPresetNames.size();

    int position = -1;
    if (currentPresetKind == PresetKind::factory)
        position = currentPresetIndex;
    else if (currentPresetKind == PresetKind::user && userPresetNames.contains (currentUserPresetName))
        position = numFactoryPresets + userPresetNames.indexOf (currentUserPresetName);

    int next = 0;
    if (position < 0)
        next = direction >= 0 ? 0 : numPresets - 1;
    else
        next = ((position + (direction >= 0 ? 1 : -1)) % numPresets + numPresets) % numPresets;

    if (next < numFactoryPresets)
        setCurrentProgram (next);
    else
        loadUserPreset (userPresetNames[next - numFactoryPresets]);
}

juce::AudioProcessorEditor* MidiForgeProcessor::createEditor()
{
    return new MidiForgeEditor (*this);
}

} // namespace midiforge

/** Called by the JUCE plug-in wrappers to create the plug-in. */
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new midiforge::MidiForgeProcessor();
}
