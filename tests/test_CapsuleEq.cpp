// Copyright (c) 2026 The Tezla <thetezla@proton.me>
// Created by The Tezla -- https://github.com/wingit33/tezla.tech
// Music: https://soundcloud.com/thetezla | https://thetezla.bandcamp.com
// Built with development assistance from Claude (Anthropic).
// SPDX-License-Identifier: AGPL-3.0-only
// GNU AGPLv3 (see LICENSE), plus NOTICE.md's attribution term. Keep intact.

#include "TestFramework.hpp"

#include <bit>
#include <cmath>
#include <cstdint>
#include <numbers>

#include <CapsuleEq.hpp>
#include <MicPattern.hpp>

using namespace tezla::membrana;

namespace
{
    // The settings the fit and hold tests sweep: each engages a different
    // mix of mechanisms (proximity only, off-axis fig-8, the rho clamp at
    // 60 mm/2 cm, grille only, a 90-degree cardioid with grille).
    struct Config
    {
        double pattern, bodyMm, character, grille, grilleHz, distanceM, axisDeg;
    };

    constexpr Config kConfigs[] = {
        { 0.5, 50.0, 0.35, 0.0, 7000.0, 0.05, 0.0 },
        { 1.0, 50.0, 0.60, 0.0, 7000.0, 0.08, 30.0 },
        { 0.5, 60.0, 1.00, 0.0, 7000.0, 0.02, 0.0 },
        { 0.5, 50.0, 0.00, 1.0, 9000.0, 1.00, 0.0 },
        { 0.5, 50.0, 0.50, 0.3, 5000.0, 0.30, 90.0 },
    };

    void apply (CapsuleEq& eq, const Config& c)
    {
        eq.setPattern (c.pattern);
        eq.setBodyMm (c.bodyMm);
        eq.setCharacter (c.character);
        eq.setGrille (c.grille, c.grilleHz);
        eq.setPosition (c.distanceM, c.axisDeg);
        eq.setLowLimitHz (40.0);
        eq.setAutoLevel (true);
    }
}

// -- Neutrality: the branch, not the arithmetic ------------------------------

TEZLA_TEST (capsule_neutral_defaults_are_bit_exact)
{
    // The defaults -- 1 m, on axis, cardioid, character 35%, grille 0 --
    // must return the input VERBATIM: character is not zero, but at the
    // reference position the diffraction difference is identically zero, so
    // the whole stage takes the predicate branch. Bit patterns compared,
    // including -0.0 and a denormal: arithmetic would flip the -0.0 sign
    // bit, a branch cannot.
    CapsuleEq eq;
    eq.prepare (48000.0);
    CHECK (eq.isNeutral());
    CHECK (CapsuleEq::latencySamples() == 0);

    const double probe[] = { 0.5, -1.0, 0.0, -0.0, 1.0e-308, -1.0e-308,
                             0.25, -0.75, 1.0, -1.0 };

    for (double x : probe)
    {
        const double y = eq.process (x);
        CHECK (std::bit_cast<std::uint64_t> (y) == std::bit_cast<std::uint64_t> (x));
    }
}

TEZLA_TEST (capsule_omni_is_position_neutral_at_any_distance)
{
    // An omni has no gradient: E(s) is identically 1 whatever the distance,
    // so with the sphere and grille disengaged the stage is still bit-exact
    // at 5 cm. (With character up, the BODY still shadows -- diffraction is
    // pattern-independent -- so only the pure-pattern claim is tested here.)
    CapsuleEq eq;
    eq.setPattern (0.0);        // omni
    eq.setCharacter (0.0);
    eq.setPosition (0.05, 0.0);
    eq.prepare (48000.0);

    CHECK (eq.isNeutral());
    const double x = -0.375;
    CHECK (std::bit_cast<std::uint64_t> (eq.process (x))
           == std::bit_cast<std::uint64_t> (x));
}

