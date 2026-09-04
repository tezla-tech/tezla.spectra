// Copyright (c) 2026 The Tezla <thetezla@proton.me>
// Created by The Tezla -- https://github.com/wingit33/tezla.tech
// Music: https://soundcloud.com/thetezla | https://thetezla.bandcamp.com
// Built with development assistance from Claude (Anthropic).
// SPDX-License-Identifier: AGPL-3.0-only
// GNU AGPLv3 (see LICENSE), plus NOTICE.md's attribution term. Keep intact.

#include "TestFramework.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <numbers>
#include <vector>

#include <tezla/dsp/FmBandwidth.hpp>
#include <tezla/dsp/HalfbandFir.hpp>
#include <tezla/dsp/Fft.hpp>
#include <tezla/dsp/ModeShapes.hpp>

using namespace tezla::dsp;

namespace
{
constexpr double kTwoPi = 2.0 * std::numbers::pi;

/// The 256-point fixed rule `ModeShapes.hpp` uses, reproduced here so a test
/// can show where it stops working without touching Malleus's copy.
[[nodiscard]] double fixedRuleBesselJ (int n, double x)
{
    return besselJ (n, x);
}

/// Solve Kepler's equation `E = theta + beta*sin(E)` by fixed-point iteration
/// and return `sin(E)` -- which is exactly what a feedback phase-modulation
/// operator settles to at steady state, evaluated without any Bessel function
/// anywhere near it.
[[nodiscard]] std::vector<double> keplerSine (double beta, int points)
{
    std::vector<double> out (static_cast<std::size_t> (points), 0.0);

    for (int i = 0; i < points; ++i)
    {
        const double theta = kTwoPi * static_cast<double> (i) / static_cast<double> (points);

        // Newton, not fixed-point iteration. The fixed point contracts by
        // `beta * cos(E)`, which approaches 1 as beta does, so the plain
        // iteration stalls at ~4e-9 by beta = 0.99 and the test would be
        // measuring the solver rather than the identity.
        double e = theta;
        for (int iter = 0; iter < 200; ++iter)
        {
            const double f = e - theta - beta * std::sin (e);
            const double slope = 1.0 - beta * std::cos (e);
            if (std::abs (slope) < 1.0e-14)
                break;

            const double step = f / slope;
            e -= step;
            if (std::abs (step) < 1.0e-16)
                break;
        }

        out[static_cast<std::size_t> (i)] = std::sin (e);
    }

    return out;
}

/// The amplitude of harmonic `n` of a real periodic signal sampled over exactly
/// one cycle, by direct correlation -- no FFT, no windowing, no leakage.
[[nodiscard]] double harmonicAmplitude (const std::vector<double>& cycle, int n)
{
    const auto size = static_cast<double> (cycle.size());

    double sin_ = 0.0;
    double cos_ = 0.0;

    for (std::size_t i = 0; i < cycle.size(); ++i)
    {
        const double theta = kTwoPi * static_cast<double> (n) * static_cast<double> (i) / size;
        sin_ += cycle[i] * std::sin (theta);
        cos_ += cycle[i] * std::cos (theta);
    }

    return 2.0 * std::sqrt (sin_ * sin_ + cos_ * cos_) / size;
}
} // namespace

// ---------------------------------------------------------------------------
// The Bessel evaluators
// ---------------------------------------------------------------------------

TEZLA_TEST (adaptive_besselJ_agrees_with_the_verified_fixed_rule_in_its_own_range)
{
    // ModeShapes.hpp states its evaluator is verified against std::cyl_bessel_j
    // for m = 0..20, x = 0..60 to 8.861e-15. Inside that range the adaptive rule
    // must agree with it, or one of them is wrong.
    double worst = 0.0;

    for (int m = 0; m <= 20; ++m)
    {
        for (double x = 0.0; x <= 60.0; x += 0.05)
        {
            const double difference = std::abs (besselJn (m, x) - fixedRuleBesselJ (m, x));
            worst = std::max (worst, difference);
        }
    }

    std::printf ("        [besselJn] worst difference from the 256-point rule over m=0..20, x=0..60: %.3e\n",
                 worst);
    CHECK (worst < 1.0e-12);
}

