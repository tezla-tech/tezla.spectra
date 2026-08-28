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

#include <tezla/dsp/TriodeStage.hpp>
#include <tezla/measure/Triode12AX7.hpp>

using namespace tezla::dsp;
namespace measure = tezla::measure;

namespace
{
constexpr double kRate = 48000.0;

TriodeStage made (const TriodeStageParameters& parameters = {})
{
    TriodeStage stage;
    stage.prepare (kRate);
    stage.setParameters (parameters);
    stage.reset();
    return stage;
}

/// Runs a constant in until the states settle, and returns the output.
double settle (TriodeStage& stage, double level, double seconds = 1.0)
{
    const int samples = static_cast<int> (kRate * seconds);
    double last = 0.0;

    for (int i = 0; i < samples; ++i)
        last = stage.process (level);

    return last;
}

/// Drives a stage with a continuous tone, optionally measuring the amplitude of
/// that tone alone.
///
/// Quadrature demodulation over a whole number of cycles rather than a peak,
/// and the difference matters: after a loud burst the coupling capacitor is
/// left chasing an offset, and a peak reading counts that drift as signal. It
/// reads *four times louder* right where the stage is in fact choked. This
/// measures the tone and ignores everything slow underneath it.
struct ToneDriver
{
    TriodeStage& stage;
    double frequency { 220.0 };
    double phase { 0.0 };

    double run (double amplitude, double cycles, bool measure)
    {
        const int samples = static_cast<int> (std::round (cycles * kRate / frequency));
        double inPhase = 0.0;
        double quadrature = 0.0;

        for (int i = 0; i < samples; ++i)
        {
            const double angle = 2.0 * std::numbers::pi * frequency * phase / kRate;
            const double y = stage.process (amplitude * std::sin (angle));
            phase += 1.0;

            if (measure)
            {
                inPhase += y * std::sin (angle);
                quadrature += y * std::cos (angle);
            }
        }

        return measure ? 2.0 * std::hypot (inPhase, quadrature) / samples : 0.0;
    }
};

/// Peak output for a sustained sine, after the dynamics have settled.
double settledPeak (TriodeStage& stage, double amplitude, double frequency = 90.0)
{
    const int samples = static_cast<int> (kRate * 0.6);
    const int from = static_cast<int> (kRate * 0.4);
    double peak = 0.0;

    for (int i = 0; i < samples; ++i)
    {
        const double t = static_cast<double> (i) / kRate;
        const double y = stage.process (amplitude * std::sin (2.0 * std::numbers::pi * frequency * t));

        if (i >= from)
            peak = std::max (peak, std::abs (y));
    }

    return peak;
}
} // namespace

// ---------------------------------------------------------------------------
// The plate ceiling
// ---------------------------------------------------------------------------

TEZLA_TEST (plate_ceiling_is_a_straight_wire_above_the_threshold)
{
    // It is a floor under the output, not a shaper of everything. Above the
    // threshold nothing at all should happen -- CLAUDE.md section 7 on
    // bit-exact neutrality.
    const PlateCeiling ceiling { 1.6, 0.45 };

    for (const double y : { 5.0, 1.0, 0.0, -1.0, -1.5999 })
        CHECK (ceiling.evaluate (y) == y);
}

TEZLA_TEST (plate_ceiling_approaches_its_floor_and_never_passes_it)
{
    // The plate cannot swing below the cathode, so there is a hard limit no
    // amount of drive reaches.
    const PlateCeiling ceiling { 1.6, 0.45 };
    const double floor = ceiling.getFloor();

    CHECK_NEAR (floor, -2.05, 1.0e-12);

    for (const double y : { -1.7, -3.0, -10.0, -1000.0 })
    {
        CHECK (ceiling.evaluate (y) >= floor);
        CHECK (ceiling.evaluate (y) < -1.6);
    }

    // Strictly above it while the exponential still has anything left...
    CHECK (ceiling.evaluate (-5.0) > floor);

    // ...and exactly on it once the exponential underflows, which is the right
    // answer rather than a rounding artefact: the plate cannot go further.
    CHECK (ceiling.evaluate (-1000.0) == floor);
}

