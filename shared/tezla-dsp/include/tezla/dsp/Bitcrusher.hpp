#pragma once

// Bit-depth and sample-rate reduction.
//
// These are the one part of this plugin that must NOT be antialiased, and must
// NOT run inside the oversampled section. Everywhere else the aliasing is a
// defect to be suppressed; here it is the instrument. A bit crusher running at
// 8x with an antialiased quantiser sounds like a slightly noisy version of the
// input -- the harsh, ringing, obviously-digital character people actually want
// from one comes precisely from the folded-back images.
//
// So: run these at the host's own rate, after the oversampled block has been
// decimated, and leave the artefacts alone.

#include <algorithm>
#include <cmath>

namespace tezla::dsp {

/// Quantises to a reduced bit depth.
class Bitcrusher
{
public:
    static constexpr double kMaxBits = 16.0;
    static constexpr double kMinBits = 1.0;

    /// 0 is bypassed exactly. Above 0 the depth falls from 16 bits to 1.
    void setAmount (double amount) noexcept
    {
        amount_ = std::clamp (amount, 0.0, 1.0);
        bypassed_ = amount_ <= 0.0;

        const double bits = kMaxBits - amount_ * (kMaxBits - kMinBits);

        // Fractional bit depths are meaningful and make the control smooth to
        // automate: the step size is continuous even though "bits" is not.
        levels_ = std::pow (2.0, bits - 1.0);
        inverseLevels_ = 1.0 / levels_;
    }

    [[nodiscard]] double getBits() const noexcept
    {
        return kMaxBits - amount_ * (kMaxBits - kMinBits);
    }

    [[nodiscard]] double process (double x) const noexcept
    {
        if (bypassed_)
            return x;

        return std::round (x * levels_) * inverseLevels_;
    }

private:
    double amount_        { 0.0 };
    double levels_        { 32768.0 };
    double inverseLevels_ { 1.0 / 32768.0 };
    bool   bypassed_      { true };
};

/// Sample-and-hold rate reduction.
///
/// Holds each input for `ratio` samples, so the signal behaves as if it were
/// running at sampleRate / ratio. Fractional ratios work and are worth having:
/// the artefacts move continuously rather than jumping between integer
/// divisions, which matters if you automate it.
class Downsampler
{
public:
    static constexpr double kMaxRatio = 64.0;

    /// 1 is bypassed exactly.
    void setRatio (double ratio) noexcept
    {
        ratio_ = std::clamp (ratio, 1.0, kMaxRatio);
        bypassed_ = ratio_ <= 1.0;
    }

    [[nodiscard]] double getRatio() const noexcept { return ratio_; }

    void reset() noexcept
    {
        counter_ = 0.0;
        held_    = 0.0;
        primed_  = false;
    }

    [[nodiscard]] double process (double x) noexcept
    {
        if (bypassed_)
            return x;

        if (! primed_)
        {
            held_ = x;
            primed_ = true;
        }

        counter_ += 1.0;
        if (counter_ >= ratio_)
        {
            counter_ -= ratio_;
            held_ = x;
        }

        return held_;
    }

private:
    double ratio_    { 1.0 };
    double counter_  { 0.0 };
    double held_     { 0.0 };
    bool   primed_   { false };
    bool   bypassed_ { true };
};

} // namespace tezla::dsp
