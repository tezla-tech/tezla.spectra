// Copyright (c) 2026 The Tezla <thetezla@proton.me>
// Created by The Tezla -- https://github.com/wingit33/tezla.tech
// Music: https://soundcloud.com/thetezla | https://thetezla.bandcamp.com
// Built with development assistance from Claude (Anthropic).
// SPDX-License-Identifier: AGPL-3.0-only
// GNU AGPLv3 (see LICENSE), plus NOTICE.md's attribution term. Keep intact.

// Crossbar's tone tables, checked against the published standards.
//
// This file is unusual for this repository: almost nothing here is a
// measurement. The tables are *data taken from standards* (CLAUDE.md section
// 9's exception -- no measurement can tell you that 941 Hz should have been
// 940), so the tests are the second, independent statement of each figure.
// That is worth exactly as much as an independent statement usually is: it
// catches a transcription slip, a swapped row and column, an off-by-one in the
// keypad, a cadence that does not add up. It cannot catch both copies being
// wrong together, which is why DSP-REFERENCES.md records where each number
// came from and how confident that source is.
//
// The two structural tests -- the map being total and injective, and the
// regions differing only where documented -- are the ones that would survive a
// standard changing, and they are the ones with teeth.

#include <cmath>
#include <set>
#include <string>

#include <ToneTables.hpp>

#include "TestFramework.hpp"

using namespace tezla::crossbar;

namespace {

/// The Q.23 matrix, written out again from the standard rather than read from
/// the header -- the point of a table test is that it does not share the
/// table.
constexpr double kRows[4] { 697.0, 770.0, 852.0, 941.0 };
constexpr double kCols[4] { 1209.0, 1336.0, 1477.0, 1633.0 };

/// The published pair for every key, in keypad reading order.
struct KeyPair { char key; double low; double high; };

constexpr KeyPair kPublishedPairs[16] {
    { '1', 697.0, 1209.0 }, { '2', 697.0, 1336.0 }, { '3', 697.0, 1477.0 }, { 'A', 697.0, 1633.0 },
    { '4', 770.0, 1209.0 }, { '5', 770.0, 1336.0 }, { '6', 770.0, 1477.0 }, { 'B', 770.0, 1633.0 },
    { '7', 852.0, 1209.0 }, { '8', 852.0, 1336.0 }, { '9', 852.0, 1477.0 }, { 'C', 852.0, 1633.0 },
    { '*', 941.0, 1209.0 }, { '0', 941.0, 1336.0 }, { '#', 941.0, 1477.0 }, { 'D', 941.0, 1633.0 },
};

/// The tone whose name is this keypad character.
Tone toneForKey (char key)
{
    for (int i = 0; i < kToneCount; ++i)
    {
        const auto tone = static_cast<Tone> (i);

        if (isDtmf (tone) && std::string (nameFor (tone)) == std::string (1, key))
            return tone;
    }

    return Tone::count;
}

/// The step a program spends sounding, summed -- the "on" part of a cadence.
double soundingSeconds (const ToneProgram& program)
{
    double total = 0.0;

    for (int i = 0; i < program.stepCount; ++i)
        if (program.steps[i].partials > 0)
            total += program.steps[i].seconds;

    return total;
}

} // namespace

// ---------------------------------------------------------------------------
// DTMF -- ITU-T Q.23
// ---------------------------------------------------------------------------

TEZLA_TEST (every_dtmf_key_is_exactly_its_q23_pair)
{
    // The whole plugin rests on this one. A DTMF pair is not a sound that
    // resembles a standard, it is the standard: a decoder accepts +/-1.5% and
    // rejects everything else, so a transposed digit or a swapped row and
    // column is a plugin that dials the wrong number.
    //
    // Measured here: every one of the sixteen pairs is exact to the bit --
    // the frequencies are literals in both this file and the header, so the
    // difference is 0.0 Hz rather than merely small.
    for (const auto& published : kPublishedPairs)
    {
        const Tone tone = toneForKey (published.key);
        CHECK (tone != Tone::count);

        const ToneProgram program = programFor (tone, Region::northAmerica);

        CHECK (program.stepCount == 1);
        CHECK (program.steps[0].partials == 2);
        CHECK (program.steps[0].seconds == kForever);

        CHECK_NEAR (program.steps[0].frequency[0], published.low, 0.0);
        CHECK_NEAR (program.steps[0].frequency[1], published.high, 0.0);

        // And comfortably inside the tolerance a real receiver applies, which
        // is the statement that actually matters to a decoder.
        CHECK (std::abs (program.steps[0].frequency[0] - published.low)
                 < published.low * kDtmfToleranceFraction);
        CHECK (std::abs (program.steps[0].frequency[1] - published.high)
                 < published.high * kDtmfToleranceFraction);
    }
}