TEZLA_TEST (plate_ceiling_is_c1_at_the_join)
{
    // A corner here would alias, and the whole reason for the exponential shape
    // is that the slope arrives at 1 from both sides.
    constexpr double threshold = 1.6;
    constexpr double softness = 0.45;
    const PlateCeiling ceiling { threshold, softness };
    constexpr double h = 1.0e-7;

    const auto slope = [&] (double y)
    {
        return (ceiling.evaluate (y + h) - ceiling.evaluate (y - h)) / (2.0 * h);
    };

    // Above the join the slope is exactly 1.
    CHECK_NEAR (slope (-1.0), 1.0, 1.0e-6);

    // Below it, exactly exp(-d/s) -- asserted as the law rather than as a
    // tolerance, so the C1 claim is the thing being checked and not an
    // arbitrary bound near the corner.
    double worst = 0.0;

    for (const double d : { 1.0e-4, 1.0e-3, 0.01, 0.1, 0.5, 1.0 })
        worst = std::max (worst, std::abs (slope (-threshold - d) - std::exp (-d / softness)));

    CHECK (worst < 1.0e-6);
}

TEZLA_TEST (plate_ceiling_antiderivative_is_the_integral)
{
    const PlateCeiling ceiling { 1.2, 0.3 };

    constexpr double from = -4.0;
    constexpr double to = 2.0;
    constexpr int steps = 400000;
    constexpr double h = (to - from) / steps;

    double integral = 0.5 * (ceiling.evaluate (from) + ceiling.evaluate (to));

    for (int i = 1; i < steps; ++i)
        integral += ceiling.evaluate (from + i * h);

    integral *= h;

    CHECK_NEAR (integral, ceiling.antiderivative (to) - ceiling.antiderivative (from), 1.0e-6);
}

// ---------------------------------------------------------------------------
// The stage
// ---------------------------------------------------------------------------

TEZLA_TEST (stage_closes_the_gap_the_bare_curve_left_on_the_grid_side)
{
    // The specification for this whole file, stated as a measurement.
    //
    // Triode.hpp matches a measured 12AX7 on the cutoff half and overshoots
    // badly on the other, because the real stage compresses where a bare power
    // law expands. Grid conduction and the plate bottoming out are what close
    // that, and the reference says by how much: its plate cannot go below the
    // cathode, so its output is bounded at quiescent-plate over gain,
    // 181.98/60.78 = 2.994 normalised units.
    //
    // Peak output for a sustained 90 Hz sine, measured:
    //
    //     amplitude    bare      reference    staged
    //         0.5      0.540       0.523       0.513
    //         1.0      1.155       1.079       1.050
    //         2.0      2.588       2.142       2.010
    //         3.0      4.264       2.994       2.538
    //         5.0      8.261       2.994       2.805
    //
    // At an amplitude of 5 the bare curve is 176% over the reference. The stage
    // is 6% under it.
    const measure::Triode12AX7Stage reference;
    const Triode bareCurve { 1.760, 1.585 };

    const auto peakOfCurve = [] (const auto& evaluate, double amplitude)
    {
        double peak = 0.0;

        for (int i = 0; i < static_cast<int> (kRate * 0.05); ++i)
        {
            const double t = static_cast<double> (i) / kRate;
            peak = std::max (peak, std::abs (evaluate (amplitude * std::sin (2.0 * std::numbers::pi * 90.0 * t))));
        }

        return peak;
    };

    for (const double amplitude : { 1.0, 2.0, 3.0, 5.0 })
    {
        const double bare = peakOfCurve ([&] (double v) { return bareCurve.evaluate (v); }, amplitude);
        const double real = peakOfCurve ([&] (double v) { return reference.normalised (v); }, amplitude);

        auto stage = made();
        const double staged = settledPeak (stage, amplitude);

        // The bare curve is never below the reference, and runs away above it.
        CHECK (bare >= real - 1.0e-9);

        // Ours compresses instead, and lands within a fifth of the reference at
        // every drive -- against the bare curve's 176% at the top.
        CHECK (std::abs (staged - real) < 0.2 * real);
    }

    // And the runaway really is a runaway, so the test above is not passing
    // because everything happens to be close.
    const double bareHot = peakOfCurve ([&] (double v) { return bareCurve.evaluate (v); }, 5.0);
    const double realHot = peakOfCurve ([&] (double v) { return reference.normalised (v); }, 5.0);
    CHECK (bareHot > realHot * 2.5);
}

TEZLA_TEST (stage_bias_drifts_towards_cutoff_when_driven_and_comes_back)
{
    // The cathode bypass capacitor. Average current up, cathode up, operating
    // point towards cutoff -- and then back, over R_k*C_k.
    auto stage = made();

    CHECK (stage.getBiasShift() == 0.0);

    settle (stage, 1.2, 0.5);
    const double driven = stage.getBiasShift();

    // Towards cutoff is positive by this file's sign convention.
    CHECK (driven > 0.05);

    // Take the drive away and it recovers.
    settle (stage, 0.0, 0.5);
    CHECK (std::abs (stage.getBiasShift()) < 0.01);
}

