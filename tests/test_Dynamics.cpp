// Copyright (c) 2026 The Tezla <thetezla@proton.me>
// Created by The Tezla -- https://github.com/wingit33/tezla.tech
// Music: https://soundcloud.com/thetezla | https://thetezla.bandcamp.com
// Built with development assistance from Claude (Anthropic).
// SPDX-License-Identifier: AGPL-3.0-only
// GNU AGPLv3 (see LICENSE), plus NOTICE.md's attribution term. Keep intact.

#include "TestFramework.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <vector>

#include <tezla/dsp/Decibels.hpp>
#include <tezla/dsp/EnvelopeFollower.hpp>
#include <limits>

#include <tezla/dsp/GainComputer.hpp>

using namespace tezla::dsp;

TEZLA_TEST (gain_computer_leaves_quiet_signal_alone)
{
    GainComputer computer;
    computer.setCeilingDb (-0.3);
    computer.setKneeDb (6.0);

    // The knee starts 6 dB below the ceiling, so anything quieter is untouched.
    for (const double levelDb : { -60.0, -30.0, -12.0, -6.31 })
        CHECK_NEAR (computer.computeGainReductionDb (levelDb), 0.0, 1.0e-12);
}

TEZLA_TEST (gain_computer_holds_the_ceiling_exactly)
{
    // Ceiling has to mean the level it says. A limiter whose output plateaus
    // half a knee below its own setting is a limiter nobody can trust.
    for (const double ceilingDb : { -12.0, -6.0, -0.3, 0.0 })
        for (const double kneeDb : { 0.0, 3.0, 6.0, 24.0 })
        {
            GainComputer computer;
            computer.setCeilingDb (ceilingDb);
            computer.setKneeDb (kneeDb);

            for (const double overshootDb : { 0.1, 3.0, 12.0, 40.0 })
            {
                const double inputDb  = ceilingDb + overshootDb;
                const double outputDb = inputDb + computer.computeGainReductionDb (inputDb);

                CHECK (outputDb <= ceilingDb + 1.0e-9);

                // Above the knee it should sit right on the ceiling, not below.
                if (overshootDb >= kneeDb)
                    CHECK_NEAR (outputDb, ceilingDb, 1.0e-9);
            }
        }
}

TEZLA_TEST (gain_computer_knee_is_smooth)
{
    // A kink in the curve is audible as a hardness right at the onset of
    // compression. Check the first derivative has no jumps across the knee.
    GainComputer computer;
    computer.setCeilingDb (0.0);
    computer.setKneeDb (12.0);

    constexpr double step = 1.0e-4;
    double previousSlope = 1.0;

    for (double levelDb = -24.0; levelDb <= 12.0; levelDb += 0.05)
    {
        const double below = levelDb - step + computer.computeGainReductionDb (levelDb - step);
        const double above = levelDb + step + computer.computeGainReductionDb (levelDb + step);
        const double slope = (above - below) / (2.0 * step);

        CHECK (slope >= -1.0e-6);          // never gains on the way up
        CHECK (slope <= 1.0 + 1.0e-6);     // never expands
        CHECK (std::abs (slope - previousSlope) < 0.02);

        previousSlope = slope;
    }
}

TEZLA_TEST (gain_computer_knee_control_moves_the_onset)
{
    // The Knee control claims to say how far below the ceiling compression
    // begins. Verify that literally.
    for (const double kneeDb : { 3.0, 6.0, 12.0, 24.0 })
    {
        GainComputer computer;
        computer.setCeilingDb (0.0);
        computer.setKneeDb (kneeDb);

        CHECK_NEAR (computer.computeGainReductionDb (-kneeDb - 0.01), 0.0, 1.0e-9);
        CHECK (computer.computeGainReductionDb (-kneeDb + 1.0) < 0.0);
    }
}

