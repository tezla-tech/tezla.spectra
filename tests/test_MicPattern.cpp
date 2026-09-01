// Copyright (c) 2026 The Tezla <thetezla@proton.me>
// Created by The Tezla -- https://github.com/wingit33/tezla.tech
// Music: https://soundcloud.com/thetezla | https://thetezla.bandcamp.com
// Built with development assistance from Claude (Anthropic).
// SPDX-License-Identifier: AGPL-3.0-only
// GNU AGPLv3 (see LICENSE), plus NOTICE.md's attribution term. Keep intact.

#include "TestFramework.hpp"

#include <cmath>
#include <numbers>

#include <MicPattern.hpp>

using namespace tezla::membrana;

// The first-order mic model is one closed form, and these tests pin it from
// two directions: the numbers (evaluated by hand from the same formula, so
// they catch wiring and unit mistakes rather than re-derive the physics) and
// the ZEROS (which are structural: they fall out of the mechanism, and a
// curve-drawing implementation would have to fake every one of them).

// -- The zeros: what the mechanism gets right by construction ---------------

TEZLA_TEST (micpattern_omni_has_exactly_no_proximity)
{
    // a = 1 makes the gradient weight (1-a) cos theta exactly 0.0, and the
    // boost returns 0 by predicate, not by rounding. Every frequency, every
    // distance, both signs of cos theta.
    for (double f : { 20.0, 100.0, 546.0, 5000.0 })
        for (double r : { 0.02, 0.05, 0.30, 1.0 })
            for (double ct : { 1.0, 0.5, 0.0, -1.0 })
            {
                CHECK (MicPattern::gradientWeight (1.0, ct) == 0.0);
                CHECK (MicPattern::boostDb (1.0, ct, r, f) == 0.0);
            }

    // And the omni's level ignores the angle entirely.
    CHECK (MicPattern::level (1.0, 1.0) == 1.0);
    CHECK (MicPattern::level (1.0, -1.0) == 1.0);
}

TEZLA_TEST (micpattern_cardioid_at_90_degrees_level_without_proximity)
{
    // At 90 degrees the gradient component is nulled, so a cardioid is its
    // pressure half alone: -6.02 dB of level and NO proximity whatever the
    // distance. cos(pi/2) in double is ~6.1e-17 rather than zero, and the
    // boost formula still lands on exactly 0.0 dB because 1 + (tiny)^2 == 1
    // in double precision -- pinned as exact, since "almost zero proximity at
    // 90 degrees" would smear the polar-pattern truth the plugin sells.
    const double ct = std::cos (std::numbers::pi / 2.0);

    CHECK_NEAR (MicPattern::level (0.5, ct), 0.5, 1.0e-15);
    CHECK_NEAR (20.0 * std::log10 (MicPattern::level (0.5, ct)), -6.0206, 1.0e-3);

    for (double f : { 30.0, 100.0, 1000.0 })
        for (double r : { 0.02, 0.10, 1.0 })
            CHECK (MicPattern::boostDb (0.5, ct, r, f) == 0.0);
}

// -- The numbers: the closed form evaluated where the plan pinned it --------

TEZLA_TEST (micpattern_cardioid_5cm_corner_and_100hz)
{
    // boost_dB(f) = 10 log10(1 + (G c / (2 pi f r D))^2), G = 0.5, D = 1,
    // c = 343, r = 0.05: corner = G c / (2 pi r) = 545.90 Hz, and the boost
    // there is +3.0103 dB (the half-power point of the 1/f term).
    CHECK_NEAR (MicPattern::cornerHz (0.5, 1.0, 0.05), 545.90, 0.01);
    CHECK_NEAR (MicPattern::boostDb (0.5, 1.0, 0.05, MicPattern::cornerHz (0.5, 1.0, 0.05)),
                3.0103, 1.0e-4);

    // At 100 Hz: 10 log10(1 + (545.90/100)^2) = +14.886 dB. The number the
    // plan quoted as "+14.8" -- this is it to three decimals.
    CHECK_NEAR (MicPattern::boostDb (0.5, 1.0, 0.05, 100.0), 14.886, 0.005);
}

TEZLA_TEST (micpattern_figure8_doubles_the_gradient)
{
    // a = 0 doubles G against the cardioid (1.0 vs 0.5), so the corner sits
    // exactly one octave up at the same distance: 1091.80 Hz, and 100 Hz
    // reads +20.799 dB.
    CHECK_NEAR (MicPattern::cornerHz (0.0, 1.0, 0.05), 1091.80, 0.02);
    CHECK_NEAR (MicPattern::boostDb (0.0, 1.0, 0.05, 100.0), 20.799, 0.005);

    // The octave relation between the two patterns is exact -- same formula,
    // one factor of two.
    CHECK_NEAR (MicPattern::cornerHz (0.0, 1.0, 0.05)
                    / MicPattern::cornerHz (0.5, 1.0, 0.05),
                2.0, 1.0e-12);
}

TEZLA_TEST (micpattern_doubling_distance_halves_the_corner_exactly)
{
    // r is linear in the corner formula, so backing off one distance-octave
    // moves the proximity corner down exactly one frequency-octave. This is
    // the relation the Distance knob's feel depends on.
    CHECK_NEAR (MicPattern::cornerHz (0.5, 1.0, 0.05)
                    / MicPattern::cornerHz (0.5, 1.0, 0.10),
                2.0, 1.0e-12);
    CHECK_NEAR (MicPattern::cornerHz (0.5, 1.0, 0.25)
                    / MicPattern::cornerHz (0.5, 1.0, 1.00),
                4.0, 1.0e-12);
}

TEZLA_TEST (micpattern_on_axis_level_is_unity_for_every_pattern)
{
    // D = a + (1-a) * 1 = 1 identically. The pattern knob must not be a
    // volume knob on axis.
    for (double a : { 0.0, 0.25, 0.5, 0.75, 1.0 })
        CHECK (MicPattern::level (a, 1.0) == 1.0);
}

TEZLA_TEST (micpattern_proximity_shrinks_with_pattern_toward_omni)
{
    // On axis the proximity weight is (1-a): fig-8 > cardioid > wide
    // cardioid > omni, monotonically, at any fixed f and r. This ordering is
    // the audible point of the pattern control.
    const double f = 100.0, r = 0.05;
    double previous = 1.0e9;
    for (double a : { 0.0, 0.25, 0.5, 0.75 })
    {
        const double boost = MicPattern::boostDb (a, 1.0, r, f);
        CHECK (boost > 0.0);
        CHECK (boost < previous);
        previous = boost;
    }
    CHECK (MicPattern::boostDb (1.0, 1.0, r, f) == 0.0);
}