TEZLA_TEST (stage_bias_time_constant_is_the_one_asked_for)
{
    // The parameter names R_k*C_k, which is the *component* time constant --
    // what the capacitor does on its own. Checked with the loop's gain turned
    // right down, so what is being measured is the pole and not the circuit
    // around it.
    TriodeStageParameters parameters;
    parameters.biasMs = 33.0;
    parameters.biasDepth = 0.01;
    parameters.gridDepth = 0.0;
    auto stage = made (parameters);

    const int total = static_cast<int> (kRate * 0.5);
    std::vector<double> shift;
    shift.reserve (static_cast<std::size_t> (total));

    for (int i = 0; i < total; ++i)
    {
        (void) stage.process (1.0);
        shift.push_back (stage.getBiasShift());
    }

    const double settled = shift.back();
    const double threshold = settled * (1.0 - 1.0 / std::numbers::e);

    int reached = total;

    for (int i = 0; i < total; ++i)
        if (shift[static_cast<std::size_t> (i)] >= threshold)
        {
            reached = i;
            break;
        }

    // Measured 32.3 ms.
    CHECK_NEAR (1000.0 * reached / kRate, 33.0, 2.0);
}

TEZLA_TEST (stage_bias_loop_makes_itself_faster_as_it_gets_deeper)
{
    // Worth pinning, because it looks like a bug until it is understood.
    //
    // The bias is computed from the current and then subtracted from the drive,
    // which is negative feedback around a first-order lag -- and negative
    // feedback pushes the closed-loop pole out, so a deeper loop settles
    // *faster* than the capacitor alone. That is what the real circuit does too:
    // the cathode bypass affects gain and speed together, which is why removing
    // it changes so much more than the low end.
    //
    // Measured, all at R_k*C_k = 33 ms: depth 0.01 -> 32.3 ms, 0.1 -> 27.5,
    // 0.35 -> 19.6, 1.0 -> 11.5.
    const auto timeToSettle = [] (double depth)
    {
        TriodeStageParameters parameters;
        parameters.biasMs = 33.0;
        parameters.biasDepth = depth;
        parameters.gridDepth = 0.0;
        auto stage = made (parameters);

        const int total = static_cast<int> (kRate * 0.6);
        std::vector<double> shift;
        shift.reserve (static_cast<std::size_t> (total));

        for (int i = 0; i < total; ++i)
        {
            (void) stage.process (1.0);
            shift.push_back (stage.getBiasShift());
        }

        const double threshold = shift.back() * (1.0 - 1.0 / std::numbers::e);

        for (int i = 0; i < total; ++i)
            if (shift[static_cast<std::size_t> (i)] >= threshold)
                return 1000.0 * i / kRate;

        return 1000.0 * total / kRate;
    };

    const double shallow = timeToSettle (0.01);
    const double medium = timeToSettle (0.35);
    const double deep = timeToSettle (1.0);

    CHECK (medium < shallow);
    CHECK (deep < medium);
    CHECK (deep < shallow * 0.5);
}

TEZLA_TEST (stage_blocks_when_slammed_and_recovers)
{
    // Blocking distortion, which is a large part of what a cranked preamp
    // actually sounds like. A loud passage charges the coupling capacitor
    // through the conducting grid; the whole signal is pushed towards cutoff
    // and the stage goes quiet until the charge leaks away through the grid
    // leak.
    //
    // Measured at a 40 ms recovery, as a fraction of the undisturbed level:
    // 0.032 immediately after, 0.358 at 18 ms, 0.755 at 36 ms, 0.960 at 73 ms,
    // 0.998 at 182 ms.
    TriodeStageParameters parameters;
    parameters.gridRecoveryMs = 40.0;
    auto stage = made (parameters);

    ToneDriver drive { stage };
    drive.run (0.3, 40, false);
    const double before = drive.run (0.3, 20, true);

    CHECK (before > 0.2);

    const auto quietLevelAfter = [&] (double cycles)
    {
        auto fresh = made (parameters);
        ToneDriver again { fresh };
        again.run (0.3, 40, false);
        again.run (8.0, 30, false);      // the loud passage
        again.run (0.3, cycles, false);  // waiting
        return again.run (0.3, 4, true);
    };

    // Choked right after.
    CHECK (quietLevelAfter (2.0) < before * 0.1);

    // Climbing back.
    CHECK (quietLevelAfter (8.0) > quietLevelAfter (2.0));
    CHECK (quietLevelAfter (16.0) > quietLevelAfter (8.0));

    // And fully recovered.
    CHECK (quietLevelAfter (120.0) > before * 0.95);
}