TEZLA_TEST (capsule_autolevel_off_applies_the_physical_level_clamped)
{
    // Same omni-at-5-cm shape, but with autoLevel off the stage now has a
    // job: the physical 20 log10(1 m / 0.05 m) = +26.02 dB, clamped to +24.
    // Tone must not change -- the response is the same flat line at every
    // frequency, sitting at exactly the clamp.
    CapsuleEq eq;
    eq.setPattern (0.0);
    eq.setCharacter (0.0);
    eq.setPosition (0.05, 0.0);
    eq.setAutoLevel (false);
    eq.prepare (48000.0);

    CHECK (! eq.isNeutral());
    CHECK_NEAR (eq.trimDb(), 24.0, 1.0e-9);
    CHECK_NEAR (eq.renderedDbAt (100.0), 24.0, 1.0e-9);
    CHECK_NEAR (eq.renderedDbAt (5000.0), 24.0, 1.0e-9);

    // And at the reference distance the physical level is exactly 0 dB, so
    // autoLevel off at 1 m is still bit-exact neutral.
    CapsuleEq ref;
    ref.setPattern (0.0);
    ref.setCharacter (0.0);
    ref.setAutoLevel (false);
    ref.prepare (48000.0);
    CHECK (ref.isNeutral());
}

// -- The fit: what plays matches what physics asked for ----------------------

TEZLA_TEST (capsule_fit_error_stays_under_a_quarter_db)
{
    // renderedDbAt reads the designed coefficients (digital sections + DFT
    // of the taps); targetDbAt is the analytic composition. Worst measured
    // over these five configs at 48 kHz: 0.023 dB, at 1329 Hz in the splice
    // ramp of the 60 mm / 2 cm config. The budget is ten times that.
    for (const auto& config : kConfigs)
    {
        CapsuleEq eq;
        apply (eq, config);
        eq.prepare (48000.0);

        for (double hz = 700.0; hz <= 20000.0; hz *= 1.06)
            CHECK_NEAR (eq.renderedDbAt (hz), eq.targetDbAt (hz), 0.25);
    }
}

TEZLA_TEST (capsule_rendered_curve_is_rate_independent)
{
    // The same physical curve at 44.1, 48, 96 and 192 kHz: worst measured
    // spread 0.012 dB (at 200 Hz). CLAUDE.md section 6 requires the plugin
    // to sound the same at every rate; +/-0.2 dB is the budget the plan
    // set, and the measurement sits sixteen times inside it.
    const double freqs[] = { 200.0, 700.0, 1000.0, 3000.0, 7000.0,
                             10000.0, 16000.0, 20000.0 };
    const double rates[] = { 44100.0, 48000.0, 96000.0, 192000.0 };

    double values[4][8];

    for (int ri = 0; ri < 4; ++ri)
    {
        CapsuleEq eq;
        eq.setPattern (0.5);
        eq.setBodyMm (50.0);
        eq.setCharacter (1.0);
        eq.setGrille (0.5, 7000.0);
        eq.setPosition (0.03, 20.0);
        eq.setLowLimitHz (40.0);
        eq.setAutoLevel (true);
        eq.prepare (rates[ri]);

        for (int fi = 0; fi < 8; ++fi)
            values[ri][fi] = eq.renderedDbAt (freqs[fi]);
    }

    for (int fi = 0; fi < 8; ++fi)
        for (int ri = 1; ri < 4; ++ri)
            CHECK_NEAR (values[ri][fi], values[0][fi], 0.2);
}

TEZLA_TEST (capsule_autolevel_holds_1khz_exactly)
{
    // The trim is measured on the actual designed coefficients (a DFT of
    // the taps, not the analytic target), so the 1 kHz hold is exact to
    // double rounding -- not "exact minus the fit error". Measured 0.00000
    // for all five configs.
    for (const auto& config : kConfigs)
    {
        CapsuleEq eq;
        apply (eq, config);
        eq.prepare (48000.0);
        CHECK_NEAR (eq.renderedDbAt (1000.0), 0.0, 1.0e-9);
    }
}