TEZLA_TEST (the_dtmf_matrix_uses_each_row_and_column_exactly_four_times)
{
    // The structural half of the check above: a table can have every entry
    // drawn from the right set of frequencies and still be wrong, if two keys
    // share a pair or a row is used five times. Sixteen keys, four rows, four
    // columns, every combination once.
    int rowUse[4] {};
    int colUse[4] {};
    std::set<std::pair<int, int>> seen;

    for (int i = 0; i < 16; ++i)
    {
        const auto tone = static_cast<Tone> (i);
        CHECK (isDtmf (tone));

        int row = -1, column = -1;
        CHECK (dtmfIndices (tone, row, column));
        CHECK (row >= 0 && row < 4);
        CHECK (column >= 0 && column < 4);

        ++rowUse[row];
        ++colUse[column];
        seen.insert ({ row, column });
    }

    CHECK (seen.size() == 16u);

    for (int i = 0; i < 4; ++i)
    {
        CHECK (rowUse[i] == 4);
        CHECK (colUse[i] == 4);
        CHECK_NEAR (kDtmfRowHz[i], kRows[i], 0.0);
        CHECK_NEAR (kDtmfColHz[i], kCols[i], 0.0);
    }
}

TEZLA_TEST (dtmf_is_international_and_does_not_change_with_the_region)
{
    // Everything else in this file is regional. DTMF is not: Q.23 is the same
    // in London as in New York, and a plugin that "localised" it would be
    // dialling gibberish. Asserted rather than assumed, because the region
    // switch touches every other tone and it would be an easy mistake to let
    // it touch these too.
    for (int i = 0; i < 16; ++i)
    {
        const auto tone = static_cast<Tone> (i);

        const ToneProgram us = programFor (tone, Region::northAmerica);
        const ToneProgram uk = programFor (tone, Region::unitedKingdom);

        for (int p = 0; p < kMaxPartials; ++p)
        {
            CHECK_NEAR (us.steps[0].frequency[p], uk.steps[0].frequency[p], 0.0);
            CHECK_NEAR (us.steps[0].gain[p], uk.steps[0].gain[p], 0.0);
        }
    }
}

TEZLA_TEST (twist_is_the_level_difference_q24_defines_and_nothing_else)
{
    // Q.24 defines twist as the high group's level relative to the low
    // group's, and requires a receiver to accept 8 dB normal and 4 dB
    // reverse. Transmitters use about +2 dB to survive line loss, so that is
    // the default.
    //
    // The gains are normalised to sum to exactly 1.0, so the *difference* is
    // the twist and the sum never moves. That second half is not tidiness: the
    // partials of a step start in phase, so the sum of the gains is the peak,
    // and the first version of this -- a symmetric split with no normalisation
    // -- peaked at 1.0067 on the default +2 dB twist. A plugin that clips on
    // its own default setting. This test found it.
    //
    // Measured across -8 to +8 dB in half-decibel steps: the difference is the
    // twist asked for to within 1e-13 dB, the sum of the gains is 1.0 to
    // within 1e-15 at every one of them, and the pair's total *power* rises by
    // up to 0.744 dB at the extremes -- which is inherent to holding the peak
    // fixed while the balance moves, and is the trade taken deliberately.
    double worstPowerDeviationDb = 0.0;

    for (double twist = -8.0; twist <= 8.0001; twist += 0.5)
    {
        const ToneProgram program = programFor (Tone::digit5, Region::northAmerica, twist);

        const double low  = program.steps[0].gain[0];
        const double high = program.steps[0].gain[1];

        const double measured = 20.0 * std::log10 (high / low);
        CHECK_NEAR (measured, twist, 1.0e-13);

        CHECK_NEAR (low + high, 1.0, 1.0e-15);

        const double powerDb = 10.0 * std::log10 ((low * low + high * high) / (2.0 * 0.25));
        worstPowerDeviationDb = std::max (worstPowerDeviationDb, std::abs (powerDb));
    }

    CHECK_NEAR (worstPowerDeviationDb, 0.744, 0.01);

    // At twist 0 the two partials are exactly equal and exactly a half.
    const ToneProgram flat = programFor (Tone::digit5, Region::northAmerica, 0.0);
    CHECK_NEAR (flat.steps[0].gain[0], 0.5, 0.0);
    CHECK_NEAR (flat.steps[0].gain[1], 0.5, 0.0);
}

