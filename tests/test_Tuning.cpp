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
    map.degrees = { 0, -1, 1, -1, 2, 3, -1, 4, -1, 5, -1, 6 };

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
