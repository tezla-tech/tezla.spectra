#pragma once

// Saturation curves.
//
// Every shaper here provides both the function and its antiderivative, because
// that is what antiderivative antialiasing needs (see Adaa.hpp). That
// requirement is the main constraint on which curves are usable: a shaper whose
// integral has no closed form has to be oversampled brutally instead.

#include <algorithm>
#include <cmath>
#include <numbers>

namespace tezla::dsp {

/// log(cosh(x)), evaluated so it does not overflow.
///
/// The naive form computes cosh(x) first, which overflows a double at |x| > 710
/// and loses precision long before that. Drive plus a transient reaches those
/// values easily, and the failure looks like the plugin going silent.
[[nodiscard]] inline double logCosh (double x) noexcept
{
    const double absX = std::abs (x);
    return absX + std::log1p (std::exp (-2.0 * absX)) - std::numbers::ln2_v<double>;
}

/// Biased hyperbolic tangent -- the workhorse curve.
///
///   bias = 0   symmetric, odd harmonics only, compressive knee. Tape-like.
///   bias > 0   asymmetric, brings in even harmonics. Valve-like.
///
/// The output is normalised by the small-signal gain so that changing bias
/// changes the *harmonic content* without changing the level. Without that,
/// a "character" control doubles as a volume control and the user cannot judge
/// what it is doing.
///
/// Asymmetry also produces a DC offset, which grows with drive. A DcBlocker
/// after this stage is not optional.
class BiasedTanh
{
public:
    explicit BiasedTanh (double bias = 0.0) noexcept { setBias (bias); }

    void setBias (double bias) noexcept
    {
        bias_        = bias;
        tanhBias_    = std::tanh (bias);
        logCoshBias_ = logCosh (bias);

        // sech^2(bias) is the slope at the origin; dividing by it restores
        // unity small-signal gain.
        const double slopeAtOrigin = 1.0 - tanhBias_ * tanhBias_;
        normalisation_ = 1.0 / std::max (slopeAtOrigin, 1.0e-6);
    }

    [[nodiscard]] double getBias() const noexcept { return bias_; }

    [[nodiscard]] double evaluate (double x) const noexcept
    {
        return (std::tanh (x + bias_) - tanhBias_) * normalisation_;
    }

    /// First antiderivative, with the constant chosen so F1(0) = 0. The choice
    /// is not arbitrary: ADAA subtracts two nearby values of this function, and
    /// an offset of, say, log(cosh(bias)) would throw away precision in exactly
    /// the small-signal case where it is least affordable.
    [[nodiscard]] double antiderivative (double x) const noexcept
    {
        return (logCosh (x + bias_) - logCoshBias_ - tanhBias_ * x) * normalisation_;
    }

private:
    double bias_          { 0.0 };
    double tanhBias_      { 0.0 };
    double logCoshBias_   { 0.0 };
    double normalisation_ { 1.0 };
};

/// Hard clip, kept for measurement baselines rather than for musical use.
/// Its aliasing figures are the numbers everything else is compared against.
class HardClip
{
public:
    [[nodiscard]] static double evaluate (double x) noexcept
    {
        return x < -1.0 ? -1.0 : (x > 1.0 ? 1.0 : x);
    }

    /// Piecewise: x^2/2 inside the linear region, |x| - 1/2 outside, which is
    /// what makes the two halves join continuously at |x| = 1.
    [[nodiscard]] static double antiderivative (double x) noexcept
    {
        const double absX = std::abs (x);
        return absX <= 1.0 ? 0.5 * x * x : absX - 0.5;
    }
};

} // namespace tezla::dsp
