// Copyright (c) 2026 The Tezla <thetezla@proton.me>
// Created by The Tezla -- https://github.com/wingit33/tezla.tech
// Music: https://soundcloud.com/thetezla | https://thetezla.bandcamp.com
// Built with development assistance from Claude (Anthropic).
// SPDX-License-Identifier: AGPL-3.0-only
// GNU AGPLv3 (see LICENSE), plus NOTICE.md's attribution term. Keep intact.

#include "TestFramework.hpp"

#include <cmath>
#include <numeric>

#include <tezla/dsp/Ratio.hpp>

using namespace tezla::dsp;

// ---------------------------------------------------------------------------
// The table
// ---------------------------------------------------------------------------

TEZLA_TEST (the_simple_ratios_are_named_and_the_awkward_ones_are_not)
{
    struct Row
    {
        double ratio;
        int numerator;
        int denominator;
        bool simple;
    };

    const Row rows[] {
        { 1.0,  1, 1, true },
        { 2.0,  2, 1, true },
        { 3.0,  3, 1, true },
        { 0.5,  1, 2, true },
        { 1.5,  3, 2, true },
        { 4.0 / 3.0, 4, 3, true },
        { 5.0 / 4.0, 5, 4, true },
        { 8.0,  8, 1, true },

        // A semitone up from 2:1. A hundred cents is nowhere near simple.
        { 2.0 * std::pow (2.0, 1.0 / 12.0), 0, 0, false },
    };

    for (const auto& row : rows)
    {
        const auto match = nearestRatio (row.ratio);

        CHECK (match.simple == row.simple);

        if (row.simple)
        {
            CHECK (match.numerator == row.numerator);
            CHECK (match.denominator == row.denominator);
            CHECK (std::abs (match.centsError) < 1.0e-9);
        }
    }
}

TEZLA_TEST (a_ratio_a_little_off_is_named_with_the_error_rather_than_refused)
{
    // 2.03:1 -- the case the readout exists for. A beating mess rather than an
    // interval, and the panel has to be able to say both halves of that: which
    // ratio it is near, and how far from it.
    const auto match = nearestRatio (2.03);

    CHECK (match.numerator == 2);
    CHECK (match.denominator == 1);
    CHECK (! match.simple);
    CHECK_NEAR (match.centsError, 25.7757, 1.0e-3);   // 1200 log2 (2.03 / 2), measured

    // Inside the tolerance it reads as the ratio. Five cents of detune is a
    // fattening, not a different interval.
    const auto detuned = nearestRatio (2.0 * std::pow (2.0, 5.0 / 1200.0));

    CHECK (detuned.numerator == 2);
    CHECK (detuned.denominator == 1);
    CHECK (detuned.simple);
    CHECK_NEAR (detuned.centsError, 5.0, 1.0e-9);

    // And the sign says which way: flat reads negative.
    const auto flat = nearestRatio (1.5 * std::pow (2.0, -4.0 / 1200.0));

    CHECK (flat.numerator == 3);
    CHECK (flat.denominator == 2);
    CHECK (flat.simple);
    CHECK_NEAR (flat.centsError, -4.0, 1.0e-9);
}

