// Copyright (c) 2026 The Tezla <thetezla@proton.me>
// Created by The Tezla -- https://github.com/wingit33/tezla.tech
// Music: https://soundcloud.com/thetezla | https://thetezla.bandcamp.com
// Built with development assistance from Claude (Anthropic).
// SPDX-License-Identifier: AGPL-3.0-only
// GNU AGPLv3 (see LICENSE), plus NOTICE.md's attribution term. Keep intact.

#include "TestFramework.hpp"

#include <cmath>
#include <complex>
#include <numbers>

#include <SphereDiffraction.hpp>

using namespace tezla::membrana;
using SD = SphereDiffraction;

// Every expected value here is from Duda & Martens, JASA 104(5):3048-3058
// (1998), read first-hand -- their equations, their plotted values, their
// bowling-ball measurements. The implementation is their own Appendix A-B
// algorithm, so these tests are not checking our derivation against our
// code: they are checking our transcription of their algorithm against their
// printed results, which is the strongest check a taken algorithm can get.
//
// Measured values quoted in comments were produced by this implementation
// and cross-read against the paper before being pinned; where the paper
// prints a rounded number ("about 13 dB down") the tolerance says so.

namespace
{
    double cosDeg (double degrees)
    {
        return std::cos (degrees * std::numbers::pi / 180.0);
    }
}

// -- Limits from the paper's equations --------------------------------------

TEZLA_TEST (sphere_low_mu_complex_limit_is_their_eq_11)
{
    // Eq (11): H -> 1 - i (3/2) mu cos theta as mu -> 0 (far field). This is
    // asserted on the COMPLEX value, not the magnitude, because it is the one
    // place the time convention shows: their e^(i(kr - wt)) puts the
    // imaginary part NEGATIVE on axis. An implementation that conjugated the
    // series -- harmless for the magnitude fit -- fails here, and a future
    // maintainer who "fixes" a sign learns why it was that way round.
    // rho = 10000 so the finite-range term 1/(mu rho) is negligible.
    const auto onAxis = SD::response (10000.0, 0.01, 1.0);
    CHECK_NEAR (onAxis.real(), 1.0, 1.0e-3);
    CHECK_NEAR (onAxis.imag(), -0.015, 5.0e-5);

    const auto behind = SD::response (10000.0, 0.01, -1.0);
    CHECK_NEAR (behind.real(), 1.0, 1.0e-3);
    CHECK_NEAR (behind.imag(), +0.015, 5.0e-5);

    // Broadside, the first-order term vanishes: H is 1 to O(mu^2).
    const auto side = SD::response (10000.0, 0.01, 0.0);
    CHECK_NEAR (side.real(), 1.0, 1.0e-3);
    CHECK_NEAR (side.imag(), 0.0, 1.0e-3);
}

TEZLA_TEST (sphere_mu_zero_predicate_and_the_near_field_it_deliberately_ignores)
{
    // mu == 0.0 exactly returns 1 by predicate (z_a does not exist there).
    // That is the FAR-FIELD static limit; at close range the true static
    // value is the geometric near field, which the header documents as a
    // level for the consumer to handle. Both facts pinned together so the
    // discontinuity is a documented contract, not a surprise:
    int terms = -1;
    const auto dc = SD::response (1.25, 0.0, 1.0, &terms);
    CHECK (dc.real() == 1.0);
    CHECK (dc.imag() == 0.0);
    CHECK (terms == 0);

    // The near-field static plateau at rho = 1.25: +18.05 dB, already flat
    // by mu = 0.01 (the series is smooth down to any positive mu).
    const double at01  = SD::magnitudeDb (1.25, 0.01,  1.0);
    const double at001 = SD::magnitudeDb (1.25, 0.001, 1.0);
    CHECK_NEAR (at01, 18.05, 0.05);
    CHECK_NEAR (at01 - at001, 0.0, 0.01);
}

TEZLA_TEST (sphere_on_axis_high_frequency_limit_is_pressure_doubling)
{
    // Eq (12): |H(inf, inf, 0)| = 2, the rigid-baffle pressure doubling.
    // At rho = 100 the measured approach is +6.09 to +6.11 dB across
    // mu = 30..105 (the residual 0.09 dB is the 1/rho range term).
    CHECK_NEAR (SD::magnitudeDb (100.0, 30.0, 1.0), 6.02, 0.3);
    CHECK_NEAR (SD::magnitudeDb (100.0, 60.0, 1.0), 6.02, 0.3);
    CHECK_NEAR (SD::magnitudeDb (100.0, 105.0, 1.0), 6.02, 0.3);

    // And the journal version's mid value: +3 dB at exactly mu = 1
    // (measured here: +3.123 dB at rho = 100).
    CHECK_NEAR (SD::magnitudeDb (100.0, 1.0, 1.0), 3.0, 0.5);
}

