#pragma once

#include <cmath>

namespace tezla::dsp {

/// Linear gain from decibels. Values at or below `floorDb` map to silence, so a
/// fader at its minimum is genuinely off rather than -60 dB of leakage.
template <typename Float>
[[nodiscard]] inline Float dbToGain (Float db, Float floorDb = static_cast<Float>(-100)) noexcept
{
    return db <= floorDb ? Float{}
                         : std::pow (static_cast<Float>(10), db * static_cast<Float>(0.05));
}

/// Decibels from linear gain, clamped at `floorDb` so silence does not produce -inf.
template <typename Float>
[[nodiscard]] inline Float gainToDb (Float gain, Float floorDb = static_cast<Float>(-100)) noexcept
{
    const Float magnitude = std::abs (gain);
    if (magnitude <= Float{})
        return floorDb;

    const Float db = static_cast<Float>(20) * std::log10 (magnitude);
    return db < floorDb ? floorDb : db;
}

} // namespace tezla::dsp
