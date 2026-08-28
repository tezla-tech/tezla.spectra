// Copyright (c) 2026 The Tezla <thetezla@proton.me>
// Created by The Tezla -- https://github.com/wingit33/tezla.tech
// Music: https://soundcloud.com/thetezla | https://thetezla.bandcamp.com
// Built with development assistance from Claude (Anthropic).
// SPDX-License-Identifier: AGPL-3.0-only
// GNU AGPLv3 (see LICENSE), plus NOTICE.md's attribution term. Keep intact.

#pragma once

// Chebyshev harmonic synthesis -- Le Brun, "Digital Waveshaping Synthesis",
// JAES 27(4), 1979.
//
// The other way to make harmonics. HarmonicGenerator.hpp picks a *curve* and
// accepts whatever series falls out of its shape; this picks the series and
// derives the curve. Because
//
//      T_n(cos t) = cos(n t)
//
// a unit-amplitude sine through the nth Chebyshev polynomial comes out as
// exactly the nth harmonic and nothing else, so a weighted sum of them is a
// recipe written in numbers rather than chosen by ear from a shape.
//
// That identity holds at unit amplitude and nowhere else, and everything below
// is a consequence of taking that seriously:
//
//   unit amplitude   T_n(a cos t) with a != 1 is a mixture of every harmonic up
//                    to n, with weights that swing wildly. The caller must
//                    present a normalised signal; in Halo that is Track pinned
//                    at 100%, which divides the band by sqrt(2 * meanSquare) --
//                    for a sine, exactly its amplitude.
//
//   the clamp        Outside [-1, 1] the polynomials do not merely misbehave,
//                    they detonate: T_n(cosh v) = cosh(n v), so T_8(1.2) = 72.7
//                    and T_8(1.414) = 576. An amplitude estimate reading 40%
//                    low -- which sqrt(2)*RMS does on a band holding two equal
//                    partials -- is enough to get there. So the input is
//                    clamped hard at +/-1. Hard, not soft-kneed: a knee
//                    starting below 1.0 would bend the one case that is
//                    supposed to be exact.
//
//   the pedestal     T_n(a cos t) has a non-zero mean at every a except 1, and
//                    here that mean gets multiplied by a signal envelope, so it
//                    arrives as a *moving* DC offset rather than a static one a
//                    blocker could remove. That fault is already recorded in
//                    Halo's README, measured at -29.5 dBFS sitting on the
//                    blocker's own corner.
//
//   the fundamental  The one that is easy to miss, and it was: away from a = 1
//                    the odd polynomials put energy back at the *fundamental*.
//                    T_3(a cos t) = 3a(a^2 - 1) cos t + a^3 cos 3t, so at
//                    a = 0.6 the fundamental is five times the third harmonic.
//                    Left alone, moving Index off 1 would quietly turn this
//                    into the band-copying exciter the whole plugin exists to
//                    avoid. It is subtracted, the same way HarmonicGenerator
//                    subtracts its describing function.
//
// Both corrections are the Fourier coefficients of the composite map
// t -> T_n(clamp(a cos t)), and both are found in closed form below the clamp
// and by quadrature above it. See analyseComposite().
//
// A bonus falls out: at index 0 the pedestal is exactly T_n(0) and the
// fundamental term is zero, so the generator becomes precisely the zero
// function. Index is a real fade to nothing rather than a fade to a constant.
//
// **This shaper is not run through ADAA, and that is the opposite of what every
// other nonlinearity in this repository does.** The reason is measured, not
// assumed. A polynomial of degree n applied to a signal band-limited to B is
// exactly band-limited to n*B, so when the caller honours that limit there is
// no aliasing for ADAA to remove -- and ADAA is not free. Its difference
// quotient averages f over a segment in *x*, and the map from time to x is
// nonlinear, so the residue is periodic at the fundamental. Feeding a 3 kHz
// tone at a 192 kHz internal rate, asking for the 5th harmonic:
//
//                       fundamental      audible alias
//     direct                -291.7 dB          -249.0 dB
//     through ADAA           -41.8 dB          -249.2 dB
//
// Identical aliasing, and 250 dB of fundamental rejection thrown away -- the
// one thing this entire plugin is built to claim. It gets better by only 12 dB
// per doubling of the sample rate, so no amount of oversampling rescues it.
// Direct evaluation it is, and the antiderivative that ADAA would need is not
// written at all rather than left lying around to be reached for.
//
// Above index 1 the clamp engages and the composite stops being a polynomial,
// so it does alias -- around -60 dB on a 120 Hz band. ADAA would improve that
// to -85 dB, but only by paying the price above in the region the mode is
// actually for. Wreckage that aliases is wreckage; a precision mode that leaks
// a band copy is not a precision mode.
//
// Everything is evaluated by the recurrence T_(k+1) = 2u T_k - T_(k-1). Not for
// accuracy: expanded coefficients were measured against it here and agree to
// 1.1e-14 at worst across the whole domain, so the usual warning about
// cancellation in 128u^8 - 256u^6 + 160u^4 - 32u^2 + 1 does not actually bite
// at this order. The reasons are that it needs no coefficient table and that it
// costs one multiply-add per harmonic instead of a polynomial each.

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>