TEZLA_TEST (envelope_attack_and_release_hit_their_stated_times)
{
    constexpr double fs = 48000.0;

    EnvelopeFollower envelope;
    envelope.prepare (fs);
    envelope.setAttackMs (10.0);
    envelope.setReleaseMs (100.0);
    envelope.reset();

    // Attack: 1/e time constant, so ~63% of the way in 10 ms.
    const int attackSamples = static_cast<int> (0.010 * fs);
    for (int i = 0; i < attackSamples; ++i)
        (void) envelope.process (-12.0);

    CHECK_NEAR (envelope.getCurrentGainReductionDb(), -12.0 * 0.632, 0.15);

    // Settle, then release.
    for (int i = 0; i < static_cast<int> (fs); ++i)
        (void) envelope.process (-12.0);
    CHECK_NEAR (envelope.getCurrentGainReductionDb(), -12.0, 0.01);

    const int releaseSamples = static_cast<int> (0.100 * fs);
    for (int i = 0; i < releaseSamples; ++i)
        (void) envelope.process (0.0);

    CHECK_NEAR (envelope.getCurrentGainReductionDb(), -12.0 * (1.0 - 0.632), 0.15);
}

TEZLA_TEST (envelope_timing_is_sample_rate_independent)
{
    // A 10 ms attack must be 10 ms at 192 kHz too, or the plugin changes
    // character with the session rate.
    for (const double fs : { 44100.0, 48000.0, 96000.0, 192000.0 })
    {
        EnvelopeFollower envelope;
        envelope.prepare (fs);
        envelope.setAttackMs (10.0);
        envelope.setReleaseMs (100.0);
        envelope.reset();

        for (int i = 0; i < static_cast<int> (0.010 * fs); ++i)
            (void) envelope.process (-12.0);

        CHECK_NEAR (envelope.getCurrentGainReductionDb(), -12.0 * 0.632, 0.1);
    }
}

TEZLA_TEST (envelope_never_overshoots_its_target)
{
    EnvelopeFollower envelope;
    envelope.prepare (48000.0);
    envelope.setAttackMs (1.0);
    envelope.setReleaseMs (50.0);
    envelope.reset();

    for (int i = 0; i < 48000; ++i)
    {
        const double target = (i / 512) % 2 == 0 ? -18.0 : 0.0;
        const double value = envelope.process (target);

        CHECK (value <= 1.0e-12);
        CHECK (value >= -18.0 - 1.0e-9);
    }
}

TEZLA_TEST (program_dependent_release_recovers_slower_after_sustained_reduction)
{
    constexpr double fs = 48000.0;

    const auto recoveryAfter = [fs] (bool programDependent, int heldSamples)
    {
        EnvelopeFollower envelope;
        envelope.prepare (fs);
        envelope.setAttackMs (1.0);
        envelope.setReleaseMs (100.0);
        envelope.setProgramDependent (programDependent);
        envelope.reset();

        for (int i = 0; i < heldSamples; ++i)
            (void) envelope.process (-12.0);

        for (int i = 0; i < static_cast<int> (0.100 * fs); ++i)
            (void) envelope.process (0.0);

        return envelope.getCurrentGainReductionDb();
    };

    // A brief peak: both modes should have let go substantially.
    const double shortFixed   = recoveryAfter (false, static_cast<int> (0.005 * fs));
    const double shortProgram = recoveryAfter (true,  static_cast<int> (0.005 * fs));
    CHECK (shortFixed > -6.0);
    CHECK (shortProgram > -6.0);

    // Sustained reduction: the program-dependent mode must still be holding on.
    const double longFixed   = recoveryAfter (false, static_cast<int> (2.0 * fs));
    const double longProgram = recoveryAfter (true,  static_cast<int> (2.0 * fs));
    CHECK (longProgram < longFixed - 2.0);
}

