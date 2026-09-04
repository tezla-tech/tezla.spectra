// Copyright (c) 2026 The Tezla <thetezla@proton.me>
// Created by The Tezla -- https://github.com/wingit33/tezla.tech
// Music: https://soundcloud.com/thetezla | https://thetezla.bandcamp.com
// Built with development assistance from Claude (Anthropic).
// SPDX-License-Identifier: AGPL-3.0-only
// GNU AGPLv3 (see LICENSE), plus NOTICE.md's attribution term. Keep intact.

#pragma once

// Where the top of an FM spectrum actually is, in closed form, before a sample
// is rendered -- and the index scale that keeps it under Nyquist.
//
// ---------------------------------------------------------------------------
// Why this exists
// ---------------------------------------------------------------------------
//
// FM at high index is the worst aliasing case in this repository. The clipper
// baseline pinned in `tests/test_Measurement.cpp` says four times the sample
// rate buys about 18 dB and no more, because a hard clipper has infinite
// bandwidth; an FM operator does not have infinite bandwidth, and that is the
// whole opportunity. Its spectrum has a **computable edge**, so the honest
// answer is not "oversample harder" but "know where the edge is, and put it
// where you want it".
//
// Three published criteria are implemented here, one per synthesis flavour.
// Each is attributed at its own function. None of them is trusted on the
// strength of being published: `tezla-measure stryda` renders the real
// spectrum and compares, and `tests/test_FmBandwidth.cpp` pins the agreement.
//
// ---------------------------------------------------------------------------
// Units: cycles here, radians there, and the factor of 2*pi between them
// ---------------------------------------------------------------------------
//
// `Oscillator::advance (phaseOffset)` takes its phase modulation **in cycles**,
// and so does `setFeedback`. Every published FM result is stated with the
// modulation index in **radians** of peak phase deviation. So
//
//     I (radians) = 2 * pi * cycles
//
// and every entry point here names its unit in the parameter. Getting this
// wrong is a factor of 6.28 in a bandwidth estimate, which reads as "the
// predictor is conservative" rather than as a bug, and would never be noticed.

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>

#include "Exact.hpp"