// ---------------------------------------------------------------------------
// Call progress -- the Bell Precise Tone Plan and the BT set
// ---------------------------------------------------------------------------

TEZLA_TEST (the_north_american_call_progress_tones_are_the_precise_tone_plan)
{
    // Dial 350+440 continuous; ringback 440+480 at 2 s on / 4 s off; busy
    // 480+620 at 0.5/0.5; reorder the same pair at 0.25/0.25. Four
    // frequencies, four signals, and the cadence is what tells them apart --
    // busy and reorder are the *same tone at different speeds*, which is why
    // a fast busy means the network is full rather than the person is.
    {
        const ToneProgram dial = programFor (Tone::dialTone, Region::northAmerica);
        CHECK (dial.stepCount == 1);
        CHECK (dial.steps[0].partials == 2);
        CHECK_NEAR (dial.steps[0].frequency[0], 350.0, 0.0);
        CHECK_NEAR (dial.steps[0].frequency[1], 440.0, 0.0);
        CHECK (dial.steps[0].seconds == kForever);
        CHECK (periodSeconds (dial) == kForever);
    }

    {
        const ToneProgram ring = programFor (Tone::ringback, Region::northAmerica);
        CHECK (ring.loops);
        CHECK (ring.stepCount == 2);
        CHECK_NEAR (ring.steps[0].frequency[0], 440.0, 0.0);
        CHECK_NEAR (ring.steps[0].frequency[1], 480.0, 0.0);
        CHECK_NEAR (ring.steps[0].seconds, 2.0, 0.0);
        CHECK_NEAR (ring.steps[1].seconds, 4.0, 0.0);
        CHECK (ring.steps[1].partials == 0);
        CHECK_NEAR (periodSeconds (ring), 6.0, 0.0);
    }

    {
        const ToneProgram busy = programFor (Tone::busy, Region::northAmerica);
        CHECK (busy.loops);
        CHECK_NEAR (busy.steps[0].frequency[0], 480.0, 0.0);
        CHECK_NEAR (busy.steps[0].frequency[1], 620.0, 0.0);
        CHECK_NEAR (periodSeconds (busy), 1.0, 0.0);
        CHECK_NEAR (soundingSeconds (busy), 0.5, 0.0);

        const ToneProgram reorder = programFor (Tone::congestion, Region::northAmerica);
        CHECK_NEAR (reorder.steps[0].frequency[0], 480.0, 0.0);
        CHECK_NEAR (reorder.steps[0].frequency[1], 620.0, 0.0);
        CHECK_NEAR (periodSeconds (reorder), 0.5, 0.0);

        // Exactly twice the rate, which is the entire distinction.
        CHECK_NEAR (periodSeconds (busy) / periodSeconds (reorder), 2.0, 1.0e-15);
    }
}

