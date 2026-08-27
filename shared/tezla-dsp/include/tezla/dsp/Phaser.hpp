#pragma once

// An allpass cascade -- the comb's smoother sibling.
//
// ---------------------------------------------------------------------------
// Why both this and Comb
// ---------------------------------------------------------------------------
//
// A flanger's notches are harmonically spaced: at 1/(2D), 3/(2D), 5/(2D) and so
// on forever, because a delay shifts every frequency by the same *time*. That
// even spacing is what makes a flange sound metallic, and it is exactly what
// you want when the comb is key-tracked onto a note's harmonic series.
//
// A phaser's notches are not evenly spaced, because a first-order allpass
// shifts every frequency by a different *phase* -- 0 degrees at DC, 90 at its
// corner, 180 at Nyquist. Stack N of them and the notches bunch around the
// corner and thin out either side. The result is vocal rather than metallic,
// and it is what the brief's second trick was for: the same rate-at-zero,
// depth-as-a-control idea applied to something that does not ring.
//
// So the two share a control surface and neither replaces the other:
//
//     FLANGE   harmonic notches      metallic, tunable, rings with feedback
//     PHASE    uneven notches        vocal, smooth, sweeps without clanging
//
// ---------------------------------------------------------------------------
// How many notches
// ---------------------------------------------------------------------------
//
// Summed with the dry, the cascade nulls wherever `A(f)^N = -1`. Each stage
// contributes up to 180 degrees, so N stages sweep N * 180 degrees from DC to
// Nyquist and cross an odd multiple of 180 exactly **N/2** times. Two stages,
// one notch; sixteen stages, eight. Measured in tests/test_Phaser.cpp.
//
// The stages are TPT one-poles rather than direct-form biquads, for the reason
// SvfFilter.hpp sets out at length: the corner is prewarped, so it is where it
// was asked for at every sample rate. A phaser whose notches move between a
// 48 kHz session and a 96 kHz one is a different plugin on each.

#include <algorithm>
#include <array>
#include <cmath>

#include "Exact.hpp"

namespace tezla::dsp {

class Phaser
{
public:
    /// The cascade's length. Two stages is one notch; sixteen is eight, which
    /// is past the point where adding more is audible as anything but phase.
    static constexpr int kMinimumStages = 2;
    static constexpr int kMaximumStages = 16;

    /// Where the notches can be centred.
    ///
    /// The bottom is low enough to phase a sub, the top high enough to reach
    /// the air band -- and clamped against Nyquist as well, because the prewarp
    /// is a tangent.
    static constexpr double kMinimumHz = 20.0;
    static constexpr double kMaximumHz = 18000.0;
    static constexpr double kMaximumCutoffFraction = 0.49;

    /// Feedback around the cascade. The loop gain is `feedback * A^N`, and an
    /// allpass has unity magnitude at every frequency, so the loop gain *is*
    /// the feedback -- there is no frequency at which it is smaller. That makes
    /// unity a genuine oscillator rather than an unlikely one, and 0.9 a peak
    /// of 1/(1 - 0.9) = 20 dB.
    static constexpr double kMaximumFeedback = 0.9;

    /// L/R corner offset, as a ratio. A ratio rather than an offset in hertz,
    /// so the two channels stay the same musical interval apart as the phaser
    /// sweeps -- the same reasoning as Comb's delay spread.
    static constexpr double kMaximumSpread = 0.30;

    void prepare (double sampleRate) noexcept
    {
        sampleRate_ = sampleRate > 0.0 ? sampleRate : 48000.0;
        reset();
    }

    void reset() noexcept
    {
        for (auto& channel : channels_)
        {
            channel.states.fill (0.0);
            channel.lastOutput = 0.0;
        }
    }

    // -----------------------------------------------------------------------
    // Controls
    // -----------------------------------------------------------------------

    void setFrequencyHz (double hz) noexcept
    {
        frequencyHz_ = std::clamp (hz, kMinimumHz, kMaximumHz);
    }