namespace tezla::dsp
{

// ---------------------------------------------------------------------------
// Bessel functions
// ---------------------------------------------------------------------------

/// Bessel function of the first kind, order n, by Bessel's integral
///
///     J_n(x) = (1 / 2pi) * integral over [0, 2pi] of cos(n*t - x*sin t) dt
///
/// with the step count chosen from the argument.
///
/// **This is not a duplicate of `ModeShapes.hpp`'s `besselJ`, and it may not be
/// replaced by it.** That one is a fixed 256-point rule, which is correct and
/// verified over the range Malleus asks for -- `m = 0..20`, `x = 0..60`. The
/// integrand's phase, `n*t - x*sin t`, sweeps at up to `n + x` cycles per turn,
/// so a fixed rule silently *aliases* once `n + x` approaches half the step
/// count. Stryda asks for far more: an index of 16 cycles is 100.5 radians, and
/// the sidebands that matter run past order 110, so `n + x` reaches 210 where
/// 256 points leave a Nyquist of 128. The result would not look wrong -- it
/// would look like a plausible smaller number.
///
/// So the rule adapts: `steps = 8 * (n + |x|)`, rounded up to a multiple of 4
/// and never below 256. Eight points per cycle of the fastest phase term is
/// comfortably inside the geometric-convergence regime for a periodic analytic
/// integrand, and `tests/test_FmBandwidth.cpp` checks it against the 256-point
/// evaluator across the whole range that one is verified for.
///
/// Design time only: nothing here runs from an audio callback.
[[nodiscard]] inline double besselJn (int n, double x) noexcept
{
    const int order = n < 0 ? -n : n;

    // J_{-n}(x) = (-1)^n J_n(x).
    const double sign = (n < 0 && (order % 2) != 0) ? -1.0 : 1.0;

    const double magnitude = x < 0.0 ? -x : x;
    const int wanted = 8 * (order + static_cast<int> (magnitude) + 1);
    const int steps  = std::max (256, ((wanted + 3) / 4) * 4);

    const double twoPi = 2.0 * std::numbers::pi;
    double sum = 0.0;

    for (int i = 0; i < steps; ++i)
    {
        const double theta = twoPi * static_cast<double> (i) / static_cast<double> (steps);
        sum += std::cos (static_cast<double> (order) * theta - x * std::sin (theta));
    }

    return sign * sum / static_cast<double> (steps);
}

/// The largest order `besselJLadder` will fill. Sized so the ladder covers the
/// whole range the significant-order search asks for (an index of 200 radians
/// needs orders past 250) with room to spare, while keeping the buffer small
/// enough to sit in thread-local storage rather than on a stack.
inline constexpr int kBesselLadderMaxOrder = 1024;

/// `J_0(x)` through `J_highest(x)`, all of them, in one pass.
///
/// ---------------------------------------------------------------------------
/// **Why this exists, and what it replaced**
/// ---------------------------------------------------------------------------
///
/// `besselJn` above evaluates one order for a cost of `8 * (n + |x|)`
/// trigonometric calls. Asking it for *every* order up to the aliasing edge --
/// which is exactly what a significant-order search does -- therefore costs
/// `O(n^2)`, and at Stryda's indices that is millions of `sin` and `cos` calls
/// per call. It made `fm::significantOrderExact` cost 5.7 ms at an index of 64
/// radians, and it is half of why the index cap froze FL Studio on the user's
/// rig (2026-09-04; the other half is `fm::feedbackOrder` below).
///
/// Miller's downward recurrence gets all of them together instead. Bessel's
/// recurrence
///
///     J_(n-1)(x) = (2n / x) * J_n(x) - J_(n+1)(x)
///
/// is unstable upwards for `n > x` -- the growing second solution `Y_n` swamps
/// it -- and stable downwards, so the ladder starts well above both the wanted
/// order and the turning point with an arbitrary seed, walks down, and fixes
/// the arbitrary scale afterwards with the normalisation identity
///
///     J_0(x) + 2 * ( J_2(x) + J_4(x) + ... ) = 1.
///
/// **Derived from the recurrence and the identity, both standard, and verified
/// rather than trusted:** `tests/test_FmBandwidth.cpp` checks the whole ladder
/// against `besselJn`'s integral over the range Stryda uses. Worst absolute
/// disagreement 2.232e-15 at `x = 200`, order 236 -- and 0.86 us for 140 orders
/// at `x = 100.53` against 5.7 ms for the same job one order at a time.
///
/// Returns false, leaving `out` untouched, if `highest` is out of range.
[[nodiscard]] inline bool besselJLadder (double x, int highest, double* out) noexcept
{
    if (out == nullptr || highest < 0 || highest > kBesselLadderMaxOrder)
        return false;

    const auto count = static_cast<std::size_t> (highest) + 1;
    const double magnitude = x < 0.0 ? -x : x;

    if (magnitude < 1e-12)
    {
        for (std::size_t i = 0; i < count; ++i)
            out[i] = 0.0;

        out[0] = 1.0;
        return true;
    }

    // Start above the turning point by a margin that grows with the order, so
    // the downward pass has spent the transient before it reaches anything we
    // keep. The margin is the usual `sqrt`-of-order rule, generously scaled.
    int start = std::max (highest, static_cast<int> (magnitude));
    start += std::max (24, static_cast<int> (
                               std::ceil (12.0 * std::sqrt (static_cast<double> (start + 1)))));
    start += start & 1;   // even, so the even-order sum below starts correctly

    double above = 0.0;      // J_(n+1)
    double here = 1e-260;    // J_n, at an arbitrary scale fixed up at the end
    double evenSum = 0.0;    // accumulates 2 * (J_2 + J_4 + ...)

    for (std::size_t i = 0; i < count; ++i)
        out[i] = 0.0;

    for (int n = start; n >= 1; --n)
    {
        const double below = (2.0 * static_cast<double> (n) / magnitude) * here - above;
        above = here;
        here = below;

        // The unnormalised values grow without bound on the way down. Rescale
        // everything, including what has already been stored, before they can
        // overflow -- the normalisation at the end is scale-free, so this
        // changes nothing about the answer.
        if ((here < 0.0 ? -here : here) > 1e250)
        {
            here *= 1e-260;
            above *= 1e-260;
            evenSum *= 1e-260;

            for (std::size_t i = 0; i < count; ++i)
                out[i] *= 1e-260;
        }

        const int order = n - 1;

        if (order <= highest)
            out[static_cast<std::size_t> (order)] = here;

        if (order > 0 && (order % 2) == 0)
            evenSum += 2.0 * here;
    }

    evenSum += here;   // `here` is J_0 by now

    if (! (evenSum > 0.0 || evenSum < 0.0))
        return false;

    const double scale = 1.0 / evenSum;

    for (std::size_t i = 0; i < count; ++i)
        out[i] *= scale;

    // J_n(-x) = (-1)^n J_n(x).
    if (x < 0.0)
        for (std::size_t i = 1; i < count; i += 2)
            out[i] = -out[i];

    return true;
}

/// Modified Bessel function of the first kind, order n:
///
///     I_n(x) = sum over k >= 0 of (x/2)^(2k+n) / (k! (k+n)!)
///
/// **The series, deliberately, and not the integral form.** `I_n` has the
/// integral representation `(1/pi) * integral of exp(x cos t) cos(n t) dt`,
/// which looks like the natural companion to `besselJn` above and is a trap:
/// the integrand is a large positive number multiplied by an oscillating
/// cosine, so for `n` much larger than `x` the answer is a tiny difference of
/// huge terms and cancellation destroys it. The series has **all-positive
/// terms**, so it stays accurate down to underflow -- which matters here,
/// because the whole question this function is asked is "is this ratio below
/// -60 dB", and a wrong sign or a cancelled digit answers it backwards.
///
/// Underflows cleanly to 0 for the far tail, which reads as -inf dB and is the
/// right answer.
[[nodiscard]] inline double besselI (int n, double x) noexcept
{
    const int order = n < 0 ? -n : n;   // I_{-n} = I_n
    const double half = 0.5 * (x < 0.0 ? -x : x);

    // First term: (x/2)^n / n!, built as a running product so a large n never
    // forms an intermediate that overflows on its own.
    double term = 1.0;
    for (int i = 1; i <= order; ++i)
        term *= half / static_cast<double> (i);

    double sum = term;
    const double halfSquared = half * half;

    for (int k = 0; k < 512; ++k)
    {
        term *= halfSquared / (static_cast<double> (k + 1) * static_cast<double> (k + order + 1));
        sum += term;

        if (term < sum * 1.0e-17)
            break;
    }

    // Even I_n(x) for x < 0 is I_n(|x|) for even n and -I_n(|x|) for odd n;
    // every caller here passes a non-negative index, so the sign is stated
    // rather than silently dropped.
    return (x < 0.0 && (order % 2) != 0) ? -sum : sum;
}

// ---------------------------------------------------------------------------
// The three criteria
// ---------------------------------------------------------------------------

namespace fm
{

/// The threshold this file measures against, and the one Stryda's tables use.
///
/// -80 dB, following Timoney & Lazzarini (DAFx-11), who define the bandwidth of
/// an FM signal as "the point at which the spectrum was 80dB below the peak
/// value" and note that this is "a reasonably strict criteria and much stronger
/// than Carson's rule". CLAUDE.md section 7 asks for no inharmonic component
/// above -60 dBFS in the audible band, so predicting to -80 dB leaves 20 dB of
/// margin between what is predicted and what is required.
inline constexpr double kThresholdDb = -80.0;

/// The highest sideband order that still carries at least `thresholdDb`,
/// relative to an unmodulated carrier, for classic phase modulation at
/// `indexRadians`.
///
/// Chowning (JAES 21(7), 1973): the side frequencies sit at `f_c +/- n*f_m`
/// with amplitudes `J_n(I)`, and "the higher the order of the side frequency
/// the larger the index must be for that side frequency to have significant
/// amplitude". This evaluates that directly rather than approximating it --
/// `J_n(I)` is not monotonic in `n`, so the answer is the *last* order above
/// the threshold, not the first one below it.
///
/// **The threshold is relative to the loudest sideband, not to unity, and that
/// is not a stylistic choice.** The first version compared `|J_n(I)|` against
/// `10^(dB/20)` directly, which is a level relative to an unmodulated carrier.
/// But FM spreads energy: at an index of 16 radians the loudest partial is
/// about 0.21, not 1, so "80 dB down from the peak" is an absolute level about
/// 13 dB lower than "80 dB down from unity". The two disagree by roughly one
/// order of magnitude in amplitude and therefore by several sidebands, and the
/// measurement caught it: `tezla-measure stryda` table 2 read the predictor
/// **short by 4 %** at index 16 -- up to 6152 Hz at 880 Hz and ratio 7 -- while
/// agreeing exactly at indices 1 and 4, where the spread is small enough that
/// the peak is still near unity.
///
/// Under-estimating is the one direction a bandwidth bound may not err in, so
/// the normalisation is by the peak, which is also what any spectrum analyser
/// shows.
[[nodiscard]] inline int significantOrderExact (double indexRadians,
                                                double thresholdDb) noexcept
{
    if (! (indexRadians > 0.0))
        return 0;

    // Search past the asymptotic edge by a healthy margin, because the aim is
    // to find the *last* order above threshold and the Airy-type shoulder
    // beyond the turning point is where it lives.
    const int ceiling = static_cast<int> (indexRadians
                                          + 6.0 * std::cbrt (indexRadians)
                                          + 16.0) + 1;

    // One ladder, both answers. The peak and the last order above threshold are
    // two reductions over the same set of orders, so evaluating that set once
    // is not a shortcut -- it is the same arithmetic with the redundancy taken
    // out. Thread-local so the buffer is neither allocated nor on the stack;
    // it is statically zero-initialised, so there is no guard and no first-call
    // allocation on any thread.
    static thread_local std::array<double, kBesselLadderMaxOrder + 1> ladder {};

    const bool haveLadder = ceiling <= kBesselLadderMaxOrder
                              && besselJLadder (indexRadians, ceiling, ladder.data());

    const auto valueAt = [haveLadder, indexRadians] (int n) noexcept
    {
        const double value = haveLadder ? ladder[static_cast<std::size_t> (n)]
                                        : besselJn (n, indexRadians);
        return value < 0.0 ? -value : value;
    };

    double peak = 0.0;
    for (int n = 0; n <= ceiling; ++n)
        peak = std::max (peak, valueAt (n));

    if (! (peak > 0.0))
        return 0;

    const double threshold = peak * std::pow (10.0, thresholdDb / 20.0);

    int highest = 0;
    for (int n = 0; n <= ceiling; ++n)
        if (valueAt (n) >= threshold)
            highest = n;

    return highest;
}

/// The same answer at the default threshold, from a table.
///
/// ---------------------------------------------------------------------------
/// **This is not premature optimisation. It is a bug fix.**
/// ---------------------------------------------------------------------------
///
/// The exact form above evaluates `besselJn` about twice per candidate order,
/// each with an adaptive step count of `8 * (n + |x|)`. At an index of 16
/// radians that is roughly **31 000 trigonometric evaluations per call**.
/// `FmBandwidth::topSidebandHz` calls it up to sixty times, and
/// `indexScaleFor` calls *that* thirty-three times while bisecting -- so one
/// index-cap resolution was **on the order of 60 million trig evaluations**.
///
/// Stryda was resolving the cap per voice per 32-sample control chunk, and the
/// editor was resolving it again at twelve frames a second to draw the
/// bandwidth readout. On the user's rig that pinned FL Studio's CPU meter past
/// 100 % the moment a knob was touched, which is exactly when the editor
/// timer and the parameter push coincide. Reported from the rig on 2026-09-04.
///
/// The table is 2048 entries over indices 0 to 200 radians, built once on first
/// use (thread-safe by C++11 static-local rules, 8 KB, never on the audio
/// thread's critical path because the first call happens at `prepare`). The
/// order is a step function of the index, so **nearest-entry lookup is not an
/// approximation of the answer -- it is the answer**, to within the table's
/// index resolution of 0.1 radians, and the value is rounded up so the result
/// stays on the safe side of the true edge.
///
/// A non-default threshold falls through to the exact form, because only the
/// tests ask for one.
[[nodiscard]] inline int significantOrder (double indexRadians,
                                           double thresholdDb = kThresholdDb) noexcept
{
    constexpr int kTableSize = 2048;
    constexpr double kTableTop = 200.0;

    if (! (indexRadians > 0.0))
        return 0;

    // `isExactly` rather than `!=`: the table is keyed on the default threshold
    // and only an exact match may read it, which is a deliberate bit comparison
    // rather than the accidental one `-Wfloat-equal` is warning about.
    if (! isExactly (thresholdDb, kThresholdDb) || indexRadians > kTableTop)
        return significantOrderExact (indexRadians, thresholdDb);

    static const std::array<int, kTableSize> table = []
    {
        std::array<int, kTableSize> built {};

        for (int i = 0; i < kTableSize; ++i)
            built[static_cast<std::size_t> (i)] = significantOrderExact (
                kTableTop * static_cast<double> (i) / static_cast<double> (kTableSize - 1),
                kThresholdDb);

        return built;
    }();

    // Rounded up rather than to nearest: the order rises with the index, so the
    // next entry is the conservative one, and conservative is the only
    // direction a bandwidth bound may err in.
    const auto slot = static_cast<std::size_t> (
        std::ceil (indexRadians * (kTableSize - 1) / kTableTop));

    return table[std::min (slot, static_cast<std::size_t> (kTableSize - 1))];
}

/// The textbook asymptotic edge, `n ~ I + 2*I^(1/3) + 1`.
///
/// **Derived, not taken from a source**, and kept only so the measurement has
/// something to disagree with: it is the standard uniform-asymptotic statement
/// that `J_n(x)` turns over at `n = x` and decays across a shoulder of width
/// `x^(1/3)`. `tezla-measure stryda` table 2 reports it beside
/// `significantOrder`, and if the two ever part company the exact one wins.
[[nodiscard]] inline double asymptoticOrder (double indexRadians) noexcept
{
    if (! (indexRadians > 0.0))
        return 0.0;

    return indexRadians + 2.0 * std::cbrt (indexRadians) + 1.0;
}

/// The highest harmonic still above `thresholdDb` for an operator modulating
/// **its own** phase by `betaRadians`.
///
/// How far the feedback harmonic count searches before it saturates.
///
/// Past this the Kapteyn coefficients fall like n^(-4/3), so -80 dB is not
/// reached until well past a thousand harmonics; counting further would only
/// make the saturation look like a measurement.
inline constexpr int kFeedbackSearchCeiling = 512;

/// Feedback phase modulation is `y = sin(w t + beta * y)`, which is **Kepler's
/// equation**, and its solution is the classical Kapteyn series: the n-th
/// harmonic of the output has amplitude exactly
///
///     2 * J_n(n * beta) / (n * beta)
///
/// so this is not an approximation of the feedback operator, it *is* the
/// feedback operator, and it is why a rising feedback walks a sine towards a
/// sawtooth -- at beta = 1 the coefficients become `2 J_n(n)/n`, which fall off
/// like `1/n^(4/3)` rather than a saw's `1/n`, so the approach is asymptotic
/// and the corner never quite arrives.
///
/// `Oscillator` computes its feedback from the mean of the last two outputs
/// (Tomisawa, US 4,249,447), which is a one-zero lowpass with a null at
/// Nyquist. That makes the real operator's high harmonics *quieter* than this,
/// so the bound stays conservative in the safe direction. Measured in
/// `tezla-measure stryda` table 3.
///
/// ---------------------------------------------------------------------------
/// **Beta = 1 radian is a real boundary, and `Oscillator` lets you go past it**
/// ---------------------------------------------------------------------------
///
/// Kepler's equation is single-valued only for `beta < 1`. At exactly 1 the
/// solution has a cusp -- `dE/dtheta` is unbounded at the origin -- and above
/// it there is no single-valued continuous-time solution at all, so there is
/// nothing for the Kapteyn series to be equal to. What the discrete operator
/// does instead is well defined but is not this: it is a *delayed* recursion,
/// `y[n] = sin(phase + beta * (y[n-1] + y[n-2]) / 2)`, which stays bounded
/// because the shape is bounded, and whose spectrum broadens towards noise.
///
/// This matters because `Oscillator::kMaxFeedback` is **1.0 cycles**, which is
/// 6.28 radians -- six times past the boundary. So:
///
///  - Below 1 radian (0.159 cycles) the return value is **exact**, and
///    `tests/test_FmBandwidth.cpp` proves it against a numerical solve of
///    Kepler's equation with no Bessel function anywhere in the comparison.
///  - At and above 1 radian the count saturates at `kSearchCeiling` and the
///    prediction should be read as "wide, and no longer predicted" rather than
///    as a number. `tezla-measure stryda` table 3 measures what actually
///    happens there, and F2 decides from that measurement whether Stryda's
///    feedback control stops at the boundary or runs past it with the cap
///    doing the work.
///
/// Saturating rather than extrapolating is the conservative direction: it makes
/// the index cap clamp hard exactly where the model stops being able to
/// promise anything.
///
/// ---------------------------------------------------------------------------
/// **Why this bisects instead of counting, and what the counting cost**
/// ---------------------------------------------------------------------------
///
/// The obvious form walks `n` from 1 to the ceiling and keeps the last order
/// above threshold. It is also the single most expensive function in this
/// header by three orders of magnitude, because `besselJn`'s step count grows
/// with `n + |x|` and here `x = n * beta`: the walk costs about
/// `8 * (1 + beta) * ceiling^2 / 2` trigonometric evaluations, which is **two
/// million per call**. Timed at 30-55 ms each, and only for `beta < 1` -- at
/// and above 1 radian the saturating early-out returns instantly, which is why
/// the cost hides at exactly the settings a synthesiser spends least time at.
///
/// The Kapteyn coefficients decay monotonically in `n` for every `beta < 1`
/// (checked at 99 x 512 points, zero violations above 1e-9, and asserted in
/// `tests/test_FmBandwidth.cpp` against the counting form itself), so the last
/// order above threshold is found by bisection in nine evaluations rather than
/// 512 -- **the same answer**, not an approximation of it. The counting form
/// lives in the test as the reference it is compared against, which is where a
/// naive implementation belongs.
[[nodiscard]] inline int feedbackOrderExact (double betaRadians,
                                             double thresholdDb) noexcept
{
    if (! (betaRadians > 0.0))
        return 1;

    if (betaRadians >= 1.0)
        return kFeedbackSearchCeiling;

    const double threshold = std::pow (10.0, thresholdDb / 20.0);

    const auto amplitudeAt = [betaRadians] (int n) noexcept
    {
        const double nb = static_cast<double> (n) * betaRadians;
        return std::abs (2.0 * besselJn (n, nb) / nb);
    };

    // The two ends first, so the invariant below is true when the loop starts
    // and the answer is never extrapolated from outside the searched range.
    if (amplitudeAt (kFeedbackSearchCeiling) >= threshold)
        return kFeedbackSearchCeiling;

    if (amplitudeAt (1) < threshold)
        return 1;

    // Invariant: `low` is at or above threshold, `high` is below it.
    int low = 1;
    int high = kFeedbackSearchCeiling;

    while (high - low > 1)
    {
        const int mid = low + (high - low) / 2;

        if (amplitudeAt (mid) >= threshold)
            low = mid;
        else
            high = mid;
    }

    return low;
}

/// The same answer at the default threshold, from a table.
///
/// Nine Bessel evaluations is cheap next to 512 and still far too expensive to
/// run per voice per control chunk: `topSidebandHz` calls this twelve times and
/// `indexScaleFor` calls *that* thirty-three times, so a single index-cap
/// resolution came to **two to three seconds** on this machine with any
/// feedback at all in the patch. That is not a CPU spike, it is a hang, and it
/// is what froze FL Studio on the user's rig when a knob was touched (reported
/// 2026-09-04). Note the trap in the arithmetic: the bisection in
/// `indexScaleFor` scales every index down as it searches, so a patch with
/// feedback *above* the 1-radian early-out still falls below it partway
/// through the search and pays the full cost anyway.
///
/// The order rises monotonically with beta, so rounding the lookup **up** to
/// the next tabulated beta is the conservative direction -- a slightly wider
/// predicted spectrum, never a narrower one. 1024 entries over (0, 1] give a
/// beta resolution of about 0.001; the table is built once on first use, which
/// happens at `prepare` and not on the audio thread's critical path.
[[nodiscard]] inline int feedbackOrder (double betaRadians,
                                        double thresholdDb = kThresholdDb) noexcept
{
    constexpr int kTableSize = 1024;

    if (! (betaRadians > 0.0))
        return 1;

    if (betaRadians >= 1.0)
        return kFeedbackSearchCeiling;

    if (! isExactly (thresholdDb, kThresholdDb))
        return feedbackOrderExact (betaRadians, thresholdDb);

    static const std::array<int, kTableSize> table = []
    {
        std::array<int, kTableSize> built {};

        // Built by walking up rather than by a bisection per entry, because the
        // order is monotone in beta as well as in n: entry `i` cannot be below
        // entry `i - 1`, so the search starts where the last one finished and
        // only ever steps upward. The total number of upward steps across the
        // whole table is therefore the final order, not the table size times
        // the search depth -- about 1500 Bessel evaluations for the lot instead
        // of 9000, and the build drops from 430 ms to well under a tenth of it.
        const double threshold = std::pow (10.0, kThresholdDb / 20.0);
        int order = 1;

        for (int i = 0; i < kTableSize; ++i)
        {
            const double beta = static_cast<double> (i + 1) / static_cast<double> (kTableSize);

            if (beta >= 1.0)
            {
                built[static_cast<std::size_t> (i)] = kFeedbackSearchCeiling;
                continue;
            }

            while (order < kFeedbackSearchCeiling)
            {
                const double nb = static_cast<double> (order + 1) * beta;

                if (std::abs (2.0 * besselJn (order + 1, nb) / nb) < threshold)
                    break;

                ++order;
            }

            built[static_cast<std::size_t> (i)] = order;
        }

        return built;
    }();

    const auto slot = static_cast<std::size_t> (
        std::max (0.0, std::ceil (betaRadians * kTableSize) - 1.0));

    return table[std::min (slot, static_cast<std::size_t> (kTableSize - 1))];
}

/// The largest ModFM index `k` whose aliasing stays below `thresholdDb`.
///
/// Lazzarini & Timoney, JAES 58(6), 2010, Eq (12): take the safe threshold of
/// -60 dB (the paper's own choice, "1/1000 of maximum amplitude") and set the
/// limit as the largest `k` with
///
///     20 * log10 ( I_n(k) / I_0(k) ) <= threshold,   n = (f_sr/2 - f_c) / f_m
///
/// `n` is simply how many harmonics of the modulator fit between the carrier
/// and Nyquist, so the condition reads: the first partial that would land above
/// Nyquist must already be inaudible.
///
/// Returns 0 if there is no room at all (the carrier is already at or above
/// Nyquist), and `maximumIndex` if even that is safe.
[[nodiscard]] inline double modFmMaxIndex (double carrierHz,
                                           double modulatorHz,
                                           double sampleRate,
                                           double thresholdDb = -60.0,
                                           double maximumIndex = 256.0) noexcept
{
    const double nyquist = 0.5 * sampleRate;
    if (! (modulatorHz > 0.0) || carrierHz >= nyquist)
        return 0.0;

    const int n = static_cast<int> ((nyquist - carrierHz) / modulatorHz);
    if (n <= 0)
        return 0.0;

    const double threshold = std::pow (10.0, thresholdDb / 20.0);

    // I_n(k)/I_0(k) rises monotonically with k for fixed n -- the modified
    // Bessel functions do not oscillate, which is the whole reason ModFM's
    // partials behave -- so a bisection is exact rather than a search.
    double low = 0.0;
    double high = maximumIndex;

    if (besselI (n, high) / besselI (0, high) <= threshold)
        return maximumIndex;

    for (int i = 0; i < 64; ++i)
    {
        const double mid = 0.5 * (low + high);
        const double i0 = besselI (0, mid);
        const double ratio = i0 > 0.0 ? besselI (n, mid) / i0 : 0.0;

        if (ratio <= threshold)
            low = mid;
        else
            high = mid;
    }

    return low;
}

/// The -80 dB bandwidth of an **exponential** FM signal, in Hz.
///
/// Timoney & Lazzarini, DAFx-11, Eq (16):
///
///     BW = f_c * e^(V0 * ln2) * 2^(Vm - 1) * p01 + (p11 + Vm) * f_m
///
/// with `p01 = 2.771` and `p11 = 4.3030`, fitted from the paper's measured
/// coefficients `p0 = 2.771 / 5.168 / 9.384` and `p1 = 4.303 / 6.461 / 9.249`
/// at modulation depths `Vm = 1 / 2 / 3`.
///
/// This is a different technique from the linear/phase modulation the rest of
/// this file is about: exponential FM is what an analogue modular does, because
/// its volts-per-octave control makes the modulation an exponential function of
/// the control voltage. Its carrier frequency **moves with the modulation
/// depth** (by `I_0(Vm ln2)`), which linear FM's does not, and its bandwidth
/// grows far faster -- which is why the paper exists, and why the criterion is
/// not Carson's rule.
///
/// The paper notes its fit deliberately over-estimates rather than
/// under-estimates. For a safety bound that is the right direction, and it is
/// the reason this is used as-published rather than re-fitted.
[[nodiscard]] inline double exponentialBandwidthHz (double carrierHz,
                                                    double modulatorHz,
                                                    double v0,
                                                    double vm) noexcept
{
    constexpr double p01 = 2.771;
    constexpr double p11 = 4.3030;

    const double carrierScale = std::exp (v0 * std::numbers::ln2);

    return carrierHz * carrierScale * std::pow (2.0, vm - 1.0) * p01
         + (p11 + vm) * modulatorHz;
}

/// The carrier frequency an exponential-FM operator actually runs at, which is
/// not the one it was told. Timoney & Lazzarini, Eq (11): `f_E = I_0(Vm ln2) *
/// f_c * e^(V0 ln2)`. The paper's own example: `V0 = 5` scales the carrier by
/// 7.17.
[[nodiscard]] inline double exponentialCarrierHz (double carrierHz,
                                                  double v0,
                                                  double vm) noexcept
{
    return besselI (0, vm * std::numbers::ln2)
         * carrierHz * std::exp (v0 * std::numbers::ln2);
}

} // namespace fm

// ---------------------------------------------------------------------------
// The matrix
// ---------------------------------------------------------------------------

/// Where the top of a whole operator matrix's spectrum sits, and by how much
/// every index would have to shrink to bring it under a given ceiling.
///
/// The composition rule is conservative by construction. An operator that is
/// itself modulated does not present a sine to whatever it modulates next: it
/// presents a band reaching up to its own top. So a modulator contributes its
/// **whole** top frequency, multiplied by the number of significant sidebands
/// the index produces:
///
///     top(j) = f_j + sum over i of ( order(index_ij) * top(i) )
///
/// evaluated in the same 6 -> 1 order the voice runs in, so the recursion is a
/// single sweep and needs no graph traversal. A cell **above** the diagonal
/// feeds an operator that has already been evaluated this sample; its
/// contribution is added on the next sweep rather than dropped, because a
/// one-sample-old modulator is still a modulator.
///
/// Self-modulation on the diagonal uses `fm::feedbackOrder`, which is exact.
///
/// ---------------------------------------------------------------------------
/// **How much this is worth, measured, and where it stops being tight**
/// ---------------------------------------------------------------------------
///
/// For a **two-operator pair the prediction is exact** -- +0 Hz against the
/// rendered -80 dB edge on 27 combinations in `tezla-measure stryda` table 2
/// and 18 more in `tests/test_FmBandwidth.cpp`, across three carriers, three
/// ratios and three indices.
///
/// For a **stack it is an upper bound and a loose one**, and the numbers are
/// worth stating rather than implying. Rendered against the same -80 dB edge,
/// 440 Hz carrier:
///
///     depth  indices (cycles)  predicted     measured    over by
///     1      1.2                  13392 Hz     13392 Hz      1.0x
///     2      1.2 / 0.7           227232 Hz     96768 Hz      2.3x
///     3      1.2 / 0.7 / 0.5    3434832 Hz    496368 Hz      6.9x
///     2      2.4 / 1.6           637632 Hz    322704 Hz      2.0x
///     3      2.4 / 1.6 / 1.0  15001632 Hz    786288 Hz     19.1x
///
/// The cause is structural: each stage multiplies the modulator's **whole top
/// frequency** by the sideband order, and a modulator that is itself spread has
/// already had that widening applied once. So the factor compounds -- roughly
/// 2*pi per stage, which is the ratio between consecutive rows above.
///
/// It is left conservative deliberately, because **for a cap that is the safe
/// direction**: clamping a patch that did not need it is a quieter failure than
/// letting one alias. But it is the wrong number to *display* as a fact, so
/// Stryda's panel labels it an upper bound, and the index cap defaults to Off
/// rather than clamping patches on a bound this loose.
///
/// Tightening it means composing in frequency **deviation** rather than in top
/// frequency -- Carson's `D + W` shape -- with the Bessel widening applied once
/// at the end instead of at every stage. Both obvious forms of that were tried
/// against the table above and one under-estimated at depth 2, which is the one
/// direction a bound may not err in, so it is written down as work rather than
/// guessed at. `plugins/Stryda/PLAN.md`, risk 1.
class FmBandwidth
{
public:
    static constexpr int kMaxOperators = 8;

