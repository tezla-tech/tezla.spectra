// Copyright (c) 2026 The Tezla <thetezla@proton.me>
// Created by The Tezla -- https://github.com/wingit33/tezla.tech
// Music: https://soundcloud.com/thetezla | https://thetezla.bandcamp.com
// Built with development assistance from Claude (Anthropic).
// SPDX-License-Identifier: AGPL-3.0-only
// GNU AGPLv3 (see LICENSE), plus NOTICE.md's attribution term. Keep intact.

#include "TestFramework.hpp"

#include <cmath>
#include <cstdio>
#include <vector>

#include <tezla/dsp/DcBlocker.hpp>
#include <tezla/measure/Fft.hpp>
#include <tezla/measure/Harmonics.hpp>
#include <tezla/measure/Signals.hpp>

#include <Hysteresis.hpp>

using namespace tezla::ferrite;
namespace measure = tezla::measure;

namespace
{
// The hysteresis stage always runs inside the oversampled section; 192 kHz
// is the effective rate the whole suite designs against (CLAUDE.md 6).
constexpr double kRate = 192000.0;

[[nodiscard]] Hysteresis makeStage (double drive, double saturation, double bias)
{
    Hysteresis stage;
    stage.prepare (kRate);
    stage.setParameters (drive, saturation, bias);
    return stage;
}

[[nodiscard]] std::vector<double> renderSine (Hysteresis& stage, double frequency,
                                              double amplitude, std::size_t count)
{
    std::vector<double> out (count);

    for (std::size_t i = 0; i < count; ++i)
        out[i] = stage.process (amplitude
                                  * std::sin (2.0 * 3.141592653589793 * frequency
                                                * static_cast<double> (i) / kRate));

    return out;
}
} // namespace

// ---------------------------------------------------------------------------
// The loop is a loop
// ---------------------------------------------------------------------------

TEZLA_TEST (the_loop_encloses_area_and_remembers)
{
    // Drive a slow sine and watch (H, M) trace one steady cycle: hysteresis
    // means the rising and falling paths differ, so the curve encloses area
    // (the energy the tape eats) and M at H = 0 depends on where H has BEEN
    // -- remanence, the memory that makes it magnetic recording at all.
    auto stage = makeStage (0.7, 0.5, 0.5);

    constexpr double frequency = 100.0;
    constexpr double amplitude = 2.0;
    const auto period = static_cast<std::size_t> (kRate / frequency);

    // Settle through the first magnetisation curve onto the steady loop.
    (void) renderSine (stage, frequency, amplitude, 4 * period);

    // Two full cycles of (H, M): the first supplies a closed loop for the
    // area integral, the second guarantees the scan sees a rising AND a
    // falling zero crossing -- the first version captured one period from
    // phase zero, whose rising crossing sits exactly on the wrap and was
    // never found.
    std::vector<double> H (2 * period), M (2 * period);

    for (std::size_t i = 0; i < 2 * period; ++i)
    {
        H[i] = amplitude * std::sin (2.0 * 3.141592653589793 * frequency
                                       * static_cast<double> (i) / kRate);
        M[i] = stage.process (H[i]);
    }

    // Signed enclosed area via the shoelace integral of M dH around the
    // first closed cycle. Zero for any memoryless curve, whatever its shape.
    double area = 0.0;

    for (std::size_t i = 0; i < period; ++i)
    {
        const std::size_t next = (i + 1) % period;
        area += 0.5 * (M[next] + M[i]) * (H[next] - H[i]);
    }

    // Pinned to the measured value, and deliberately tight: the deltaM gate
    // (the term forbidding negative susceptibility) widens the loop from
    // -0.562 to this -0.536, so a +-0.02 pin is what makes that gate's
    // removal VISIBLE -- with only a loose is-there-any-area check, deleting
    // the gate passed every test in this file.
    CHECK (std::abs (area - -0.5363) < 0.02);

    // Remanence: find M where H crosses zero falling (from +peak) and
    // rising (from -peak). Falling keeps a positive memory, rising a
    // negative one, and they must genuinely differ.
    double falling = 0.0, rising = 0.0;

    for (std::size_t i = 1; i < 2 * period; ++i)
    {
        if (H[i - 1] > 0.0 && H[i] <= 0.0)
            falling = M[i];
        if (H[i - 1] < 0.0 && H[i] >= 0.0)
            rising = M[i];
    }

    // Measured: +-0.1326 against a ceiling of 1.25.
    CHECK (falling > 0.02 * stage.saturationCeiling());
    CHECK (rising < -0.02 * stage.saturationCeiling());
}

