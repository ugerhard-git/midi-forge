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

/** Fixed host-side names of the modulation targets: "None", "Slot 1 A" ... "Slot 8 D". */
juce::StringArray fixedModulationTargetNames()
{
    juce::StringArray result { "None" };
    for (int slotIndex = 0; slotIndex < numSlots; ++slotIndex)
        for (int parameterIndex = 0; parameterIndex < numParametersPerSlot; ++parameterIndex)
            result.add ("Slot " + juce::String (slotIndex + 1) + " " +
                        juce::String (ParameterIds::valueSuffixes[parameterIndex]).toUpperCase());
    return result;
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

    for (auto& readout : modulatedValueReadouts)
        readout.store (0.0f);
    for (auto& readout : lfoValueReadouts)
        readout.store (0.0f);
    for (auto& readout : lfoPhaseReadouts)
        readout.store (0.0f);
    for (auto& readout : noteRandomValueReadouts)
        readout.store (0.0f);

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

        for (int parameterIndex = 0; parameterIndex < numParametersPerSlot; ++parameterIndex)
        {
            // The host shows these values in the units of whichever module is loaded in the slot.
            auto attributes =
                juce::AudioParameterFloatAttributes()
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

            group->addChild (std::make_unique<juce::AudioParameterFloat> (
                juce::ParameterID { ParameterIds::moduleParameter (slotIndex, parameterIndex),
                                    ParameterIds::slotParameterVersion },
                slotName + " " + juce::String (ParameterIds::valueSuffixes[parameterIndex]).toUpperCase(),
                juce::NormalisableRange<float> (0.0f, 1.0f), 0.0f, attributes));
        }
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

    readEngineSettings();
    const BlockContext context = readBlockContext (audio.getNumSamples());

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

    for (int lfoIndex = 0; lfoIndex < numLfos; ++lfoIndex)
    {
        lfoValueReadouts[(size_t) lfoIndex].store (engine->getLastLfoValues()[lfoIndex], std::memory_order_relaxed);
        lfoPhaseReadouts[(size_t) lfoIndex].store ((float) engine->getLastLfoPhases()[lfoIndex],
                                                   std::memory_order_relaxed);
    }
    for (int randomIndex = 0; randomIndex < numNoteRandoms; ++randomIndex)
        noteRandomValueReadouts[(size_t) randomIndex].store (engine->getNoteRandomValue (randomIndex),
                                                             std::memory_order_relaxed);
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
}

void MidiForgeProcessor::swapSlots (int firstSlotIndex, int secondSlotIndex)
{
    const auto isValid = [] (int slotIndex) { return slotIndex >= 0 && slotIndex < numSlots; };
    if (firstSlotIndex == secondSlotIndex || ! isValid (firstSlotIndex) || ! isValid (secondSlotIndex))
        return;

    juce::StringArray firstIds { ParameterIds::moduleType (firstSlotIndex),
                                 ParameterIds::slotEnabled (firstSlotIndex) };
    juce::StringArray secondIds { ParameterIds::moduleType (secondSlotIndex),
                                  ParameterIds::slotEnabled (secondSlotIndex) };
    for (int parameterIndex = 0; parameterIndex < numParametersPerSlot; ++parameterIndex)
    {
        firstIds.add (ParameterIds::moduleParameter (firstSlotIndex, parameterIndex));
        secondIds.add (ParameterIds::moduleParameter (secondSlotIndex, parameterIndex));
    }

    for (int index = 0; index < firstIds.size(); ++index)
    {
        const float firstValue = parameters.getParameter (firstIds[index])->getValue();
        const float secondValue = parameters.getParameter (secondIds[index])->getValue();
        setParameterAsGesture (firstIds[index], secondValue);
        setParameterAsGesture (secondIds[index], firstValue);
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
            if (! std::isnan (presetSlot.plainValues[parameterIndex]))
                setParameterAsGesture (ParameterIds::moduleParameter (slotIndex, parameterIndex),
                                       plainValueToNormalised (description.parameters[parameterIndex],
                                                               presetSlot.plainValues[parameterIndex]));
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
}

void MidiForgeProcessor::getStateInformation (juce::MemoryBlock& destination)
{
    auto state = parameters.copyState();
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
    parameters.replaceState (state);
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
    currentPresetKind = PresetKind::user;
    currentUserPresetName = UserPresetLibrary::cleanName (name);
    rememberValuesOfCurrentPreset();
    return true;
}

juce::String MidiForgeProcessor::saveUserPreset (const juce::String& name)
{
    const auto error = userPresets.save (name, parameters.copyState());
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
