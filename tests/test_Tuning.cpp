// Copyright (c) 2026 The Tezla <thetezla@proton.me>
// Created by The Tezla -- https://github.com/wingit33/tezla.tech
// Music: https://soundcloud.com/thetezla | https://thetezla.bandcamp.com
// Built with development assistance from Claude (Anthropic).
// SPDX-License-Identifier: AGPL-3.0-only
// GNU AGPLv3 (see LICENSE), plus NOTICE.md's attribution term. Keep intact.

#include "TestFramework.hpp"

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

#include <tezla/dsp/ScalaFile.hpp>
#include <tezla/dsp/Scales.hpp>
#include <tezla/dsp/Tuning.hpp>

using namespace tezla::dsp;

namespace
{
double centsBetween (double lower, double upper)
{
    return 1200.0 * std::log2 (upper / lower);
}
} // namespace

// ---------------------------------------------------------------------------
// The mapping
// ---------------------------------------------------------------------------

TEZLA_TEST (twelve_tone_equal_reproduces_the_standard_pitches)
{
    // The floor of everything: whatever else this does, an untouched Sonitus
    // has to play in tune with the rest of the project.
    Tuning tuning;

    for (int note = 0; note <= 127; ++note)
    {
        const double expected = 440.0 * std::pow (2.0, (note - 69) / 12.0);

        // Not bit-exact and it cannot be: the tuning reaches a note through a
        // repeat count and a degree ratio, while the formula reaches it in one
        // exponential, and the two round differently in the last place. Ten
        // parts per billion is 1.7e-8 cents.
        CHECK_NEAR (tuning.frequencyFor (note) / expected, 1.0, 1.0e-8);
    }

    CHECK_NEAR (tuning.frequencyFor (69), 440.0, 1.0e-12);
    CHECK_NEAR (tuning.frequencyFor (60), 261.6255653005986, 1.0e-9);
    CHECK_NEAR (tuning.frequencyFor (21), 27.5, 1.0e-9);
}

TEZLA_TEST (notes_below_the_root_land_on_the_right_degree)
{
    // Floor division, and why it gets its own test: `-9 / 12` is 0 in C++ and
    // -1 here. Getting it wrong puts every note below the root on the wrong
    // degree *and* in the wrong octave, and the two errors partly cancel -- so
    // it sounds nearly right, which is the worst way to be wrong.
    //
    // A scale with wildly uneven steps makes the failure loud.
    Tuning tuning;

    Scale lopsided;
    lopsided.name = "lopsided";
    lopsided.ratios = { 1.0, 1.02, 1.9 };
    lopsided.repeat = 2.0;

    CHECK (tuning.setScale (lopsided));

    tuning.setRootNote (60);
    tuning.setReference (60, 100.0);

    // Three degrees per repeat, so key 60 is degree 0, 61 is degree 1, 62 is
    // degree 2, 63 is degree 0 an octave up -- and 59 is degree 2 an octave
    // *down*, not degree -1.
    CHECK_NEAR (tuning.frequencyFor (60), 100.0, 1.0e-12);
    CHECK_NEAR (tuning.frequencyFor (61), 102.0, 1.0e-12);
    CHECK_NEAR (tuning.frequencyFor (62), 190.0, 1.0e-12);
    CHECK_NEAR (tuning.frequencyFor (63), 200.0, 1.0e-12);

    CHECK_NEAR (tuning.frequencyFor (59), 95.0, 1.0e-12);
    CHECK_NEAR (tuning.frequencyFor (58), 51.0, 1.0e-12);
    CHECK_NEAR (tuning.frequencyFor (57), 50.0, 1.0e-12);

    // And it stays monotonic all the way down, which a wrong floor division
    // would break.
    for (int note = 1; note <= 127; ++note)
        CHECK (tuning.frequencyFor (note) > tuning.frequencyFor (note - 1));
}

TEZLA_TEST (a_non_octave_repeat_still_round_trips)
{
    // Bohlen-Pierce repeats at 3/1, thirteen notes at a time. This is the
    // property that a tuning built around a hard-coded octave silently fails.
    Tuning tuning;

    CHECK (tuning.setScale (scales::bohlenPierce()));

    tuning.setRootNote (60);
    tuning.setReference (60, 220.0);

    CHECK_NEAR (tuning.frequencyFor (60), 220.0, 1.0e-12);

    // Thirteen keys up is a tritave, not an octave.
    CHECK_NEAR (tuning.frequencyFor (73), 660.0, 1.0e-9);
    CHECK_NEAR (tuning.frequencyFor (47), 220.0 / 3.0, 1.0e-9);

    // Twelve keys up is *not* a doubling, which is the whole point.
    CHECK (std::abs (tuning.frequencyFor (72) - 440.0) > 5.0);

    // Every step is the same size, and it is a thirteenth of a tritave.
    const double step = 1200.0 * std::log2 (3.0) / 13.0;

    for (int note = 61; note <= 100; ++note)
        CHECK_NEAR (centsBetween (tuning.frequencyFor (note - 1), tuning.frequencyFor (note)),
                    step, 1.0e-9);

    CHECK_NEAR (step, 146.304231, 1.0e-6);
}

TEZLA_TEST (the_reference_pins_one_key_and_the_scale_does_the_rest)
{
    Tuning tuning;

    CHECK (tuning.setScale (scales::justMajor()));

    tuning.setRootNote (60);
    tuning.setReference (69, 432.0);

    CHECK_NEAR (tuning.frequencyFor (69), 432.0, 1.0e-12);

    // Seven degrees per octave now, so key 67 is one octave and no degrees
    // above key 60.
    CHECK_NEAR (tuning.frequencyFor (67) / tuning.frequencyFor (60), 2.0, 1.0e-12);

    // And the degrees are the exact ratios, not approximations of them.
    CHECK_NEAR (tuning.frequencyFor (64) / tuning.frequencyFor (60), 3.0 / 2.0, 1.0e-12);
    CHECK_NEAR (tuning.frequencyFor (62) / tuning.frequencyFor (60), 5.0 / 4.0, 1.0e-12);
}