TEZLA_TEST (the_precise_tone_plans_level_differences_are_honoured)
{
    // -13 dBm dial, -19 dBm ringback, -24 dBm busy and reorder. The absolute
    // reference is meaningless in a plugin -- there is no line -- but the
    // differences are audible and are the reason a busy tone has always
    // sounded further away than a dial tone.
    //
    // Measured against the dial tone: ringback -6.00 dB, busy -11.00 dB,
    // reorder -11.00 dB, matching the plan's 6 and 11 dB steps exactly.
    const auto stepLevelDb = [] (const ToneProgram& program)
    {
        double sum = 0.0;

        for (int i = 0; i < program.steps[0].partials; ++i)
            sum += program.steps[0].gain[i];

        return 20.0 * std::log10 (sum);
    };

    const double dial = stepLevelDb (programFor (Tone::dialTone, Region::northAmerica));
    CHECK_NEAR (dial, 0.0, 1.0e-12);

    CHECK_NEAR (stepLevelDb (programFor (Tone::ringback, Region::northAmerica)) - dial,
                -6.0, 1.0e-12);
    CHECK_NEAR (stepLevelDb (programFor (Tone::busy, Region::northAmerica)) - dial,
                -11.0, 1.0e-12);
    CHECK_NEAR (stepLevelDb (programFor (Tone::congestion, Region::northAmerica)) - dial,
                -11.0, 1.0e-12);

    // And no step anywhere, in either region, peaks above full scale -- the
    // sum of a step's gains is its peak, because the partials start in phase.
    for (int t = 0; t < kToneCount; ++t)
        for (auto region : { Region::northAmerica, Region::unitedKingdom })
        {
            const ToneProgram program = programFor (static_cast<Tone> (t), region);

            for (int s = 0; s < program.stepCount; ++s)
            {
                double peak = 0.0;

                for (int p = 0; p < program.steps[s].partials; ++p)
                    peak += program.steps[s].gain[p];

                CHECK (peak <= 1.0 + 1.0e-12);
            }
        }
}

TEZLA_TEST (the_uk_set_is_bts_and_its_congestion_tone_steps_six_decibels)
{
    // BT: dial 350+450 continuous (the 100 Hz beat is the sound people mean
    // when they say "British dial tone"); engaged 400 Hz at 0.375/0.375;
    // ringing 400+450 as two 400 ms bursts 200 ms apart then two seconds of
    // silence; number unobtainable a continuous 400 Hz; congestion 400 Hz at
    // 0.4 / 0.35 / 0.225 / 0.525 with the second burst 6 dB louder.
    {
        const ToneProgram dial = programFor (Tone::dialTone, Region::unitedKingdom);
        CHECK (dial.steps[0].partials == 2);
        CHECK_NEAR (dial.steps[0].frequency[0], 350.0, 0.0);
        CHECK_NEAR (dial.steps[0].frequency[1], 450.0, 0.0);

        // 100 Hz apart: the beat is the identity of the tone, so it is worth
        // stating as a difference rather than as two numbers.
        CHECK_NEAR (dial.steps[0].frequency[1] - dial.steps[0].frequency[0], 100.0, 0.0);
    }

    {
        const ToneProgram engaged = programFor (Tone::busy, Region::unitedKingdom);
        CHECK (engaged.stepCount == 2);
        CHECK (engaged.steps[0].partials == 1);
        CHECK_NEAR (engaged.steps[0].frequency[0], 400.0, 0.0);
        CHECK_NEAR (engaged.steps[0].seconds, 0.375, 0.0);
        CHECK_NEAR (periodSeconds (engaged), 0.75, 0.0);
    }

    {
        const ToneProgram ring = programFor (Tone::ringback, Region::unitedKingdom);
        CHECK (ring.stepCount == 4);
        CHECK (ring.loops);
        CHECK_NEAR (ring.steps[0].seconds, 0.4, 0.0);
        CHECK_NEAR (ring.steps[1].seconds, 0.2, 0.0);
        CHECK_NEAR (ring.steps[2].seconds, 0.4, 0.0);
        CHECK_NEAR (ring.steps[3].seconds, 2.0, 0.0);
        CHECK_NEAR (periodSeconds (ring), 3.0, 1.0e-15);

        // Both bursts carry both frequencies: 400 and 450, a 50 Hz beat.
        for (int s : { 0, 2 })
        {
            CHECK (ring.steps[s].partials == 2);
            CHECK_NEAR (ring.steps[s].frequency[1] - ring.steps[s].frequency[0], 50.0, 0.0);
        }
    }

    {
        const ToneProgram nu = programFor (Tone::unobtainable, Region::unitedKingdom);
        CHECK (nu.stepCount == 1);
        CHECK (nu.steps[0].seconds == kForever);
        CHECK_NEAR (nu.steps[0].frequency[0], 400.0, 0.0);
    }

    {
        const ToneProgram congestion = programFor (Tone::congestion, Region::unitedKingdom);
        CHECK (congestion.stepCount == 4);
        CHECK_NEAR (congestion.steps[0].seconds, 0.400, 0.0);
        CHECK_NEAR (congestion.steps[1].seconds, 0.350, 0.0);
        CHECK_NEAR (congestion.steps[2].seconds, 0.225, 0.0);
        CHECK_NEAR (congestion.steps[3].seconds, 0.525, 0.0);
        CHECK_NEAR (periodSeconds (congestion), 1.5, 1.0e-15);

        // The level step is the character. Measured: exactly 6.00 dB.
        const double quiet = 20.0 * std::log10 (congestion.steps[0].gain[0]);
        const double loud  = 20.0 * std::log10 (congestion.steps[2].gain[0]);
        CHECK_NEAR (loud - quiet, 6.0, 1.0e-12);
    }
}

