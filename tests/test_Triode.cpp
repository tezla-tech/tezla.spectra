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

#include <tezla/dsp/Adaa.hpp>
#include <tezla/dsp/Triode.hpp>
#include <tezla/measure/Harmonics.hpp>
#include <tezla/measure/Triode12AX7.hpp>
#include <tezla/measure/Signals.hpp>

using namespace tezla::dsp;
namespace measure = tezla::measure;

namespace
{
constexpr double kRate = 48000.0;

/// Numerical derivative, centred.
double slopeAt (const Triode& triode, double v, double h = 1.0e-6)
{
    return (triode.evaluate (v + h) - triode.evaluate (v - h)) / (2.0 * h);
}
} // namespace

TEZLA_TEST (triode_is_a_straight_wire_at_the_operating_point)
{
    // Unity small-signal gain and no offset, so a stage at rest neither
    // colours nor shifts. Everything downstream assumes it.
    const Triode triode { 1.0, Triode::kChildExponent };

    CHECK_NEAR (triode.evaluate (0.0), 0.0, 1.0e-15);
    CHECK_NEAR (slopeAt (triode, 0.0), -1.0, 1.0e-6);
}

TEZLA_TEST (triode_cuts_off_exactly_and_stays_there)
{
    // Current cannot go negative, so the output stops dead rather than tailing
    // off. A model that leaks past cutoff has invented a mechanism the device
    // does not have.
    const Triode triode { 0.8, Triode::kChildExponent };
    const double ceiling = triode.getCeiling();

    CHECK_NEAR (triode.evaluate (-0.8), ceiling, 1.0e-12);

    // And it is flat below, at every depth, not merely asymptotic.
    for (const double v : { -0.80001, -1.0, -5.0, -1000.0 })
        CHECK (triode.evaluate (v) == ceiling);
}

TEZLA_TEST (triode_follows_the_three_halves_power_law)
{
    // The claim the whole file rests on. Current above cutoff goes as e^1.5,
    // so the output away from the operating point must too -- checked against
    // the law directly rather than against a shape that looks about right.
    constexpr double k = 1.0;
    constexpr double p = Triode::kChildExponent;
    const Triode triode { k, p };

    double worst = 0.0;

    for (double v = -0.99; v < 3.0; v += 0.01)
    {
        const double e = 1.0 + v / k;
        const double expected = -(std::pow (e, p) - 1.0) * k / p;
        worst = std::max (worst, std::abs (triode.evaluate (v) - expected));
    }

    CHECK (worst < 1.0e-12);
}

TEZLA_TEST (triode_is_asymmetric_and_expands_on_the_current_side)
{
    // The two directions are different, and the difference is the point.
    // Towards cutoff the curve compresses; towards more current it expands,
    // because transconductance rises with current.
    const Triode triode { 1.0, Triode::kChildExponent };

    // Compressive towards cutoff: half a knee of drive gives less than half a
    // knee of output.
    CHECK (triode.evaluate (-0.5) < 0.5);

    // Expansive the other way: the same drive gives more.
    CHECK (std::abs (triode.evaluate (0.5)) > 0.5);

    // And the slope grows with current rather than shrinking.
    CHECK (std::abs (slopeAt (triode, 1.0)) > std::abs (slopeAt (triode, 0.0)));
    CHECK (std::abs (slopeAt (triode, -0.5)) < std::abs (slopeAt (triode, 0.0)));
}

TEZLA_TEST (triode_generates_even_harmonics)
{
    // An asymmetric curve makes even harmonics; that is what "warmth" means
    // when it is a measurement rather than an adjective. A symmetric shaper
    // driven the same way makes none, so this separates the two.
    constexpr std::size_t fftSize = 32768;
    const double frequency = measure::binExactFrequency (1000.0, kRate, fftSize);

    const Triode triode { 1.0, Triode::kChildExponent };
    auto x = measure::sine (frequency, 0.8, kRate, fftSize);

    for (auto& sample : x)
        sample = triode.evaluate (sample);

    const auto report = measure::analyseHarmonics (x, kRate, frequency);

    // Measured at 0.8 of a knee: H2 -41.7, H3 -56.7, H4 -69.0. Even order on
    // top and each order about 13 dB down on the last, which is the signature
    // of a gentle asymmetric curvature rather than a clipper.
    CHECK (report.harmonicsDb.size() >= 4);
    CHECK (report.harmonicsDb[1] > -45.0);
    CHECK (report.harmonicsDb[1] > report.harmonicsDb[2] + 10.0);
    CHECK (report.harmonicsDb[2] > report.harmonicsDb[3] + 8.0);
}