TEZLA_TEST (the_fixed_256_point_rule_aliases_where_stryda_needs_an_answer)
{
    // This is the reason FmBandwidth.hpp carries its own evaluator, and it is
    // asserted rather than asserted-in-a-comment. The identity is exact:
    //
    //     J_0(x)^2 + 2 * sum over n >= 1 of J_n(x)^2 = 1
    //
    // at every x. An index of 16 cycles is 100.5 radians, which is squarely
    // inside Stryda's range and well outside the fixed rule's.
    const double x = 100.53;

    const auto sumRule = [x] (auto&& evaluate)
    {
        double total = evaluate (0, x) * evaluate (0, x);
        for (int n = 1; n <= 220; ++n)
        {
            const double j = evaluate (n, x);
            total += 2.0 * j * j;
        }
        return total;
    };

    const double adaptive = sumRule ([] (int n, double v) { return besselJn (n, v); });
    const double fixedRule = sumRule ([] (int n, double v) { return fixedRuleBesselJ (n, v); });

    std::printf ("        [besselJn] sum rule at x=%.2f: adaptive %.12f, fixed 256-point %.6f\n",
                 x, adaptive, fixedRule);

    CHECK_NEAR (adaptive, 1.0, 1.0e-10);

    // And the fixed rule is not merely less accurate, it is wrong by a margin
    // no tolerance would forgive -- which is what makes reusing it a defect
    // rather than a compromise.
    CHECK (std::abs (fixedRule - 1.0) > 0.1);
}

TEZLA_TEST (adaptive_besselJ_satisfies_the_recurrence_at_high_order)
{
    // J_{n-1}(x) + J_{n+1}(x) = (2n/x) J_n(x), checked where the answers are
    // large enough to mean something.
    double worst = 0.0;

    for (double x : { 20.0, 60.0, 100.53, 160.0 })
    {
        for (int n = 1; n <= static_cast<int> (x) + 20; ++n)
        {
            const double lhs = besselJn (n - 1, x) + besselJn (n + 1, x);
            const double rhs = (2.0 * static_cast<double> (n) / x) * besselJn (n, x);
            worst = std::max (worst, std::abs (lhs - rhs));
        }
    }

    std::printf ("        [besselJn] worst recurrence residual to order x+20 at x up to 160: %.3e\n",
                 worst);
    CHECK (worst < 1.0e-11);
}

TEZLA_TEST (besselI_order_zero_matches_the_halfband_designer_s_own_evaluator)
{
    double worst = 0.0;

    for (double x = 0.0; x <= 30.0; x += 0.01)
    {
        const double mine = besselI (0, x);
        const double theirs = detail::besselI0 (x);
        worst = std::max (worst, std::abs (mine - theirs) / std::max (1.0, theirs));
    }

    std::printf ("        [besselI] worst relative difference from detail::besselI0, x=0..30: %.3e\n",
                 worst);
    CHECK (worst < 1.0e-14);
}

TEZLA_TEST (besselI_satisfies_the_recurrence)
{
    // I_{n-1}(x) - I_{n+1}(x) = (2n/x) I_n(x).
    double worst = 0.0;

    for (double x : { 0.5, 2.0, 8.0, 30.0, 64.0 })
    {
        for (int n = 1; n <= 40; ++n)
        {
            const double lhs = besselI (n - 1, x) - besselI (n + 1, x);
            const double rhs = (2.0 * static_cast<double> (n) / x) * besselI (n, x);
            const double scale = std::max (1.0e-30, std::abs (lhs));
            worst = std::max (worst, std::abs (lhs - rhs) / scale);
        }
    }

    std::printf ("        [besselI] worst relative recurrence residual, n=1..40: %.3e\n", worst);
    CHECK (worst < 1.0e-12);
}