TEZLA_TEST (an_unusable_scale_is_refused_rather_than_installed)
{
    Tuning tuning;

    const std::string original = tuning.getScale().name;

    Scale empty;
    empty.ratios.clear();
    CHECK (! tuning.setScale (empty));

    Scale descending;
    descending.ratios = { 1.0, 1.5, 1.2 };
    descending.repeat = 2.0;
    CHECK (! tuning.setScale (descending));

    Scale pastTheRepeat;
    pastTheRepeat.ratios = { 1.0, 1.5, 2.5 };
    pastTheRepeat.repeat = 2.0;
    CHECK (! tuning.setScale (pastTheRepeat));

    Scale noRepeat;
    noRepeat.ratios = { 1.0, 1.5 };
    noRepeat.repeat = 1.0;
    CHECK (! tuning.setScale (noRepeat));

    Scale wrongTonic;
    wrongTonic.ratios = { 1.1, 1.5 };
    wrongTonic.repeat = 2.0;
    CHECK (! tuning.setScale (wrongTonic));

    // And after all that, still in tune.
    CHECK (tuning.getScale().name == original);
    CHECK_NEAR (tuning.frequencyFor (69), 440.0, 1.0e-12);
}

TEZLA_TEST (a_note_outside_the_midi_range_is_silent_rather_than_wrong)
{
    Tuning tuning;

    CHECK (tuning.frequencyFor (-1) == 0.0);
    CHECK (tuning.frequencyFor (128) == 0.0);
    CHECK (tuning.frequencyFor (1000000) == 0.0);
}

// ---------------------------------------------------------------------------
// Keyboard maps
// ---------------------------------------------------------------------------

TEZLA_TEST (a_keyboard_map_can_leave_keys_unmapped)
{
    // A hole in a map is a key that plays nothing, which is the correct
    // behaviour rather than an error -- it is how a seven-note scale is laid
    // out on the white keys.
    Tuning tuning;

    CHECK (tuning.setScale (scales::justMajor()));

    KeyboardMap map;
    map.size = 12;
    map.middleNote = 60;
    map.referenceNote = 60;
    map.referenceHz = 261.0;
    map.formalOctaveDegree = 0;
    map.degrees = { 0, KeyboardMap::kUnmapped, 1, KeyboardMap::kUnmapped, 2, 3,
                    KeyboardMap::kUnmapped, 4, KeyboardMap::kUnmapped, 5,
                    KeyboardMap::kUnmapped, 6 };

    tuning.setKeyboardMap (map);

    CHECK_NEAR (tuning.frequencyFor (60), 261.0, 1.0e-12);
    CHECK (tuning.frequencyFor (61) == 0.0);
    CHECK_NEAR (tuning.frequencyFor (62) / 261.0, 9.0 / 8.0, 1.0e-12);
    CHECK (tuning.frequencyFor (63) == 0.0);
    CHECK_NEAR (tuning.frequencyFor (64) / 261.0, 5.0 / 4.0, 1.0e-12);
    CHECK_NEAR (tuning.frequencyFor (67) / 261.0, 3.0 / 2.0, 1.0e-12);

    // Twelve keys up is one repeat of the map, which is one octave.
    CHECK_NEAR (tuning.frequencyFor (72) / tuning.frequencyFor (60), 2.0, 1.0e-12);

    // And below the middle note too.
    CHECK_NEAR (tuning.frequencyFor (48) / tuning.frequencyFor (60), 0.5, 1.0e-12);
    CHECK (tuning.frequencyFor (49) == 0.0);
}

TEZLA_TEST (a_map_can_be_narrower_than_the_keyboard)
{
    Tuning tuning;

    KeyboardMap map;
    map.size = 12;
    map.firstNote = 48;
    map.lastNote = 72;
    map.middleNote = 60;
    map.referenceNote = 69;
    map.referenceHz = 440.0;
    map.degrees = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11 };

    tuning.setKeyboardMap (map);

    CHECK (tuning.frequencyFor (47) == 0.0);
    CHECK (tuning.frequencyFor (73) == 0.0);
    CHECK (tuning.frequencyFor (48) > 0.0);
    CHECK (tuning.frequencyFor (72) > 0.0);
    CHECK_NEAR (tuning.frequencyFor (69), 440.0, 1.0e-12);
}

// ---------------------------------------------------------------------------
// The scales, checked against their definitions
// ---------------------------------------------------------------------------

TEZLA_TEST (a_pythagorean_fifth_is_701_955_cents_and_not_700)
{
    // The headline number, and the difference the whole idea rests on.
    const Scale scale = scales::pythagorean();

    CHECK (scale.size() == 12);
    CHECK (scale.isUsable());

    // Degree 7 of the sorted chromatic is the fifth.
    CHECK_NEAR (scale.ratios[7], 1.5, 1.0e-15);
    CHECK_NEAR (scale.cents (7), 701.9550008653874, 1.0e-9);

    // Its major third is 81/64 -- 21.5 cents wide of a just 5/4, which is why
    // it sounds the way it does.
    CHECK_NEAR (scale.ratios[4], 81.0 / 64.0, 1.0e-15);
    CHECK_NEAR (scale.cents (4), 407.82, 0.01);
    CHECK_NEAR (scale.cents (4) - 1200.0 * std::log2 (1.25), 21.506, 0.001);

    // The minor second is the Pythagorean limma, and **bit-exactly** 256/243
    // rather than nearly so. That is the one degree where building the chain
    // in integers differs from a running multiply by 1.5 -- three in the last
    // place, which a CHECK_NEAR at 1e-15 cannot see and which is the reason
    // this one assertion is an equality. Break-checking found the tolerance
    // version passing on either construction.
    CHECK (scale.ratios[1] == 256.0 / 243.0);

    // Eleven of the twelve fifths are pure. The twelfth is the wolf, and it is
    // a Pythagorean comma narrow.
    int pure = 0;

    for (int degree = 0; degree < 12; ++degree)
    {
        const double above = degree + 7 < 12 ? scale.ratios[static_cast<std::size_t> (degree + 7)]
                                             : scale.ratios[static_cast<std::size_t> (degree - 5)] * 2.0;

        if (std::abs (centsBetween (scale.ratios[static_cast<std::size_t> (degree)], above) - 701.955)
              < 0.01)
            ++pure;
    }

    CHECK (pure == 11);
}