// ---------------------------------------------------------------------------
// Symmetry
// ---------------------------------------------------------------------------

TEZLA_TEST (a_symmetric_drive_makes_odd_harmonics_only)
{
    // The loop is odd-symmetric, so a steady sine may generate 3rd, 5th,
    // 7th... but any even harmonic is a defect (an asymmetry the model does
    // not contain). The start-up transient IS asymmetric -- the virgin
    // curve -- so the measurement waits for the steady loop.
    auto stage = makeStage (0.8, 0.5, 0.5);

    constexpr std::size_t window = 1 << 15;
    const double frequency = measure::binExactFrequency (997.0, kRate, window);

    (void) renderSine (stage, frequency, 1.5, window);
    const auto signal = renderSine (stage, frequency, 1.5, window);

    const auto report = measure::analyseHarmonics (signal, kRate, frequency);

    // harmonicsDb[0] is the 2nd, [2] the 4th, relative to the fundamental.
    // Measured: 2nd -292 dB, 3rd -15.5 dB, 4th -294 dB, 5th -26.6 dB.
    CHECK (report.harmonicsDb.size() >= 4);
    CHECK (report.harmonicsDb[0] < -60.0);
    CHECK (report.harmonicsDb[2] < -60.0);

    // And the odd ones are genuinely there -- this is a saturator.
    CHECK (report.harmonicsDb[1] > -40.0);
}

// ---------------------------------------------------------------------------
// Distortion tracks the drive control
// ---------------------------------------------------------------------------

TEZLA_TEST (thd_rises_with_drive_and_is_pinned)
{
    // Measured on this implementation (997 Hz bin-exact, amplitude 1.0,
    // saturation 0.5, bias 0.5, 192 kHz):
    //
    //     drive 0.1     THD  -31.5 dB
    //     drive 0.5     THD  -22.6 dB
    //     drive 0.9     THD  -17.4 dB
    //
    // The pins hold each figure to +-3 dB, and the ordering -- more drive,
    // more distortion -- is asserted outright.
    constexpr std::size_t window = 1 << 15;
    const double frequency = measure::binExactFrequency (997.0, kRate, window);

    auto thdAt = [&] (double drive)
    {
        auto stage = makeStage (drive, 0.5, 0.5);
        (void) renderSine (stage, frequency, 1.0, window);
        const auto signal = renderSine (stage, frequency, 1.0, window);
        return measure::analyseHarmonics (signal, kRate, frequency).thdDb;
    };

    const double gentle = thdAt (0.1);
    const double middle = thdAt (0.5);
    const double heavy = thdAt (0.9);

    CHECK (gentle < middle);
    CHECK (middle < heavy);

    CHECK (std::abs (gentle - -31.5) < 3.0);
    CHECK (std::abs (middle - -22.6) < 3.0);
    CHECK (std::abs (heavy - -17.4) < 3.0);
}

// ---------------------------------------------------------------------------
// The solver, measured
// ---------------------------------------------------------------------------

