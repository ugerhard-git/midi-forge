/**
 * @file NoteDispatcherTests.cpp
 * @brief The Note Dispatcher: the three algorithms, chords, stealing busy channels, the input filter,
 *        copied controllers and note-offs that follow their notes.
 */
#include "../TestFramework.h"
#include "ChainTestRig.h"

#include <set>

using namespace testing;

namespace
{
/** Choice indices (see ModuleDescriptions.cpp). */
namespace Algorithm
{
constexpr float roundRobin = 0, random = 1, pingPong = 2;
}
namespace DispatchMode
{
constexpr float singleNotes = 0, chords = 1;
}

/** Loads a Note Dispatcher into slot 1: algorithm, mode, first channel, channels, input, chord window. */
void setUpDispatcher (ChainTestRig& rig, float algorithm, float mode = DispatchMode::singleNotes,
                      float firstChannel = 1, float numberOfChannels = 4, float input = 0, float chordWindow = 30)
{
    rig.loadModule (0, ModuleType::NoteDispatcher,
                    { algorithm, mode, firstChannel, numberOfChannels, input, chordWindow });
}

/** @return The output channels (zero based) of all note-ons, in order. */
std::vector<int> noteOnChannels (const ChainTestRig& rig)
{
    std::vector<int> channels;
    for (const auto& event : rig.everyOutputEvent)
        if (event.isNoteOn())
            channels.push_back (event.channel());
    return channels;
}

/** Plays short notes one after another (each released before the next starts). */
void playStaccato (ChainTestRig& rig, int numberOfNotes)
{
    for (int note = 0; note < numberOfNotes; ++note)
    {
        rig.processBlock ({ noteOnAt (0, 60 + note) });
        rig.processBlock ({ noteOffAt (0, 60 + note) });
    }
}
} // namespace

TEST_CASE ("note dispatcher: round robin goes through the channels in turn and wraps")
{
    ChainTestRig rig;
    setUpDispatcher (rig, Algorithm::roundRobin, DispatchMode::singleNotes, 3, 4); // channels 3-6
    playStaccato (rig, 6);
    EXPECT (noteOnChannels (rig) == std::vector<int> ({ 2, 3, 4, 5, 2, 3 }));
    EXPECT (rig.notesStillSounding().empty());
}

TEST_CASE ("note dispatcher: round robin skips busy channels")
{
    ChainTestRig rig;
    setUpDispatcher (rig, Algorithm::roundRobin);
    rig.processBlock ({ noteOnAt (0, 60) });  // channel 1, held
    rig.processBlock ({ noteOnAt (0, 62) });  // channel 2, held
    rig.processBlock ({ noteOffAt (0, 60) }); // channel 1 is free again
    rig.processBlock ({ noteOnAt (0, 64) });  // channel 3
    rig.processBlock ({ noteOnAt (0, 65) });  // channel 4
    rig.processBlock ({ noteOnAt (0, 67) });  // wraps: channel 1 (2 is still busy)
    EXPECT (noteOnChannels (rig) == std::vector<int> ({ 0, 1, 2, 3, 0 }));
}

TEST_CASE ("note dispatcher: ping pong goes up and down without repeating the ends")
{
    ChainTestRig rig;
    setUpDispatcher (rig, Algorithm::pingPong);
    playStaccato (rig, 10);
    EXPECT (noteOnChannels (rig) == std::vector<int> ({ 0, 1, 2, 3, 2, 1, 0, 1, 2, 3 }));
}

TEST_CASE ("note dispatcher: random picks only free channels inside the range, and all of them")
{
    ChainTestRig rig;
    setUpDispatcher (rig, Algorithm::random, DispatchMode::singleNotes, 5, 4); // channels 5-8
    playStaccato (rig, 60);
    const auto channels = noteOnChannels (rig);
    EXPECT (std::set<int> (channels.begin(), channels.end()) == std::set<int> ({ 4, 5, 6, 7 }));

    // Held notes: four notes always land on four different channels.
    ChainTestRig held;
    setUpDispatcher (held, Algorithm::random);
    for (int round = 0; round < 20; ++round)
    {
        held.everyOutputEvent.clear();
        held.processBlock ({ noteOnAt (0, 60), noteOnAt (1, 62), noteOnAt (2, 64), noteOnAt (3, 65) });
        const auto heldChannels = noteOnChannels (held);
        EXPECT (std::set<int> (heldChannels.begin(), heldChannels.end()).size() == 4);
        held.processBlock ({ noteOffAt (0, 60), noteOffAt (0, 62), noteOffAt (0, 64), noteOffAt (0, 65) });
    }
}

TEST_CASE ("note dispatcher: when every channel is busy, the oldest note is ended and its channel reused")
{
    ChainTestRig rig;
    setUpDispatcher (rig, Algorithm::roundRobin);
    for (int note = 0; note < 4; ++note)
        rig.processBlock ({ noteOnAt (0, 60 + note) });
    const size_t eventsBefore = rig.everyOutputEvent.size();
    rig.processBlock ({ noteOnAt (0, 70) });

    // A note-off for the first note (channel 1), then the new note on channel 1.
    EXPECT (rig.everyOutputEvent.size() == eventsBefore + 2);
    const auto& stolen = rig.everyOutputEvent[eventsBefore];
    const auto& newNote = rig.everyOutputEvent[eventsBefore + 1];
    EXPECT (stolen.isNoteOff() && stolen.noteNumber() == 60 && stolen.channel() == 0);
    EXPECT (newNote.isNoteOn() && newNote.noteNumber() == 70 && newNote.channel() == 0);

    // The first key's own note-off is swallowed later; nothing is left hanging.
    const auto releasedFirstKey = rig.processBlock ({ noteOffAt (0, 60) });
    EXPECT (releasedFirstKey.empty());
    rig.processBlock ({ noteOffAt (0, 61), noteOffAt (0, 62), noteOffAt (0, 63), noteOffAt (0, 70) });
    EXPECT (rig.notesStillSounding().empty());
}

