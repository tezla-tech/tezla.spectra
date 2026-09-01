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
#include <vector>

#include <PresenceTracker.hpp>

using namespace tezla::membrana;

namespace
{
    constexpr double kFs = 48000.0;

    // Steady-state RMS gain of a sine through the tracker: settle, then
    // integrate half a second.
    double sineGainDb (PresenceTracker& tracker, double hz, double peak,
                       double settleSeconds)
    {
        const double dPhase = 2.0 * std::numbers::pi * hz / kFs;
        double phase = 0.0;

        for (int n = 0; n < static_cast<int> (kFs * settleSeconds); ++n)
        {
            (void) tracker.process (peak * std::sin (phase));
            phase += dPhase;
        }

        double energyIn = 0.0, energyOut = 0.0;

        for (int n = 0; n < static_cast<int> (kFs * 0.5); ++n)
        {
            const double x = peak * std::sin (phase);
            phase += dPhase;
            const double y = tracker.process (x);
            energyIn += x * x;
            energyOut += y * y;
        }

        return 10.0 * std::log10 (energyOut / energyIn);
    }

    // A square wave has |x| == amplitude at EVERY sample, so the tracker's
    // instantaneous-dB detector reads the level exactly, with no crest
    // ripple -- the clean way to place a known level on the curve.
    void runSquare (PresenceTracker& tracker, double levelDb, double seconds)
    {
        const double amplitude = std::pow (10.0, levelDb / 20.0);
        const int halfPeriod = 96;   // 250 Hz at 48 k

        for (int n = 0; n < static_cast<int> (kFs * seconds); ++n)
            (void) tracker.process ((n / halfPeriod) % 2 != 0 ? amplitude : -amplitude);
    }

    PresenceTracker makeTracker (double amountDb, double track,
                                 double frequencyHz = 4500.0,
                                 double thresholdDb = -28.0)
    {
        PresenceTracker tracker;
        tracker.setAmountDb (amountDb);
        tracker.setTrack (track);
        tracker.setFrequencyHz (frequencyHz);
        tracker.setThresholdDb (thresholdDb);
        tracker.prepare (kFs);
        return tracker;
    }
}

// -- Neutral and silence ------------------------------------------------------

TEZLA_TEST (presence_neutral_is_verbatim_and_silence_stays_exact)
{
    // amount == 0 or disabled: the branch returns the input verbatim.
    // x + 0 * HP(x) would flip -0.0 to +0.0; the bit patterns say it never
    // runs.
    PresenceTracker neutral = makeTracker (0.0, 1.0);
    const double probe[] = { 0.5, -0.5, 0.0, -0.0, 1.0e-308, -1.0 };

    for (double x : probe)
        CHECK (std::bit_cast<std::uint64_t> (neutral.process (x))
               == std::bit_cast<std::uint64_t> (x));

    PresenceTracker disabled = makeTracker (6.0, 1.0);
    disabled.setEnabled (false);

    for (double x : probe)
        CHECK (std::bit_cast<std::uint64_t> (disabled.process (x))
               == std::bit_cast<std::uint64_t> (x));

    // Silence through an ENGAGED tracker: the floor calls for full lift,
    // and full lift times zero is still exactly zero. Two seconds is five
    // release time-constants, so the lift has genuinely leaned in by the
    // end (9 * (1 - e^-5) = 8.94 dB) while every output stays exact zero.
    PresenceTracker engaged = makeTracker (9.0, 1.0);

    for (int n = 0; n < 96000; ++n)
        CHECK (engaged.process (0.0) == 0.0);

    CHECK (engaged.currentLiftDb() > 8.9);   // it genuinely leaned in
}

// -- The static shelf (track = 0) --------------------------------------------

