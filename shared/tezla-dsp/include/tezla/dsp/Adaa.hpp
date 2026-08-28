// Copyright (c) 2026 The Tezla <thetezla@proton.me>
// Created by The Tezla -- https://github.com/wingit33/tezla.tech
// Music: https://soundcloud.com/thetezla | https://thetezla.bandcamp.com
// Built with development assistance from Claude (Anthropic).
// SPDX-License-Identifier: AGPL-3.0-only
// GNU AGPLv3 (see LICENSE), plus NOTICE.md's attribution term. Keep intact.

#pragma once

// Antiderivative antialiasing (Parker, Zavalishin & Le Bivic, DAFx-16).
//
// Oversampling alone does not make a saturator clean. Measured on this rig: a
// naive hard clipper at 4x drive aliases at -47 dB at 48 kHz and -65 dB at
// 192 kHz -- four times the rate buys 18 dB and then stops, because the shaper
// itself has infinite bandwidth. ADAA attacks the other half of the problem by
// band-limiting the shaper.
//
// The idea: instead of evaluating f(x[n]) pointwise, evaluate the *average* of
// f over the segment between x[n-1] and x[n], which is what the continuous-time
// signal actually did between those two instants:
//
//      y[n] = ( F1(x[n]) - F1(x[n-1]) ) / ( x[n] - x[n-1] )
//
// where F1 is the antiderivative of f. It costs one extra function evaluation
// and one divide, and it introduces half a sample of delay.
//
// The divide is the catch, and it is the classic bug in every implementation:
// as x[n] approaches x[n-1] the numerator is a difference of two nearly equal
// numbers divided by a nearly zero denominator. On quiet or slow-moving
// material that is most samples, and the result is not a crash but a crackle.
// Below a tolerance the code falls back to evaluating f at the midpoint, which
// is the limit the expression tends to.

#include <cmath>

namespace tezla::dsp {

/// First-order ADAA around any shaper providing evaluate() and antiderivative().
///
/// The shaper is passed in at call time rather than stored, so a smoothed
/// parameter (drive, bias) can move between samples. Strictly, ADAA assumes the
/// nonlinearity is time-invariant across the pair of samples it looks at; with
/// parameters smoothed over 10-50 ms the per-sample change is far below the
/// method's own error, so this is safe. Stepping a parameter without smoothing
/// would not be.
template <typename Shaper>
class Adaa1
{
public:
    /// Below this difference between consecutive inputs, use the midpoint.
    ///
    /// Chosen from the numerics rather than by taste: the numerator carries
    /// about 1e-16 of absolute error, so dividing by a difference smaller than
    /// ~1e-7 amplifies that error above the audible noise floor. 1e-5 sits
    /// comfortably clear of that, and the fallback is accurate to second order
    /// anyway, so erring towards using it costs nothing.
    static constexpr double kTolerance = 1.0e-5;

    void reset() noexcept
    {
        previousInput_ = 0.0;
        previousF1_    = 0.0;
        primed_        = false;
    }

    [[nodiscard]] double process (double x, const Shaper& shaper) noexcept
    {
        const double f1 = shaper.antiderivative (x);

        if (! primed_)
        {
            previousInput_ = x;
            previousF1_    = f1;
            primed_        = true;
            return shaper.evaluate (x);
        }

        const double difference = x - previousInput_;

        const double y = std::abs (difference) < kTolerance
                       ? shaper.evaluate (0.5 * (x + previousInput_))
                       : (f1 - previousF1_) / difference;

        previousInput_ = x;
        previousF1_    = f1;
        return y;
    }

private:
    double previousInput_ { 0.0 };
    double previousF1_    { 0.0 };
    bool   primed_        { false };
};

} // namespace tezla::dsp