TEZLA_TEST (four_meantone_fifths_make_an_exactly_pure_major_third)
{
    // The definition of quarter-comma meantone, stated as arithmetic and then
    // checked as arithmetic. Nothing here is a measurement of anything.
    const Scale scale = scales::quarterCommaMeantone();

    CHECK (scale.size() == 12);
    CHECK (scale.isUsable());

    const double fifth = 1.5 / std::pow (scales::kSyntonicComma, 0.25);

    CHECK_NEAR (1200.0 * std::log2 (fifth), 696.5784, 0.0001);

    // Four of them, folded back two octaves, is 5/4 to the last digit.
    CHECK_NEAR (std::pow (fifth, 4.0) / 4.0, 1.25, 1.0e-14);

    // And the scale's own major third is that.
    CHECK_NEAR (scale.ratios[4], 1.25, 1.0e-12);
    CHECK_NEAR (scale.cents (4), 386.3137, 0.0001);
}

TEZLA_TEST (the_well_temperaments_hide_the_comma_where_they_say_they_do)
{
    // Each of these is a choice about where to put a Pythagorean comma of
    // error. The test is the defining interval of each, not a table of twelve
    // numbers -- a construction can be checked against its purpose.
    const double justThird = 1200.0 * std::log2 (1.25);
    const double pythagoreanThird = 1200.0 * std::log2 (81.0 / 64.0);
    const double equalThird = 400.0;

    CHECK_NEAR (justThird, 386.3137, 0.0001);
    CHECK_NEAR (pythagoreanThird, 407.8200, 0.0001);

    // Werckmeister III: C-E is a quarter comma narrower than Pythagorean,
    // because one of the four tempered fifths is not in the C-E chain.
    {
        const Scale scale = scales::werckmeisterThree();

        CHECK (scale.isUsable());
        CHECK (scale.size() == 12);

        const double third = scale.cents (4);

        CHECK (third < pythagoreanThird);
        CHECK (third > justThird);
        CHECK (third < equalThird);
        CHECK_NEAR (third, 390.225, 0.01);
    }

    // Kirnberger III: C-E is *exactly* just, which is what four fifths each
    // narrowed by a quarter syntonic comma buys.
    {
        const Scale scale = scales::kirnbergerThree();

        CHECK (scale.isUsable());
        CHECK_NEAR (scale.ratios[4], 1.25, 1.0e-12);
        CHECK_NEAR (scale.cents (4), justThird, 1.0e-9);
    }

    // Vallotti: the most even of the three. Its C-E sits between Werckmeister's
    // and equal temperament's, and no third is worse than Pythagorean.
    {
        const Scale scale = scales::vallotti();

        CHECK (scale.isUsable());

        const double third = scale.cents (4);

        CHECK (third > justThird);
        CHECK (third < equalThird);

        for (int degree = 0; degree < 12; ++degree)
        {
            const double above = degree + 4 < 12
                                   ? scale.ratios[static_cast<std::size_t> (degree + 4)]
                                   : scale.ratios[static_cast<std::size_t> (degree - 8)] * 2.0;

            const double width = centsBetween (scale.ratios[static_cast<std::size_t> (degree)],
                                               above);

            CHECK (width <= pythagoreanThird + 0.01);
            CHECK (width >= justThird - 0.01);
        }
    }
}

TEZLA_TEST (the_carlos_scales_split_the_difference_they_were_built_to_split)
{
    // The derivation: each divides a pure fifth into the number of parts that
    // best balances a just minor third against a just major third. The errors
    // are the definition.
    const double justMinorThird = 1200.0 * std::log2 (6.0 / 5.0);
    const double justMajorThird = 1200.0 * std::log2 (5.0 / 4.0);

    struct Case
    {
        Scale scale;
        int minorSteps;
        int majorSteps;
        double expectedError;
    };

    const Case cases[] = {
        { scales::carlosAlpha(), 4, 5, 3.6613 },
        { scales::carlosBeta(), 5, 6, 3.4292 },
        { scales::carlosGamma(), 9, 11, 0.2385 },
    };

    for (const auto& item : cases)
    {
        CHECK (item.scale.isUsable());
        CHECK_NEAR (item.scale.repeat, 1.5, 1.0e-15);

        const double step = 1200.0 * std::log2 (1.5) / item.scale.size();

        const double minorError = item.minorSteps * step - justMinorThird;
        const double majorError = item.majorSteps * step - justMajorThird;

        // Balanced: one sharp by as much as the other is flat.
        CHECK_NEAR (minorError, -majorError, 0.01);
        CHECK_NEAR (std::abs (minorError), item.expectedError, 0.0002);

        // And the fifth itself is pure by construction, since it *is* the
        // repeat interval.
        CHECK_NEAR (item.scale.size() * step, 1200.0 * std::log2 (1.5), 1.0e-9);
    }

    // Gamma's triad is fifteen times closer to just than Alpha's, which is the
    // reason anybody remembers it.
    CHECK_NEAR (3.6613 / 0.2385, 15.35, 0.01);
}