TEZLA_TEST (besselI_reproduces_the_modfm_paper_s_own_worked_number)
{
    // Timoney & Lazzarini (DAFx-11) give one arithmetic example in the text:
    // a modulating DC term of 5 scales the carrier "by a factor of 7.17".
    // That figure is I_0(5 * ln2) -- the paper's own Eq (11) scaling -- and it
    // is the only published value in any of these papers that this file can be
    // checked against without re-deriving it.
    //
    // (The text attributes the 5 to V0 while the formula puts it on Vm; the
    // arithmetic only works for I_0(5 ln2), so that is what is pinned here.
    // The distinction matters for `exponentialCarrierHz`, which follows the
    // formula rather than the sentence.)
    const double scaling = besselI (0, 5.0 * std::numbers::ln2);

    std::printf ("        [besselI] I_0(5 ln2) = %.5f, the paper's 7.17\n", scaling);
    CHECK_NEAR (scaling, 7.17, 0.005);
}

// ---------------------------------------------------------------------------
// The classic criterion
// ---------------------------------------------------------------------------

TEZLA_TEST (significant_order_is_the_last_one_above_the_threshold_not_the_first_below)
{
    // J_n(I) is not monotonic in n, so a search that stops at the first order
    // below the threshold under-reports. Assert the returned order really does
    // carry the threshold, and that nothing above it does.
    //
    // The threshold is relative to the **loudest** sideband, matching what a
    // spectrum analyser shows and what `tezla-measure stryda` table 2 measures.
    // Comparing against unity instead read the predictor 4 % short at index 16
    // -- 6152 Hz at 880 Hz and ratio 7 -- because FM spreads the energy and the
    // peak partial is only about 0.21 there, not 1.
    for (double index : { 1.0, 4.0, 12.0, 50.0, 100.53 })
    {
        const int order = fm::significantOrder (index, -80.0);

        double peak = 0.0;
        for (int n = 0; n <= order + 60; ++n)
            peak = std::max (peak, std::abs (besselJn (n, index)));

        const double threshold = peak * std::pow (10.0, -80.0 / 20.0);

        CHECK (std::abs (besselJn (order, index)) >= threshold);

        bool anythingAbove = false;
        for (int n = order + 1; n <= order + 40; ++n)
            if (std::abs (besselJn (n, index)) >= threshold)
                anythingAbove = true;

        CHECK (! anythingAbove);

        std::printf ("        [order] index %7.2f rad -> order %4d, peak partial %.4f\n",
                     index, order, peak);
    }
}

TEZLA_TEST (the_asymptotic_edge_tracks_the_exact_one_and_the_gap_is_reported)
{
    std::printf ("        [order] index (rad)   exact   asymptotic   gap\n");

    double worstGap = 0.0;

    for (double index : { 1.0, 3.14, 6.28, 12.57, 25.13, 50.27, 100.53 })
    {
        const int exact = fm::significantOrder (index, -80.0);
        const double asymptotic = fm::asymptoticOrder (index);
        const double gap = static_cast<double> (exact) - asymptotic;
        worstGap = std::max (worstGap, std::abs (gap));

        std::printf ("        [order] %10.2f   %5d   %10.2f   %+6.2f\n",
                     index, exact, asymptotic, gap);
    }

    // The asymptotic is a shape statement, not a bound: at -80 dB the exact
    // edge sits a few orders above it, and the point of printing the gap is
    // that the predictor uses the exact one.
    CHECK (worstGap < 12.0);

    // Measured spread with the peak-relative threshold: +1.00 at 1 rad rising to
    // +9.17 at 100.53 rad. The asymptotic is a shape statement, not a bound.
}

// ---------------------------------------------------------------------------
// Feedback: the Kapteyn series, checked against Kepler's equation
// ---------------------------------------------------------------------------