TEZLA_TEST (capsule_position_target_is_the_closed_form)
{
    // Wiring check: with the sphere and grille off, the analytic target
    // must equal the MicPattern closed form assembled by hand -- pattern
    // boost at the set distance, minus the reference mic's own residual
    // proximity at 1 m, plus the LF-limit rolloff. Cardioid at 5 cm,
    // lowLimit 20 Hz, autoLevel off (so the trim is the pure physical
    // level, subtracted back out here).
    CapsuleEq eq;
    eq.setPattern (0.5);
    eq.setCharacter (0.0);
    eq.setPosition (0.05, 0.0);
    eq.setLowLimitHz (20.0);
    eq.setAutoLevel (false);
    eq.prepare (48000.0);

    for (double hz : { 100.0, 200.0, 546.0, 2000.0 })
    {
        const double boostAtR = MicPattern::boostDb (0.5, 1.0, 0.05, hz);
        const double boostAtRef = MicPattern::boostDb (0.5, 1.0, 1.0, hz);
        const double ratio20 = 20.0 / hz;
        const double hp = -10.0 * std::log10 (1.0 + ratio20 * ratio20 * ratio20 * ratio20);
        const double expected = boostAtR - boostAtRef + hp;

        CHECK_NEAR (eq.targetDbAt (hz) - eq.trimDb(), expected, 1.0e-9);
    }
}

TEZLA_TEST (capsule_close_range_reads_warm_in_the_composed_target)
{
    // The audible point of modelling range: at full character, a cardioid
    // at 3 cm on a 50 mm body (rho at the 1.2 clamp) has HF sitting BELOW
    // the splice anchor -- measured -4.2 dB at 16 kHz -- because close
    // range trades high rise for low. A parametric "presence curve" would
    // have to fake this; the series just has it.
    CapsuleEq eq;
    eq.setPattern (0.5);
    eq.setBodyMm (50.0);
    eq.setCharacter (1.0);
    eq.setPosition (0.03, 0.0);
    eq.setAutoLevel (true);
    eq.prepare (48000.0);

    CHECK (eq.targetDbAt (16000.0) - eq.trimDb() < -1.0);
    CHECK_NEAR (eq.targetDbAt (16000.0) - eq.trimDb(), -4.24, 0.5);
}

// -- Retune: state-preserving, click-free ------------------------------------

TEZLA_TEST (capsule_retune_sweep_is_click_free)
{
    // Distance dragged 1 m -> 10 cm over 200 control chunks with a 1 kHz
    // sine playing: the largest sample-to-sample step in the output must
    // stay within 20% of the sine's own natural step. Measured: 0.06612
    // against the sine's 0.06545 -- a ratio of 1.010, i.e. no step at all.
    // Seen red first by making applyChanges() reset the filter state
    // instead of preserving it: the step jumped an order of magnitude.
    CapsuleEq eq;
    eq.setPattern (0.5);
    eq.setBodyMm (50.0);
    eq.setCharacter (0.35);
    eq.setPosition (1.0, 0.0);
    eq.setLowLimitHz (40.0);
    eq.setAutoLevel (true);
    eq.prepare (48000.0);

    const double dPhase = 2.0 * std::numbers::pi * 1000.0 / 48000.0;
    const int chunk = 256, chunks = 200;

    double phase = 0.0, previous = 0.0, maxStep = 0.0;

    for (int c = 0; c < chunks; ++c)
    {
        const double distance = 1.0 - 0.9 * c / (chunks - 1.0);
        eq.setPosition (distance, 0.0);
        eq.applyChanges();

        for (int n = 0; n < chunk; ++n)
        {
            const double y = eq.process (0.5 * std::sin (phase));
            phase += dPhase;

            if (c > 2 || n > 0)
                maxStep = std::max (maxStep, std::abs (y - previous));

            previous = y;
        }
    }

    CHECK (maxStep < 0.5 * dPhase * 1.2);
}
