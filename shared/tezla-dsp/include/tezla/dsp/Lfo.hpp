#pragma once

// A modulation LFO: seven waveforms, free-running or locked to the host's
// transport.
//
// Two decisions here are worth more than the waveform table, because both are
// about the same thing -- whether the same bar of music sounds the same twice.
//
//   phase from the transport, not from a clock
//
//     The obvious way to sync is to convert tempo to a frequency and keep
//     accumulating. That drifts: a rounding error per sample never corrects
//     itself, so a loop is a little further out on each pass and a bounce does
//     not match what was heard. setPhaseFromPpq() instead *derives* the phase
//     from the host's ppq position every time it is asked, so bar 33 is
//     identical to bar 1 by construction and there is nothing to drift.
//
//   random values hashed from the cycle number, not drawn from a sequence
//
//     Sample & hold with a running generator gives a different sequence every
//     pass through a loop, which is fine on a synth and useless on a bus
//     effect you are trying to bounce. Each cycle's value is a hash of *which*
//     cycle it is, so the sequence is a property of the timeline rather than of
//     how long the plugin has been running.
//
// Output is bipolar, -1 to +1, so a depth of +0.2 wobbles a control around
// where it was set rather than only pushing it upwards.

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <numbers>

namespace tezla::dsp {

class Lfo
{
public:
    enum class Wave
    {
        sine = 0,
        triangle,
        sawUp,
        sawDown,
        square,
        sampleHold,     ///< a new random level each cycle, held
        smoothRandom    ///< the same levels, interpolated instead of stepped
    };

    static constexpr int kNumWaves = 7;

    void prepare (double sampleRate) noexcept
    {
        sampleRate_ = sampleRate > 0.0 ? sampleRate : 44100.0;
        reset();
    }

    void reset() noexcept
    {
        totalPhase_  = 0.0;
        smoothed_    = 0.0;
        accumulator_ = 0;
        primed_      = false;
    }

    void setWave (Wave wave) noexcept { wave_ = wave; }
    [[nodiscard]] Wave getWave() const noexcept { return wave_; }

    /// The absolute ceiling on the free-running rate.
    ///
    /// A sanity bound rather than a musical one -- it is here so a runaway
    /// modulation of the rate cannot produce a phase increment that swamps the
    /// accumulator, not to say what is useful. **The binding limit is the
    /// caller's**, and it is lower: an LFO is read at whatever the consumer's
    /// control rate is, and above half of that the output aliases to something
    /// other than what the knob says. A caller reading this every 32 samples at
    /// 48 kHz has 750 Hz to play with; one reading it per sample has the whole
    /// range. Sonitus derives its own clamp from its control rate for exactly
    /// this reason.
    static constexpr double kMaximumRateHz = 1000.0;

    /// Free-running rate. Ignored while the phase is being driven from the
    /// transport.
    void setRateHz (double hz) noexcept { rateHz_ = std::clamp (hz, 0.0, kMaximumRateHz); }
    [[nodiscard]] double getRateHz() const noexcept { return rateHz_; }

    /// Rotates the waveform without moving the clock, 0 to 1 of a cycle.
    void setPhaseOffset (double offset) noexcept
    {
        phaseOffset_ = offset - std::floor (offset);
    }

    /// Rounds the output, 0 to 1.
    ///
    /// The corner is a fraction of the LFO's own period rather than a fixed
    /// frequency, so "half smooth" means the same amount of rounding at 0.1 Hz
    /// as at 10 Hz. A fixed corner would do nothing at the bottom of the range
    /// and swallow the waveform whole at the top.
    void setSmooth (double amount) noexcept { smooth_ = std::clamp (amount, 0.0, 1.0); }

    /// Advances the free-running clock and returns the new output.
    [[nodiscard]] double advance (int numSamples) noexcept
    {
        int remaining = std::max (numSamples, 0);

        while (remaining > 0)
        {
            const int take = std::min (kSmoothStepSamples - accumulator_, remaining);

            if (rateHz_ > 0.0)
                totalPhase_ += rateHz_ * static_cast<double> (take) / sampleRate_;

            accumulator_ += take;
            remaining    -= take;

            if (accumulator_ >= kSmoothStepSamples)
            {
                accumulator_ = 0;
                stepSmoother (currentTarget());
            }
        }

        return settle();
    }

    /// Locks the phase to the host's transport and returns the new output.
    ///
    /// `cyclesPerBeat` is the reciprocal of the note division: 4 for a
    /// sixteenth, 1 for a quarter, 0.25 for a whole bar in 4/4.
    ///
    /// The phase is *assigned*, never accumulated, which is the whole point --
    /// there is no running total to drift, so the same bar is the same phase
    /// however the transport arrived there. The cycle count comes out of the
    /// same number, so the random waveforms repeat with the loop too.
    [[nodiscard]] double setPhaseFromPpq (double ppqPosition, double cyclesPerBeat,
                                          int numSamples) noexcept
    {
        totalPhase_ = ppqPosition * cyclesPerBeat;

        // The phase is assigned rather than walked, so there are no intermediate
        // positions to evaluate -- but the smoother still has to advance by the
        // elapsed time, and by the same number of steps however the host cut the
        // block up.
        int remaining = std::max (numSamples, 0);
        const double target = currentTarget();

        while (remaining > 0)
        {
            const int take = std::min (kSmoothStepSamples - accumulator_, remaining);

            accumulator_ += take;
            remaining    -= take;

            if (accumulator_ >= kSmoothStepSamples)
            {
                accumulator_ = 0;
                stepSmoother (target);
            }
        }

        return settle();
    }

