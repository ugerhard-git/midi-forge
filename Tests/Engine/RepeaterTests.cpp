/**
 * @file RepeaterTests.cpp
 * @brief The Repeater (ratchet) module: timing, chords, release, swing, accents and velocity sources.
 */
#include "../TestFramework.h"
#include "ChainTestRig.h"

#include <algorithm>
#include <cmath>

using namespace testing;

namespace
{
/** Choice indices (see ModuleDescriptions.cpp). */
namespace RepeaterRate
{
constexpr float quarter = 1, eighth = 4, sixteenth = 7, sixteenthTriplet = 8;
}
namespace RepeaterVelocity
{
constexpr float lastNote = 0, aftertouch = 1;
}
namespace RepeaterAccent
{
constexpr float off = 0, everySecond = 1, everyThird = 2, everyFourth = 3, onTheBeat = 4;
}

constexpr int sixteenthAt120Bpm = 6000; // samples at 48 kHz
constexpr int noteLength = sixteenthAt120Bpm / 2 - 1;

struct Hit
{
    int time;
    int noteNumber;
    int velocity;
};

std::vector<Hit> noteOnsOf (const ChainTestRig& rig)
{
    std::vector<Hit> hits;
    for (const auto& event : rig.everyOutputEvent)
        if (event.isNoteOn())
            hits.push_back ({ event.samplePosition, event.noteNumber(), event.velocity() });
    return hits;
}

/** Loads a Repeater into slot 1 of the rig. (Rigs cannot be copied: they own a whole engine.) */
void setUpRepeater (ChainTestRig& rig, float rate, float velocitySource, float swingPercent, float accent,
                    bool followsHost)
{
    rig.context.isPlaying = followsHost;
    rig.context.hasPpqPosition = followsHost;
    rig.loadModule (0, ModuleType::Repeater, { rate, velocitySource, swingPercent, accent });
}
} // namespace

TEST_CASE ("repeater: first hit right away, then repeats every step (free running)")
{
    ChainTestRig rig;
    setUpRepeater (rig, RepeaterRate::sixteenth, RepeaterVelocity::lastNote, 50, RepeaterAccent::off, false);
    rig.processSeconds (0.3); // the clock runs before any key is pressed
    const int pressTime = (int) rig.samplesElapsed + 100;
    rig.processBlock ({ noteOnAt (100, 60, 90) });
    rig.processSeconds (0.6);

    const auto hits = noteOnsOf (rig);
    EXPECT (hits.size() >= 4);
    for (size_t index = 0; index < hits.size(); ++index)
    {
        EXPECT (hits[index].time == pressTime + (int) index * sixteenthAt120Bpm);
        EXPECT (hits[index].noteNumber == 60 && hits[index].velocity == 90);
    }

    // Every hit lasts half a step
    int lastOn = -1;
    for (const auto& event : rig.everyOutputEvent)
    {
        if (event.isNoteOn())
            lastOn = event.samplePosition;
        else if (event.isNoteOff())
            EXPECT (event.samplePosition - lastOn == noteLength);
    }
}

TEST_CASE ("repeater: a held chord repeats together, and releasing stops it without stuck notes")
{
    ChainTestRig rig;
    setUpRepeater (rig, RepeaterRate::eighth, RepeaterVelocity::lastNote, 50, RepeaterAccent::off, false);
    rig.processBlock ({ noteOnAt (0, 60), noteOnAt (0, 64), noteOnAt (0, 67) });
    rig.processSeconds (1.0); // 1/8 at 120 bpm: 4 steps per second
    rig.processBlock ({ noteOffAt (0, 60), noteOffAt (0, 64), noteOffAt (0, 67) });
    const size_t hitsWhenReleased = noteOnsOf (rig).size();
    rig.processSeconds (1.0);

    const auto hits = noteOnsOf (rig);
    EXPECT (hits.size() == hitsWhenReleased); // nothing after the release
    EXPECT (hits.size() % 3 == 0 && hits.size() >= 12);
    for (size_t index = 0; index + 2 < hits.size(); index += 3)
        EXPECT (hits[index].time == hits[index + 1].time && hits[index].time == hits[index + 2].time);
    EXPECT (rig.notesStillSounding().empty());
}

TEST_CASE ("repeater: repeats lock to the host grid while the transport runs")
{
    ChainTestRig rig;
    setUpRepeater (rig, RepeaterRate::sixteenth, RepeaterVelocity::lastNote, 50, RepeaterAccent::off, true);
    rig.context.ppqPositionAtBlockStart = 0.1;
    rig.processBlock ({ noteOnAt (0, 60) });
    rig.processSeconds (0.5);

    const auto hits = noteOnsOf (rig);
    EXPECT (hits.size() >= 3);
    if (hits.size() >= 3)
    {
        EXPECT (hits[0].time == 0);                                       // immediately at the key press
        EXPECT (std::abs (hits[1].time - 3600) <= 1);                     // beat 0.25: (0.25 - 0.1) * 24000
        EXPECT (std::abs (hits[2].time - 3600 - sixteenthAt120Bpm) <= 1); // then every 1/16
    }
}