TEZLA_TEST (presence_track_zero_is_a_static_shelf_of_exactly_amount)
{
    // track = 0 makes the curve a constant: lift = amount at every level.
    // The smoother approaches it exponentially (release tau 400 ms), so
    // after 3 s the lift reads amount to within 0.004 dB -- measured
    // 5.9967 -- and the realised shelf at 16 x the corner reads +5.9855 dB
    // (the second-order highpass still carries ~7 degrees of phase there;
    // the header documents the asymptote).
    PresenceTracker tracker = makeTracker (6.0, 0.0, 1000.0);
    const double highSide = sineGainDb (tracker, 16000.0, 0.25, 3.0);

    CHECK_NEAR (tracker.currentLiftDb(), 6.0, 0.01);
    CHECK_NEAR (highSide, 6.0, 0.05);

    // The low side: flat to within the realisation's small dip. The +HP
    // form inverts below the corner (a 2nd-order highpass's low tail is
    // 180 degrees out), so fc/8 reads -0.13 dB at full 6 dB lift; two
    // octaves further down it is gone. Pinned as a documented property,
    // not hidden by a loose "close to 0" check.
    PresenceTracker low = makeTracker (6.0, 0.0, 1000.0);
    const double dip = sineGainDb (low, 125.0, 0.25, 3.0);
    CHECK (dip > -0.2);
    CHECK (dip < 0.05);

    PresenceTracker lower = makeTracker (6.0, 0.0, 4500.0);
    CHECK_NEAR (sineGainDb (lower, 100.0, 0.25, 3.0), 0.0, 0.02);
}

// -- The curve (track = 1) ----------------------------------------------------

TEZLA_TEST (presence_curve_lands_on_its_three_defining_points)
{
    // Full tracking, amount 6, threshold -28, knee 12: at the threshold the
    // lift is 0; half a knee below it is half (the Hermite midpoint); a
    // full knee below it is all 6 dB. Square carriers place the level
    // exactly; measured 0.0000 / 2.9983 / 5.9967 after 3 s (the release
    // tail costs 0.0033 dB of the asymptote).
    struct Point { double levelDb, expectedLiftDb; };
    constexpr Point points[] = {
        { -16.0, 0.0 },   // above threshold: nothing
        { -28.0, 0.0 },   // at threshold: the curve starts here
        { -34.0, 3.0 },   // half knee: Hermite midpoint
        { -40.0, 6.0 },   // full knee: all of it
        { -52.0, 6.0 },   // and no more below
    };

    for (const auto& point : points)
    {
        PresenceTracker tracker = makeTracker (6.0, 1.0);
        runSquare (tracker, point.levelDb, 3.0);
        CHECK_NEAR (tracker.currentLiftDb(), point.expectedLiftDb, 0.01);
    }

    // And the whole path realises it: a full-knee-quiet carrier drives the
    // shelf to its full amount, measured on a 16 x probe added afterwards.
    PresenceTracker full = makeTracker (6.0, 1.0, 1000.0);
    runSquare (full, -40.0, 3.0);
    // Probe quietly enough not to move the detector off the curve's flat.
    const double gain = sineGainDb (full, 16000.0, 0.005, 0.5);
    CHECK_NEAR (gain, 6.0, 0.1);
}

TEZLA_TEST (presence_track_blends_static_and_ridden)
{
    // track = 0.5 at a loud level: half the shelf stands, half is ridden
    // away. lift = 6 * ((1 - 0.5) + 0.5 * 0) = 3 dB.
    PresenceTracker tracker = makeTracker (6.0, 0.5);
    runSquare (tracker, -10.0, 3.0);
    CHECK_NEAR (tracker.currentLiftDb(), 3.0, 0.01);
}

// -- The bound ----------------------------------------------------------------