TEZLA_TEST (the_sweep_stays_finite_bounded_and_guard_silent)
{
    // Every parameter corner x frequency x amplitude the stage can meet in
    // real use: output finite, magnetisation inside its physical ceiling
    // (plus the integrator's measured few-percent overshoot), and NO guard
    // engaged -- a clamp that fires routinely would be shaping the sound,
    // and every measurement of the loop would be measuring the clamp
    // instead (CLAUDE.md section 10). Measured across this sweep the
    // excursion peaks at 1.142x the ceiling -- one corner, drive, bias and
    // saturation all maxed with 20 kHz at amplitude 5 -- so the bound holds
    // the integrator to 1.2x and the guard rail at 1.5x stays untouched.
    for (const double drive : { 0.0, 0.5, 1.0 })
        for (const double saturation : { 0.0, 0.5, 1.0 })
            for (const double bias : { 0.0, 0.5, 1.0 })
                for (const double frequency : { 50.0, 1000.0, 5000.0, 20000.0 })
                    for (const double amplitude : { 0.5, 2.0, 5.0 })
                    {
                        auto stage = makeStage (drive, saturation, bias);
                        bool healthy = true;

                        for (int i = 0; i < 2000; ++i)
                        {
                            const double sample = stage.process (
                                amplitude * std::sin (2.0 * 3.141592653589793
                                                        * frequency * i / kRate));

                            healthy = healthy && std::isfinite (sample)
                                   && std::abs (sample)
                                        <= 1.2 * stage.saturationCeiling();
                        }

                        if (! healthy || stage.hasClamped())
                        {
                            std::printf ("FERRITE-SWEEP failed at d=%.1f s=%.1f "
                                         "b=%.1f f=%g a=%.1f\n",
                                         drive, saturation, bias, frequency,
                                         amplitude);
                            CHECK (false);
                            return;
                        }
                    }

    CHECK (true);
}

TEZLA_TEST (one_step_per_sample_tracks_an_eightfold_finer_integration)
{
    // The convergence claim, measured: the same field trajectory fed to an
    // identical stage running at 8x the rate (each sample split into eight
    // linearly interpolated sub-samples) must land on the same
    // magnetisation. This bounds the per-sample integrator's error against
    // a reference it cannot cheat, because both are the same model.
    //
    // Measured RMS difference, relative to the saturation ceiling:
    // 4.75e-4 at drive 0.5 / 1 kHz, 9.86e-4 at drive 0.9 / 5 kHz -- about
    // -66 dB of integration disagreement, dominated by the derivative
    // recursion smoothing differently at the two rates.
    auto compare = [] (double drive, double frequency, double amplitude)
    {
        auto coarse = makeStage (drive, 0.5, 0.5);

        Hysteresis fine;
        fine.prepare (8.0 * kRate);
        fine.setParameters (drive, 0.5, 0.5);

        double sumSq = 0.0;
        double previous = 0.0;
        const int count = 6000;

        for (int i = 0; i < count; ++i)
        {
            const double input = amplitude
                                   * std::sin (2.0 * 3.141592653589793
                                                 * frequency * i / kRate);

            const double coarseOut = coarse.process (input);

            double fineOut = 0.0;

            for (int sub = 1; sub <= 8; ++sub)
                fineOut = fine.process (previous
                                          + (input - previous) * sub / 8.0);

            previous = input;

            // Compare once past the differing start-up transients.
            if (i >= 1000)
            {
                const double difference = coarseOut - fineOut;
                sumSq += difference * difference;
            }
        }

        return std::sqrt (sumSq / (count - 1000)) / coarse.saturationCeiling();
    };

    const double gentle = compare (0.5, 1000.0, 1.5);
    const double brutal = compare (0.9, 5000.0, 2.0);

    CHECK (gentle < 1.5e-3);
    CHECK (brutal < 3.0e-3);
}

TEZLA_TEST (insane_input_is_bounded_and_the_guard_says_so)
{
    // Fifty times full scale -- far beyond the field bound. The output must
    // stay finite and inside the magnetisation rail, and the sticky flag
    // must REPORT that guarding happened rather than absorbing it silently.
    auto stage = makeStage (1.0, 0.0, 0.0);

    bool allBounded = true;

    for (int i = 0; i < 4000; ++i)
    {
        const double out = stage.process (
            50.0 * std::sin (2.0 * 3.141592653589793 * 9000.0 * i / kRate));

        allBounded = allBounded
                  && std::isfinite (out)
                  && std::abs (out) <= 1.5 * stage.saturationCeiling() + 1e-12;
    }

    CHECK (allBounded);
    CHECK (stage.hasClamped());
}