TEZLA_TEST (the_harmonic_series_scale_is_the_harmonic_series)
{
    const Scale scale = scales::harmonicSeries();

    CHECK (scale.size() == 8);
    CHECK (scale.isUsable());

    for (int harmonic = 8; harmonic < 16; ++harmonic)
        CHECK_NEAR (scale.ratios[static_cast<std::size_t> (harmonic - 8)], harmonic / 8.0, 1.0e-15);

    CHECK_NEAR (scale.repeat, 2.0, 1.0e-15);

    // The eleventh harmonic is the famously strange one: 551.3 cents, half way
    // between a fourth and a tritone and nowhere near either.
    CHECK_NEAR (scale.cents (3), 551.3179, 0.0001);
    CHECK (std::abs (scale.cents (3) - 500.0) > 45.0);
    CHECK (std::abs (scale.cents (3) - 600.0) > 45.0);
}

TEZLA_TEST (the_greek_tetrachords_span_an_exact_fourth)
{
    // What makes a tetrachord a tetrachord: three intervals whose product is
    // 4/3. If the arithmetic is wrong the scale is not Greek, it is a mistake.
    struct Case
    {
        Scale scale;
        double first;
        double second;
        double third;
    };

    const Case cases[] = {
        { scales::archytasEnharmonic(), 28.0 / 27.0, 36.0 / 35.0, 5.0 / 4.0 },
        { scales::archytasDiatonic(), 28.0 / 27.0, 8.0 / 7.0, 9.0 / 8.0 },
        { scales::archytasChromatic(), 28.0 / 27.0, 243.0 / 224.0, 32.0 / 27.0 },
    };

    for (const auto& item : cases)
    {
        CHECK_NEAR (item.first * item.second * item.third, 4.0 / 3.0, 1.0e-14);

        CHECK (item.scale.isUsable());
        CHECK (item.scale.size() == 7);

        // Degree 3 closes the first tetrachord at a pure fourth, degree 4 is
        // the disjunctive fifth.
        CHECK_NEAR (item.scale.ratios[3], 4.0 / 3.0, 1.0e-14);
        CHECK_NEAR (item.scale.ratios[4], 3.0 / 2.0, 1.0e-15);

        // The upper tetrachord is the lower one transposed by a fifth.
        CHECK_NEAR (item.scale.ratios[5] / item.scale.ratios[4],
                    item.scale.ratios[1], 1.0e-14);
        CHECK_NEAR (item.scale.ratios[6] / item.scale.ratios[4],
                    item.scale.ratios[2], 1.0e-14);
    }

    // The enharmonic genus opens with two steps so small they are barely steps
    // -- 63 and 49 cents -- and then a pure major third.
    const Scale enharmonic = scales::archytasEnharmonic();

    CHECK_NEAR (enharmonic.cents (1), 62.96, 0.01);
    CHECK_NEAR (centsBetween (enharmonic.ratios[1], enharmonic.ratios[2]), 48.77, 0.01);
    CHECK_NEAR (centsBetween (enharmonic.ratios[2], enharmonic.ratios[3]), 386.31, 0.01);
}

TEZLA_TEST (every_built_in_scale_is_usable_and_uniquely_named)
{
    const auto everything = scales::all();

    CHECK (everything.size() >= 20);

    std::vector<std::string> names;

    for (const auto& scale : everything)
    {
        CHECK (scale.isUsable());
        CHECK (! scale.name.empty());
        CHECK (scale.size() >= 1);

        // Every built-in carries its theorem and its story -- the tuning
        // panel shows them, so an empty one is a blank panel, and the whole
        // point of generating scales from definitions is that the definition
        // can be said out loud.
        CHECK (! scale.construction.empty());
        CHECK (! scale.story.empty());

        names.push_back (scale.name);

        // And every one of them can actually tune a keyboard.
        Tuning tuning;

        CHECK (tuning.setScale (scale));

        for (int note = 0; note <= 127; ++note)
        {
            const double hz = tuning.frequencyFor (note);

            CHECK (std::isfinite (hz));
            CHECK (hz > 0.0);
        }

        CHECK (tuning.frequencyFor (127) > tuning.frequencyFor (0));
    }

    std::sort (names.begin(), names.end());

    CHECK (std::adjacent_find (names.begin(), names.end()) == names.end());
}

TEZLA_TEST (swapping_a_scale_in_allocates_nothing_and_keeps_the_old_one)
{
    // How a tuning reaches the audio thread. `setScale` copies, which means a
    // vector assignment, which means a possible reallocation -- and the audio
    // thread reads that vector inside note-on to work out a frequency. A host
    // calls setStateInformation with audio running, so a copy there is a
    // reallocation under a pointer somebody is dereferencing: rare, and a crash
    // when it happens.
    //
    // A swap is a pointer exchange. The caller ends up holding the old scale
    // and destroys it later, on whichever thread it likes.
    Tuning tuning;

    auto incoming = scales::bohlenPierce();

    // The addresses before, so the test is about pointers rather than about
    // values -- a copy would pass a value comparison.
    const double* const incomingData = incoming.ratios.data();
    const double* const liveData = tuning.getScale().ratios.data();

    CHECK (tuning.swapScale (incoming));

    // The live scale now points at what the caller handed over, and the caller
    // holds what was live. Nothing was allocated and nothing was freed.
    CHECK (tuning.getScale().ratios.data() == incomingData);
    CHECK (incoming.ratios.data() == liveData);

    CHECK (tuning.getScale().name == "Bohlen-Pierce");
    CHECK (incoming.name == "12-TET");

    // And it plays the scale it was given: the tritave, not the octave.
    tuning.setReference (60, 440.0);

    const double repeat = 1200.0 * std::log2 (tuning.frequencyFor (73) / tuning.frequencyFor (60));

    CHECK (std::abs (repeat - 1901.955) < 0.01);
}