TEST_CASE ("repeater: a key pressed just before a step does not double-trigger")
{
    ChainTestRig rig;
    setUpRepeater (rig, RepeaterRate::sixteenth, RepeaterVelocity::lastNote, 50, RepeaterAccent::off, true);
    rig.processBlock();                        // grid step 0 at sample 0 passes empty
    rig.processBlock ({ noteOnAt (200, 60) }); // pressed at 456, step 1 is at 6000
    rig.processSeconds (0.3);

    const auto hits = noteOnsOf (rig);
    EXPECT (hits.size() >= 3);
    if (hits.size() >= 3)
    {
        EXPECT (hits[0].time == testBlockSize + 200);
        EXPECT (hits[1].time == 6000); // the press sounded long enough before this step: no skip
        EXPECT (hits[2].time == 12000);
    }

    ChainTestRig lateRig;
    setUpRepeater (lateRig, RepeaterRate::sixteenth, RepeaterVelocity::lastNote, 50, RepeaterAccent::off, true);
    for (int block = 0; block < 22; ++block)
        lateRig.processBlock();                    // up to sample 5632
    lateRig.processBlock ({ noteOnAt (100, 60) }); // pressed at 5732, 268 samples before step 1
    lateRig.processSeconds (0.2);
    const auto lateHits = noteOnsOf (lateRig);
    EXPECT (lateHits.size() >= 2);
    if (lateHits.size() >= 2)
    {
        EXPECT (lateHits[0].time == 5732);
        EXPECT (lateHits[1].time == 12000); // step 1 (6000) skipped: the first hit was still sounding
    }
    EXPECT (lateRig.notesStillSounding().size() <= 1);
}

TEST_CASE ("repeater: swing delays every second step")
{
    ChainTestRig rig;
    setUpRepeater (rig, RepeaterRate::sixteenth, RepeaterVelocity::lastNote, 75, RepeaterAccent::off, true);
    rig.processBlock ({ noteOnAt (0, 60) });
    rig.processSeconds (0.6);

    const auto hits = noteOnsOf (rig);
    EXPECT (hits.size() >= 5);
    if (hits.size() >= 5)
    {
        EXPECT (hits[0].time == 0);
        EXPECT (std::abs (hits[1].time - 9000) <= 1);  // step 1 pushed half a step: 1.5 * 6000
        EXPECT (std::abs (hits[2].time - 12000) <= 1); // step 2 on the grid
        EXPECT (std::abs (hits[3].time - 21000) <= 1);
        EXPECT (std::abs (hits[4].time - 24000) <= 1);
    }
    EXPECT (rig.processBlock ({ noteOffAt (0, 60) }).size() <= 1);
    rig.processSeconds (0.3);
    EXPECT (rig.notesStillSounding().empty());
}

TEST_CASE ("repeater: accent patterns")
{
    const auto accentedSteps = [] (float accent, float rate)
    {
        ChainTestRig rig;
        setUpRepeater (rig, rate, RepeaterVelocity::lastNote, 50, accent, true);
        rig.processBlock ({ noteOnAt (0, 60, 80) });
        rig.processSeconds (2.0);
        std::vector<bool> isLoud;
        const auto hits = noteOnsOf (rig);
        for (size_t index = 1; index < hits.size() && index <= 12; ++index) // index = grid step number
            isLoud.push_back (hits[index].velocity == 105);
        return isLoud;
    };

    const auto none = accentedSteps (RepeaterAccent::off, RepeaterRate::sixteenth);
    EXPECT (std::count (none.begin(), none.end(), true) == 0);

    const auto second = accentedSteps (RepeaterAccent::everySecond, RepeaterRate::sixteenth);
    const auto third = accentedSteps (RepeaterAccent::everyThird, RepeaterRate::sixteenth);
    const auto fourth = accentedSteps (RepeaterAccent::everyFourth, RepeaterRate::sixteenth);
    const auto beat = accentedSteps (RepeaterAccent::onTheBeat, RepeaterRate::sixteenth);
    const auto tripletBeat = accentedSteps (RepeaterAccent::onTheBeat, RepeaterRate::sixteenthTriplet);
    for (size_t index = 0; index < 12; ++index)
    {
        const size_t step = index + 1;
        EXPECT (second.size() > index && second[index] == (step % 2 == 0));
        EXPECT (third.size() > index && third[index] == (step % 3 == 0));
        EXPECT (fourth.size() > index && fourth[index] == (step % 4 == 0));
        EXPECT (beat.size() > index && beat[index] == (step % 4 == 0));               // four 1/16 per beat
        EXPECT (tripletBeat.size() > index && tripletBeat[index] == (step % 6 == 0)); // six 1/16T per beat
    }
}