TEZLA_TEST (sphere_mu_frequency_correspondence_is_the_papers)
{
    // Their stated correspondence: mu = 1 falls at 624 Hz for the 8.75 cm
    // head radius (c = 343 m/s gives 1.00018 -- the paper rounded).
    CHECK_NEAR (SD::muFor (624.0, 0.0875), 1.0, 1.0e-3);

    // Scaled to a 50 mm mic body (25 mm radius), mu = 1 is ~2183.6 Hz:
    // the presence region, which is why body size is an audible control.
    CHECK_NEAR (SD::muFor (2183.6, 0.025), 1.0, 1.0e-4);
}

// -- Angles: shadow, flat flank, bright spot --------------------------------

TEZLA_TEST (sphere_shadow_at_150_degrees_matches_their_measurement)
{
    // Their Fig. 5 region: theta = 150 degrees is down about 13 dB at
    // mu = 30, where theory and their bowling-ball measurement agree.
    // This implementation: -12.24 dB at rho = 100.
    CHECK_NEAR (SD::magnitudeDb (100.0, 30.0, cosDeg (150.0)), -13.0, 1.5);
}

TEZLA_TEST (sphere_flank_near_100_degrees_is_roughly_flat)
{
    // Their observation: the response is nearly flat around theta = 100
    // degrees -- the transition between baffle gain and shadow. Measured
    // extremes here over mu = 0.5..5 at rho = 100: -0.77 to +0.91 dB.
    for (double mu : { 0.5, 1.0, 2.0, 5.0 })
        CHECK_NEAR (SD::magnitudeDb (100.0, mu, cosDeg (100.0)), 0.0, 1.5);
}

TEZLA_TEST (sphere_bright_spot_at_180_degrees)
{
    // Directly behind the sphere the creeping waves arrive in phase: the
    // bright spot. Flat within 2 dB out to mu = 20 (measured -1.72 dB
    // there), and past it the response finally falls (-3.52 dB at mu = 30)
    // -- non-monotonic, unlike every other angle in the shadow.
    for (double mu : { 1.0, 5.0, 10.0, 20.0 })
        CHECK_NEAR (SD::magnitudeDb (100.0, mu, -1.0), 0.0, 2.0);

    CHECK (SD::magnitudeDb (100.0, 30.0, -1.0) < -2.0);
    CHECK (SD::magnitudeDb (100.0, 5.0, -1.0) > SD::magnitudeDb (100.0, 20.0, -1.0));
}

// -- Range: the close-in trade the plugin is really about -------------------

TEZLA_TEST (sphere_close_range_trades_high_rise_for_low)
{
    // The journal paper's central range result, on axis: the LF-to-HF rise
    // that is ~+6 dB in the far field collapses to ~+2 dB at rho = 1.25 --
    // moving in close makes the sphere relatively warmer, not brighter.
    // Measured (mu = 0.1 for LF, mu = 60 for HF):
    //   rho = 1.25:  rise +1.926 dB      rho = 100:  rise +5.964 dB
    const auto rise = [] (double rho)
    {
        return SD::magnitudeDb (rho, 60.0, 1.0) - SD::magnitudeDb (rho, 0.1, 1.0);
    };

    CHECK_NEAR (rise (1.25), 1.93, 0.3);
    CHECK_NEAR (rise (100.0), 5.96, 0.3);

    // And the rise recovers monotonically with distance -- the knob sweeps
    // smoothly from "warm" to "bright", with nothing to zipper over.
    CHECK (rise (1.25) < rise (1.5));
    CHECK (rise (1.5) < rise (2.0));
    CHECK (rise (2.0) < rise (5.0));
    CHECK (rise (5.0) < rise (100.0));

    // Range dependence is visible below rho = 5 and finished by rho = 100:
    // rho = 5 is still 0.85 dB short of the far-field rise, rho = 20 is
    // within a quarter dB of rho = 100.
    CHECK (rise (100.0) - rise (5.0) > 0.5);
    CHECK (std::abs (rise (100.0) - rise (20.0)) < 0.25);
}