    void reset() noexcept
    {
        frequencies_.fill (0.0);
        feedback_.fill (0.0);

        for (auto& row : indices_)
            row.fill (0.0);
    }

    /// The operator's own frequency, before anything modulates it.
    void setOperatorFrequency (int op, double hz) noexcept
    {
        if (op >= 0 && op < kMaxOperators)
            frequencies_[static_cast<std::size_t> (op)] = hz > 0.0 ? hz : 0.0;
    }

    /// `from` modulates `to`, by `cycles` of peak phase deviation. Note the
    /// unit: cycles, as `Oscillator::advance` takes, not radians.
    void setIndex (int from, int to, double cycles) noexcept
    {
        if (from >= 0 && from < kMaxOperators && to >= 0 && to < kMaxOperators)
            indices_[static_cast<std::size_t> (to)][static_cast<std::size_t> (from)]
                = cycles > 0.0 ? cycles : 0.0;
    }

    /// Self-modulation, in cycles, matching `Oscillator::setFeedback`.
    void setFeedback (int op, double cycles) noexcept
    {
        if (op >= 0 && op < kMaxOperators)
            feedback_[static_cast<std::size_t> (op)] = cycles > 0.0 ? cycles : 0.0;
    }

    void setOperatorCount (int count) noexcept
    {
        count_ = std::clamp (count, 1, kMaxOperators);
    }