TEZLA_TEST (limiter_holds_a_sine_below_the_ceiling)
{
    // End to end through the static curve and the time constants: a steady tone
    // well over the ceiling must settle at the ceiling and stay there.
    constexpr double fs = 48000.0;
    constexpr double ceilingDb = -6.0;

    GainComputer computer;
    computer.setCeilingDb (ceilingDb);
    computer.setKneeDb (3.0);

    EnvelopeFollower envelope;
    envelope.prepare (fs);
    envelope.setAttackMs (1.0);
    envelope.setReleaseMs (50.0);
    envelope.reset();

    const double omega = 2.0 * std::numbers::pi * 200.0 / fs;
    const double inputGain = dbToGain (6.0);   // 12 dB over the ceiling

    double worstOutputDb = -200.0;

    for (int i = 0; i < static_cast<int> (fs); ++i)
    {
        const double input = inputGain * std::sin (omega * static_cast<double> (i));
        const double levelDb = gainToDb (std::abs (input));
        const double gainDb = envelope.process (computer.computeGainReductionDb (levelDb));
        const double output = input * dbToGain (gainDb);

        // Skip the attack, then look at the steady state.
        if (i > static_cast<int> (0.2 * fs))
            worstOutputDb = std::max (worstOutputDb, gainToDb (std::abs (output)));
    }

    // A feed-forward limiter with a finite attack tracks the envelope rather
    // than predicting it, so a little ripple above the ceiling is expected and
    // honest. Anything more than a dB would not be.
    CHECK (worstOutputDb <= ceilingDb + 1.0);
    CHECK (worstOutputDb >= ceilingDb - 2.0);
}

// ---------------------------------------------------------------------------
// The ratio generalisation (Phonoss V1)
// ---------------------------------------------------------------------------

namespace
{
/// The curve exactly as it stood before a ratio existed, written out here so
/// the bit-exactness claim is checked against the real thing rather than
/// against a memory of it.
[[nodiscard]] double limiterCurveAsItWas (double levelDb, double ceilingDb,
                                          double kneeDb)
{
    const double kneeWidth = 2.0 * kneeDb;
    const double overshoot = levelDb - ceilingDb;

    if (kneeWidth <= 0.0)
        return overshoot <= 0.0 ? 0.0 : -overshoot;

    if (overshoot <= -kneeDb)
        return 0.0;

    if (overshoot >= kneeDb)
        return -overshoot;

    const double intoKnee = overshoot + kneeDb;
    return -(intoKnee * intoKnee) / (2.0 * kneeWidth);
}
} // namespace

TEZLA_TEST (an_infinite_ratio_is_the_limiter_this_class_used_to_be)
{
    // The load-bearing test of the whole generalisation. Capstone is built on
    // this class and has shipped, so a curve that moved its output by one ulp
    // would have changed every project using it. Bit-for-bit, over a swept
    // level and several knees -- not CHECK_NEAR.
    //
    // Break-checked, and the numbers are the argument for it existing:
    // perturbing the knee constant by one part in 1e10 fails this test 8088
    // times, and CAPSTONE'S OWN TEN TESTS STILL PASS. Capstone's suite cannot
    // see a change to the curve it is built on; this is what can.
    //
    // A second attempt at breaking it is worth recording because it is not a
    // break at all: reordering the knee expression to multiply by the slope
    // before dividing rather than after leaves every bit identical, because
    // at infinite ratio the slope is exactly 1.0 and multiplying by exactly
    // 1.0 is the identity whichever side of the division it sits. The test is
    // not weak there -- the change genuinely does nothing.
    GainComputer computer;

    for (const double knee : { 0.0, 0.5, 6.0, 24.0 })
        for (const double ceiling : { -0.3, -6.0, 0.0, 3.0 })
        {
            computer.setCeilingDb (ceiling);
            computer.setKneeDb (knee);

            // Default ratio, untouched: the state Capstone leaves it in.
            CHECK (std::isinf (computer.getRatio()));

            for (double level = -60.0; level <= 24.0; level += 0.03125)
                CHECK (computer.computeGainReductionDb (level)
                         == limiterCurveAsItWas (level, ceiling, knee));
        }

    // And explicitly asking for it changes nothing either.
    computer.setCeilingDb (-0.3);
    computer.setKneeDb (6.0);
    computer.setRatio (std::numeric_limits<double>::infinity());

    for (double level = -60.0; level <= 24.0; level += 0.03125)
        CHECK (computer.computeGainReductionDb (level)
                 == limiterCurveAsItWas (level, -0.3, 6.0));
}