TEZLA_TEST (feedback_harmonics_are_the_kapteyn_series_of_keplers_equation)
{
    // A feedback phase-modulation operator settles to y = sin(theta + beta*y),
    // which is Kepler's equation, so its n-th harmonic is exactly
    // 2*J_n(n*beta)/(n*beta). This solves the equation numerically -- fixed
    // point, no Bessel functions -- and compares.
    std::printf ("        [feedback] beta   harmonic   solved      Kapteyn     diff\n");

    double worst = 0.0;

    // Beta stops at 0.95 deliberately: at beta = 1 exactly, Kepler's equation
    // has a cusp (dE/dtheta is unbounded at theta = 0) and beyond it the
    // continuous-time equation is no longer single-valued at all, so there is
    // nothing for the series to be equal to. That boundary is the subject of
    // its own test below, and of `fm::feedbackOrder`'s documentation.
    for (double beta : { 0.25, 0.5, 0.9, 0.95 })
    {
        const auto cycle = keplerSine (beta, 8192);

        for (int n : { 1, 2, 3, 5, 9 })
        {
            const double solved = harmonicAmplitude (cycle, n);
            const double nb = static_cast<double> (n) * beta;
            const double kapteyn = std::abs (2.0 * besselJn (n, nb) / nb);

            worst = std::max (worst, std::abs (solved - kapteyn));

            if (n <= 3)
                std::printf ("        [feedback] %.2f   %8d   %.8f  %.8f  %.2e\n",
                             beta, n, solved, kapteyn, std::abs (solved - kapteyn));
        }
    }

    std::printf ("        [feedback] worst difference over beta 0.25..0.95, harmonics 1..9: %.3e\n",
                 worst);
    CHECK (worst < 1.0e-12);
}

TEZLA_TEST (feedback_order_grows_as_the_loop_tightens_and_is_one_at_zero)
{
    CHECK (fm::feedbackOrder (0.0, -80.0) == 1);

    int previous = 0;
    for (double beta : { 0.1, 0.25, 0.5, 0.75, 0.9, 0.99 })
    {
        const int order = fm::feedbackOrder (beta, -80.0);
        std::printf ("        [feedback] beta %.2f -> %d significant harmonics at -80 dB\n",
                     beta, order);
        CHECK (order >= previous);
        previous = order;
    }
}

// ---------------------------------------------------------------------------
// ModFM: the paper's Eq (12)
// ---------------------------------------------------------------------------

TEZLA_TEST (modfm_max_index_lands_on_the_papers_condition)
{
    // The returned k must put the first out-of-band partial exactly at the
    // threshold: raise it and the condition breaks, lower it and there is room
    // left over.
    constexpr double sampleRate = 48000.0;
    constexpr double thresholdDb = -60.0;
    const double threshold = std::pow (10.0, thresholdDb / 20.0);

    for (double f0 : { 55.0, 220.0, 880.0, 2000.0 })
    {
        const double k = fm::modFmMaxIndex (f0, f0, sampleRate, thresholdDb);
        const int n = static_cast<int> ((0.5 * sampleRate - f0) / f0);

        const double atK = besselI (n, k) / besselI (0, k);
        const double clamped = k >= 255.999;

        std::printf ("        [modfm] f0 %7.1f Hz, n = %4d, max k = %8.3f, ratio %.3e (limit %.3e)%s\n",
                     f0, n, k, atK, threshold, clamped ? "  [no limit in range]" : "");

        CHECK (atK <= threshold * 1.0001);

        // Below the search ceiling the answer must be tight: nudge it up and
        // the condition has to break. At or on the ceiling there is simply no
        // limit to find, which is the correct answer for a low note.
        if (! clamped)
        {
            const double above = besselI (n, k * 1.05) / besselI (0, k * 1.05);
            CHECK (above > threshold);
        }
    }
}

TEZLA_TEST (modfm_max_index_falls_as_the_note_climbs_and_rises_with_the_sample_rate)
{
    const double low  = fm::modFmMaxIndex (110.0, 110.0, 48000.0);
    const double high = fm::modFmMaxIndex (880.0, 880.0, 48000.0);
    const double fast = fm::modFmMaxIndex (880.0, 880.0, 192000.0);

    std::printf ("        [modfm] 110 Hz @48k: %.2f   880 Hz @48k: %.2f   880 Hz @192k: %.2f\n",
                 low, high, fast);

    CHECK (high < low);
    CHECK (fast > high);
}

// ---------------------------------------------------------------------------
// Exponential FM: the paper's Eq (16)
// ---------------------------------------------------------------------------

