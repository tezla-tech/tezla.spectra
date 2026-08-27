#pragma once

// A stack of detuned oscillators, which is where a reese comes from.
//
// ---------------------------------------------------------------------------
// A reese is a comb you cannot see
// ---------------------------------------------------------------------------
//
// Two saws a few cents apart drift in and out of phase with each other at the
// difference frequency. Where they agree, harmonics add; where they oppose,
// harmonics cancel. That is a comb filter whose notches sweep -- and the sweep
// rate is the detune, in Hz, at each harmonic. It is the same mechanism as the
// flanger downstream of it, arrived at from the other direction, and stacking
// seven of them is what turns a beat into a churn.
//
// So the detune is the instrument's first tone control, and it is in cents
// rather than Hz on purpose: a fixed cent spread beats faster on high notes
// than low ones, which is what keeps a bass line's movement proportional to
// its pitch instead of turning to mud at the bottom.
//
// ---------------------------------------------------------------------------
// Three things that separate a good stack from seven copies
// ---------------------------------------------------------------------------
//
// **Phases start apart.** Seven oscillators reset to the same phase are one
// oscillator seven times as loud, and they stay that way for as long as it
// takes the detune to pull them apart -- which at 5 cents on a low E is over a
// second. The attack of every note would be a single loud saw. So note-on
// randomises them.
//
// **The sum is normalised for incoherent addition.** N uncorrelated sources sum
// to sqrt(N), not N. Dividing by N makes a seven-voice stack quieter than a
// one-voice one and the control unusable; dividing by sqrt(N) holds the level
// where the ear expects it. Measured in the tests.
//
// **The drift is not the detune.** A real analogue stack is never exactly in
// tune with itself, and never exactly out either -- each oscillator wanders. A
// static detune gives a periodic churn that the ear locks onto within a bar. A
// slow random wander on top of it never repeats, and that is the whole of what
// the "analogue" knob on an old polysynth was doing.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

#include "Oscillator.hpp"

namespace tezla::dsp {

/// A small deterministic generator, so a test can rely on what it produces.
///
/// xorshift64*, which is cheap, has no visible structure at this scale, and --
/// unlike rand() -- gives the same stream on every platform. That last part is
/// what makes "the drift is bounded" a testable claim rather than a hope.
class SmallRandom
{
public:
    explicit SmallRandom (std::uint64_t seed = 0x9e3779b97f4a7c15ull) noexcept
        : state_ (seed | 1ull)
    {
    }

    void seed (std::uint64_t value) noexcept { state_ = value | 1ull; }

    /// Uniform in [0, 1).
    [[nodiscard]] double next() noexcept
    {
        state_ ^= state_ >> 12;
        state_ ^= state_ << 25;
        state_ ^= state_ >> 27;

        const std::uint64_t value = state_ * 0x2545f4914f6cdd1dull;

        // The top 53 bits, which is exactly a double's mantissa.
        return static_cast<double> (value >> 11) * (1.0 / 9007199254740992.0);
    }

    /// Uniform in [-1, 1).
    [[nodiscard]] double bipolar() noexcept { return next() * 2.0 - 1.0; }

private:
    std::uint64_t state_;
};

class UnisonBank
{
public:
    static constexpr int kMaxVoices = 7;

    /// How far the outermost pair sits, as a multiple of the detune control.
    ///
    /// The spread is not linear across the stack. Spacing seven voices evenly
    /// puts most of them close to the centre where they beat slowly and do
    /// little; pushing the outer pairs further gives a wide, dense churn from
    /// the same nominal detune. The exponent below is what shapes that.
    static constexpr double kSpreadExponent = 1.6;

    /// The drift's corner, in Hz. Slow enough to be a wander rather than a
    /// vibrato -- above about 2 Hz it stops sounding like tuning and starts
    /// sounding like modulation, which is what the LFOs are for.
    static constexpr double kDriftHz = 0.35;

    void prepare (double sampleRate) noexcept
    {
        sampleRate_ = sampleRate > 0.0 ? sampleRate : 48000.0;

        // A one-pole at kDriftHz, so the wander is bounded and smooth.
        driftCoefficient_ = std::clamp (6.283185307179586 * kDriftHz / sampleRate_, 0.0, 1.0);

        updateIncrements();
    }

    /// Clears every oscillator and, unless told otherwise, scatters their
    /// phases -- see the header: a stack that starts in phase is one loud saw.
    /// The seed the phase scatter and the drift are drawn from.
    ///
    /// Settable, because **every bank scattering identically defeats the
    /// point**. With one fixed seed, two voices playing the same note start in
    /// exactly the same phase relationship and sum coherently -- +6 dB rather
    /// than the +3 the sqrt(N) normalisation assumes -- and oscillator A and
    /// oscillator B inside one voice drift in lockstep instead of against each
    /// other. Left alone it keeps the value it always had.
    void setSeed (std::uint64_t seed) noexcept { seed_ = seed | 1ull; }

    void reset (bool randomisePhases = true) noexcept
    {
        random_.seed (seed_);

        for (int i = 0; i < kMaxVoices; ++i)
        {
            const auto index = static_cast<std::size_t> (i);

            voices_[index].reset (randomisePhases ? random_.next() : 0.0);
            drift_[index] = 0.0;
            driftTarget_[index] = 0.0;
        }

        driftCountdown_ = 0;
    }

    /// 1 to 7. One voice is a plain oscillator and costs what one costs.
    void setVoiceCount (int count) noexcept
    {
        const int wanted = std::clamp (count, 1, kMaxVoices);

        if (wanted == voiceCount_)
            return;

        voiceCount_ = wanted;
        updateIncrements();
    }

    [[nodiscard]] int getVoiceCount() const noexcept { return voiceCount_; }

