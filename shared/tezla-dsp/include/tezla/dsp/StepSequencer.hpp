#pragma once

// A step sequencer used as a modulation source: sixteen values, a length, a
// glide, and either a free rate or the host's transport.
//
// This exists because of a specific thing the brief asked for -- automating an
// LFO's *rate* so it steps through a pattern of speeds -- which used to be done
// by drawing an automation clip on the rate knob. Pointing this at `lfo rate`
// does it inside the instrument, where it can be tempo-locked and saved with
// the preset. It is equally the thing that draws a comb sweep, a filter
// pattern, or a per-step pitch.
//
// ---------------------------------------------------------------------------
// Two decisions copied from Lfo.hpp, for the same reason
// ---------------------------------------------------------------------------
//
// **The position is assigned from the transport, never accumulated.** A
// running total drifts by a rounding error per block and never corrects, so a
// loop comes back a little further out each pass and a bounce does not match
// what was heard. `setPhaseFromPpq()` derives the position from the host's ppq
// every time it is asked, so bar 33 is bar 1 by construction.
//
// **The output is bipolar**, -1 to +1, so a depth applies symmetrically around
// wherever the target control was set rather than only pushing it one way.
//
// ---------------------------------------------------------------------------
// The glide slides out of a step, not into it
// ---------------------------------------------------------------------------
//
// Which of those it is matters more than it sounds. Gliding *into* a value
// means the output only arrives at step 3 at the moment step 3 ends, so the
// pattern you see and the pattern you hear are half a step apart. Gliding *out*
// holds each value for `1 - glide` of its step and then slides to the next, so
// the output is exactly the step's value at the instant the step begins, at
// every glide setting including 1.0. That is also what a hardware sequencer's
// slide does.
//
// The interpolation is a smoothstep rather than a line, so the joins have no
// corner in them at any setting -- a modulation source with a corner in it puts
// that corner into whatever it is modulating.

#include <algorithm>
#include <array>
#include <cmath>

namespace tezla::dsp {

class StepSequencer
{
public:
    static constexpr int kMaxSteps = 16;

    void prepare (double sampleRate) noexcept
    {
        sampleRate_ = sampleRate > 0.0 ? sampleRate : 48000.0;
        reset();
    }

    /// Back to the start of the pattern. Per-voice retriggering, and what a
    /// transport stop should do.
    void reset() noexcept { position_ = 0.0; }

    // -----------------------------------------------------------------------
    // The pattern
    // -----------------------------------------------------------------------

    void setStep (int index, double value) noexcept
    {
        if (index < 0 || index >= kMaxSteps)
            return;

        steps_[static_cast<std::size_t> (index)] = std::clamp (value, -1.0, 1.0);
    }

    [[nodiscard]] double getStep (int index) const noexcept
    {
        if (index < 0 || index >= kMaxSteps)
            return 0.0;

        return steps_[static_cast<std::size_t> (index)];
    }

    /// How many steps before the pattern repeats, 1 to 16.
    void setLength (int length) noexcept { length_ = std::clamp (length, 1, kMaxSteps); }
    [[nodiscard]] int getLength() const noexcept { return length_; }

    /// 0 is hard steps; 1 is a continuous curve through the step values.
    void setGlide (double glide) noexcept { glide_ = std::clamp (glide, 0.0, 1.0); }
    [[nodiscard]] double getGlide() const noexcept { return glide_; }

    /// Free-running rate, in steps per second. Ignored while the position is
    /// being driven from the transport.
    void setRateHz (double stepsPerSecond) noexcept
    {
        rateHz_ = std::clamp (stepsPerSecond, 0.0, 200.0);
    }

    [[nodiscard]] double getRateHz() const noexcept { return rateHz_; }

    // -----------------------------------------------------------------------
    // Running
    // -----------------------------------------------------------------------

    /// Advances the free-running clock and returns the new output.
    [[nodiscard]] double advance (int numSamples) noexcept
    {
        if (rateHz_ > 0.0)
            position_ += rateHz_ * static_cast<double> (std::max (numSamples, 0)) / sampleRate_;

        return getValue();
    }

    /// Locks the position to the host's transport and returns the new output.
    ///
    /// `stepsPerBeat` is 4 for sixteenths, 2 for eighths, 1 for quarters,
    /// 0.25 for a step per bar in 4/4.
    [[nodiscard]] double setPhaseFromPpq (double ppqPosition, double stepsPerBeat) noexcept
    {
        position_ = ppqPosition * stepsPerBeat;

        return getValue();
    }

    /// Which step is playing, 0 to `length - 1`. For a playhead.
    [[nodiscard]] int getStepIndex() const noexcept
    {
        return indexAt (std::floor (position_));
    }

    /// How far through the current step, 0 to 1. For a playhead.
    [[nodiscard]] double getStepFraction() const noexcept
    {
        return position_ - std::floor (position_);
    }

    /// The output at the current position, -1 to +1.
    [[nodiscard]] double getValue() const noexcept
    {
        const double whole = std::floor (position_);
        const double fraction = position_ - whole;

        const double current = steps_[static_cast<std::size_t> (indexAt (whole))];

        if (glide_ <= 0.0)
            return current;

        // Hold, then slide. `hold` is what is left of the step after the glide
        // has taken its share, so at glide 1 the slide starts immediately and
        // the output is still exactly `current` at fraction 0.
        const double hold = 1.0 - glide_;
        const double t = std::clamp ((fraction - hold) / glide_, 0.0, 1.0);

        if (t <= 0.0)
            return current;

        const double next = steps_[static_cast<std::size_t> (indexAt (whole + 1.0))];

        // Smoothstep: zero slope at both ends, so a fully glided pattern has no
        // corner at a step boundary either.
        return current + t * t * (3.0 - 2.0 * t) * (next - current);
    }

private:
    /// Wraps a step number into the pattern, for negative positions too.
    ///
    /// `std::fmod` keeps the sign of its left operand, so a transport that
    /// reports a negative ppq -- a count-in, or a loop that starts before bar 1
    /// -- would index backwards off the front of the array. Adding the length
    /// before the second fold costs nothing and removes the whole class.
    [[nodiscard]] int indexAt (double step) const noexcept
    {
        const double length = static_cast<double> (length_);
        const double wrapped = std::fmod (std::fmod (step, length) + length, length);

        return std::clamp (static_cast<int> (wrapped), 0, length_ - 1);
    }

    double sampleRate_ { 48000.0 };
    double position_ { 0.0 };
    double rateHz_ { 4.0 };
    double glide_ { 0.0 };

    int length_ { 16 };

    std::array<double, kMaxSteps> steps_ {};
};

} // namespace tezla::dsp
