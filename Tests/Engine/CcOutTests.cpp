/**
 * @file CcOutTests.cpp
 * @brief The CC Out module: values mapped onto Low ... High, sent only on change and at most once per
 *        millisecond, modulation routes moving it, the Send modes, blocking of the same incoming controller,
 *        pitch bend and channel pressure, and smoothing.
 */
#include "../TestFramework.h"
#include "ChainTestRig.h"

#include <algorithm>

using namespace testing;

namespace
{
/** CC Out knob positions used below. */
namespace Cc
{
constexpr float cutoff = 74, modWheel = 1;
constexpr float always = 0, whilePlaying = 1, whileNotesHeld = 2;
constexpr int valueKnob = 2;
} // namespace Cc

/** A message CC Out sent (or let through), with its absolute time. */
struct SentValue
{
    int time;
    int channel;
    int value;
};

/** @return Every control change of one controller number at the chain's output. */
std::vector<SentValue> controlChangesOf (const ChainTestRig& rig, int controller)
{
    std::vector<SentValue> sent;
    for (const auto& event : rig.everyOutputEvent)
        if (event.isControlChange() && event.data1 == controller)
            sent.push_back ({ event.samplePosition, event.channel(), event.data2 });
    return sent;
}

std::vector<SentValue> pitchBendsOf (const ChainTestRig& rig)
{
    std::vector<SentValue> sent;
    for (const auto& event : rig.everyOutputEvent)
        if (event.isPitchBend())
            sent.push_back ({ event.samplePosition, event.channel(), event.pitchBendValue() });
    return sent;
}
} // namespace

TEST_CASE ("cc out: sends Value mapped onto Low ... High, and again only when it changes")
{
    EXPECT (getModuleDescription (ModuleType::CcOut).category == ModuleCategory::Generator);

    ChainTestRig rig;
    rig.loadModule (0, ModuleType::CcOut, { Cc::cutoff, 3, 64, Cc::always, 40, 100 });
    rig.processBlock ({ noteOnAt (10, 60) });
    rig.processSeconds (0.1);
    auto sent = controlChangesOf (rig, 74);
    EXPECT (sent.size() == 1);
    if (sent.size() == 1)
        EXPECT (sent[0].time == 0 && sent[0].channel == 2 && sent[0].value == 70); // 40 + 60 · 64/127
    EXPECT (rig.playedNoteNumbers() == (std::vector<int> { 60 }));                 // notes pass

    rig.setParameter (0, Cc::valueKnob, 127);
    rig.processBlock();
    rig.setParameter (0, 5, 20); // High below Low: the range is turned round
    rig.processBlock();
    sent = controlChangesOf (rig, 74);
    EXPECT (sent.size() == 3);
    if (sent.size() == 3)
        EXPECT (sent[1].value == 100 && sent[2].value == 20);
}

TEST_CASE ("cc out: an LFO route sweeps the controller, at most once per millisecond")
{
    ChainTestRig rig;
    rig.loadModule (0, ModuleType::CcOut, { Cc::cutoff, 1, 64, Cc::always });
    rig.lfos[0].isTempoSynced = false;
    rig.lfos[0].rateHz = 4.0; // a fast sweep: the value changes more often than once per millisecond
    rig.routeLfo (0, 0, 0, Cc::valueKnob, 50.0f);
    rig.processSeconds (1.0);

    const auto sent = controlChangesOf (rig, 74);
    EXPECT (sent.size() > 300); // 4 sweeps up and down over about 127 values each
    int smallestGap = INT32_MAX, lowest = 127, highest = 0;
    for (size_t index = 0; index < sent.size(); ++index)
    {
        if (index > 0)
        {
            smallestGap = std::min (smallestGap, sent[index].time - sent[index - 1].time);
            EXPECT (sent[index].value != sent[index - 1].value); // only changes are sent
        }
        lowest = std::min (lowest, sent[index].value);
        highest = std::max (highest, sent[index].value);
    }
    EXPECT (smallestGap >= 48); // 1 ms at 48 kHz
    EXPECT (lowest <= 2 && highest >= 125);
}

