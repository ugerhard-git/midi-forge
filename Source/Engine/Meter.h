/**
 * @file Meter.h
 * @brief Time signatures and the beat structure of a bar, shared by every module that needs to know where in
 *        the bar a note falls (Groove, Ghost Notes, Mutate, Beat Thinning, bar-long divisions).
 *
 * MIDI Forge makes no assumption about the time signature: bar lengths, groups and accents always come from
 * the host's time signature. Only when the host reports none is 4/4 used.
 */
#pragma once

#include <array>
#include <cmath>
#include <cstdint>

namespace midiforge
{

/** How odd meters such as 5/8, 7/8 or 7/4 are split into groups of two and three counts. */
enum class OddMeterGrouping
{
    longGroupLast,  ///< e.g. 7/8 as 2+2+3
    longGroupFirst, ///< e.g. 7/8 as 3+2+2
    longGroupMiddle ///< e.g. 7/8 as 2+3+2
};

/**
 * @brief A time signature such as 4/4, 6/8 or 7/8.
 *
 * A "count" is one note of the denominator: a quarter note in x/4, an eighth note in x/8. Lengths are in
 * quarter notes, the unit of the host's musical position.
 */
struct TimeSignature
{
    static constexpr int maximumNumerator = 64;

    int numerator = 4;
    int denominator = 4;

    /**
     * @return The time signature reported by a host, or 4/4 when it makes no sense (numerator outside
     *         1..64, or a denominator that is not 1, 2, 4, 8, 16 or 32).
     */
    static TimeSignature fromHost (int hostNumerator, int hostDenominator) noexcept
    {
        const bool hasValidDenominator = hostDenominator == 1 || hostDenominator == 2 || hostDenominator == 4 ||
                                         hostDenominator == 8 || hostDenominator == 16 || hostDenominator == 32;
        if (! hasValidDenominator || hostNumerator < 1 || hostNumerator > maximumNumerator)
            return {};
        return { hostNumerator, hostDenominator };
    }

    /** @return The length of one count in quarter notes (1 in x/4, 0.5 in x/8). */
    double countLengthInQuarters() const noexcept { return 4.0 / denominator; }

    /** @return The length of a bar in quarter notes (4 in 4/4, 3 in 6/8, 3.5 in 7/8). */
    double barLengthInQuarters() const noexcept { return numerator * countLengthInQuarters(); }

    bool operator== (const TimeSignature& other) const noexcept
    {
        return numerator == other.numerator && denominator == other.denominator;
    }
    bool operator!= (const TimeSignature& other) const noexcept { return ! (*this == other); }
};

/**
 * @brief The beat structure of one bar: its counts, how they are grouped, which positions are strong and
 *        which counts are backbeats.
 *
 * Grouping rules (by the numerator n):
 *  - n up to 3: one group (3/4, 3/8, 2/4 …);
 *  - n divisible by 3: groups of three (6/8 = 3+3, 9/8, 12/8, 6/4);
 *  - other even n: groups of two (4/4 = 2+2, 4/8);
 *  - other odd n: groups of two plus one group of three, placed by OddMeterGrouping (7/8 = 2+2+3 …).
 *
 * Beats ("pulses") are the groups in x/8, x/16 and x/32 meters and in odd meters (5/4, 7/4), otherwise the
 * counts. Backbeats: with an even number of beats every second beat (2 and 4 in 4/4, the second group in
 * 6/8); with an odd number every beat after the first (2 and 3 in 3/4, groups 2 and 3 in 7/8).
 *
 * Built without allocation, so modules can make one per block on the audio thread.
 */
class BarStructure
{
public:
    /** How strong a position in the bar is; lower is stronger. */
    enum AccentLevel
    {
        barStart = 0,     ///< The first count of the bar
        groupStart = 1,   ///< The first count of a later group (beat 3 in 4/4, eighth 4 in 6/8)
        countStart = 2,   ///< Any other count (beats 2 and 4 in 4/4)
        halfCount = 3,    ///< Halfway through a count (the "and" of a beat in 4/4)
        quarterCount = 4, ///< A quarter or three quarters through a count (sixteenths in 4/4)
        finer = 5         ///< Anything else
    };
    static constexpr int numAccentLevels = 6;