TEZLA_TEST (an_unusable_scale_is_refused_by_the_swap_as_well_as_the_setter)
{
    // The swap has to refuse for the same reason the setter does, and it has to
    // leave the caller's scale alone when it refuses -- a caller that swapped
    // in garbage and got nothing back would have no way to tell what happened,
    // and would then free a scale it does not own.
    Tuning tuning;

    Scale broken;
    broken.name = "nonsense";
    broken.ratios = { 1.0, 0.5 };     // descending, so not usable
    broken.repeat = 2.0;

    const double* const brokenData = broken.ratios.data();

    CHECK (! broken.isUsable());
    CHECK (! tuning.swapScale (broken));

    CHECK (broken.ratios.data() == brokenData);
    CHECK (broken.name == "nonsense");
    CHECK (tuning.getScale().name == "12-TET");

    // Still in tune, which is the point of refusing.
    CHECK (std::abs (tuning.frequencyFor (69) - 440.0) < 1.0e-9);
}

TEZLA_TEST (swapping_a_keyboard_map_moves_it_rather_than_copying_it)
{
    Tuning tuning;

    KeyboardMap map;
    map.size = 12;
    map.middleNote = 60;
    map.referenceNote = 69;
    map.referenceHz = 432.0;
    map.degrees = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11 };

    const int* const data = map.degrees.data();

    tuning.swapKeyboardMap (map);

    CHECK (tuning.getKeyboardMap().degrees.data() == data);
    CHECK (tuning.getKeyboardMap().size == 12);
    CHECK (std::abs (tuning.getKeyboardMap().referenceHz - 432.0) < 1.0e-12);

    // The caller gets the old one back -- an empty map, which is what a Tuning
    // starts with.
    CHECK (map.size == 0);
}

TEZLA_TEST (the_new_scales_are_the_arithmetic_they_claim_to_be)
{
    const auto cents = [] (double ratio) { return 1200.0 * std::log2 (ratio); };

    const auto named = [] (const char* name)
    {
        for (const auto& scale : scales::all())
            if (scale.name == name)
                return scale;

        return Scale {};
    };

    // The undertone series is the harmonic series reflected: its steps *grow*
    // as they climb where the overtone series' shrink. Checked as that property
    // rather than against a table, because a table would pass even if the
    // reflection were the wrong way round.
    {
        const auto under = named ("Undertone series 16-9");

        CHECK (under.ratios.size() == 8);
        CHECK_NEAR (cents (under.ratios[1]), 111.731, 1.0e-3);
        CHECK_NEAR (cents (under.ratios[7]), 996.090, 1.0e-3);

        double previousGap = 0.0;

        for (std::size_t i = 1; i < under.ratios.size(); ++i)
        {
            const double gap = cents (under.ratios[i]) - cents (under.ratios[i - 1]);

            CHECK (gap > previousGap);
            previousGap = gap;
        }
    }

    // And the overtone series does the opposite, which is what makes the pair a
    // mirror rather than two arbitrary scales.
    {
        const auto over = named ("Harmonic series 8-16");

        double previousGap = 1000.0;

        for (std::size_t i = 1; i < over.ratios.size(); ++i)
        {
            const double gap = cents (over.ratios[i]) - cents (over.ratios[i - 1]);

            CHECK (gap < previousGap);
            previousGap = gap;
        }
    }

    // The harmonic seventh, which is what 7-limit is for: 969 cents, a full 31
    // cents flat of 12-TET's 1000.
    {
        const auto seven = named ("Just 7-limit");

        CHECK (seven.ratios.size() == 12);
        CHECK_NEAR (cents (seven.ratios[10]), 968.826, 1.0e-3);
        CHECK_NEAR (cents (seven.ratios[3]), 266.871, 1.0e-3);   // septimal minor third
        CHECK_NEAR (cents (seven.ratios[6]), 582.512, 1.0e-3);   // septimal tritone
    }

    // Seventeen notes from a chain of pure fifths, folded into the octave and
    // sorted. The chain does not close -- which is the whole reason the scale
    // has both a sharp and a flat for each black key -- so the degrees must all
    // be distinct, which a fold that is off by an octave quietly breaks.
    {
        const auto chain = named ("Pythagorean 17");

        CHECK (chain.ratios.size() == 17);
        CHECK_NEAR (chain.ratios[0], 1.0, 1.0e-12);

        bool foundPureFifth = false;

        for (double ratio : chain.ratios)
            if (std::abs (cents (ratio) - 701.955) < 1.0e-3)
                foundPureFifth = true;

        CHECK (foundPureFifth);

        for (std::size_t i = 1; i < chain.ratios.size(); ++i)
            CHECK (chain.ratios[i] > chain.ratios[i - 1] + 1.0e-9);
    }

    // The equal divisions are one line each and still worth checking: a step of
    // 22-TET is 1200/22 cents and nothing else.
    for (const auto& entry : std::vector<std::pair<const char*, int>> {
             { "17-TET", 17 }, { "22-TET", 22 }, { "41-TET", 41 } })
    {
        const auto scale = named (entry.first);

        CHECK (static_cast<int> (scale.ratios.size()) == entry.second);
        CHECK_NEAR (cents (scale.ratios[1]), 1200.0 / entry.second, 1.0e-9);
    }
}

// ---------------------------------------------------------------------------
// The microtuning expansion: Persia, Babylon, Baghdad, Istanbul, China,
// Partch, and the golden section
// ---------------------------------------------------------------------------

namespace
{
const Scale* findScale (const std::vector<Scale>& everything, const std::string& name)
{
    for (const auto& scale : everything)
        if (scale.name == name)
            return &scale;

    return nullptr;
}
} // namespace