TEZLA_TEST (north_america_has_no_unobtainable_tone_so_that_key_gives_the_long_sit)
{
    // A documented asymmetry rather than an oversight. The UK has a dedicated
    // number-unobtainable tone; North America signals the same condition with
    // an intercept SIT and a recording. So in the Bell region that key gives
    // the *long* SIT variant, leaving the short one on its own key -- two
    // different real signals instead of one wasted key.
    const ToneProgram longSit = programFor (Tone::unobtainable, Region::northAmerica);
    const ToneProgram shortSit = programFor (Tone::sit, Region::northAmerica);

    CHECK (longSit.stepCount == 3);
    CHECK (shortSit.stepCount == 3);
    CHECK (! longSit.loops);
    CHECK (! shortSit.loops);

    CHECK_NEAR (longSit.steps[0].frequency[0], 985.2, 0.0);
    CHECK_NEAR (longSit.steps[1].frequency[0], 1428.5, 0.0);
    CHECK_NEAR (shortSit.steps[0].frequency[0], 913.8, 0.0);
    CHECK_NEAR (shortSit.steps[1].frequency[0], 1370.6, 0.0);

    // The third segment is 1776.7 Hz for 380 ms in every variant.
    CHECK_NEAR (longSit.steps[2].frequency[0], 1776.7, 0.0);
    CHECK_NEAR (shortSit.steps[2].frequency[0], 1776.7, 0.0);
    CHECK_NEAR (longSit.steps[2].seconds, 0.380, 0.0);
    CHECK_NEAR (shortSit.steps[2].seconds, 0.380, 0.0);

    // The variants differ in the first two segments' duration: 380 ms long,
    // 274 ms short.
    CHECK_NEAR (longSit.steps[0].seconds, 0.380, 0.0);
    CHECK_NEAR (shortSit.steps[0].seconds, 0.274, 0.0);

    // Ascending, which is what makes it recognisable across a bad line.
    for (const ToneProgram* sit : { &longSit, &shortSit })
        for (int s = 1; s < sit->stepCount; ++s)
            CHECK (sit->steps[s].frequency[0] > sit->steps[s - 1].frequency[0]);
}

TEZLA_TEST (the_howler_is_four_frequencies_at_a_tenth_of_a_second)
{
    // 1400 + 2060 + 2450 + 2600 Hz, 0.1 s on and 0.1 s off, in both regions --
    // no BT-specific figure was obtainable, and saying so beats inventing one.
    for (auto region : { Region::northAmerica, Region::unitedKingdom })
    {
        const ToneProgram howler = programFor (Tone::howler, region);

        CHECK (howler.loops);
        CHECK (howler.stepCount == 2);
        CHECK (howler.steps[0].partials == kMaxPartials);
        CHECK_NEAR (howler.steps[0].frequency[0], 1400.0, 0.0);
        CHECK_NEAR (howler.steps[0].frequency[1], 2060.0, 0.0);
        CHECK_NEAR (howler.steps[0].frequency[2], 2450.0, 0.0);
        CHECK_NEAR (howler.steps[0].frequency[3], 2600.0, 0.0);
        CHECK_NEAR (periodSeconds (howler), 0.2, 1.0e-15);

        // It is the loudest thing here, at the dial tone's level -- the one
        // published figure deliberately not honoured, because 0 dBm0 per
        // frequency times four would clip every time. See ToneTables.hpp.
        double peak = 0.0;

        for (int p = 0; p < kMaxPartials; ++p)
            peak += howler.steps[0].gain[p];

        CHECK_NEAR (peak, 1.0, 1.0e-12);
    }
}