TEZLA_TEST (exponential_bandwidth_beats_the_linear_one_at_equal_peak_deviation)
{
    // The paper's claim is that exponential FM has "a larger bandwidth with
    // respect to the modulation depth". Comparing the two needs care, and the
    // careless comparison is the one worth writing down: an exponential FM
    // depth `Vm` is in **volts, one per octave**, not in cycles or radians of
    // phase. So the like-for-like comparison is at equal *peak frequency
    // deviation*, which for exponential FM is `f_c (2^Vm - 1)` and for linear
    // FM is `I * f_m`, giving `I = f_c (2^Vm - 1) / f_m` radians.
    //
    // Compare the two by putting Vm straight into a linear index instead and
    // the answer inverts, because a "depth" of 4 is one thing in octaves and
    // quite another in radians. That mistake would have made the exponential
    // bound look unnecessary.
    constexpr double fc = 110.0;
    constexpr double fm_ = 110.0;

    std::printf ("        [expfm] Vm   peak dev    exponential BW    linear top (same dev)\n");

    for (double vm : { 1.0, 2.0, 3.0, 4.0 })
    {
        const double deviation = fc * (std::pow (2.0, vm) - 1.0);
        const double indexRadians = deviation / fm_;

        const double exponential = fm::exponentialBandwidthHz (fc, fm_, 0.0, vm);
        const double linear = fc + static_cast<double> (fm::significantOrder (indexRadians, -80.0)) * fm_;

        std::printf ("        [expfm] %.1f  %8.1f Hz  %12.1f Hz  %14.1f Hz\n",
                     vm, deviation, exponential, linear);

        CHECK (exponential > linear);
    }
}

TEZLA_TEST (exponential_bandwidth_is_monotonic_in_depth_and_in_both_frequencies)
{
    CHECK (fm::exponentialBandwidthHz (110.0, 110.0, 0.0, 3.0)
             > fm::exponentialBandwidthHz (110.0, 110.0, 0.0, 2.0));
    CHECK (fm::exponentialBandwidthHz (220.0, 110.0, 0.0, 2.0)
             > fm::exponentialBandwidthHz (110.0, 110.0, 0.0, 2.0));
    CHECK (fm::exponentialBandwidthHz (110.0, 220.0, 0.0, 2.0)
             > fm::exponentialBandwidthHz (110.0, 110.0, 0.0, 2.0));
    CHECK (fm::exponentialBandwidthHz (110.0, 110.0, 1.0, 2.0)
             > fm::exponentialBandwidthHz (110.0, 110.0, 0.0, 2.0));
}

// ---------------------------------------------------------------------------
// The matrix
// ---------------------------------------------------------------------------

TEZLA_TEST (an_unmodulated_matrix_predicts_its_highest_operator)
{
    FmBandwidth bandwidth;
    bandwidth.setOperatorCount (6);

    for (int op = 0; op < 6; ++op)
        bandwidth.setOperatorFrequency (op, 110.0 * static_cast<double> (op + 1));

    CHECK_NEAR (bandwidth.topSidebandHz(), 660.0, 1.0e-9);
}

TEZLA_TEST (a_deeper_stack_predicts_a_wider_spectrum)
{
    const auto predict = [] (int depth)
    {
        FmBandwidth bandwidth;
        bandwidth.setOperatorCount (6);
        bandwidth.reset();

        for (int op = 0; op < 6; ++op)
            bandwidth.setOperatorFrequency (op, 110.0);

        // 5 -> 4 -> 3 -> ... -> 0, `depth` links deep. Operator 0 is the
        // carrier and the chain runs downhill, which is the below-diagonal
        // (instantaneous) direction.
        for (int link = 0; link < depth; ++link)
            bandwidth.setIndex (link + 1, link, 1.0);

        return bandwidth.topSidebandHz();
    };

    double previous = 0.0;
    for (int depth = 0; depth <= 4; ++depth)
    {
        const double top = predict (depth);
        std::printf ("        [matrix] stack depth %d -> predicted top %.1f Hz\n", depth, top);
        CHECK (top > previous);
        previous = top;
    }
}