TEZLA_TEST (sphere_near_field_level_at_rho_1_25)
{
    // The raw |H| at close range carries the geometric near field on top of
    // the diffraction shape: +18.05 dB at rho = 1.25 in the static limit
    // (the source is 0.25 radii from the surface but 1.25 from the centre
    // the definition references, plus the sphere's own enhancement).
    // Pinned as a regression so no "simplification" quietly renormalises
    // the series -- CapsuleEq subtracts levels, so it depends on this being
    // stable, not on it being zero.
    CHECK_NEAR (SD::magnitudeDb (1.25, 0.1, 1.0), 18.05, 0.1);
}

// -- Convergence and the stopping rule --------------------------------------

TEZLA_TEST (sphere_convergence_cap_is_never_the_stopper)
{
    // The whole parameter space the plugin can ever ask for -- rho from the
    // 1.2 clamp to 80, frequencies 20 Hz to 192 kHz (an octave past any
    // design grid at the highest supported rate), bodies 20 to 60 mm, angles
    // over the full sphere -- and the series must converge by its own
    // stopping rule every time, with finite values throughout.
    // Measured worst case: 197 terms, at rho = 1.2, mu = 105.5, theta = 180.
    int worst = 0;

    for (double rho : { 1.2, 1.25, 1.5, 2.0, 5.0, 20.0, 80.0 })
        for (double f : { 20.0, 100.0, 1000.0, 10000.0, 20000.0, 96000.0, 192000.0 })
            for (double deg : { 0.0, 45.0, 90.0, 135.0, 180.0 })
                for (double radius : { 0.010, 0.025, 0.030 })
                {
                    int terms = 0;
                    const auto h = SD::response (rho, SD::muFor (f, radius),
                                                 cosDeg (deg), &terms);
                    CHECK (std::isfinite (h.real()));
                    CHECK (std::isfinite (h.imag()));
                    CHECK (terms < SD::kMaxTerms);
                    worst = terms > worst ? terms : worst;
                }

    CHECK_NEAR (worst, 197, 25);
}

TEZLA_TEST (sphere_rescale_keeps_the_slow_corner_finite)
{
    // rho = 1.2 at 20 Hz on a 30 mm radius: mu = 0.011, |z_a| = 91, and the
    // rho^-m convergence tail needs ~120 terms -- the exact corner where the
    // carried Q values pass 1e300 and the printed algorithm returns NaN.
    // The power-of-two rescale keeps it finite AND on the physics: the
    // static near-field plateau for rho = 1.2 (+19.6 dB), smooth against
    // its own neighbourhood.
    int terms = 0;
    const auto h = SD::response (1.2, SD::muFor (20.0, 0.030), 1.0, &terms);
    CHECK (std::isfinite (h.real()));
    CHECK (std::isfinite (h.imag()));
    CHECK (terms > 100);   // the slow tail was actually walked

    const double db20 = 20.0 * std::log10 (std::abs (h));
    const double db40 = SD::magnitudeDb (1.2, SD::muFor (40.0, 0.030), 1.0);
    CHECK_NEAR (db20 - db40, 0.0, 0.05);   // static plateau: 20 vs 40 Hz agree
}

TEZLA_TEST (sphere_stopping_rule_term_counts_are_frozen)
{
    // Their rule: stop only when the fractional change has been below
    // threshold for TWO successive terms, because the terms oscillate and a
    // single small one proves nothing. These exact counts freeze the rule:
    // weakening it to a single confirmation (the obvious "optimisation")
    // changes them and this test goes red. Counts also document the two
    // convergence regimes -- mu-driven (more terms at high mu) and
    // rho-driven (more terms close in).
    const auto termsFor = [] (double rho, double mu, double deg)
    {
        int terms = 0;
        SD::response (rho, mu, cosDeg (deg), &terms);
        return terms;
    };

    CHECK (termsFor (20.0, 5.0, 150.0) == 23);
    CHECK (termsFor (1.25, 2.0, 0.0) == 99);
    CHECK (termsFor (100.0, 30.0, 0.0) == 59);

    // And the rho^-m decay law directly: at fixed small mu the count is set
    // by range alone, falling as the source backs away.
    CHECK (termsFor (1.2, 0.05, 0.0) == 120);
    CHECK (termsFor (2.0, 0.05, 0.0) == 35);
    CHECK (termsFor (5.0, 0.05, 0.0) == 17);
    CHECK (termsFor (20.0, 0.05, 0.0) == 10);
}