TEZLA_TEST (the_rotary_dial_is_ten_pulses_a_second_and_carries_clicks_not_tones)
{
    // A rotary dial does not make a tone at all: it breaks the loop current
    // ten times a second, 60% break and 40% make, and what reaches the
    // earpiece is the transient at each edge. So these steps carry a click
    // flag and no frequency -- and a test that only looked at frequencies
    // would have called this program silent.
    const ToneProgram pulse = programFor (Tone::rotaryPulse, Region::northAmerica);

    CHECK (pulse.loops);
    CHECK (pulse.stepCount == 2);
    CHECK (pulse.steps[0].click);
    CHECK (pulse.steps[1].click);
    CHECK (pulse.steps[0].partials == 0);
    CHECK (pulse.steps[1].partials == 0);
    CHECK_NEAR (pulse.steps[0].seconds, 0.060, 0.0);
    CHECK_NEAR (pulse.steps[1].seconds, 0.040, 0.0);

    // Ten a second, exactly.
    CHECK_NEAR (1.0 / periodSeconds (pulse), 10.0, 1.0e-12);
}

TEZLA_TEST (the_data_tones_are_the_published_carrier_frequencies)
{
    // CNG 1100 Hz at 0.5 s on / 3 s off -- what a fax sends while it waits.
    // CED 2100 Hz continuous -- what the answering modem replies with.
    // SF 2600 Hz continuous -- the supervision tone that told an old trunk the
    // line was idle, and the most famous frequency in telephone history.
    const ToneProgram cng = programFor (Tone::faxCalling, Region::northAmerica);
    CHECK (cng.loops);
    CHECK_NEAR (cng.steps[0].frequency[0], 1100.0, 0.0);
    CHECK_NEAR (cng.steps[0].seconds, 0.5, 0.0);
    CHECK_NEAR (periodSeconds (cng), 3.5, 0.0);

    const ToneProgram ced = programFor (Tone::modemAnswer, Region::northAmerica);
    CHECK (ced.steps[0].seconds == kForever);
    CHECK_NEAR (ced.steps[0].frequency[0], 2100.0, 0.0);

    const ToneProgram sf = programFor (Tone::singleFrequency, Region::northAmerica);
    CHECK (sf.steps[0].seconds == kForever);
    CHECK_NEAR (sf.steps[0].frequency[0], 2600.0, 0.0);
}

TEZLA_TEST (the_eight_constituent_frequencies_are_the_matrix_taken_apart)
{
    // Eight keys giving one DTMF frequency each, for sound design. They must
    // be the *same* eight numbers the matrix uses -- if these drifted, the
    // matrix and the singles would be two sources of truth.
    const Tone singles[8] {
        Tone::row697, Tone::row770, Tone::row852, Tone::row941,
        Tone::col1209, Tone::col1336, Tone::col1477, Tone::col1633,
    };

    for (int i = 0; i < 8; ++i)
    {
        const ToneProgram program = programFor (singles[i], Region::northAmerica);

        CHECK (program.stepCount == 1);
        CHECK (program.steps[0].partials == 1);
        CHECK (program.steps[0].seconds == kForever);
        CHECK_NEAR (program.steps[0].frequency[0], i < 4 ? kRows[i] : kCols[i - 4], 0.0);

        // One partial at the dial tone's level, so a single frequency peaks at
        // full scale rather than at half.
        CHECK_NEAR (program.steps[0].gain[0], 1.0, 1.0e-12);
    }
}

// ---------------------------------------------------------------------------
// The keyboard map
// ---------------------------------------------------------------------------