namespace tezla::dsp {

/// Harmonics 2 through 8, each with its own level.
///
/// 1 is deliberately absent from the sum, and actively cancelled on top of
/// that, so no fundamental can appear in the output at any setting.
///
/// The ceiling of 8 is not arbitrary either. T_n of a band topping out at B
/// reaches n*B, so exactness has a limit set by the internal Nyquist -- around
/// 96 kHz whatever the session rate, since Auto targets ~192 kHz. On a 120 Hz
/// bass band that allows harmonic 800; on a full-bandwidth treble band it allows
/// about 4. Eight is where the control stops being useful on the material this
/// exists for, and the caller band-limits its input accordingly.
class ChebyshevGenerator
{
public:
    static constexpr int kFirstHarmonic = 2;
    static constexpr int kLastHarmonic  = 8;
    static constexpr int kNumHarmonics  = kLastHarmonic - kFirstHarmonic + 1;

    /// Below this the generator is switched off outright.
    ///
    /// The corrections already cancel to nothing as index falls, so this is not
    /// load-bearing arithmetic -- it is what makes getHighestActiveHarmonic()
    /// answer 0 for a generator that cannot be heard, so a caller does not
    /// band-limit its input on behalf of something producing silence.
    static constexpr double kMinIndex = 1.0e-6;

    /// Decibels of tilt per harmonic step at full deflection. Seven harmonics
    /// across +/-1 of tilt therefore spans 24 dB end to end.
    static constexpr double kTiltDbPerStep = 4.0;

    /// Which harmonic the tilt pivots around. The middle of 2..8, so tilt moves
    /// the ends and leaves the centre of the recipe where it was.
    static constexpr int kTiltCentre = 5;

    ChebyshevGenerator() noexcept { updateWeights(); }

    /// Linear gain for one harmonic. 0 removes it completely.
    void setHarmonicGain (int harmonic, double gain) noexcept
    {
        if (harmonic < kFirstHarmonic || harmonic > kLastHarmonic)
            return;

        gains_[slot (harmonic)] = std::max (gain, 0.0);
        updateWeights();
    }

    [[nodiscard]] double getHarmonicGain (int harmonic) const noexcept
    {
        if (harmonic < kFirstHarmonic || harmonic > kLastHarmonic)
            return 0.0;

        return gains_[slot (harmonic)];
    }

    /// -1 tilts the recipe towards the low harmonics, +1 towards the high ones,
    /// 0 leaves every level exactly as set -- pow(10, 0) is 1.0 to the bit, so
    /// the macro is a true identity at its centre rather than an almost-one.
    void setTilt (double tilt) noexcept
    {
        tilt_ = std::clamp (tilt, -1.0, 1.0);
        updateWeights();
    }

