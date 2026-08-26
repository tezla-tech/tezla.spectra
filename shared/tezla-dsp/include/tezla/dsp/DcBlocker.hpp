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

    /// Moves the corner while audio is flowing, keeping the filter's memory.
    ///
    /// prepare() resets, which is right when the graph is being built and wrong
    /// once samples are running through it. The state here *is* the last input
    /// and the last output: zeroing them makes the next sample come out as `x`
    /// instead of `x - x[n-1] + R*y[n-1]`, which is a step the size of the
    /// previous sample. Once, on a knob turn, that is a tick. Under modulation
    /// it is a tick every chunk -- measured at four times the signal's own
    /// roughness on Emberdrive's expert DC control, which is a continuous,
    /// automatable parameter and had this problem before modulation existed.
    void retune (double sampleRate, Float cutoffHz) noexcept
    {
        sampleRate_ = sampleRate > 0.0 ? sampleRate : 44100.0;
        setCutoff (cutoffHz);
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
