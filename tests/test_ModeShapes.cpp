// Copyright (c) 2026 The Tezla <thetezla@proton.me>
// Created by The Tezla -- https://github.com/wingit33/tezla.tech
// Music: https://soundcloud.com/thetezla | https://thetezla.bandcamp.com
// Built with development assistance from Claude (Anthropic).
// SPDX-License-Identifier: AGPL-3.0-only
// GNU AGPLv3 (see LICENSE), plus NOTICE.md's attribution term. Keep intact.

#include "TestFramework.hpp"

#include <cmath>

#include <tezla/dsp/ModeShapes.hpp>
#include <tezla/dsp/Scales.hpp>

using namespace tezla::dsp;

namespace
{
[[nodiscard]] double centsBetween (double a, double b)
{
    return 1200.0 * std::log2 (a / b);
}
} // namespace

TEZLA_TEST (the_bar_table_lands_on_the_classic_figures)
{
    // Free-free Euler-Bernoulli beam: mode frequencies go as the squares of
    // the roots of cos x cosh x = 1. Every acoustics text prints the first
    // ratios as 2.756, 5.404, 8.933 -- the copy-proof kind of citation,
    // because a root-finder cannot get them right by accident.
    const auto& bar = ModeShapes::table (ModeMaterial::Bar);

    CHECK_NEAR (bar[0], 1.0, 1.0e-12);
    CHECK_NEAR (bar[1], 2.7565, 0.002);
    CHECK_NEAR (bar[2], 5.4039, 0.002);
    CHECK_NEAR (bar[3], 8.9330, 0.002);
}

TEZLA_TEST (the_membrane_table_lands_on_the_bessel_zeros)
{
    // The ideal circular membrane's modes are the zeros of J_m, all m
    // together, sorted. The first zero is 2.4048 (J_0), and the classic
    // ratio sequence 1 : 1.593 : 2.136 : 2.296 : 2.653 : 2.918 follows.
    const auto& membrane = ModeShapes::table (ModeMaterial::Membrane);

    CHECK_NEAR (membrane[0], 1.0, 1.0e-12);
    CHECK_NEAR (membrane[1], 3.8317 / 2.4048, 0.002);   // J_1's first
    CHECK_NEAR (membrane[2], 5.1356 / 2.4048, 0.002);   // J_2's first
    CHECK_NEAR (membrane[3], 5.5201 / 2.4048, 0.002);   // J_0's second
    CHECK_NEAR (membrane[4], 6.3802 / 2.4048, 0.002);   // J_3's first
    CHECK_NEAR (membrane[5], 7.0156 / 2.4048, 0.002);   // J_1's second
}

TEZLA_TEST (string_is_harmonic_plate_is_closed_form_bell_is_canonical)
{
    const auto& string = ModeShapes::table (ModeMaterial::String);

    for (int n = 0; n < ModeShapes::kMaxModes; ++n)
        CHECK (string[static_cast<std::size_t> (n)] == static_cast<double> (n + 1));

    // Plate, aspect sqrt(2): f ~ m^2/2 + n^2, minimum 1.5 at (1,1). The next
    // distinct values are (1,2)=4.5? no -- ordered: (2,1)=3, (1,2)=4.5,
    // (2,2)=6, (3,1)=5.5... sorted: 3, 4.5, 5.5, 6. Ratios to 1.5.
    const auto& plate = ModeShapes::table (ModeMaterial::Plate);

    CHECK_NEAR (plate[0], 1.0, 1.0e-12);
    CHECK_NEAR (plate[1], 3.0 / 1.5, 1.0e-9);
    CHECK_NEAR (plate[2], 4.5 / 1.5, 1.0e-9);
    CHECK_NEAR (plate[3], 5.5 / 1.5, 1.0e-9);

    // The bell's canonical seven, hum below the prime included.
    const auto& bell = ModeShapes::table (ModeMaterial::Bell);
    const double canonical[] { 0.5, 1.0, 1.2, 1.5, 2.0, 3.0, 4.0 };

    for (int n = 0; n < 7; ++n)
        CHECK_NEAR (bell[static_cast<std::size_t> (n)], canonical[n], 1.0e-12);
}

TEZLA_TEST (every_table_is_strictly_ascending_and_contains_the_prime)
{
    // Ascending is what makes "mode n" mean anything; the prime at exactly
    // 1.0 is the anchor the played pitch hangs on -- for the bell it is the
    // second entry, not the first, and that is the point.
    for (int material = 0; material < static_cast<int> (ModeMaterial::count); ++material)
    {
        const auto& table = ModeShapes::table (static_cast<ModeMaterial> (material));

        bool ascending = true;
        bool hasPrime = false;

        for (int n = 0; n < ModeShapes::kMaxModes; ++n)
        {
            if (n > 0)
                ascending = ascending
                    && table[static_cast<std::size_t> (n)]
                         > table[static_cast<std::size_t> (n - 1)];

            hasPrime = hasPrime
                || std::abs (table[static_cast<std::size_t> (n)] - 1.0) < 1.0e-12;
        }

        CHECK (ascending);
        CHECK (hasPrime);
    }
}

TEZLA_TEST (morph_endpoints_are_the_pure_tables_to_the_bit)
{
    // An integer Material position IS its table -- no interpolation
    // arithmetic touches it, so a project saved on a pure material reopens
    // producing bit-identical mode frequencies.
    for (int material = 0; material < static_cast<int> (ModeMaterial::count); ++material)
    {
        const auto& table = ModeShapes::table (static_cast<ModeMaterial> (material));

        for (int mode = 0; mode < ModeShapes::kMaxModes; ++mode)
            CHECK (ModeShapes::ratioAt (static_cast<double> (material), mode, 0.0)
                     == table[static_cast<std::size_t> (mode)]);
    }
}