    [[nodiscard]] double getTilt() const noexcept { return tilt_; }

    /// How hard the normalised signal is pushed into the polynomials.
    ///
    /// 1 is the exact point: a unit sine arrives as a unit sine and each
    /// harmonic comes out at the level it was given. Below 1 the harmonics
    /// blend into one another -- this is Le Brun's waveshaping index, the
    /// analogue of an FM modulation index, and it is what makes a recipe
    /// breathe with the material instead of standing still. Above 1 the clamp
    /// is what stops the polynomials running away, and what comes out is no
    /// longer a chosen series but the wreckage of one.
    void setIndex (double indexValue) noexcept
    {
        index_ = std::max (indexValue, 0.0);
        updateWeights();
    }

    [[nodiscard]] double getIndex() const noexcept { return index_; }

    /// Sets everything in one go.
    ///
    /// The individual setters each rebuild the whole correction, which above
    /// index 1 means a quadrature; a caller pushing nine smoothed parameters
    /// every block would run it nine times for one answer. `gains` holds
    /// kNumHarmonics entries, harmonic 2 first.
    void setAll (const double* gains, double tilt, double indexValue) noexcept
    {
        for (int i = 0; i < kNumHarmonics; ++i)
            gains_[static_cast<std::size_t> (i)] = std::max (gains[i], 0.0);

        tilt_  = std::clamp (tilt, -1.0, 1.0);
        index_ = std::max (indexValue, 0.0);

        updateWeights();
    }

    /// The highest harmonic with a non-zero weight, or 0 if the generator is
    /// silent. The caller needs it to band-limit its input: content above
    /// internalNyquist / this folds back.
    [[nodiscard]] int getHighestActiveHarmonic() const noexcept { return highest_; }

    /// Bound on |evaluate(x)| over the design domain |x| <= 1.
    ///
    /// |T_n| <= 1 inside the clamp and is exactly 1 outside it, so the shaped
    /// part is bounded everywhere; the fundamental cancellation adds a linear
    /// term, which means growth beyond |x| = 1 is at most linear and never the
    /// cosh runaway the clamp exists to prevent. That is the property worth
    /// having, and it is what makes the level of this generator predictable in
    /// a way a saturating curve's never is.
    [[nodiscard]] double getPeakBound() const noexcept { return peakBound_; }

    /// The linear term subtracted to cancel the fundamental. Zero at index 1,
    /// where there is nothing to cancel.
    [[nodiscard]] double getFundamentalTrim() const noexcept { return fundamentalSum_; }

    [[nodiscard]] double evaluate (double x) const noexcept
    {
        if (highest_ == 0)
            return 0.0;

        return shaped (index_ * x) - fundamentalSum_ * x;
    }

private:
    /// Intervals for the quadrature used above the clamp. Simpson on a smooth
    /// piece, so the error goes as h^4: with 256 intervals that is around 4e-7
    /// even against the eighth polynomial's fourth derivative.
    static constexpr int kQuadratureIntervals = 256;

    [[nodiscard]] static constexpr std::size_t slot (int harmonic) noexcept
    {
        return static_cast<std::size_t> (harmonic - kFirstHarmonic);
    }

    /// T_0 .. T_upTo at u, by the stable recurrence.
    static void fillChebyshev (double u, int upTo,
                               std::array<double, kLastHarmonic + 1>& t) noexcept
    {
        t[0] = 1.0;
        t[1] = u;

        const int last = std::min (upTo, kLastHarmonic);

        for (int k = 2; k <= last; ++k)
        {
            const auto j = static_cast<std::size_t> (k);
            t[j] = 2.0 * u * t[j - 1] - t[j - 2];
        }
    }