TEZLA_TEST (presence_lift_never_exceeds_amount_over_the_swept_space)
{
    // Swept, not sampled (CLAUDE.md section 7): every combination of
    // amount, track, threshold and a level ladder, with the lift checked
    // at EVERY sample against amount. Measured worst overshoot: exactly
    // 0.0 -- the curve lives in [0, 1] and the smoother is a convex
    // combination, so the bound holds by construction; this asserts the
    // construction survived the implementation.
    for (double amount : { 0.0, 3.0, 9.0 })
        for (double track : { 0.0, 0.5, 1.0 })
            for (double threshold : { -60.0, -28.0, 0.0 })
                for (double levelDb = -80.0; levelDb <= 0.0; levelDb += 8.0)
                {
                    PresenceTracker tracker =
                        makeTracker (amount, track, 4500.0, threshold);
                    const double amplitude = std::pow (10.0, levelDb / 20.0);

                    for (int n = 0; n < 24000; ++n)
                    {
                        (void) tracker.process ((n / 96) % 2 != 0 ? amplitude
                                                                  : -amplitude);
                        CHECK (tracker.currentLiftDb() <= amount + 1.0e-9);
                        CHECK (tracker.currentLiftDb() >= -1.0e-9);
                    }
                }
}

// -- Block-size independence --------------------------------------------------

TEZLA_TEST (presence_output_is_bit_identical_across_block_sizes)
{
    // The dB-to-linear pow() runs on the tracker's own 64-sample timer,
    // never per processBlock call, so chunking the same stream as 64s or
    // 512s is bit-identical (Emberdrive's 0.296 lesson, section 7).
    // Break-checked: replacing the timer with a once-per-block update went
    // red here. (A weaker break -- forcing an update at each block head
    // while KEEPING the 64-sample reload -- stayed green, because head
    // updates on 64-multiple blocks land on the same grid; the cadence has
    // to actually differ for the outputs to.)
    constexpr int kTotal = 47616;   // divisible by both 64 and 512

    std::vector<double> input (kTotal);
    double phase = 0.0;
    const double dPhase = 2.0 * std::numbers::pi * 700.0 / kFs;

    for (int n = 0; n < kTotal; ++n)
    {
        // A level drop halfway keeps the tracker moving during the test.
        input[n] = 0.4 * std::sin (phase) * (n < kTotal / 2 ? 1.0 : 0.05);
        phase += dPhase;
    }

    PresenceTracker a = makeTracker (6.0, 1.0);
    PresenceTracker b = makeTracker (6.0, 1.0);

    std::vector<double> outA (input), outB (input);

    for (int n = 0; n < kTotal; n += 64)
        a.processBlock (outA.data() + n, 64);

    for (int n = 0; n < kTotal; n += 512)
        b.processBlock (outB.data() + n, 512);

    for (int n = 0; n < kTotal; ++n)
        CHECK (std::bit_cast<std::uint64_t> (outA[static_cast<std::size_t> (n)])
               == std::bit_cast<std::uint64_t> (outB[static_cast<std::size_t> (n)]));
}

// -- No zipper ----------------------------------------------------------------

TEZLA_TEST (presence_amount_sweep_produces_no_step)
{
    // Drag the Presence knob 0 -> 9 dB over 150 control chunks with a
    // 5 kHz sine playing through a 1 kHz shelf: every mechanism between
    // the knob and the gain (the dB smoother, the 30 ms gain smoother, the
    // 64-sample update timer) has to hand over smoothly. The output's
    // neighbour step must stay within 25% of the sine's own.
    PresenceTracker tracker = makeTracker (0.0, 0.0, 1000.0);

    const double dPhase = 2.0 * std::numbers::pi * 5000.0 / kFs;
    double phase = 0.0, previous = 0.0, maxStep = 0.0;

    for (int c = 0; c < 150; ++c)
    {
        tracker.setAmountDb (9.0 * c / 149.0);

        for (int n = 0; n < 256; ++n)
        {
            const double y = tracker.process (0.25 * std::sin (phase));
            phase += dPhase;

            if (c > 0 || n > 0)
                maxStep = std::max (maxStep, std::abs (y - previous));

            previous = y;
        }
    }

    // The sine's own largest step at 5 kHz, allowing for the shelf's near
    // doubling of HF amplitude by the end of the sweep.
    const double sineStep = 0.25 * dPhase;
    CHECK (maxStep < sineStep * 2.82 * 1.25);
}
