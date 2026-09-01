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

#include <DetailLift.hpp>

using namespace tezla::membrana;

namespace
{
    constexpr double kFs = 48000.0;

    double sineGainDb (DetailLift& lift, double hz, double peakDb,
                       double settleSeconds)
    {
        const double amplitude = std::pow (10.0, peakDb / 20.0);
        const double dPhase = 2.0 * std::numbers::pi * hz / kFs;
        double phase = 0.0;

        for (int n = 0; n < static_cast<int> (kFs * settleSeconds); ++n)
        {
            (void) lift.process (amplitude * std::sin (phase));
            phase += dPhase;
        }

        double energyIn = 0.0, energyOut = 0.0;

        for (int n = 0; n < static_cast<int> (kFs * 0.5); ++n)
        {
            const double x = amplitude * std::sin (phase);
            phase += dPhase;
            const double y = lift.process (x);
            energyIn += x * x;
            energyOut += y * y;
        }

        return 10.0 * std::log10 (energyOut / energyIn);
    }

    DetailLift makeLift (double amountDb, double floorDb = -55.0,
                         double splitHz = 3000.0)
    {
        DetailLift lift;
        lift.setAmountDb (amountDb);
        lift.setFloorDb (floorDb);
        lift.setSplitHz (splitHz);
        lift.prepare (kFs);
        return lift;
    }
}

// -- The curve, exactly -------------------------------------------------------

TEZLA_TEST (detail_curve_is_the_windowed_product_exactly)
{
    // The static curve as a pure function: detail * s((T_d - L)/15) *
    // s((L - floor)/6), T_d = floor + 20. Exact rationals at the pinned
    // points, detail 6, floor -55:
    CHECK_NEAR (DetailLift::curveLiftDb (6.0, -55.0, -35.0), 0.0, 1.0e-12);
    CHECK_NEAR (DetailLift::curveLiftDb (6.0, -55.0, -40.0), 1.5555555555555556, 1.0e-12);
    CHECK_NEAR (DetailLift::curveLiftDb (6.0, -55.0, -45.0), 4.4444444444444444, 1.0e-12);
    CHECK_NEAR (DetailLift::curveLiftDb (6.0, -55.0, -50.0), 5.5555555555555556, 1.0e-12);

    // The floor is absolute: at it and below it, exactly nothing.
    CHECK (DetailLift::curveLiftDb (6.0, -55.0, -55.0) == 0.0);
    CHECK (DetailLift::curveLiftDb (12.0, -55.0, -70.0) == 0.0);
    CHECK (DetailLift::curveLiftDb (12.0, -55.0, -120.0) == 0.0);

    // And so is the top: anything at or above T_d gets no lift at all --
    // loud detail does not need help.
    CHECK (DetailLift::curveLiftDb (12.0, -55.0, -30.0) == 0.0);
    CHECK (DetailLift::curveLiftDb (12.0, -55.0, 0.0) == 0.0);
}

// -- Neutral, silence, hiss ---------------------------------------------------

TEZLA_TEST (detail_neutral_is_verbatim_and_silence_is_exact)
{
    // detail == 0 or disabled: the branch, bit patterns preserved, -0.0
    // included.
    DetailLift neutral = makeLift (0.0);
    const double probe[] = { 0.5, -0.5, 0.0, -0.0, 1.0e-308, -1.0 };

    for (double x : probe)
        CHECK (std::bit_cast<std::uint64_t> (neutral.process (x))
               == std::bit_cast<std::uint64_t> (x));

    DetailLift disabled = makeLift (12.0);
    disabled.setEnabled (false);

    for (double x : probe)
        CHECK (std::bit_cast<std::uint64_t> (disabled.process (x))
               == std::bit_cast<std::uint64_t> (x));

    // Silence through an ENGAGED stage: below the floor the curve calls
    // for zero lift, so two seconds of zeros are two seconds of exact
    // zeros and the lift never moves.
    DetailLift engaged = makeLift (12.0);

    for (int n = 0; n < 96000; ++n)
        CHECK (engaged.process (0.0) == 0.0);

    CHECK (engaged.currentLiftDb() == 0.0);
}

TEZLA_TEST (detail_hiss_below_the_floor_is_lifted_exactly_nothing)
{
    // A -70 dBFS 6 kHz tone -- tape hiss territory -- through full detail
    // with the default -55 floor: the floor factor is exactly zero there,
    // the lift never leaves zero, and the measured gain is 0.000000000 dB.
    // This is the selling point: consonants up, hiss not.
    DetailLift lift = makeLift (12.0);
    const double gain = sineGainDb (lift, 6000.0, -70.0, 1.5);

    CHECK_NEAR (gain, 0.0, 1.0e-9);
    CHECK (lift.currentLiftDb() == 0.0);
}

// -- Vowels ------------------------------------------------------------------