    /// The weighted, pedestal-corrected polynomial sum, clamped. No fundamental
    /// term -- that is linear in x and lives outside the clamp entirely.
    [[nodiscard]] double shaped (double u) const noexcept
    {
        if (u >= 1.0)  return atPositiveLimit_;
        if (u <= -1.0) return atNegativeLimit_;

        std::array<double, kLastHarmonic + 1> t {};
        fillChebyshev (u, highest_, t);

        double sum = 0.0;
        for (int n = kFirstHarmonic; n <= highest_; ++n)
            sum += weights_[slot (n)] * (t[static_cast<std::size_t> (n)] - pedestals_[slot (n)]);

        return sum;
    }

    /// DC and fundamental coefficients of t -> T_n(clamp(a cos t)), for every
    /// harmonic at once.
    ///
    /// Symmetry does most of the work and is worth spelling out, because it is
    /// also the reason the two corrections never interfere. The clamped cosine
    /// is odd about t = pi/2, so T_n of it is even there for even n and odd
    /// there for odd n. An even function of t about pi/2 has only even
    /// harmonics and a DC term; an odd one has only odd harmonics and no DC. So
    /// **even harmonics need only the pedestal and odd harmonics only the
    /// fundamental**, at every amplitude, clamped or not.
    ///
    /// Below the clamp both are polynomials in a, from expanding T_n and taking
    /// mean(cos^k) = C(k, k/2) / 2^k and the cos-t coefficient of cos^k t =
    /// C(k, (k-1)/2) / 2^(k-1) term by term. They are written factored, as
    /// (s - 1) times a remainder, because a = 1 is the setting the whole mode is
    /// built around: in that form the correction is exactly zero there by
    /// construction rather than by four large terms happening to cancel.
    ///
    /// Above the clamp there is no closed form -- the flat top is not a
    /// polynomial -- so the flat part is integrated exactly and the smooth
    /// remainder by Simpson.
    static void analyseComposite (double a, int upTo,
                                  std::array<double, kNumHarmonics>& dc,
                                  std::array<double, kNumHarmonics>& fundamental) noexcept
    {
        dc.fill (0.0);
        fundamental.fill (0.0);

        if (a <= 1.0)
        {
            const double s = a * a;
            const double d = s - 1.0;

            if (upTo >= 2) dc[slot (2)] = d;
            if (upTo >= 4) dc[slot (4)] = d * (3.0 * s - 1.0);
            if (upTo >= 6) dc[slot (6)] = d * (s * (10.0 * s - 8.0) + 1.0);
            if (upTo >= 8) dc[slot (8)] = d * (s * (s * (35.0 * s - 45.0) + 15.0) - 1.0);

            if (upTo >= 3) fundamental[slot (3)] = 3.0 * a * d;
            if (upTo >= 5) fundamental[slot (5)] = 5.0 * a * d * (2.0 * s - 1.0);
            if (upTo >= 7) fundamental[slot (7)] = 7.0 * a * d * (s * (5.0 * s - 5.0) + 1.0);

            return;
        }

        // Above the clamp. Integrating over a quarter cycle is enough by the
        // symmetry above; the pieces either side of the clamp crossing are each
        // smooth, which is what Simpson needs and what the kinked whole is not.
        constexpr double halfPi = 0.5 * std::numbers::pi_v<double>;
        const double crossing = std::acos (1.0 / a);   // cos t = 1/a

        // The flat top: T_n(+1) = 1 for every n, so both integrals are exact.
        // (2/pi) * crossing for the mean, (4/pi) * sin(crossing) for the
        // fundamental.
        const double flatDc          = (2.0 / std::numbers::pi_v<double>) * crossing;
        const double flatFundamental = (4.0 / std::numbers::pi_v<double>) * std::sin (crossing);

        const double width = halfPi - crossing;
        const double step  = width / kQuadratureIntervals;

        std::array<double, kNumHarmonics> dcIntegral {};
        std::array<double, kNumHarmonics> fundamentalIntegral {};

        for (int i = 0; i <= kQuadratureIntervals; ++i)
        {
            const double theta = crossing + step * i;

            // Simpson: ends once, odd nodes four times, even nodes twice.
            const double weight = (i == 0 || i == kQuadratureIntervals) ? 1.0
                                : (i % 2 == 1)                          ? 4.0
                                                                        : 2.0;

            const double cosine = std::cos (theta);

            std::array<double, kLastHarmonic + 1> t {};
            fillChebyshev (a * cosine, upTo, t);

            for (int n = kFirstHarmonic; n <= upTo; ++n)
            {
                const double value = t[static_cast<std::size_t> (n)];
                dcIntegral[slot (n)]          += weight * value;
                fundamentalIntegral[slot (n)] += weight * value * cosine;
            }
        }

        const double scale = step / 3.0;

        for (int n = kFirstHarmonic; n <= upTo; ++n)
        {
            if (n % 2 == 0)
                dc[slot (n)] = flatDc
                             + (2.0 / std::numbers::pi_v<double>) * scale * dcIntegral[slot (n)];
            else
                fundamental[slot (n)] = flatFundamental
                             + (4.0 / std::numbers::pi_v<double>) * scale * fundamentalIntegral[slot (n)];
        }
    }