TEZLA_TEST (the_index_cap_is_exactly_one_when_it_is_not_binding)
{
    // Bit-exact, not approximately: a cap that is not binding must leave every
    // index untouched, and `scale * index` has to be `index` to the last bit.
    FmBandwidth bandwidth;
    bandwidth.setOperatorCount (2);
    bandwidth.setOperatorFrequency (0, 55.0);
    bandwidth.setOperatorFrequency (1, 55.0);
    bandwidth.setIndex (1, 0, 2.0);

    const double scale = bandwidth.indexScaleFor (96000.0);

    std::printf ("        [cap] predicted top %.1f Hz under a 96 kHz ceiling -> scale %.17g\n",
                 bandwidth.topSidebandHz(), scale);

    CHECK (scale == 1.0);
    CHECK ((2.0 * scale) == 2.0);
}

TEZLA_TEST (the_index_cap_brings_a_screaming_patch_under_the_ceiling)
{
    FmBandwidth bandwidth;
    bandwidth.setOperatorCount (3);
    bandwidth.setOperatorFrequency (0, 440.0);
    bandwidth.setOperatorFrequency (1, 440.0 * 7.0);
    bandwidth.setOperatorFrequency (2, 440.0 * 3.0);
    bandwidth.setIndex (1, 0, 8.0);
    bandwidth.setIndex (2, 1, 4.0);

    const double uncapped = bandwidth.topSidebandHz();
    const double ceiling = 0.9 * 96000.0;
    const double scale = bandwidth.indexScaleFor (ceiling);
    const double capped = bandwidth.topSidebandHz (fm::kThresholdDb, scale);

    std::printf ("        [cap] uncapped %.0f Hz, ceiling %.0f Hz, scale %.4f, capped %.0f Hz\n",
                 uncapped, ceiling, scale, capped);

    CHECK (uncapped > ceiling);
    CHECK (scale < 1.0);
    CHECK (capped <= ceiling);
}

TEZLA_TEST (feedback_widens_the_prediction_and_the_cap_answers_it)
{
    FmBandwidth bandwidth;
    bandwidth.setOperatorCount (1);
    bandwidth.setOperatorFrequency (0, 1000.0);

    const double quiet = bandwidth.topSidebandHz();

    bandwidth.setFeedback (0, 0.16);   // ~1 radian
    const double loud = bandwidth.topSidebandHz();

    std::printf ("        [matrix] 1 kHz operator: no feedback %.0f Hz, beta ~1 rad %.0f Hz\n",
                 quiet, loud);

    CHECK (loud > quiet);
    CHECK (bandwidth.indexScaleFor (0.9 * 96000.0) <= 1.0);
}

TEZLA_TEST (the_feedback_model_saturates_at_one_radian_and_says_so)
{
    // Kepler's equation is single-valued only below beta = 1. Oscillator's
    // kMaxFeedback is 1.0 *cycles* -- 6.28 radians -- so the operator can be
    // driven six times past the point where the closed form means anything.
    // The predictor must saturate there rather than extrapolate, so that the
    // index cap clamps hard exactly where the model stops promising.
    const int justBelow = fm::feedbackOrder (0.999, -80.0);
    const int atBoundary = fm::feedbackOrder (1.0, -80.0);
    const int wayPast = fm::feedbackOrder (2.0 * std::numbers::pi, -80.0);

    std::printf ("        [feedback] order at beta 0.999 / 1.0 / 6.28 rad: %d / %d / %d\n",
                 justBelow, atBoundary, wayPast);

    CHECK (atBoundary == wayPast);
    CHECK (justBelow <= atBoundary);
}