TEZLA_TEST (detail_a_vowel_alone_reads_exactly_zero_lift)
{
    // The 4th-order sidechain highpass is why: a 300 Hz body at any level
    // reads ~-80 dB in the detector, far below any floor, so the lift is
    // EXACTLY zero -- not small, zero -- and the vowel passes at 0.000000
    // dB even at maximum detail. (The first-order audio split alone would
    // have let the vowel's -20 dB leak trigger its own lift; the steep
    // detector is the fix, and it costs the audio path nothing.)
    for (double levelDb : { -6.0, -10.0, -20.0, -30.0 })
    {
        DetailLift lift = makeLift (12.0);
        const double gain = sineGainDb (lift, 300.0, levelDb, 1.0);
        CHECK_NEAR (gain, 0.0, 1.0e-9);
        CHECK (lift.currentLiftDb() == 0.0);
    }
}

TEZLA_TEST (detail_mixed_vowel_and_consonant_leak_stays_bounded)
{
    // A -10 dB vowel with -38 dB consonant bursts at full detail: the lift
    // peaks at 3.78 dB during bursts (measured), which through the -20 dB
    // audio-split leak bounds the vowel body's rise at ~0.06 dB -- masked
    // by the consonant that caused it. The header documents this as the
    // price of the allpass-free split; here it is held to stay small.
    DetailLift lift = makeLift (12.0);

    double phaseV = 0.0, phaseC = 0.0, maxLift = 0.0;
    const double dV = 2.0 * std::numbers::pi * 300.0 / kFs;
    const double dC = 2.0 * std::numbers::pi * 6000.0 / kFs;

    for (int n = 0; n < 96000; ++n)
    {
        const bool consonant = (n / 4800) % 2 == 1;
        const double x = 0.316 * std::sin (phaseV)
                         + (consonant ? 0.0126 * std::sin (phaseC) : 0.0);
        (void) lift.process (x);
        phaseV += dV;
        phaseC += dC;
        maxLift = std::max (maxLift, lift.currentLiftDb());
    }

    CHECK_NEAR (maxLift, 3.78, 0.5);

    // The vowel-rise bound through the leak: |1 + g * H(300 Hz)|.
    const double g = std::pow (10.0, maxLift / 20.0) - 1.0;
    const double rise = 20.0 * std::log10 (std::hypot (1.0 + g * 0.00985,
                                                       g * 0.09851));
    CHECK (rise < 0.1);
}

// -- Detail in the window -----------------------------------------------------

TEZLA_TEST (detail_in_the_window_is_lifted_per_the_curve)
{
    // 6 kHz sines at curve levels, detail 6, floor -55. The 5 ms detector
    // mean reads a sine ~3.9 dB below its peak, and the asymmetric
    // smoother rides slightly below the nominal curve on the ripple;
    // measured lifts pinned as the realised behaviour:
    //   peak -38 -> lift 2.56 dB     peak -43 -> lift 5.28 dB
    // with the audio gain smaller than the lift (the first-order residual
    // is 0.894 at 2 x the corner and carries 27 degrees of phase):
    //   peak -38 -> +1.81 dB         peak -43 -> +3.95 dB
    {
        DetailLift lift = makeLift (6.0);
        const double gain = sineGainDb (lift, 6000.0, -38.0, 1.5);
        CHECK_NEAR (lift.currentLiftDb(), 2.56, 0.25);
        CHECK_NEAR (gain, 1.81, 0.25);
    }
    {
        DetailLift lift = makeLift (6.0);
        const double gain = sineGainDb (lift, 6000.0, -43.0, 1.5);
        CHECK_NEAR (lift.currentLiftDb(), 5.28, 0.25);
        CHECK_NEAR (gain, 3.95, 0.25);
    }

    // Above the window: a healthy -25 dB peak gets nothing.
    {
        DetailLift lift = makeLift (6.0);
        const double gain = sineGainDb (lift, 6000.0, -25.0, 1.5);
        CHECK_NEAR (gain, 0.0, 0.01);
    }
}

// -- The bound ----------------------------------------------------------------

TEZLA_TEST (detail_lift_never_exceeds_amount_over_the_swept_space)
{
    // Amount x floor x level ladder, lift checked at every sample.
    // Measured worst overshoot: exactly 0.0 -- both curve factors live in
    // [0, 1] and the smoother is convex.
    for (double amount : { 0.0, 6.0, 12.0 })
        for (double floorDb : { -90.0, -55.0, -30.0 })
            for (double levelDb = -90.0; levelDb <= 0.0; levelDb += 10.0)
            {
                DetailLift lift = makeLift (amount, floorDb);
                const double amplitude = std::pow (10.0, levelDb / 20.0);
                double phase = 0.0;
                const double dPhase = 2.0 * std::numbers::pi * 6000.0 / kFs;

                for (int n = 0; n < 24000; ++n)
                {
                    (void) lift.process (amplitude * std::sin (phase));
                    phase += dPhase;
                    CHECK (lift.currentLiftDb() <= amount + 1.0e-9);
                    CHECK (lift.currentLiftDb() >= -1.0e-9);
                }
            }
}