TEST_CASE ("note dispatcher: in chords mode a chord shares one channel, the next chord takes the next")
{
    ChainTestRig rig;
    setUpDispatcher (rig, Algorithm::roundRobin, DispatchMode::chords);
    // A slightly uneven chord (within 30 ms), then a second chord half a second later while the first is held.
    rig.processBlock ({ noteOnAt (0, 60), noteOnAt (200, 64) });
    rig.processBlock ({ noteOnAt (100, 67) });
    rig.processSeconds (0.5);
    rig.processBlock ({ noteOnAt (0, 62), noteOnAt (10, 65) });
    EXPECT (noteOnChannels (rig) == std::vector<int> ({ 0, 0, 0, 1, 1 }));

    // With all chords released, the next chord continues the rotation.
    rig.processBlock (
        { noteOffAt (0, 60), noteOffAt (0, 64), noteOffAt (0, 67), noteOffAt (0, 62), noteOffAt (0, 65) });
    rig.processSeconds (0.1);
    rig.processBlock ({ noteOnAt (0, 72), noteOnAt (5, 76) });
    const auto channels = noteOnChannels (rig);
    EXPECT (channels[5] == 2 && channels[6] == 2);
}

TEST_CASE ("note dispatcher: note-offs follow their note even when the channels change while it is held")
{
    ChainTestRig rig;
    setUpDispatcher (rig, Algorithm::roundRobin);
    rig.processBlock ({ noteOnAt (0, 60), noteOnAt (0, 62) }); // channels 1 and 2
    rig.setParameter (0, 2, 9);                                // first channel now 9
    const auto offs = rig.processBlock ({ noteOffAt (0, 62), noteOffAt (0, 60) });
    EXPECT (offs.size() == 2 && offs[0].channel() == 1 && offs[1].channel() == 0);
    EXPECT (rig.notesStillSounding().empty());
}

TEST_CASE ("note dispatcher: controllers are copied to every channel; poly pressure follows its note")
{
    ChainTestRig rig;
    setUpDispatcher (rig, Algorithm::roundRobin, DispatchMode::singleNotes, 1, 3);
    rig.processBlock ({ noteOnAt (0, 60), noteOnAt (0, 62) });
    const auto copies = rig.processBlock ({ MidiEvent::controlChange (0, 0, 1, 99) });
    EXPECT (copies.size() == 3);
    for (int index = 0; index < (int) copies.size(); ++index)
        EXPECT (copies[(size_t) index].isControlChange() && copies[(size_t) index].channel() == index);

    MidiEvent pressure { 0, (uint8_t) (MidiStatus::polyPressure | 0), 62, 80, 3 };
    const auto followed = rig.processBlock ({ pressure });
    EXPECT (followed.size() == 1 && followed[0].channel() == 1);
}

TEST_CASE ("note dispatcher: only the chosen input channel is dispatched")
{
    ChainTestRig rig;
    setUpDispatcher (rig, Algorithm::roundRobin, DispatchMode::singleNotes, 5, 4, 2); // input: channel 2
    rig.processBlock ({ noteOnAt (0, 60, 100, 0), noteOnAt (0, 62, 100, 1) });
    rig.processBlock ({ MidiEvent::controlChange (0, 0, 64, 127) });
    EXPECT (noteOnChannels (rig) == std::vector<int> ({ 0, 4 })); // channel 1 untouched, channel 2 dispatched
    int numSustainMessages = 0;
    for (const auto& event : rig.everyOutputEvent)
        numSustainMessages += event.isControlChange() ? 1 : 0;
    EXPECT (numSustainMessages == 1); // the pedal on channel 1 is not copied
}

TEST_CASE ("note dispatcher: the range never goes past channel 16")
{
    ChainTestRig rig;
    setUpDispatcher (rig, Algorithm::roundRobin, DispatchMode::singleNotes, 15, 8);
    playStaccato (rig, 5);
    EXPECT (noteOnChannels (rig) == std::vector<int> ({ 14, 15, 14, 15, 14 }));
}

TEST_CASE ("note dispatcher: changing the Input while a key repeats leaves nothing hanging")
{
    // A key that sounds twice (e.g. overlapping echoes) gets one note-off for both, as a synth would treat it.
    ChainTestRig rig;
    setUpDispatcher (rig, Algorithm::roundRobin, DispatchMode::singleNotes, 5, 4, 2);
    rig.processBlock ({ noteOnAt (0, 60, 100, 1) }); // dispatched to channel 5
    rig.setParameter (0, 4, 3);                      // Input now channel 3
    rig.processBlock ({ noteOnAt (0, 60, 100, 1) }); // passes unchanged; the dispatched note ends
    rig.processBlock ({ noteOffAt (0, 60, 1) });
    EXPECT (rig.notesStillSounding().empty());

    rig.setParameter (0, 4, 2);                      // and back: channel 2 is dispatched again
    rig.processBlock ({ noteOnAt (0, 62, 100, 1) }); // dispatched
    rig.setParameter (0, 4, 0);
    rig.processBlock ({ noteOnAt (0, 62, 100, 1) }); // dispatched again (All): the first one ends
    rig.processBlock ({ noteOffAt (0, 62, 1) });
    EXPECT (rig.notesStillSounding().empty());
}