TEZLA_TEST (the_morph_moves_monotonically_in_log_frequency)
{
    // Halfway between String and Bar, a mode sits at the geometric mean of
    // the two tables -- the log-domain claim, checked directly.
    const auto& string = ModeShapes::table (ModeMaterial::String);
    const auto& bar = ModeShapes::table (ModeMaterial::Bar);

    for (int mode = 0; mode < 8; ++mode)
    {
        const double expected = std::sqrt (string[static_cast<std::size_t> (mode)]
                                           * bar[static_cast<std::size_t> (mode)]);

        CHECK_NEAR (ModeShapes::ratioAt (0.5, mode, 0.0), expected, 1.0e-9);
    }
}

TEZLA_TEST (stretch_keeps_the_prime_fixed_and_spreads_the_rest)
{
    // ratio^(1+s): the prime at 1.0 cannot move (1^x = 1), overtones above
    // it rise with s, and the bell's hum -- below 1 -- falls, which is the
    // right direction: stretching an object apart pushes its partials away
    // from the prime on both sides.
    CHECK (ModeShapes::ratioAt (4.0, 1, 1.5) == 1.0);   // the bell's prime

    const double plain = ModeShapes::ratioAt (0.0, 4, 0.0);      // 5th harmonic
    const double stretched = ModeShapes::ratioAt (0.0, 4, 0.5);  // 5^1.5

    CHECK_NEAR (stretched, std::pow (plain, 1.5), 1.0e-12);
    CHECK (stretched > plain);

    const double hum = ModeShapes::ratioAt (4.0, 0, 0.5);        // 0.5^1.5

    CHECK (hum < 0.5);
}

TEZLA_TEST (overtone_lock_lands_every_partial_on_a_degree_of_the_scale)
{
    // The flagship, on the hard case: Bohlen-Pierce repeats at 3/1, and at
    // full lock every partial of a stretched bar must sit within half a
    // cent of fundamental * ratio * 3^k for some degree and some repeat.
    const Scale bp = scales::bohlenPierce();
    const Scale fiveTet = scales::fiveToneEqual();

    for (const Scale* scale : { &bp, &fiveTet })
    {
        for (int mode = 0; mode < 32; ++mode)
        {
            const double free = 55.0 * ModeShapes::ratioAt (1.3, mode, 0.4);
            const double locked = ModeShapes::lockToScale (free, 55.0, *scale, 1.0);

            // Fold the locked partial back into one repeat of the lattice
            // and measure its distance to the nearest degree (the repeat's
            // top counts as degree zero of the next repeat).
            const double x = locked / 55.0;
            const double k = std::floor (std::log (x) / std::log (scale->repeat));
            const double base = x / std::pow (scale->repeat, k);

            double nearest = 1.0e9;

            for (const double ratio : scale->ratios)
                nearest = std::min (nearest, std::abs (centsBetween (base, ratio)));

            nearest = std::min (nearest, std::abs (centsBetween (base, scale->repeat)));

            CHECK (nearest < 0.5);
        }
    }
}

TEZLA_TEST (overtone_lock_amount_blends_in_cents_and_zero_is_exact)
{
    const Scale scale = scales::twelveToneEqual();

    // A partial 40 cents off a degree: amount 0 must return it bit-exactly,
    // amount 0.5 must move it half the distance, amount 1 all of it.
    const double fundamental = 110.0;
    const double free = fundamental * std::pow (2.0, (7.0 + 0.4) / 12.0);

    CHECK (ModeShapes::lockToScale (free, fundamental, scale, 0.0) == free);

    const double half = ModeShapes::lockToScale (free, fundamental, scale, 0.5);
    const double full = ModeShapes::lockToScale (free, fundamental, scale, 1.0);

    CHECK_NEAR (centsBetween (free, full), 40.0, 0.01);
    CHECK_NEAR (centsBetween (free, half), 20.0, 0.01);

    // Nearest means nearest: a partial 30 cents below the repeat's top must
    // snap UP to the octave (30 cents), not down to the major seventh
    // (70 cents) -- the repeat-top candidate exists for exactly this, and
    // removing it drags every such partial flat.
    const double nearTop = fundamental * std::pow (2.0, 11.7 / 12.0);
    const double snapped = ModeShapes::lockToScale (nearTop, fundamental, scale, 1.0);

    CHECK_NEAR (snapped, fundamental * 2.0, 0.01);
}

TEZLA_TEST (overtone_lock_reaches_below_the_fundamental_for_the_bell_hum)
{
    // The hum at half the fundamental lives one repeat DOWN the lattice. In
    // 12-TET half is exactly the octave below -- already a degree -- so full
    // lock must leave it in place to within numeric dust.
    const Scale scale = scales::twelveToneEqual();

    const double hum = 220.0 * 0.5;
    const double locked = ModeShapes::lockToScale (hum, 220.0, scale, 1.0);

    CHECK_NEAR (locked, hum, 1.0e-9);

    // And a partial at 0.49 of the fundamental -- 35 cents from the octave
    // below, 65 from the next degree down -- snaps to that octave. (First
    // written with 0.47, which actually sits 7 cents from the SEMITONE
    // below the octave and rightly snapped there instead: the test's
    // arithmetic was wrong, not the lattice's.)
    const double off = ModeShapes::lockToScale (220.0 * 0.49, 220.0, scale, 1.0);

    CHECK_NEAR (off, 110.0, 0.01);
}