    void updateWeights() noexcept
    {
        highest_ = 0;

        const bool live = index_ >= kMinIndex;

        for (int n = kFirstHarmonic; n <= kLastHarmonic; ++n)
        {
            const auto i = slot (n);

            // pow(10, 0) is exactly 1, so tilt at centre multiplies every gain
            // by one to the bit and the macro cannot colour a recipe set by hand.
            const double tiltDb = tilt_ * kTiltDbPerStep * (n - kTiltCentre);
            const double weight = live ? gains_[i] * std::pow (10.0, tiltDb / 20.0) : 0.0;

            weights_[i] = weight;

            if (weight > 0.0)
                highest_ = n;
        }

        peakBound_      = 0.0;
        pedestalSum_    = 0.0;
        fundamentalSum_ = 0.0;

        if (highest_ == 0)
        {
            pedestals_.fill (0.0);
            atPositiveLimit_ = atNegativeLimit_ = 0.0;
            return;
        }

        std::array<double, kNumHarmonics> fundamentals {};
        analyseComposite (index_, highest_, pedestals_, fundamentals);

        for (int n = kFirstHarmonic; n <= highest_; ++n)
        {
            const double w = weights_[slot (n)];

            pedestalSum_    += w * pedestals_[slot (n)];
            fundamentalSum_ += w * fundamentals[slot (n)];
            peakBound_      += w * (1.0 + std::abs (pedestals_[slot (n)]));
        }

        // The bound is quoted over the design domain |x| <= 1, where the linear
        // term contributes at most its own coefficient.
        peakBound_ += std::abs (fundamentalSum_);

        // The two clamp boundaries, so the outside branches are a lookup.
        atPositiveLimit_ = 0.0;
        atNegativeLimit_ = 0.0;

        for (int n = kFirstHarmonic; n <= highest_; ++n)
        {
            const double w = weights_[slot (n)];
            const double p = pedestals_[slot (n)];

            atPositiveLimit_ += w * (1.0 - p);                        // T_n(+1) = 1
            atNegativeLimit_ += w * ((n % 2 == 0 ? 1.0 : -1.0) - p);  // T_n(-1) = (-1)^n
        }
    }

    std::array<double, kNumHarmonics> gains_     {};
    std::array<double, kNumHarmonics> weights_   {};
    std::array<double, kNumHarmonics> pedestals_ {};

    double tilt_  { 0.0 };
    double index_ { 1.0 };

    int    highest_        { 0 };
    double peakBound_      { 0.0 };
    double pedestalSum_    { 0.0 };
    double fundamentalSum_ { 0.0 };

    double atPositiveLimit_ { 0.0 };
    double atNegativeLimit_ { 0.0 };
};

} // namespace tezla::dsp