TEZLA_TEST (a_finite_ratio_realises_exactly_that_ratio_above_the_knee)
{
    // Above the knee a compressor's job is arithmetic: N dB over the
    // threshold comes out as N/ratio dB over it. Checked as the realised
    // OUTPUT level rather than as the reduction, because that is the claim
    // the control makes to the user.
    GainComputer computer;
    computer.setThresholdDb (-20.0);
    computer.setKneeDb (0.0);          // hard corner: no knee to blur it

    for (const double ratio : { 1.5, 2.0, 3.0, 4.0, 8.0, 20.0 })
    {
        computer.setRatio (ratio);

        for (const double over : { 1.0, 4.0, 12.0, 30.0 })
        {
            const double level = -20.0 + over;
            const double outputDb = level + computer.computeGainReductionDb (level);

            CHECK_NEAR (outputDb - (-20.0), over / ratio, 1.0e-12);
        }
    }

    // Below the threshold, nothing, at any ratio.
    for (const double ratio : { 1.5, 4.0, 20.0 })
    {
        computer.setRatio (ratio);

        for (double level = -60.0; level < -20.0; level += 0.5)
            CHECK (computer.computeGainReductionDb (level) == 0.0);
    }
}

TEZLA_TEST (one_to_one_is_bit_exact_bypass)
{
    // CLAUDE.md section 7: a neutral setting is identity, not nearly
    // identity. At 1:1 the slope is exactly 0, so every branch returns a
    // signed zero -- and a signed zero in dB is a gain of exactly 1.
    GainComputer computer;
    computer.setRatio (1.0);

    for (const double knee : { 0.0, 6.0, 24.0 })
    {
        computer.setKneeDb (knee);
        computer.setThresholdDb (-24.0);

        for (double level = -60.0; level <= 24.0; level += 0.125)
        {
            const double reduction = computer.computeGainReductionDb (level);

            CHECK (reduction == 0.0);                    // -0.0 == 0.0 in IEEE
            CHECK (dbToGain (reduction) == 1.0);         // and the gain is exact
        }
    }

    // A ratio below 1 would be an upward expander -- a different curve. It is
    // clamped rather than silently produced.
    computer.setRatio (0.25);
    CHECK (computer.getRatio() == 1.0);
}

TEZLA_TEST (the_knee_is_continuous_in_value_and_slope_at_every_ratio)
{
    // The knee exists to remove the corner; a knee that is continuous in
    // value but not in slope still audibly clicks into compression, and the
    // quadratic form is chosen precisely to be C1 at both ends.
    GainComputer computer;
    computer.setThresholdDb (-18.0);
    computer.setKneeDb (6.0);

    constexpr double h = 1.0e-6;

    for (const double ratio : { 2.0, 4.0, 10.0,
                                std::numeric_limits<double>::infinity() })
    {
        computer.setRatio (ratio);

        for (const double corner : { -18.0 - 6.0, -18.0 + 6.0 })
        {
            const double below = computer.computeGainReductionDb (corner - h);
            const double at    = computer.computeGainReductionDb (corner);
            const double above = computer.computeGainReductionDb (corner + h);

            CHECK_NEAR (below, at, 1.0e-5);              // value
            CHECK_NEAR (at, above, 1.0e-5);

            const double slopeBelow = (at - below) / h;  // slope
            const double slopeAbove = (above - at) / h;

            CHECK_NEAR (slopeBelow, slopeAbove, 1.0e-3);
        }

        // Monotone: more input never means less reduction.
        double previous = 0.0;

        for (double level = -60.0; level <= 24.0; level += 0.125)
        {
            const double reduction = computer.computeGainReductionDb (level);
            CHECK (reduction <= previous + 1.0e-12);
            previous = reduction;
        }
    }
}