TEZLA_TEST (the_key_map_is_total_and_injective_over_its_range)
{
    // The structural test with teeth, and the one that survives a standard
    // changing. Thirty-seven consecutive notes from the root, every tone
    // reachable, no tone reachable twice, and everything outside the range
    // silent rather than wrapped -- a wrap would make a bass note play a
    // keypad digit, which is the kind of bug you find on stage.
    std::set<int> reached;

    for (int note = 0; note < 128; ++note)
    {
        const Tone tone = toneForNote (note, kDefaultMapRoot);

        if (note < kDefaultMapRoot || note >= kDefaultMapRoot + kToneCount)
        {
            CHECK (tone == Tone::count);
            continue;
        }

        CHECK (tone != Tone::count);
        CHECK (reached.insert (static_cast<int> (tone)).second);
        CHECK (noteForTone (tone, kDefaultMapRoot) == note);
    }

    CHECK (reached.size() == static_cast<std::size_t> (kToneCount));
    CHECK (kToneCount == 37);
}

TEZLA_TEST (the_first_octave_is_a_telephone_keypad)
{
    // The layout choice, asserted so it cannot drift: twelve semitones from
    // the root are `1 2 3 4 5 6 7 8 9 * 0 #`, which means one octave played by
    // hand is the whole domestic keypad and a phone number is a melody in it.
    const char* expected[12] { "1", "2", "3", "4", "5", "6", "7", "8", "9", "*", "0", "#" };

    for (int i = 0; i < 12; ++i)
    {
        const Tone tone = toneForNote (kDefaultMapRoot + i);
        CHECK (std::string (nameFor (tone)) == std::string (expected[i]));
    }

    // C1 is the root, where a drum map starts.
    CHECK (kDefaultMapRoot == 36);

    // The map moves as a block: every offset is preserved at any root.
    for (int root : { 0, 24, 36, 60, 91 })
        for (int i = 0; i < kToneCount; ++i)
            CHECK (toneForNote (root + i, root) == static_cast<Tone> (i));
}

TEZLA_TEST (every_tone_has_a_name_and_every_playable_one_has_a_program)
{
    // A missing name is a blank button in the editor; a missing program is a
    // silent key. Both are cheap to check and neither shows up in a build.
    for (int i = 0; i < kToneCount; ++i)
    {
        const auto tone = static_cast<Tone> (i);

        CHECK (std::string (nameFor (tone)).length() > 0);

        if (tone == Tone::dialNumber)
            continue;   // the engine plays the stored string; no program of its own

        for (auto region : { Region::northAmerica, Region::unitedKingdom })
        {
            const ToneProgram program = programFor (tone, region);
            CHECK (program.stepCount > 0);
            CHECK (program.stepCount <= kMaxSteps);

            // Every step either sounds, clicks, or is a deliberate gap in a
            // cadence -- and a gap only exists inside a program that loops or
            // has something before it.
            bool anythingHappens = false;

            for (int s = 0; s < program.stepCount; ++s)
                anythingHappens = anythingHappens
                                    || program.steps[s].partials > 0
                                    || program.steps[s].click;

            CHECK (anythingHappens);
        }
    }
}

TEZLA_TEST (the_dialler_reads_a_written_phone_number)
{
    // A phone number as people write it: spaces, dashes and brackets are
    // skipped rather than dialled, and the sixteen real keys map to the
    // matrix. This is what makes "+1 (555) 010-4477" playable.
    int row = -1, column = -1;

    CHECK (dtmfForCharacter ('5', row, column));
    CHECK (row == 1 && column == 1);

    CHECK (dtmfForCharacter ('0', row, column));
    CHECK (row == 3 && column == 1);

    CHECK (dtmfForCharacter ('#', row, column));
    CHECK (row == 3 && column == 2);

    CHECK (dtmfForCharacter ('D', row, column));
    CHECK (row == 3 && column == 3);

    for (char c : { ' ', '-', '(', ')', '+', '.', 'x', 'a' })
        CHECK (! dtmfForCharacter (c, row, column));

    // Pulse dialling counts loop breaks, and '0' is ten of them -- which is
    // why 999 and 911 were chosen to be quick to dial and 000 was not.
    CHECK (pulsesForCharacter ('1') == 1);
    CHECK (pulsesForCharacter ('9') == 9);
    CHECK (pulsesForCharacter ('0') == 10);
    CHECK (pulsesForCharacter ('#') == 0);
    CHECK (pulsesForCharacter ('A') == 0);
}