    [[nodiscard]] double getFrequencyHz() const noexcept { return frequencyHz_; }

    void setStages (int stages) noexcept
    {
        stages_ = std::clamp (stages, kMinimumStages, kMaximumStages);
    }

    [[nodiscard]] int getStages() const noexcept { return stages_; }

    /// How many notches the current stage count produces. `stages / 2`.
    [[nodiscard]] int notchCount() const noexcept { return stages_ / 2; }

    /// -1 to +1. Negative moves the notches, as it does on the comb.
    void setFeedback (double feedback) noexcept
    {
        feedback_ = std::clamp (feedback, -1.0, 1.0) * kMaximumFeedback;
    }

    [[nodiscard]] double getFeedback() const noexcept { return feedback_; }

    void setWetInverted (bool inverted) noexcept { wetInverted_ = inverted; }
    [[nodiscard]] bool isWetInverted() const noexcept { return wetInverted_; }

    /// 0 is bit-exactly transparent, 1 is the deepest notches.
    void setMix (double mix) noexcept { mix_ = std::clamp (mix, 0.0, 1.0); }
    [[nodiscard]] double getMix() const noexcept { return mix_; }

    void setSpread (double spread) noexcept { spread_ = std::clamp (spread, 0.0, 1.0); }
    [[nodiscard]] double getSpread() const noexcept { return spread_; }

    // -----------------------------------------------------------------------
    // Running
    // -----------------------------------------------------------------------

    void process (double& left, double& right) noexcept
    {
        const double offset = spread_ * kMaximumSpread;

        left = processChannel (channels_[0], left, coefficientFor (frequencyHz_ * (1.0 - offset)));
        right = processChannel (channels_[1], right, coefficientFor (frequencyHz_ * (1.0 + offset)));
    }

    /// The allpass coefficient the current settings produce, for a test that
    /// wants to predict the response rather than read it back.
    [[nodiscard]] double coefficientFor (double hz) const noexcept
    {
        const double limit = sampleRate_ * kMaximumCutoffFraction;
        const double clamped = std::clamp (hz, kMinimumHz, std::min (kMaximumHz, limit));

        const double g = std::tan (3.141592653589793 * clamped / sampleRate_);

        return (g - 1.0) / (g + 1.0);
    }

private:
    struct Channel
    {
        std::array<double, kMaximumStages> states {};
        double lastOutput { 0.0 };
    };

    /// One first-order TPT allpass.
    ///
    ///     AP(z) = (c + z^-1) / (1 + c * z^-1),   c = (g - 1) / (g + 1)
    ///
    /// which has unity magnitude at every frequency for |c| < 1 -- exactly,
    /// not approximately, which is why the notches are complete nulls rather
    /// than deep dips. The test asserts that by measuring the null depth.
    [[nodiscard]] static double allpass (double& state, double input, double c) noexcept
    {
        const double output = c * input + state;

        state = input - c * output;

        return output;
    }

    [[nodiscard]] double processChannel (Channel& channel, double input, double c) noexcept
    {
        double value = input + feedback_ * channel.lastOutput;

        for (int stage = 0; stage < stages_; ++stage)
            value = allpass (channel.states[static_cast<std::size_t> (stage)], value, c);

        channel.lastOutput = value;

        // A fast path, not the mechanism -- `input + 0.0 * value` is already
        // `input` bit for bit. See the same note in Comb.hpp.
        if (isExactlyZero (mix_))
            return input;

        return input + (wetInverted_ ? -mix_ : mix_) * value;
    }

    double sampleRate_ { 48000.0 };

    double frequencyHz_ { 800.0 };
    double feedback_ { 0.0 };
    double mix_ { 0.0 };
    double spread_ { 0.0 };

    int stages_ { 4 };
    bool wetInverted_ { false };

    Channel channels_[2];
};

} // namespace tezla::dsp