TEZLA_TEST (the_discrete_feedback_operator_stays_bounded_past_the_boundary)
{
    // The closed form gives up at 1 radian; the operator must not. This is the
    // section 7 requirement -- a feedback loop around a nonlinearity needs a
    // bound that cannot be defeated -- checked across the whole range
    // Oscillator will accept, in the same delayed form it uses.
    double worst = 0.0;

    for (double cycles = 0.0; cycles <= 1.0; cycles += 0.005)
    {
        const double beta = cycles * kTwoPi;
        double y1 = 0.0;
        double y2 = 0.0;
        double phase = 0.0;

        for (int i = 0; i < 20000; ++i)
        {
            const double y = std::sin (kTwoPi * (phase + beta * 0.5 * (y1 + y2)));
            y2 = y1;
            y1 = y;
            phase += 0.011;
            if (phase >= 1.0)
                phase -= 1.0;

            CHECK (std::isfinite (y));
            worst = std::max (worst, std::abs (y));
        }
    }

    std::printf ("        [feedback] peak output over the whole feedback range: %.6f\n", worst);
    CHECK (worst <= 1.0);
}

// ---------------------------------------------------------------------------
// The predictor against a real spectrum
// ---------------------------------------------------------------------------

TEZLA_TEST (the_predictor_never_under_estimates_a_rendered_spectrum)
{
    // `tezla-measure stryda` table 2 reports this across 27 combinations; this
    // is the same check with teeth, in the suite, so a change to the predictor
    // cannot quietly go back to under-estimating. Rendered far above the band
    // of interest so nothing folds and the measured edge is the real one.
    constexpr double kRate = 393216.0;               // 6 x 65536, so bins are exact
    constexpr std::size_t kFrames = 1u << 16;
    constexpr double kFloorDb = -80.0;

    const double binWidth = kRate / static_cast<double> (kFrames);

    std::printf ("        [predict] carrier  ratio  index   predicted   measured   error\n");

    double worst = 0.0;

    for (double carrierHz : { 220.0, 880.0 })
    {
        for (double ratio : { 1.0, 3.0, 7.0 })
        {
            for (double indexRadians : { 1.0, 4.0, 16.0 })
            {
                // Bin-exact carrier, and an exact-integer ratio keeps the
                // modulator bin-exact too.
                const double carrier = std::round (carrierHz / binWidth) * binWidth;
                const double modulator = carrier * ratio;

                std::vector<double> rendered (kFrames, 0.0);
                double carrierPhase = 0.0;
                double modulatorPhase = 0.0;
                const double carrierInc = carrier / kRate;
                const double modulatorInc = modulator / kRate;

                for (std::size_t i = 0; i < kFrames; ++i)
                {
                    rendered[i] = std::sin (kTwoPi * carrierPhase
                                            + indexRadians * std::sin (kTwoPi * modulatorPhase));

                    carrierPhase += carrierInc;
                    if (carrierPhase >= 1.0)
                        carrierPhase -= 1.0;

                    modulatorPhase += modulatorInc;
                    if (modulatorPhase >= 1.0)
                        modulatorPhase -= 1.0;
                }

                const auto spectrum = fftOfReal (rendered);
                const std::size_t half = kFrames / 2;

                double peak = 0.0;
                for (std::size_t k = 1; k < half; ++k)
                    peak = std::max (peak, std::norm (spectrum[k]));

                const double threshold = peak * std::pow (10.0, kFloorDb / 10.0);

                std::size_t highest = 0;
                for (std::size_t k = 1; k < half; ++k)
                    if (std::norm (spectrum[k]) >= threshold)
                        highest = k;

                const double measured = static_cast<double> (highest) * binWidth;

                FmBandwidth bandwidth;
                bandwidth.setOperatorCount (2);
                bandwidth.setOperatorFrequency (0, carrier);
                bandwidth.setOperatorFrequency (1, modulator);
                bandwidth.setIndex (1, 0, indexRadians / kTwoPi);

                const double predicted = bandwidth.topSidebandHz (kFloorDb);
                const double error = predicted - measured;
                worst = std::min (worst, error);

                std::printf ("        [predict] %7.0f  %5.1f  %5.1f  %9.0f  %9.0f  %+6.0f\n",
                             carrier, ratio, indexRadians, predicted, measured, error);

                // One bin of slack for the rounding of the edge itself; never
                // more, and never in the unsafe direction beyond that.
                CHECK (error >= -binWidth);
            }
        }
    }

    std::printf ("        [predict] worst error across the sweep: %+.0f Hz (bin width %.1f Hz)\n",
                 worst, binWidth);
}