    /// Where in the current cycle the LFO is, 0 to 1. For a display.
    [[nodiscard]] double getPhase() const noexcept
    {
        const double p = totalPhase_ + phaseOffset_;
        return p - std::floor (p);
    }

    [[nodiscard]] double getValue() const noexcept { return smoothed_; }

    /// The waveform alone, unsmoothed, at a given phase. Public so a display can
    /// draw the shape without running the clock.
    [[nodiscard]] static double shapeAt (Wave wave, double phase) noexcept
    {
        const double p = phase - std::floor (phase);

        switch (wave)
        {
            case Wave::sine:     return std::sin (2.0 * std::numbers::pi_v<double> * p);
            case Wave::triangle: return 1.0 - 4.0 * std::abs (p - 0.5);
            case Wave::sawUp:    return 2.0 * p - 1.0;
            case Wave::sawDown:  return 1.0 - 2.0 * p;
            case Wave::square:   return p < 0.5 ? 1.0 : -1.0;

            // Which cycle we are in, not how many have gone by: the same bar
            // gives the same value however the transport got there.
            case Wave::sampleHold:
                return randomForCycle (static_cast<std::int64_t> (std::floor (phase)));

            case Wave::smoothRandom:
            {
                const auto cycle = static_cast<std::int64_t> (std::floor (phase));
                const double a = randomForCycle (cycle);
                const double b = randomForCycle (cycle + 1);

                // Smoothstep, so the joins have no corner in them and the
                // result reads as drift rather than as a series of ramps.
                const double t = p * p * (3.0 - 2.0 * p);
                return a + t * (b - a);
            }
        }

        return 0.0;
    }

private:
    /// How often the smoother takes a step, in samples.
    ///
    /// Fixed in samples rather than tied to the caller's chunk, and this is the
    /// difference between a smoothing control that means something and one that
    /// does not. Advancing a one-pole once per call in closed form looks right
    /// and is not: it treats the target as constant across the chunk, and the
    /// target is what is moving. Measured, that made a 64-sample buffer and a
    /// 512-sample one disagree by 3% of full scale on *every* waveform, sine
    /// included -- so it was the approximation, not any discontinuity.
    ///
    /// Sixteen samples is a 3 kHz update rate at 48 kHz, far above anything an
    /// LFO does, and the leftover is carried between calls so the step lands at
    /// the same absolute sample position however the host blocks.
    static constexpr int kSmoothStepSamples = 16;

    /// A cycle index turned into a level in [-1, 1].
    ///
    /// splitmix64's finaliser: cheap, and its output is uncorrelated between
    /// adjacent inputs, which a linear congruential step is not -- consecutive
    /// cycles from one of those drift in one direction and sound like a ramp.
    [[nodiscard]] static double randomForCycle (std::int64_t cycle) noexcept
    {
        auto z = static_cast<std::uint64_t> (cycle) + 0x9e3779b97f4a7c15ULL;
        z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ULL;
        z = (z ^ (z >> 27)) * 0x94d049bb133111ebULL;
        z ^= z >> 31;

        // 53 bits into [0, 1), then to [-1, 1].
        const double unit = static_cast<double> (z >> 11) * (1.0 / 9007199254740992.0);
        return 2.0 * unit - 1.0;
    }

    [[nodiscard]] double currentTarget() const noexcept
    {
        // The whole phase, integer part included. The periodic waveforms take
        // its fraction; the random ones hash its integer part, which is what
        // makes them a property of the timeline rather than of how long the
        // plugin has been running.
        return shapeAt (wave_, totalPhase_ + phaseOffset_);
    }

    void stepSmoother (double target) noexcept
    {
        if (! primed_)
        {
            smoothed_ = target;
            primed_   = true;
            return;
        }

        // A one-pole whose time constant is a fraction of the cycle, so "half
        // smooth" means the same rounding at 0.1 Hz as at 10 Hz.
        const double period = rateHz_ > 0.0 ? 1.0 / rateHz_ : 1.0;
        const double tau = std::max (smooth_ * 0.25 * period, 1.0e-6);
        const double alpha = 1.0 - std::exp (-static_cast<double> (kSmoothStepSamples)
                                             / (tau * sampleRate_));

        smoothed_ += alpha * (target - smoothed_);
    }

    /// Finishes a call.
    ///
    /// The first call after a reset lands on the waveform rather than sliding
    /// to it from zero, whatever the smoothing is set to: a plugin that has just
    /// loaded should have its LFO where the phase says, not ramping up from
    /// nothing. Getting that wrong put a full-scale step at the start of every
    /// smoothed run, which is exactly the click a smoothing control exists to
    /// prevent.
    [[nodiscard]] double settle() noexcept
    {
        if (smooth_ <= 0.0 || ! primed_)
        {
            smoothed_ = currentTarget();
            primed_   = true;
        }

        return smoothed_;
    }

    double sampleRate_  { 44100.0 };
    Wave   wave_        { Wave::sine };
    double rateHz_      { 1.0 };
    double phaseOffset_ { 0.0 };
    double smooth_      { 0.0 };

    /// Unbounded rather than wrapped to [0, 1), because the random waveforms
    /// need to know *which* cycle this is. At 1 Hz that is 86400 a day, which
    /// leaves a double most of its mantissa.
    double totalPhase_  { 0.0 };
    double smoothed_    { 0.0 };
    int    accumulator_ { 0 };
    bool   primed_      { false };
};

} // namespace tezla::dsp