TEZLA_TEST (triode_antiderivative_is_the_integral_of_the_curve)
{
    // ADAA is only antialiasing if the antiderivative really is one. Checked by
    // integrating the curve numerically and comparing, across the cutoff corner
    // as well as either side of it, because that join is where an algebra slip
    // would hide.
    for (const double p : { 1.0, 1.25, Triode::kChildExponent, 2.0, 3.0 })
    {
        const Triode triode { 0.7, p };

        constexpr double from = -2.0;
        constexpr double to = 2.0;
        constexpr int steps = 400000;
        constexpr double h = (to - from) / steps;

        // Trapezium, which is plenty at this step size.
        double integral = 0.5 * (triode.evaluate (from) + triode.evaluate (to));

        for (int i = 1; i < steps; ++i)
            integral += triode.evaluate (from + i * h);

        integral *= h;

        const double closedForm = triode.antiderivative (to) - triode.antiderivative (from);

        CHECK_NEAR (integral, closedForm, 1.0e-6);
    }
}

TEZLA_TEST (triode_antiderivative_is_zero_at_the_operating_point)
{
    // Not required by ADAA -- it only ever takes differences -- but it keeps the
    // numbers small around the region the signal actually lives in, which is
    // where the subtraction in ADAA loses precision.
    for (const double p : { 1.0, Triode::kChildExponent, 2.5 })
        CHECK_NEAR (Triode (1.3, p).antiderivative (0.0), 0.0, 1.0e-15);
}

TEZLA_TEST (triode_cutoff_corner_is_c1_for_a_physical_exponent)
{
    // The claim in the header, and the reason the cutoff shelf is quiet: the
    // slope arrives at the corner already at zero, so there is no derivative
    // discontinuity for harmonics to come from.
    //
    // Asserted as the law rather than as a bound, because a bound is a number
    // somebody chose. The Child branch differentiates to exactly -e^(p-1), so
    // approaching cutoff at p = 3/2 the slope must go as -sqrt(e) -- and it
    // does, to six figures, all the way down.
    const Triode triode { 1.0, Triode::kChildExponent };

    double worst = 0.0;

    for (const double e : { 0.1, 0.01, 0.001, 0.0001 })
    {
        const double slope = slopeAt (triode, e - 1.0, 1.0e-9);
        worst = std::max (worst, std::abs (slope + std::sqrt (e)));
    }

    CHECK (worst < 1.0e-6);

    // And past the corner it is flat, not merely small.
    CHECK (slopeAt (triode, -1.001, 1.0e-7) == 0.0);
}

TEZLA_TEST (triode_exponent_of_one_is_a_hard_corner_and_says_so)
{
    // The other half of the same claim. At p = 1 the slope reaches the corner
    // at -1 and meets a flat shelf, which is a clipper. Allowed, because it is
    // a usable sound -- but it is not C1 and the difference has to be visible
    // here rather than only audible later.
    const Triode clipper { 1.0, 1.0 };

    CHECK (std::abs (slopeAt (clipper, -0.999, 1.0e-7)) > 0.9);
    CHECK (std::abs (slopeAt (clipper, -1.001, 1.0e-7)) < 1.0e-9);
}

TEZLA_TEST (triode_adaa_beats_the_naive_curve_on_aliasing)
{
    // The whole reason for the closed-form antiderivative. Same curve, same
    // drive, same rate -- the only difference is whether the shaper is
    // band-limited, and it has to show up as a number.
    constexpr std::size_t fftSize = 1 << 14;
    const double frequency = measure::binExactFrequency (2200.0, kRate, fftSize);

    const Triode triode { 0.35, Triode::kChildExponent };
    const auto input = measure::sine (frequency, 0.9, kRate, 2 * fftSize);

    std::vector<double> naive (input.size());

    for (std::size_t i = 0; i < input.size(); ++i)
        naive[i] = triode.evaluate (input[i]);

    Adaa1<TriodeShaper> adaa;
    const TriodeShaper shaper { triode };
    std::vector<double> antialiased (input.size());

    for (std::size_t i = 0; i < input.size(); ++i)
        antialiased[i] = adaa.process (input[i], shaper);

    // The second half only: ADAA primes on its first sample, and the DFT treats
    // its block as circular, so a wrap discontinuity would read as broadband
    // aliasing that is not there.
    const std::vector<double> naiveTail (naive.begin() + static_cast<long> (fftSize), naive.end());
    const std::vector<double> adaaTail (antialiased.begin() + static_cast<long> (fftSize), antialiased.end());

    const auto naiveReport = measure::analyseHarmonics (naiveTail, kRate, frequency);
    const auto adaaReport = measure::analyseHarmonics (adaaTail, kRate, frequency);

    CHECK (adaaReport.audibleAliasingDb < naiveReport.audibleAliasingDb - 6.0);
}