TEZLA_TEST (the_babylonian_tunings_are_the_seven_rotations_of_the_chain)
{
    const auto everything = scales::all();

    // Nid qabli is the major rotation, bit-exact against the chain of fifths
    // computed independently here in integers.
    const auto* nidQabli = findScale (everything, "Nid qabli (Babylonian)");
    CHECK (nidQabli != nullptr);
    CHECK (nidQabli->size() == 7);

    const double major[7] = { 1.0, 9.0 / 8.0, 81.0 / 64.0, 4.0 / 3.0,
                              3.0 / 2.0, 27.0 / 16.0, 243.0 / 128.0 };

    for (int degree = 0; degree < 7; ++degree)
        CHECK_NEAR (nidQabli->ratios[static_cast<std::size_t> (degree)], major[degree], 1.0e-15);

    // Embubum is the palindrome: its step pattern reads the same up and down.
    // That property is what survives every scholarly argument about rising
    // versus falling readings, which is why it is the one asserted.
    const auto* embubum = findScale (everything, "Embubum (Babylonian)");
    CHECK (embubum != nullptr);

    std::vector<double> steps;

    for (int degree = 0; degree < embubum->size(); ++degree)
    {
        const double from = embubum->ratios[static_cast<std::size_t> (degree)];
        const double to = degree + 1 < embubum->size()
                            ? embubum->ratios[static_cast<std::size_t> (degree + 1)]
                            : embubum->repeat;

        steps.push_back (centsBetween (from, to));
    }

    for (std::size_t i = 0; i < steps.size(); ++i)
        CHECK (std::abs (steps[i] - steps[steps.size() - 1 - i]) < 1.0e-9);

    // Isartum leads with the semitone (the limma, 256/243), which is what
    // makes it the Greek Dorian species under the rising reading.
    const auto* isartum = findScale (everything, "Isartum (Babylonian)");
    CHECK (isartum != nullptr);
    CHECK_NEAR (isartum->ratios[1], 256.0 / 243.0, 1.0e-15);

    // Six of the seven contain the pure fifth; qablitum -- the Locrian-shaped
    // rotation -- has the diminished 1024/729 where the fifth would be, and
    // that is its identity rather than a defect.
    const char* names[] = { "Nid qabli (Babylonian)", "Isartum (Babylonian)",
                            "Embubum (Babylonian)",   "Kitmum (Babylonian)",
                            "Pitum (Babylonian)",     "Nis gabrim (Babylonian)",
                            "Qablitum (Babylonian)" };

    int withFifth = 0;
    std::vector<std::vector<double>> allSeven;

    for (const char* name : names)
    {
        const auto* scale = findScale (everything, name);
        CHECK (scale != nullptr);
        CHECK (scale->size() == 7);

        allSeven.push_back (scale->ratios);

        bool hasFifth = false;

        for (const double ratio : scale->ratios)
            if (std::abs (ratio - 1.5) < 1.0e-12)
                hasFifth = true;

        if (hasFifth)
            ++withFifth;
        else
            CHECK (std::string (name) == "Qablitum (Babylonian)");
    }

    CHECK (withFifth == 6);

    const auto* qablitum = findScale (everything, "Qablitum (Babylonian)");
    bool hasDiminished = false;

    for (const double ratio : qablitum->ratios)
        if (std::abs (ratio - 1024.0 / 729.0) < 1.0e-12)
            hasDiminished = true;

    CHECK (hasDiminished);

    // And all seven are genuinely different scales.
    for (std::size_t a = 0; a < allSeven.size(); ++a)
        for (std::size_t b = a + 1; b < allSeven.size(); ++b)
        {
            double difference = 0.0;

            for (std::size_t i = 0; i < 7; ++i)
                difference = std::max (difference, std::abs (allSeven[a][i] - allSeven[b][i]));

            CHECK (difference > 1.0e-6);
        }
}

TEZLA_TEST (farhats_dastgah_frames_close_and_carry_their_neutral_seconds)
{
    const auto everything = scales::all();

    // Shur: Farhat's 135-cent neutral second on a pure Pythagorean frame.
    const auto* shur = findScale (everything, "Shur (Persian)");
    CHECK (shur != nullptr);
    CHECK (shur->size() == 7);

    CHECK_NEAR (shur->cents (1), 135.0, 1.0e-9);
    CHECK_NEAR (shur->cents (3), 498.0, 1.0e-9);   // the pure fourth
    CHECK_NEAR (shur->cents (4), 702.0, 1.0e-9);   // the pure fifth
    CHECK_NEAR (shur->cents (6), 996.0, 1.0e-9);   // the minor seventh

    // The second neutral step is the large one: 300 - 135 = 165.
    CHECK_NEAR (shur->cents (2) - shur->cents (1), 165.0, 1.0e-9);

    // Chahargah: two *identical* tetrachords of 135 + 270 + 93, each spanning
    // the pure fourth, joined by a 204 tone. The identity of the two is the
    // construction, so it is what gets asserted.
    const auto* chahargah = findScale (everything, "Chahargah (Persian)");
    CHECK (chahargah != nullptr);
    CHECK (chahargah->size() == 7);

    const double lower[3] = { chahargah->cents (1) - chahargah->cents (0),
                              chahargah->cents (2) - chahargah->cents (1),
                              chahargah->cents (3) - chahargah->cents (2) };
    const double upper[3] = { chahargah->cents (5) - chahargah->cents (4),
                              chahargah->cents (6) - chahargah->cents (5),
                              1200.0 - chahargah->cents (6) };

    for (int i = 0; i < 3; ++i)
        CHECK_NEAR (lower[i], upper[i], 1.0e-9);

    CHECK_NEAR (lower[0], 135.0, 1.0e-9);
    CHECK_NEAR (lower[1], 270.0, 1.0e-9);          // the plus tone
    CHECK_NEAR (chahargah->cents (3), 498.0, 1.0e-9);
    CHECK_NEAR (chahargah->cents (4) - chahargah->cents (3), 204.0, 1.0e-9);
}