TEZLA_TEST (tiny_inputs_pass_the_langevin_guard_smoothly)
{
    // Amplitudes that live entirely inside the small-argument branch of the
    // Langevin function: the output must be finite, tiny, and free of the
    // steps a discontinuous branch switch would make.
    auto stage = makeStage (0.5, 0.5, 0.5);

    const auto out = renderSine (stage, 1000.0, 1.0e-4, 8192);

    double biggest = 0.0, worstJump = 0.0;

    for (std::size_t i = 1; i < out.size(); ++i)
    {
        CHECK (std::isfinite (out[i]));
        biggest = std::max (biggest, std::abs (out[i]));
        worstJump = std::max (worstJump, std::abs (out[i] - out[i - 1]));
    }

    CHECK (biggest > 0.0);          // it does respond
    CHECK (biggest < 1.0e-2);       // proportionately
    CHECK (worstJump < biggest);    // and without steps between samples
}

// ---------------------------------------------------------------------------
// Silence and no-ops
// ---------------------------------------------------------------------------

TEZLA_TEST (after_the_signal_stops_remanence_is_dc_and_a_blocker_removes_it)
{
    // Magnetic memory is the point: when the field is taken away, M holds
    // its remanent value rather than falling to zero. That remanence is
    // pure DC -- Jiles-Atherton only moves M while H moves, so H frozen at
    // zero freezes M exactly -- and the DC blocker that always follows this
    // stage turns it into exact quiet. Both halves are asserted: the memory
    // is real, and it is removable.
    //
    // The classic remanence measurement: raise the field to a peak, then
    // let it down GENTLY. (A burst that stops at an arbitrary phase parks M
    // wherever that phase left it -- possibly near zero -- which is how
    // this test's first version accidentally asserted on luck.)
    auto stage = makeStage (0.7, 0.5, 0.5);
    tezla::dsp::DcBlocker<double> blocker;
    blocker.prepare (kRate, 10.0);

    constexpr int kRamp = 4000;

    for (int i = 0; i < kRamp; ++i)   // up the virgin curve to +2
        (void) stage.process (2.0 * 0.5 * (1.0 - std::cos (3.141592653589793 * i / kRamp)));

    for (int i = 0; i < kRamp; ++i)   // and gently back down to zero
        (void) stage.process (2.0 * 0.5 * (1.0 + std::cos (3.141592653589793 * i / kRamp)));

    std::vector<double> tail (static_cast<std::size_t> (kRate));   // one second

    for (auto& sample : tail)
        sample = blocker.process (stage.process (0.0));

    // The remanence exists...
    CHECK (std::abs (stage.magnetization()) > 0.01);

    // ...is frozen solid...
    const double late = stage.magnetization();
    (void) stage.process (0.0);
    CHECK (stage.magnetization() == late);

    // ...and the blocked output has decayed to numerical silence.
    double worstLate = 0.0;

    for (std::size_t i = tail.size() - 4800; i < tail.size(); ++i)
        worstLate = std::max (worstLate, std::abs (tail[i]));

    CHECK (worstLate < 1.0e-9);
}

TEZLA_TEST (no_op_parameter_sets_do_not_disturb_the_stream)
{
    // The house rule that has bitten four times, applied here from day one:
    // pushing the current values every sample must be bit-identical to
    // never touching the parameters at all.
    auto quiet = makeStage (0.6, 0.4, 0.5);
    auto pushed = makeStage (0.6, 0.4, 0.5);

    double worst = 0.0;

    for (int i = 0; i < 8192; ++i)
    {
        const double input =
            1.5 * std::sin (2.0 * 3.141592653589793 * 440.0 * i / kRate);

        const double a = quiet.process (input);
        pushed.setParameters (0.6, 0.4, 0.5);
        const double b = pushed.process (input);

        worst = std::max (worst, std::abs (a - b));
    }

    CHECK (worst == 0.0);
}