// ---------------------------------------------------------------------------
// Against a real 12AX7
// ---------------------------------------------------------------------------

TEZLA_TEST (triode_matches_a_measured_12ax7_on_the_half_it_models)
{
    // The claim the whole design rests on: a curve derived from Child's law,
    // with a closed-form antiderivative, is a good enough model of the cutoff
    // half of a real valve that nothing is lost by keeping it integrable.
    //
    // The reference is Dempwolf's fitted 12AX7 solved on an actual load line --
    // 100k from 250 V, grid biased 1.5 V down -- normalised the same way ours
    // is. Not a shape that looks about right: a published measurement.
    const measure::Triode12AX7Stage reference;

    // Fitted offline over vg in [-3, 0]; pinned here so a change to either
    // curve has to be argued for rather than absorbed.
    const Triode ours { 1.760, 1.585 };

    double sumSquared = 0.0;
    double worst = 0.0;
    double peak = 0.0;
    int count = 0;

    for (double v = -3.0; v <= 0.0; v += 0.005)
    {
        const double target = reference.normalised (v);
        const double difference = ours.evaluate (v) - target;

        sumSquared += difference * difference;
        worst = std::max (worst, std::abs (difference));
        peak = std::max (peak, std::abs (target));
        ++count;
    }

    const double rms = std::sqrt (sumSquared / count);

    // Measured: rms 0.0118, worst 0.0267 against a peak swing of 1.12, so
    // 2.39% of full swing at the worst point.
    CHECK (rms < 0.015);
    CHECK (worst / peak < 0.03);
}

TEZLA_TEST (triode_fitted_exponent_lands_on_the_space_charge_law)
{
    // Worth its own test because it is the part that could have gone either
    // way. The exponent was not assumed: it was fitted to the reference over
    // the cutoff half, and it came back 1.585 -- within 6% of Child's 3/2, and
    // inside the range the published 12AX7 fits already span (Dempwolf 1.303,
    // Koren 1.4, Cardarilli 1.5, quadric surface 2.0).
    //
    // Refitted here rather than asserted, on a coarser grid so it stays quick.
    const measure::Triode12AX7Stage reference;

    std::vector<double> vs, ys;

    for (double v = -3.0; v <= 0.0; v += 0.02)
    {
        vs.push_back (v);
        ys.push_back (reference.normalised (v));
    }

    double bestExponent = 0.0;
    double bestError = 1.0e30;

    for (double knee = 1.0; knee <= 2.6; knee += 0.02)
        for (double exponent = 1.0; exponent <= 3.0; exponent += 0.01)
        {
            const Triode candidate { knee, exponent };
            double sum = 0.0;

            for (std::size_t i = 0; i < vs.size(); ++i)
            {
                const double d = candidate.evaluate (vs[i]) - ys[i];
                sum += d * d;
            }

            if (sum < bestError)
            {
                bestError = sum;
                bestExponent = exponent;
            }
        }

    CHECK_NEAR (bestExponent, 1.585, 0.05);
}

TEZLA_TEST (triode_diverges_on_the_grid_side_by_the_size_of_the_missing_mechanism)
{
    // The other half of the same finding, and the reason this curve is only
    // half a stage.
    //
    // Driven the other way a real stage *compresses*, because the grid starts
    // conducting and the plate bottoms out. Ours expands, because a power law
    // above 1 has nothing to stop it. Fitting both sides at once forces the
    // exponent down to 1.075 and still leaves 6.2% of error -- which is how a
    // curve that quietly absorbs two dynamic mechanisms ends up not being a
    // good model of either.
    //
    // So the divergence is asserted rather than tolerated: it is the budget the
    // stage's grid conduction and plate bottoming have to account for, and if
    // it ever shrinks on its own, something has been folded into the curve that
    // does not belong there.
    const measure::Triode12AX7Stage reference;
    const Triode ours { 1.760, 1.585 };

    const double atThreeVolts = std::abs (ours.evaluate (3.0) - reference.normalised (3.0));

    // Measured 1.27 normalised units, against a reference swing of 2.99 there.
    CHECK (atThreeVolts > 1.0);

    // And the sign of it: the real stage is the one that compresses.
    CHECK (std::abs (reference.normalised (3.0)) < std::abs (ours.evaluate (3.0)));
}