TEZLA_TEST (the_two_rasts_disagree_exactly_where_history_says)
{
    const auto everything = scales::all();

    // Zalzal's wosta: 27/22, 354.55 cents, and the upper tetrachord is the
    // lower one moved up a pure fifth.
    const auto* zalzal = findScale (everything, "Rast (Zalzal, just)");
    CHECK (zalzal != nullptr);
    CHECK (zalzal->size() == 7);
    CHECK_NEAR (zalzal->ratios[2], 27.0 / 22.0, 1.0e-15);
    CHECK_NEAR (zalzal->cents (2), 354.547, 0.01);

    for (int degree = 0; degree < 2; ++degree)
        CHECK_NEAR (zalzal->ratios[static_cast<std::size_t> (degree + 5)],
                    1.5 * zalzal->ratios[static_cast<std::size_t> (degree + 1)], 1.0e-12);

    // The AEU Rast: comma degrees on the 53 grid, and its third is the
    // *schismatic* one -- 17 commas is 384.9 cents, under two cents from a
    // pure 5/4, where Zalzal's neutral third floats thirty cents below.
    const auto* aeu = findScale (everything, "Rast (Turkish, AEU)");
    CHECK (aeu != nullptr);
    CHECK (aeu->size() == 7);

    const int commas[] = { 0, 9, 17, 22, 31, 40, 48 };

    for (int degree = 0; degree < 7; ++degree)
        CHECK_NEAR (aeu->cents (degree), commas[degree] * 1200.0 / 53.0, 1.0e-9);

    CHECK (std::abs (aeu->cents (2) - 386.31) < 2.0);
    CHECK (std::abs (aeu->cents (2) - zalzal->cents (2)) > 25.0);

    // Its fourth and fifth are the near-perfect 53-EDO ones.
    CHECK (std::abs (aeu->cents (3) - 498.04) < 0.2);
    CHECK (std::abs (aeu->cents (4) - 701.96) < 0.2);
}

TEZLA_TEST (ptolemys_even_genus_telescopes_to_an_exact_fourth)
{
    const auto everything = scales::all();
    const auto* even = findScale (everything, "Ptolemy even diatonic");

    CHECK (even != nullptr);

    // 12/11 * 11/10 * 10/9 = 4/3 by cancellation; the doubles get there to
    // rounding. And the 12/11 neutral second is the point of the genus.
    CHECK_NEAR (even->ratios[3], 4.0 / 3.0, 1.0e-14);
    CHECK_NEAR (even->cents (1), 150.64, 0.01);
}

TEZLA_TEST (the_twelve_lu_run_the_chain_of_fifths_one_way_only)
{
    const auto everything = scales::all();
    const auto* lu = findScale (everything, "Twelve lu (China)");

    CHECK (lu != nullptr);
    CHECK (lu->size() == 12);

    // Independently in integers: 3^k folded into the octave, k = 0..11.
    std::vector<double> expected;

    for (int k = 0; k <= 11; ++k)
    {
        long long numerator = 1, denominator = 1;

        for (int i = 0; i < k; ++i)
            numerator *= 3, denominator *= 2;

        while (numerator >= 2 * denominator) denominator *= 2;
        while (numerator < denominator)      numerator *= 2;

        expected.push_back (static_cast<double> (numerator) / static_cast<double> (denominator));
    }

    std::sort (expected.begin(), expected.end());

    for (int degree = 0; degree < 12; ++degree)
        CHECK_NEAR (lu->ratios[static_cast<std::size_t> (degree)],
                    expected[static_cast<std::size_t> (degree)], 1.0e-15);

    // The one-directional chain is what distinguishes it from the Pythagorean
    // scale in this same menu: no pure fourth anywhere -- the eleventh fifth
    // lands at 521.5 cents instead of 498.
    bool hasPureFourth = false;
    bool hasSharpFourth = false;

    for (const double ratio : lu->ratios)
    {
        if (std::abs (ratio - 4.0 / 3.0) < 1.0e-9)
            hasPureFourth = true;

        if (std::abs (ratio - 177147.0 / 131072.0) < 1.0e-9)
            hasSharpFourth = true;
    }

    CHECK (! hasPureFourth);
    CHECK (hasSharpFourth);
}

TEZLA_TEST (partch_forty_three_is_symmetric_eleven_limit_and_ordered)
{
    // The list is reproduced from Genesis of a Music rather than derived --
    // there is no rule that generates it -- so the test verifies every
    // structural property the book states: 43 degrees, strictly ascending,
    // nothing beyond the 11-limit, and exact inversional symmetry.
    const auto everything = scales::all();
    const auto* partch = findScale (everything, "Partch 43");

    CHECK (partch != nullptr);
    CHECK (partch->size() == 43);
    CHECK (partch->isUsable());

    for (const double ratio : partch->ratios)
    {
        // Recover the fraction and factor it: nothing above 11 may remain.
        const auto fraction = nearestFraction (ratio);

        CHECK (fraction.found);

        long long numerator = fraction.numerator;
        long long denominator = fraction.denominator;

        for (const long long prime : { 2LL, 3LL, 5LL, 7LL, 11LL })
        {
            while (numerator % prime == 0)   numerator /= prime;
            while (denominator % prime == 0) denominator /= prime;
        }

        CHECK (numerator == 1);
        CHECK (denominator == 1);
    }

    // Inversional symmetry: for every ratio r above the tonic, 2/r is also a
    // degree. Partch's Monophony is built on the identity of otonality and
    // utonality, and this is that identity as arithmetic.
    for (int a = 1; a < partch->size(); ++a)
    {
        const double complement = 2.0 / partch->ratios[static_cast<std::size_t> (a)];
        bool present = false;

        for (int b = 1; b < partch->size(); ++b)
            if (std::abs (partch->ratios[static_cast<std::size_t> (b)] - complement) < 1.0e-12)
                present = true;

        CHECK (present);
    }
}