TEST_CASE ("repeater: repeats use the velocity of the last key pressed")
{
    ChainTestRig rig;
    setUpRepeater (rig, RepeaterRate::quarter, RepeaterVelocity::lastNote, 50, RepeaterAccent::off, false);
    rig.processBlock ({ noteOnAt (0, 60, 50) });
    rig.processSeconds (0.1);
    rig.processBlock ({ noteOnAt (0, 64, 110) });
    rig.processSeconds (1.0);

    const auto hits = noteOnsOf (rig);
    EXPECT (hits.size() >= 4);
    if (hits.size() >= 4)
    {
        EXPECT (hits[0].noteNumber == 60 && hits[0].velocity == 50); // first hits keep their own velocity
        EXPECT (hits[1].noteNumber == 64 && hits[1].velocity == 110);
        for (size_t index = 2; index < hits.size(); ++index)
            EXPECT (hits[index].velocity == 110); // repeats: the last key's velocity
    }
}

TEST_CASE ("repeater: aftertouch sets the velocity, poly pressure before channel pressure")
{
    ChainTestRig rig;
    setUpRepeater (rig, RepeaterRate::quarter, RepeaterVelocity::aftertouch, 50, RepeaterAccent::off, false);
    rig.processBlock ({ noteOnAt (0, 60, 70), noteOnAt (0, 64, 40) });
    rig.processSeconds (0.6); // one repeat (0.5 s per quarter): no pressure yet -> own velocities

    const MidiEvent polyPressureOn60 { 0, 0xA0, 60, 120, 3 };
    const MidiEvent channelPressure { 0, 0xD0, 20, 0, 2 };
    const auto passedThrough = rig.processBlock ({ polyPressureOn60, channelPressure });
    EXPECT (passedThrough.size() == 2); // aftertouch reaches the synth too
    rig.processSeconds (0.5);

    const auto hits = noteOnsOf (rig);
    EXPECT (hits.size() == 6);
    if (hits.size() == 6)
    {
        EXPECT (hits[2].velocity == (hits[2].noteNumber == 60 ? 70 : 40));
        EXPECT (hits[3].velocity == (hits[3].noteNumber == 60 ? 70 : 40));
        for (size_t index = 4; index < 6; ++index)
            EXPECT (hits[index].velocity == (hits[index].noteNumber == 60 ? 120 : 20));
    }
}

TEST_CASE ("repeater: an LFO on the rate never leaves notes hanging")
{
    ChainTestRig rig;
    setUpRepeater (rig, RepeaterRate::sixteenth, RepeaterVelocity::lastNote, 60, RepeaterAccent::everyThird, true);
    rig.lfos[0].waveform = LfoWaveform::SampleAndHold;
    rig.lfos[0].divisionIndex = 9; // 1/8 dotted
    rig.routeLfo (0, 0, 0, 0, 60.0f);
    rig.processBlock ({ noteOnAt (0, 48), noteOnAt (10, 55) });
    rig.processSeconds (3.0);
    rig.processBlock ({ noteOffAt (0, 48), noteOffAt (0, 55) });
    rig.processSeconds (2.0);
    EXPECT (noteOnsOf (rig).size() > 20);
    EXPECT (rig.notesStillSounding().empty());
}

//==============================================================================
// Page 2: accent boost and note length

TEST_CASE ("repeater: note length sets how long each repeat lasts, never longer than a step")
{
    const auto lengthsOf = [] (float lengthPercent, float swingPercent)
    {
        ChainTestRig rig;
        setUpRepeater (rig, RepeaterRate::sixteenth, RepeaterVelocity::lastNote, swingPercent, RepeaterAccent::off,
                       true);
        rig.setParameter (0, 5, lengthPercent);
        rig.processBlock ({ noteOnAt (0, 60) });
        rig.processSeconds (1.0);
        rig.processBlock ({ noteOffAt (0, 60) });
        rig.processSeconds (0.5);
        EXPECT (rig.notesStillSounding().empty());

        std::vector<int> lengths;
        int lastOn = 0;
        for (const auto& event : rig.everyOutputEvent)
        {
            if (event.isNoteOn())
                lastOn = event.samplePosition;
            else if (event.isNoteOff())
                lengths.push_back (event.samplePosition - lastOn);
        }
        return lengths;
    };

    for (int length : lengthsOf (25, 50))
        EXPECT (length == sixteenthAt120Bpm / 4 - 1);
    for (int length : lengthsOf (100, 50))
        EXPECT (length == sixteenthAt120Bpm - 1); // ends just before the next repeat

    // With heavy swing the short gaps cap the length, so repeats never overlap or get skipped.
    const auto swung = lengthsOf (100, 75);
    EXPECT (swung.size() >= 7);
    for (size_t index = 0; index < swung.size(); ++index)
        EXPECT (swung[index] == sixteenthAt120Bpm - 1 || swung[index] == sixteenthAt120Bpm / 2 - 1);
}

TEST_CASE ("repeater: accent boost sets how much louder accented repeats are")
{
    ChainTestRig rig;
    setUpRepeater (rig, RepeaterRate::sixteenth, RepeaterVelocity::lastNote, 50, RepeaterAccent::everySecond, true);
    rig.setParameter (0, 4, 40);
    rig.processBlock ({ noteOnAt (0, 60, 70) });
    rig.processSeconds (0.5);

    const auto hits = noteOnsOf (rig);
    EXPECT (hits.size() >= 5);
    for (size_t step = 1; step < hits.size(); ++step)
        EXPECT (hits[step].velocity == (step % 2 == 0 ? 110 : 70));
}