TEST_CASE ("cc out: Send While Playing and While Notes Held send the current value when they start")
{
    ChainTestRig playing;
    playing.loadModule (0, ModuleType::CcOut, { Cc::modWheel, 1, 100, Cc::whilePlaying });
    playing.processSeconds (0.1);
    EXPECT (controlChangesOf (playing, 1).empty()); // the DAW is stopped
    playing.context.isPlaying = playing.context.hasPpqPosition = true;
    playing.processBlock();
    EXPECT (controlChangesOf (playing, 1).size() == 1);
    playing.context.isPlaying = false;
    playing.processSeconds (0.1);
    playing.context.isPlaying = true; // started again: the same value is sent again
    playing.processBlock();
    EXPECT (controlChangesOf (playing, 1).size() == 2);

    ChainTestRig held;
    held.loadModule (0, ModuleType::CcOut, { Cc::modWheel, 1, 100, Cc::whileNotesHeld });
    held.processSeconds (0.1);
    EXPECT (controlChangesOf (held, 1).empty());
    held.processBlock ({ noteOnAt (0, 60), noteOnAt (0, 60) }); // a repeated note-on counts once
    held.processBlock ({ noteOffAt (0, 60) });
    held.setParameter (0, Cc::valueKnob, 50);
    held.processSeconds (0.1);
    EXPECT (controlChangesOf (held, 1).size() == 1); // released: Value changes are not sent
    held.processBlock ({ noteOnAt (0, 62) });
    const auto sent = controlChangesOf (held, 1);
    EXPECT (sent.size() == 2 && sent.back().value == 50);
}

TEST_CASE ("cc out: the same incoming controller is blocked, everything else passes")
{
    ChainTestRig rig;
    rig.loadModule (0, ModuleType::CcOut, { Cc::cutoff, 1, 64, Cc::always });
    rig.processBlock ({ MidiEvent::controlChange (5, 0, 74, 10), // blocked: CC Out sends CC 74 on channel 1
                        MidiEvent::controlChange (5, 1, 74, 11), // other channel
                        MidiEvent::controlChange (5, 0, 1, 12),  // other controller
                        MidiEvent::pitchBend (5, 0, 1000) });    // other message
    EXPECT (controlChangesOf (rig, 74).size() == 2);             // CC Out's own and channel 2's
    EXPECT (controlChangesOf (rig, 1).size() == 1 && pitchBendsOf (rig).size() == 1);

    ChainTestRig bend;
    bend.loadModule (0, ModuleType::CcOut, { (float) ccOutPitchBendController, 1, 64, Cc::always });
    bend.processBlock ({ MidiEvent::pitchBend (5, 0, 1000), MidiEvent::pitchBend (5, 1, 2000) });
    const auto bends = pitchBendsOf (bend);
    EXPECT (bends.size() == 2); // its own (centre) and the one on channel 2
    if (bends.size() == 2)
        EXPECT (bends[0].value == MidiEvent::pitchBendCentre && bends[1].channel == 1);
}

TEST_CASE ("cc out: pitch bend uses the full range with 64 in the centre; channel pressure has two bytes")
{
    ChainTestRig bend;
    bend.loadModule (0, ModuleType::CcOut, { (float) ccOutPitchBendController, 1, 0, Cc::always });
    for (float value : { 0.0f, 64.0f, 127.0f })
    {
        bend.setParameter (0, Cc::valueKnob, value);
        bend.processSeconds (0.01);
    }
    const auto bends = pitchBendsOf (bend);
    EXPECT (bends.size() == 3);
    if (bends.size() == 3)
        EXPECT (bends[0].value == 0 && bends[1].value == 8192 && bends[2].value == 16383);

    ChainTestRig pressure;
    pressure.loadModule (0, ModuleType::CcOut, { (float) ccOutChannelPressureController, 2, 90, Cc::always });
    const auto output = pressure.processBlock();
    EXPECT (output.size() == 1);
    if (output.size() == 1)
        EXPECT (output[0].isChannelPressure() && output[0].channel() == 1 && output[0].data1 == 90 &&
                output[0].numBytes == 2);
}

TEST_CASE ("cc out: Smoothing glides to a new value instead of jumping")
{
    ChainTestRig rig;
    rig.loadModule (0, ModuleType::CcOut, { Cc::cutoff, 1, 0, Cc::always, 0, 127, 100 }); // 100 ms
    rig.processBlock();
    rig.setParameter (0, Cc::valueKnob, 127);
    rig.processSeconds (0.05);
    auto sent = controlChangesOf (rig, 74);
    EXPECT (sent.size() > 3);
    if (! sent.empty())
        EXPECT (sent.back().value > 80 && sent.back().value < 127); // about 78 % there after 50 ms
    rig.processSeconds (0.3);
    sent = controlChangesOf (rig, 74);
    EXPECT (! sent.empty() && sent.back().value == 127);

    // A new destination (another controller) gets its value right away, without gliding.
    rig.setParameter (0, 0, Cc::modWheel);
    rig.processBlock();
    const auto modWheel = controlChangesOf (rig, 1);
    EXPECT (modWheel.size() == 1 && modWheel[0].value == 127);
}