    explicit BarStructure (TimeSignature meter, OddMeterGrouping grouping = OddMeterGrouping::longGroupLast) noexcept
        : timeSignature (meter)
    {
        const int numCounts = timeSignature.numerator;
        std::array<int, TimeSignature::maximumNumerator> groupLengths {};
        int numGroups = 0;
        if (numCounts <= 3)
            groupLengths[(size_t) numGroups++] = numCounts;
        else if (numCounts % 3 == 0)
            for (int group = 0; group < numCounts / 3; ++group)
                groupLengths[(size_t) numGroups++] = 3;
        else if (numCounts % 2 == 0)
            for (int group = 0; group < numCounts / 2; ++group)
                groupLengths[(size_t) numGroups++] = 2;
        else
        {
            numGroups = (numCounts - 3) / 2 + 1;
            const int longGroup = grouping == OddMeterGrouping::longGroupFirst    ? 0
                                  : grouping == OddMeterGrouping::longGroupMiddle ? numGroups / 2
                                                                                  : numGroups - 1;
            for (int group = 0; group < numGroups; ++group)
                groupLengths[(size_t) group] = group == longGroup ? 3 : 2;
        }

        int count = 0;
        for (int group = 0; group < numGroups; ++group)
        {
            startsGroup[(size_t) count] = true;
            count += groupLengths[(size_t) group];
        }

        const bool beatsAreGroups =
            timeSignature.denominator >= 8 || (numCounts >= 5 && numCounts % 2 != 0 && numCounts % 3 != 0);
        int numBeats = 0;
        std::array<int, TimeSignature::maximumNumerator> beatNumberOfCount {};
        for (count = 0; count < numCounts; ++count)
        {
            const bool startsBeat = ! beatsAreGroups || startsGroup[(size_t) count];
            beatNumberOfCount[(size_t) count] = startsBeat ? numBeats++ : -1;
        }
        for (count = 0; count < numCounts; ++count)
        {
            const int beat = beatNumberOfCount[(size_t) count];
            if (beat <= 0)
                continue;
            isBackbeatCount[(size_t) count] = (numBeats % 2 == 0) ? (beat % 2 == 1) : true;
        }
        numberOfGroups = numGroups;
    }

    const TimeSignature& getTimeSignature() const noexcept { return timeSignature; }
    int getNumberOfGroups() const noexcept { return numberOfGroups; }

    /** @return True if a count (0-based) starts a group of the bar; count 0 always does. */
    bool isGroupStart (int count) const noexcept { return isValidCount (count) && startsGroup[(size_t) count]; }

    /** @return True if a count (0-based) is a backbeat of the bar (see the class description). */
    bool isBackbeat (int count) const noexcept { return isValidCount (count) && isBackbeatCount[(size_t) count]; }

    /**
     * @return How strong a position is.
     * @param positionInBar Quarter notes from the start of the bar (0 up to the bar length).
     */
    AccentLevel accentLevelAt (double positionInBar) const noexcept
    {
        constexpr double tolerance = 1.0e-6;
        const double countPosition = positionInBar / timeSignature.countLengthInQuarters();
        const double nearestCount = std::round (countPosition);
        if (std::abs (countPosition - nearestCount) < tolerance)
        {
            const int count = (int) nearestCount % timeSignature.numerator;
            if (count == 0)
                return barStart;
            return isGroupStart (count) ? groupStart : countStart;
        }
        const double withinCount = countPosition - std::floor (countPosition);
        if (std::abs (withinCount - 0.5) < tolerance)
            return halfCount;
        if (std::abs (withinCount - 0.25) < tolerance || std::abs (withinCount - 0.75) < tolerance)
            return quarterCount;
        return finer;
    }

    /** @return The count (0-based) a position in the bar lies in. */
    int countAt (double positionInBar) const noexcept
    {
        const auto count = (int) std::floor (positionInBar / timeSignature.countLengthInQuarters() + 1.0e-6);
        return ((count % timeSignature.numerator) + timeSignature.numerator) % timeSignature.numerator;
    }

private:
    bool isValidCount (int count) const noexcept { return count >= 0 && count < timeSignature.numerator; }

    TimeSignature timeSignature;
    int numberOfGroups = 1;
    std::array<bool, TimeSignature::maximumNumerator> startsGroup {};
    std::array<bool, TimeSignature::maximumNumerator> isBackbeatCount {};
};

} // namespace midiforge
