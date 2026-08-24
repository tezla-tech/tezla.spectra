#pragma once

#include <cmath>
#include <numbers>

#include "Denormals.hpp"

namespace tezla::dsp {

/// First-order DC blocker: y[n] = x[n] - x[n-1] + R * y[n-1]
///
/// Asymmetric nonlinearities are what produce even-order harmonics, which is
/// most of what "tube warmth" means -- but asymmetry also produces a DC offset
/// that grows with drive. Left alone it eats headroom, shifts the operating
/// point of the next stage, and thumps on bypass.
///
/// The cutoff has to be low. This music lives at 40 Hz and below, and a DC
/// blocker set at 30 Hz will audibly thin a sub. 5-20 Hz, first order only.
template <typename Float = double>
class DcBlocker
{
public:
    void prepare (double sampleRate, Float cutoffHz = static_cast<Float>(10)) noexcept
    {
        sampleRate_ = sampleRate > 0.0 ? sampleRate : 44100.0;
        setCutoff (cutoffHz);
        reset();
    }

    void setCutoff (Float cutoffHz) noexcept
    {
        constexpr auto twoPi = static_cast<Float>(2.0 * std::numbers::pi);
        r_ = std::exp (-twoPi * cutoffHz / static_cast<Float>(sampleRate_));
    }

    void reset() noexcept
    {
        previousInput_  = Float{};
        previousOutput_ = Float{};
    }

    [[nodiscard]] Float process (Float x) noexcept
    {
        const Float y = x - previousInput_ + r_ * previousOutput_;
        previousInput_  = x;
        previousOutput_ = snapToZero (y);
        return y;
    }

private:
    double sampleRate_     { 44100.0 };
    Float  r_              { static_cast<Float>(0.9995) };
    Float  previousInput_  {};
    Float  previousOutput_ {};
};

} // namespace tezla::dsp