    /// Total spread of the outermost pair, in cents.
    void setDetuneCents (double cents) noexcept
    {
        const double wanted = std::clamp (cents, 0.0, 100.0);

        if (isExactly (wanted, detuneCents_))
            return;

        detuneCents_ = wanted;
        updateIncrements();
    }

    /// 0 keeps the stack in the middle; 1 puts the outermost pair hard left and
    /// right. The centre voice, when there is one, stays centred either way.
    void setSpread (double spread) noexcept { spread_ = std::clamp (spread, 0.0, 1.0); }

    /// How far each oscillator is allowed to wander, in cents.
    void setDrift (double cents) noexcept { driftCents_ = std::clamp (cents, 0.0, 50.0); }

    void setFrequency (double hz) noexcept
    {
        const double wanted = std::max (hz, 0.0);

        if (isExactly (wanted, frequency_))
            return;

        frequency_ = wanted;
        updateIncrements();
    }

    [[nodiscard]] double getFrequency() const noexcept { return frequency_; }

    void setShape (OscShape shape) noexcept
    {
        for (auto& voice : voices_)
            voice.setShape (shape);
    }

    void setWidth (double width) noexcept
    {
        for (auto& voice : voices_)
            voice.setWidth (width);
    }

    /// Direct access, so a caller can wire hard sync between two banks voice by
    /// voice -- the master's Nth oscillator driving the slave's Nth.
    [[nodiscard]] Oscillator& voice (int index) noexcept
    {
        return voices_[static_cast<std::size_t> (std::clamp (index, 0, kMaxVoices - 1))];
    }

    /// One sample, into a stereo pair. `phaseMod` is applied to every voice.
    void process (double phaseMod, double& left, double& right) noexcept
    {
        advanceDrift();

        left = 0.0;
        right = 0.0;

        for (int i = 0; i < voiceCount_; ++i)
        {
            const auto index = static_cast<std::size_t> (i);

            // The drift rides on top of the detune, as a further ratio.
            voices_[index].setIncrement (increments_[index] * driftRatio (index));

            const double value = voices_[index].advance (phaseMod);

            left  += value * gainL_[index];
            right += value * gainR_[index];
        }

        left *= normalisation_;
        right *= normalisation_;
    }

    /// What the stack is being scaled by, for tests and for a display.
    [[nodiscard]] double getNormalisation() const noexcept { return normalisation_; }

private:
    /// Where voice `i` sits in [-1, 1] across the stack.
    ///
    /// Symmetric, and shaped by kSpreadExponent so the outer pairs sit further
    /// out than an even spacing would put them.
    [[nodiscard]] double position (int i) const noexcept
    {
        if (voiceCount_ <= 1)
            return 0.0;

        const double linear = 2.0 * static_cast<double> (i)
                                / static_cast<double> (voiceCount_ - 1) - 1.0;

        return std::copysign (std::pow (std::abs (linear), kSpreadExponent), linear);
    }

    void updateIncrements() noexcept
    {
        for (int i = 0; i < kMaxVoices; ++i)
        {
            const auto index = static_cast<std::size_t> (i);
            const double offset = position (i) * detuneCents_ * 0.5;

            // Cents to a frequency ratio, then to cycles per sample.
            const double ratio = std::pow (2.0, offset / 1200.0);

            increments_[index] = frequency_ * ratio / sampleRate_;

            // Equal-power panning, so the stack's width does not change its
            // loudness. The centre voice of an odd stack lands at 0.5/0.5.
            const double pan = position (i) * spread_;
            const double angle = (pan * 0.5 + 0.5) * 1.5707963267948966;

            gainL_[index] = std::cos (angle);
            gainR_[index] = std::sin (angle);
        }

        // sqrt(N), not N: the voices are uncorrelated once their phases have
        // scattered, so that is how their sum actually grows.
        normalisation_ = 1.0 / std::sqrt (static_cast<double> (voiceCount_));
    }

    /// A bounded random walk per voice, refreshed on a timer rather than every
    /// sample -- the target only needs to move slowly, and the one-pole between
    /// here and the pitch does the smoothing.
    void advanceDrift() noexcept
    {
        if (driftCountdown_ <= 0)
        {
            for (int i = 0; i < kMaxVoices; ++i)
                driftTarget_[static_cast<std::size_t> (i)] = random_.bipolar();

            driftCountdown_ = static_cast<int> (sampleRate_ * 0.05);
        }

        --driftCountdown_;

        for (int i = 0; i < kMaxVoices; ++i)
        {
            const auto index = static_cast<std::size_t> (i);

            drift_[index] += driftCoefficient_ * (driftTarget_[index] - drift_[index]);
        }
    }

    [[nodiscard]] double driftRatio (std::size_t index) const noexcept
    {
        if (isExactlyZero (driftCents_))
            return 1.0;

        return std::pow (2.0, drift_[index] * driftCents_ / 1200.0);
    }

    double sampleRate_ { 48000.0 };
    double frequency_  { 0.0 };

    int    voiceCount_   { 1 };
    double detuneCents_  { 0.0 };
    double spread_       { 0.0 };
    double driftCents_   { 0.0 };

    double normalisation_ { 1.0 };

    std::array<Oscillator, kMaxVoices> voices_ {};
    std::array<double, kMaxVoices> increments_ {};
    std::array<double, kMaxVoices> gainL_ {};
    std::array<double, kMaxVoices> gainR_ {};

    std::array<double, kMaxVoices> drift_ {};
    std::array<double, kMaxVoices> driftTarget_ {};
    double driftCoefficient_ { 0.0 };
    int    driftCountdown_   { 0 };

    std::uint64_t seed_ { 0x5bf03635c1e5a2b3ull };
    SmallRandom random_;
};

} // namespace tezla::dsp