TEZLA_TEST (stage_blocking_recovery_is_the_time_constant_asked_for)
{
    // Not merely "it recovers": the grid-leak time constant is a control, and a
    // longer one has to block for longer. Measured 36 ms after a loud passage,
    // as a fraction of the undisturbed level: 0.989 at 5 ms recovery, 0.755 at
    // 40 ms, 0.027 at 200 ms.
    const auto levelAfterWaiting = [] (double recoveryMs)
    {
        TriodeStageParameters parameters;
        parameters.gridRecoveryMs = recoveryMs;

        auto reference = made (parameters);
        ToneDriver settle { reference };
        settle.run (0.3, 40, false);
        const double undisturbed = settle.run (0.3, 20, true);

        auto stage = made (parameters);
        ToneDriver drive { stage };
        drive.run (0.3, 40, false);
        drive.run (8.0, 30, false);
        drive.run (0.3, 8, false);

        return drive.run (0.3, 4, true) / undisturbed;
    };

    const double quick = levelAfterWaiting (5.0);
    const double medium = levelAfterWaiting (40.0);
    const double slow = levelAfterWaiting (200.0);

    CHECK (quick > 0.9);
    CHECK (medium < quick);
    CHECK (slow < medium);
    CHECK (slow < 0.2);
}

TEZLA_TEST (stage_is_quiet_and_neutral_at_rest)
{
    // Silence in, silence out, and no DC arriving from the asymmetry.
    auto stage = made();

    for (int i = 0; i < 4096; ++i)
        CHECK (stage.process (0.0) == 0.0);
}

TEZLA_TEST (stage_never_lets_either_feedback_path_run_away)
{
    // CLAUDE.md section 7: a feedback loop around a nonlinearity needs a bound
    // that cannot be defeated, checked by sweeping the parameter space rather
    // than sampling it.
    double worstOutput = 0.0;
    double worstBias = 0.0;
    double worstCharge = 0.0;

    for (const double knee : { 0.1, 0.5, 1.76, 4.0 })
        for (const double biasDepth : { 0.0, 0.5, 2.0, 8.0 })
            for (const double gridDepth : { 0.0, 0.9, 3.0 })
                for (const double drive : { 1.0, 20.0, 500.0 })
                {
                    TriodeStageParameters parameters;
                    parameters.knee = knee;
                    parameters.biasDepth = biasDepth;
                    parameters.gridDepth = gridDepth;
                    parameters.biasMs = 1.0;
                    parameters.gridRecoveryMs = 1.0;

                    auto stage = made (parameters);

                    for (int i = 0; i < 8000; ++i)
                    {
                        const double t = static_cast<double> (i) / kRate;
                        const double y = stage.process (drive * std::sin (2.0 * std::numbers::pi * 110.0 * t));

                        worstOutput = std::max (worstOutput, std::abs (y));
                        worstBias = std::max (worstBias, std::abs (stage.getBiasShift()));
                        worstCharge = std::max (worstCharge, std::abs (stage.getGridCharge()));

                        CHECK (std::isfinite (y));
                    }
                }

    // The plate floor bounds the output on one side and the cutoff shelf on the
    // other, whatever the loops do.
    CHECK (worstOutput < 100.0);
    CHECK (worstBias < TriodeStage::kStateLimitInKnees * 4.0 + 1.0e-9);
    CHECK (worstCharge < TriodeStage::kStateLimitInKnees * 4.0 + 1.0e-9);
}

TEZLA_TEST (stage_output_is_bounded_below_by_the_plate_floor)
{
    // However hard it is driven, the plate cannot go below the cathode. The
    // coupling capacitor shifts the average, so the bound is checked on the
    // shaped signal's excursion rather than on an absolute level.
    TriodeStageParameters parameters;
    parameters.plateHeadroom = 1.6;
    parameters.plateSoftness = 0.45;
    parameters.couplingHz = 0.01;    // nearly out of the way
    auto stage = made (parameters);

    double lowest = 0.0;

    for (int i = 0; i < 40000; ++i)
    {
        const double t = static_cast<double> (i) / kRate;
        lowest = std::min (lowest, stage.process (30.0 * std::sin (2.0 * std::numbers::pi * 90.0 * t)));
    }

    CHECK (lowest > PlateCeiling (1.6, 0.45).getFloor() - 0.05);
}