TEZLA_TEST (the_golden_scale_repeats_at_phi_and_phi_behaves)
{
    const auto everything = scales::all();
    const auto* golden = findScale (everything, "Golden phi");

    CHECK (golden != nullptr);
    CHECK (golden->size() == 7);
    CHECK_NEAR (golden->repeatCents(), 833.0903, 0.001);

    // The property the scale is built on: 1/phi = phi - 1, so the difference
    // tone of two notes a repeat apart lands exactly one repeat below the
    // lower note. This is the golden ratio's defining identity, checked here
    // so the construction's claim is arithmetic rather than prose.
    const double phi = golden->repeat;

    CHECK_NEAR (1.0 / phi, phi - 1.0, 1.0e-15);

    for (int degree = 1; degree < golden->size(); ++degree)
        CHECK_NEAR (golden->cents (degree) - golden->cents (degree - 1), 833.0903 / 7.0, 0.001);
}

TEZLA_TEST (nearest_fraction_finds_ratios_and_refuses_temperament)
{
    // Finds the genuine rationals...
    const auto simple = nearestFraction (1.5);
    CHECK (simple.found);
    CHECK (simple.numerator == 3);
    CHECK (simple.denominator == 2);

    const auto wosta = nearestFraction (27.0 / 22.0);
    CHECK (wosta.found);
    CHECK (wosta.numerator == 27);
    CHECK (wosta.denominator == 22);

    const auto wide = nearestFraction (160.0 / 81.0);
    CHECK (wide.found);
    CHECK (wide.numerator == 160);
    CHECK (wide.denominator == 81);

    // ...and refuses the tempered degrees, which is just as important: an
    // equal-tempered fifth labelled "3/2" would be a lie of half a percent.
    CHECK (! nearestFraction (std::pow (2.0, 7.0 / 12.0)).found);
    CHECK (! nearestFraction (std::pow (2.0, 17.0 / 53.0)).found);
    CHECK (! nearestFraction (0.0).found);
}

// ---------------------------------------------------------------------------
// Concert pitch
// ---------------------------------------------------------------------------

TEZLA_TEST (concert_pitch_scales_the_whole_tuning_by_one_ratio)
{
    // Default: A440 exactly, and the control reads back what it is.
    Tuning tuning;
    CHECK_NEAR (tuning.frequencyFor (69), 440.0, 1.0e-12);
    CHECK_NEAR (tuning.getConcertPitch(), 440.0, 1.0e-12);

    // At 432, A is 432 -- and *every* note moved by the same 432/440.
    // Intervals untouched: the control is one ratio over the lot, which is
    // what lets it mean something even in a tuning with no A in it.
    tuning.setConcertPitch (432.0);
    CHECK_NEAR (tuning.frequencyFor (69), 432.0, 1.0e-12);
    CHECK_NEAR (tuning.frequencyFor (60), 261.6255653005986 * 432.0 / 440.0, 1.0e-9);

    Tuning reference;

    for (int note = 12; note <= 120; ++note)
        CHECK_NEAR (tuning.frequencyFor (note) / reference.frequencyFor (note),
                    432.0 / 440.0, 1.0e-12);

    // A non-octave scale scales identically: the tritave stays an exact 3x.
    CHECK (tuning.setScale (scales::bohlenPierce()));
    tuning.setRootNote (60);
    CHECK_NEAR (tuning.frequencyFor (73) / tuning.frequencyFor (60), 3.0, 1.0e-12);

    // A keyboard map's own reference is scaled too -- not fought with.
    Tuning mapped;
    CHECK (mapped.setScale (scales::justMajor()));

    KeyboardMap map;
    map.size = 7;
    map.middleNote = 60;
    map.referenceNote = 60;
    map.referenceHz = 300.0;
    map.formalOctaveDegree = 0;
    map.degrees = { 0, 1, 2, 3, 4, 5, 6 };
    mapped.setKeyboardMap (map);

    CHECK_NEAR (mapped.frequencyFor (60), 300.0, 1.0e-12);

    mapped.setConcertPitch (432.0);
    CHECK_NEAR (mapped.frequencyFor (60), 300.0 * 432.0 / 440.0, 1.0e-12);

    // Nonsense is clamped, not obeyed.
    mapped.setConcertPitch (0.0);
    CHECK_NEAR (mapped.getConcertPitch(), 440.0, 1.0e-12);

    mapped.setConcertPitch (10000.0);
    CHECK_NEAR (mapped.getConcertPitch(), Tuning::kMaximumConcertHz, 1.0e-12);
}

TEZLA_TEST (the_scales_that_had_a_pitch_standard_say_so)
{
    // The panel's bold line: traditions with something real to say about
    // absolute pitch carry it -- including the honest "nothing survives" of
    // Babylon and Greece -- and the pure interval systems stay empty, where
    // the panel shows the generic line instead of inventing a frequency.
    const auto everything = scales::all();

    const char* withLore[] = { "12-TET",
                               "Quarter-comma meantone", "Werckmeister III",
                               "Kirnberger III", "Vallotti",
                               "Archytas enharmonic", "Archytas diatonic",
                               "Archytas chromatic", "Ptolemy even diatonic",
                               "Nid qabli (Babylonian)", "Isartum (Babylonian)",
                               "Embubum (Babylonian)", "Kitmum (Babylonian)",
                               "Pitum (Babylonian)", "Nis gabrim (Babylonian)",
                               "Qablitum (Babylonian)",
                               "Shur (Persian)", "Chahargah (Persian)",
                               "Rast (Zalzal, just)", "Rast (Turkish, AEU)",
                               "Twelve lu (China)", "Partch 43", "5-TET" };

    for (const char* name : withLore)
    {
        const auto* scale = findScale (everything, name);

        CHECK (scale != nullptr);
        CHECK (! scale->pitchStandard.empty());
    }

    // Partch's is the one that fixes a number: 1/1 at G-392.
    CHECK (findScale (everything, "Partch 43")->pitchStandard.find ("392")
             != std::string::npos);
}
