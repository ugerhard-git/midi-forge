/**
 * @file MidiForgeEngine.h
 * @brief The complete engine: the module chain plus the modulation (LFOs, Note Random) that drives it.
 */
#pragma once

#include "Modulation/Lfo.h"
#include "Modulation/ModulationRoutes.h"
#include "Modulation/NoteRandom.h"
#include "ModuleChain.h"

namespace midiforge
{

/** Everything the engine needs to know about the user's settings for one block. */
struct EngineSettings
{
    SlotSettings slots[numSlots];
    LfoSettings lfos[numLfos];
    NoteRandomSettings noteRandoms[numNoteRandoms];
    ModulationRoute routes[numModulationRoutes];
};

/**
 * @brief Runs the modulation sources (LFOs, Note Random) and the module chain together.
 *
 * When modulation is active, a block is processed in short pieces ("segments"): one starts at every
 * incoming event and at least every maxSegmentLength samples. The sources are evaluated at the start of
 * each segment, so incoming notes see the LFO value at their exact sample position (and a Note Random
 * value rolled for that very note), and generated notes (arpeggio steps, echoes) see a value at most
 * maxSegmentLength samples old.
 *
 * Real-time safe: no allocation after construction.
 */
class MidiForgeEngine
{
public:
    /** Longest segment while modulation is active: 32 samples = 0.7 ms at 48 kHz. */
    static constexpr int maxSegmentLength = 32;

    MidiForgeEngine();

    void prepare (double sampleRate);
    void reset();

    /**
     * Processes one block in place.
     * @param events    Input events, sorted by sample position; replaced by the output.
     * @param context   Host timing for the block.
     * @param settings  Current user settings.
     */
    void process (MidiEventList& events, const BlockContext& context, const EngineSettings& settings);

    /** Thread safe: releases all notes at the start of the next block. */
    void requestPanic() noexcept { moduleChain.requestPanic(); }

    /** @return The slot settings after modulation, as used at the end of the last block. */
    const SlotSettings* getLastModulatedSlots() const noexcept { return modulatedSlots; }
    /** @return Each LFO's output (-1..+1) at the end of the last block. */
    const float* getLastLfoValues() const noexcept { return lfoValues; }
    /** @return Each LFO's position in its cycle (0..1, including phase) at the end of the last block. */
    const double* getLastLfoPhases() const noexcept { return lfoPhases; }
    /** @return Each Note Random modulator's current value (-1..+1). */
    float getNoteRandomValue (int randomIndex) const noexcept { return noteRandoms[randomIndex].getValue(); }

private:
    void processSegment (int segmentStart, int segmentLength, const BlockContext& blockContext,
                         const EngineSettings& settings, bool isModulationActive);
    BlockContext contextForSegment (const BlockContext& blockContext, int segmentStart, int segmentLength) const;
    /** Restarts LFOs on the first key and lets the Note Random modulators roll for played notes. */
    void handleNotesForModulators (const MidiEventList& segmentInput, const BlockContext& segmentContext,
                                   int64_t segmentStartSample, const EngineSettings& settings);
    void computeSourceValues (const BlockContext& segmentContext, const EngineSettings& settings);
    void updateLfoReadouts (const BlockContext& endOfBlockContext, const EngineSettings& settings);

    ModuleChain moduleChain;
    Lfo lfos[numLfos];
    NoteRandom noteRandoms[numNoteRandoms];
    ModulationSourceValues sourceValues;
    int64_t samplesProcessed = 0;
    SlotSettings modulatedSlots[numSlots] {};
    float lfoValues[numLfos] {};
    double lfoPhases[numLfos] {};

    std::array<bool, numChannelNoteCombinations> isKeyHeld {}; ///< Keys held at the engine input.
    int numKeysHeld = 0;

    MidiEventList blockInput, segmentEvents, blockOutput;
};

} // namespace midiforge
