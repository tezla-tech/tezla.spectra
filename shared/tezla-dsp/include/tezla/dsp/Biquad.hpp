#pragma once

#include <algorithm>
#include <cmath>
#include <complex>
#include <numbers>

#include "Denormals.hpp"

namespace tezla::dsp {

/// Biquad coefficients, normalised so a0 == 1.
template <typename Float = double>
struct BiquadCoefficients
{
    Float b0 { 1 }, b1 {}, b2 {}, a1 {}, a2 {};

    /// Magnitude response at `frequencyHz`, for offline verification. A filter
    /// that is never plotted is a filter nobody has actually checked.
    [[nodiscard]] Float magnitudeAt (Float frequencyHz, double sampleRate) const noexcept
    {
        const auto w = static_cast<Float>(2.0 * std::numbers::pi * static_cast<double>(frequencyHz) / sampleRate);
        const std::complex<Float> z1 { std::cos (-w), std::sin (-w) };
        const std::complex<Float> z2 = z1 * z1;
        const auto numerator   = b0 + b1 * z1 + b2 * z2;
        const auto denominator = Float{1} + a1 * z1 + a2 * z2;
        return std::abs (numerator / denominator);
    }
};

/// RBJ audio-EQ-cookbook coefficient designs.
///
/// All of them take the sample rate explicitly and derive everything from it.
/// Never cache a coefficient computed at one rate and reuse it at another --
/// that is the single easiest way to make a plugin sound different at 48 kHz
/// and 192 kHz, which this repository does not permit.
namespace design {

namespace detail {

template <typename Float>
struct Intermediates
{
    Float cosW {}, alpha {};
};

template <typename Float>
[[nodiscard]] inline Intermediates<Float> prewarp (Float frequencyHz, Float q, double sampleRate) noexcept
{
    // Keep the corner strictly inside the band; a corner at or above Nyquist
    // yields nonsense coefficients rather than an error.
    const auto nyquist = static_cast<Float>(sampleRate * 0.5);
    const auto f = std::min (std::max (frequencyHz, static_cast<Float>(1)), nyquist * static_cast<Float>(0.999));
    const auto safeQ = std::max (q, static_cast<Float>(0.001));

    const auto w = static_cast<Float>(2.0 * std::numbers::pi * static_cast<double>(f) / sampleRate);
    return { std::cos (w), std::sin (w) / (static_cast<Float>(2) * safeQ) };
}

template <typename Float>
[[nodiscard]] inline BiquadCoefficients<Float> normalise (Float b0, Float b1, Float b2,
                                                          Float a0, Float a1, Float a2) noexcept
{
    const Float inverseA0 = Float{1} / a0;
    return { b0 * inverseA0, b1 * inverseA0, b2 * inverseA0, a1 * inverseA0, a2 * inverseA0 };
}

} // namespace detail

template <typename Float = double>
[[nodiscard]] inline BiquadCoefficients<Float> lowpass (Float frequencyHz, Float q, double sampleRate) noexcept
{
    const auto [cosW, alpha] = detail::prewarp (frequencyHz, q, sampleRate);
    const Float b1 = Float{1} - cosW;
    return detail::normalise (b1 * static_cast<Float>(0.5), b1, b1 * static_cast<Float>(0.5),
                              Float{1} + alpha, static_cast<Float>(-2) * cosW, Float{1} - alpha);
}

template <typename Float = double>
[[nodiscard]] inline BiquadCoefficients<Float> highpass (Float frequencyHz, Float q, double sampleRate) noexcept
{
    const auto [cosW, alpha] = detail::prewarp (frequencyHz, q, sampleRate);
    const Float onePlusCos = Float{1} + cosW;
    return detail::normalise (onePlusCos * static_cast<Float>(0.5), -onePlusCos, onePlusCos * static_cast<Float>(0.5),
                              Float{1} + alpha, static_cast<Float>(-2) * cosW, Float{1} - alpha);
}

/// Constant 0 dB peak gain bandpass.
template <typename Float = double>
[[nodiscard]] inline BiquadCoefficients<Float> bandpass (Float frequencyHz, Float q, double sampleRate) noexcept
{
    const auto [cosW, alpha] = detail::prewarp (frequencyHz, q, sampleRate);
    return detail::normalise (alpha, Float{}, -alpha,
                              Float{1} + alpha, static_cast<Float>(-2) * cosW, Float{1} - alpha);
}

template <typename Float = double>
[[nodiscard]] inline BiquadCoefficients<Float> notch (Float frequencyHz, Float q, double sampleRate) noexcept
{
    const auto [cosW, alpha] = detail::prewarp (frequencyHz, q, sampleRate);
    return detail::normalise (Float{1}, static_cast<Float>(-2) * cosW, Float{1},
                              Float{1} + alpha, static_cast<Float>(-2) * cosW, Float{1} - alpha);
}

template <typename Float = double>
[[nodiscard]] inline BiquadCoefficients<Float> allpass (Float frequencyHz, Float q, double sampleRate) noexcept
{
    const auto [cosW, alpha] = detail::prewarp (frequencyHz, q, sampleRate);
    return detail::normalise (Float{1} - alpha, static_cast<Float>(-2) * cosW, Float{1} + alpha,
                              Float{1} + alpha, static_cast<Float>(-2) * cosW, Float{1} - alpha);
}

template <typename Float = double>
[[nodiscard]] inline BiquadCoefficients<Float> peak (Float frequencyHz, Float q, Float gainDb, double sampleRate) noexcept
{
    const auto [cosW, alpha] = detail::prewarp (frequencyHz, q, sampleRate);
    const Float a = std::pow (static_cast<Float>(10), gainDb / static_cast<Float>(40));
    return detail::normalise (Float{1} + alpha * a, static_cast<Float>(-2) * cosW, Float{1} - alpha * a,
                              Float{1} + alpha / a, static_cast<Float>(-2) * cosW, Float{1} - alpha / a);
}

template <typename Float = double>
[[nodiscard]] inline BiquadCoefficients<Float> lowShelf (Float frequencyHz, Float q, Float gainDb, double sampleRate) noexcept
{
    const auto [cosW, alpha] = detail::prewarp (frequencyHz, q, sampleRate);
    const Float a       = std::pow (static_cast<Float>(10), gainDb / static_cast<Float>(40));
    const Float twoSqrtA = static_cast<Float>(2) * std::sqrt (a) * alpha;
    const Float aPlus1  = a + Float{1};
    const Float aMinus1 = a - Float{1};

    return detail::normalise (a * (aPlus1 - aMinus1 * cosW + twoSqrtA),
                              static_cast<Float>(2) * a * (aMinus1 - aPlus1 * cosW),
                              a * (aPlus1 - aMinus1 * cosW - twoSqrtA),
                              aPlus1 + aMinus1 * cosW + twoSqrtA,
                              static_cast<Float>(-2) * (aMinus1 + aPlus1 * cosW),
                              aPlus1 + aMinus1 * cosW - twoSqrtA);
}

template <typename Float = double>
[[nodiscard]] inline BiquadCoefficients<Float> highShelf (Float frequencyHz, Float q, Float gainDb, double sampleRate) noexcept
{
    const auto [cosW, alpha] = detail::prewarp (frequencyHz, q, sampleRate);
    const Float a       = std::pow (static_cast<Float>(10), gainDb / static_cast<Float>(40));
    const Float twoSqrtA = static_cast<Float>(2) * std::sqrt (a) * alpha;
    const Float aPlus1  = a + Float{1};
    const Float aMinus1 = a - Float{1};

    return detail::normalise (a * (aPlus1 + aMinus1 * cosW + twoSqrtA),
                              static_cast<Float>(-2) * a * (aMinus1 + aPlus1 * cosW),
                              a * (aPlus1 + aMinus1 * cosW - twoSqrtA),
                              aPlus1 - aMinus1 * cosW + twoSqrtA,
                              static_cast<Float>(2) * (aMinus1 - aPlus1 * cosW),
                              aPlus1 - aMinus1 * cosW - twoSqrtA);
}

} // namespace design

/// Transposed direct form II biquad. TDF-II is the form to use here: it has the
/// best numerical behaviour of the direct forms for time-invariant coefficients
/// and only needs two state variables.
template <typename Float = double>
class Biquad
{
public:
    void setCoefficients (const BiquadCoefficients<Float>& c) noexcept { c_ = c; }
    [[nodiscard]] const BiquadCoefficients<Float>& getCoefficients() const noexcept { return c_; }

    void reset() noexcept { s1_ = s2_ = Float{}; }

    [[nodiscard]] Float process (Float x) noexcept
    {
        const Float y = c_.b0 * x + s1_;
        s1_ = snapToZero (c_.b1 * x - c_.a1 * y + s2_);
        s2_ = snapToZero (c_.b2 * x - c_.a2 * y);
        return y;
    }

    void process (Float* samples, int numSamples) noexcept
    {
        for (int i = 0; i < numSamples; ++i)
            samples[i] = process (samples[i]);
    }

private:
    BiquadCoefficients<Float> c_ {};
    Float s1_ {}, s2_ {};
};

} // namespace tezla::dsp