    [[nodiscard]] int getOperatorCount() const noexcept { return count_; }

    /// The predicted top of the spectrum, in Hz, with every index scaled by
    /// `indexScale` (1.0 for "as set").
    [[nodiscard]] double topSidebandHz (double thresholdDb = fm::kThresholdDb,
                                        double indexScale = 1.0) const noexcept
    {
        std::array<double, kMaxOperators> top {};

        // Two sweeps rather than one: the first resolves the below-diagonal
        // (instantaneous) paths, the second lets an above-diagonal cell -- the
        // one-sample-old reverse path -- carry the width it has by then. A
        // third sweep changes nothing that is not already covered by the
        // conservatism of the rule.
        for (int sweep = 0; sweep < 2; ++sweep)
        {
            for (int j = count_ - 1; j >= 0; --j)
            {
                const auto index = static_cast<std::size_t> (j);
                double width = frequencies_[index];

                const double beta = feedback_[index] * indexScale * kTwoPi;
                if (beta > 0.0)
                    width = frequencies_[index]
                          * static_cast<double> (fm::feedbackOrder (beta, thresholdDb));

                for (int i = 0; i < count_; ++i)
                {
                    if (i == j)
                        continue;

                    const double cycles = indices_[index][static_cast<std::size_t> (i)] * indexScale;
                    if (! (cycles > 0.0))
                        continue;

                    const double modulatorTop = top[static_cast<std::size_t> (i)] > 0.0
                                                  ? top[static_cast<std::size_t> (i)]
                                                  : frequencies_[static_cast<std::size_t> (i)];

                    width += static_cast<double> (fm::significantOrder (cycles * kTwoPi, thresholdDb))
                           * modulatorTop;
                }

                top[index] = width;
            }
        }

        double highest = 0.0;
        for (int j = 0; j < count_; ++j)
            highest = std::max (highest, top[static_cast<std::size_t> (j)]);

        return highest;
    }

    /// The largest scale in [0, 1] that every index can be multiplied by while
    /// keeping the predicted top under `ceilingHz`. Returns exactly 1.0 when
    /// the matrix is already inside the ceiling -- **exactly**, so a cap that
    /// is not binding is bit-exactly inert and the audio path is untouched.
    [[nodiscard]] double indexScaleFor (double ceilingHz,
                                        double thresholdDb = fm::kThresholdDb) const noexcept
    {
        if (topSidebandHz (thresholdDb, 1.0) <= ceilingHz)
            return 1.0;

        double low = 0.0;
        double high = 1.0;

        for (int i = 0; i < 32; ++i)
        {
            const double mid = 0.5 * (low + high);

            if (topSidebandHz (thresholdDb, mid) <= ceilingHz)
                low = mid;
            else
                high = mid;
        }

        return low;
    }

private:
    static constexpr double kTwoPi = 2.0 * std::numbers::pi;

    std::array<double, kMaxOperators> frequencies_ {};
    std::array<double, kMaxOperators> feedback_ {};
    std::array<std::array<double, kMaxOperators>, kMaxOperators> indices_ {};
    int count_ { 6 };
};

} // namespace tezla::dsp