TEZLA_TEST (the_answer_is_always_in_lowest_terms_and_the_simplest_form_wins)
{
    // Lowest terms falls out of trying denominators in order rather than out
    // of a reduction step -- 6:4 is reached as 3:2 at q = 2, before q = 4 is
    // looked at. Asserted over the grid below rather than assumed, because the
    // day someone changes the loop order is the day it stops being true.
    for (const double ratio : { 6.0 / 4.0, 8.0 / 6.0, 10.0 / 4.0, 2.0, 4.0, 0.25 })
    {
        const auto match = nearestRatio (ratio);

        CHECK (match.simple);
        CHECK (std::gcd (match.numerator, match.denominator) == 1);
    }

    // Named, so a regression reads as a wrong answer rather than as a failed
    // gcd: three halves is 3:2, not 6:4.
    CHECK (nearestRatio (6.0 / 4.0).numerator == 3);
    CHECK (nearestRatio (6.0 / 4.0).denominator == 2);
    CHECK (nearestRatio (10.0 / 4.0).numerator == 5);
    CHECK (nearestRatio (10.0 / 4.0).denominator == 2);

    // Every coprime pair in range reports itself, reduced, with no error --
    // the whole grid rather than the handful above.
    int checked = 0;

    for (int q = 1; q <= 16; ++q)
        for (int p = 1; p <= 16; ++p)
        {
            if (std::gcd (p, q) != 1)
                continue;

            const auto match = nearestRatio (static_cast<double> (p) / static_cast<double> (q));

            CHECK (match.simple);
            CHECK (match.numerator == p);
            CHECK (match.denominator == q);
            CHECK (std::abs (match.centsError) < 1.0e-9);

            ++checked;
        }

    CHECK (checked == 159);   // coprime pairs with both terms 1..16, counted
}

TEZLA_TEST (a_ratio_beyond_the_simple_range_says_so_rather_than_inventing_one)
{
    // Six octaves is 64:1, which no pair of terms under sixteen expresses.
    // Reporting nothing is what lets the panel print the decimal instead of a
    // ratio that would be wrong by an octave.
    const auto wide = nearestRatio (64.0);

    CHECK (wide.numerator == 0);
    CHECK (! wide.simple);

    // And the degenerate inputs are answers rather than crashes.
    CHECK (nearestRatio (0.0).numerator == 0);
    CHECK (nearestRatio (-2.0).numerator == 0);
    CHECK (nearestRatio (2.0, 0).numerator == 0);
}

TEZLA_TEST (the_readout_reads_the_same_offset_the_voice_plays)
{
    // `ratioFromOffset` is the voice's own arithmetic, and the readout would
    // be a decoration if it were an approximation of it.
    CHECK_NEAR (ratioFromOffset (1.0, 0.0, 0.0), 2.0, 1.0e-12);
    CHECK_NEAR (ratioFromOffset (0.0, 12.0, 0.0), 2.0, 1.0e-12);
    CHECK_NEAR (ratioFromOffset (0.0, 0.0, 1200.0), 2.0, 1.0e-12);
    CHECK_NEAR (ratioFromOffset (-1.0, 0.0, 0.0), 0.5, 1.0e-12);
    CHECK (ratioFromOffset (0.0, 0.0, 0.0) == 1.0);

    // A tempered fifth is seven semitones, and it is flat of 3:2 -- which is
    // why 12-TET fifths beat, and the readout says so instead of rounding it
    // away.
    const auto fifth = nearestRatio (ratioFromOffset (0.0, 7.0, 0.0));

    CHECK (fifth.numerator == 3);
    CHECK (fifth.denominator == 2);
    CHECK (fifth.simple);
    CHECK_NEAR (fifth.centsError, -1.955, 1.0e-3);
}

TEZLA_TEST (the_tolerance_is_the_same_width_at_every_ratio)
{
    // The reason nearness is measured in cents and not as a difference of
    // ratios: a fixed ratio tolerance is eight times as forgiving at 8:1 as at
    // 1:1, and an ear is not. Six cents off is six cents off wherever it sits.
    for (const int whole : { 1, 2, 3, 4, 5, 8, 16 })
    {
        const double exact = static_cast<double> (whole);

        // Just inside the tolerance: still that ratio.
        const auto inside = nearestRatio (exact * std::pow (2.0, 5.0 / 1200.0));

        CHECK (inside.simple);
        CHECK (inside.numerator == whole);
        CHECK (inside.denominator == 1);
        CHECK_NEAR (inside.centsError, 5.0, 1.0e-6);

        // And just outside it: named, with the error, but not called simple.
        const auto outside = nearestRatio (exact * std::pow (2.0, 9.0 / 1200.0));

        CHECK (! outside.simple);
        CHECK (outside.numerator == whole);
        CHECK_NEAR (outside.centsError, 9.0, 1.0e-6);
    }
}
