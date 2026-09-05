// Copyright (c) 2026 The Tezla <thetezla@proton.me>
// Created by The Tezla -- https://github.com/wingit33/tezla.tech
// Music: https://soundcloud.com/thetezla | https://thetezla.bandcamp.com
// Built with development assistance from Claude (Anthropic).
// SPDX-License-Identifier: AGPL-3.0-only
// GNU AGPLv3 (see LICENSE), plus NOTICE.md's attribution term. Keep intact.

#pragma once

// The growl as rhythm: sixteen steps whose value is a **ratio**.
//
// ---------------------------------------------------------------------------
// Why this is not just an LFO pointed at a ratio knob
// ---------------------------------------------------------------------------
//
// In FM the ratio *is* the interval -- sidebands land at the carrier plus and
// minus whole multiples of the modulator -- so stepping a modulator's ratio
// does not sweep a timbre, it swaps one harmonic identity for another. A saw
// LFO on the same control would spend most of its time between identities,
// which is the one place the sound is neither.
//
// Two rules follow, and both were learned elsewhere in this repository:
//
//  - **A ratio change never resets the phase accumulator.** It is a frequency
//    step, not a retrigger: the phase stays continuous and the spectrum jumps,
//    which is the point. Glide interpolates the ratio, not the phase.
//  - **The caller cuts its sample loop at the step boundary**, not at the
//    callback's. `dsp::StepSequencer::samplesToNextStep` exists for that, and
//    `StrydaEngine` uses it. Reading a step once per block would make the
//    output depend on the host's buffer size, and no arrangement of a per-call
//    timer fixes it (CLAUDE.md section 7).

#include <algorithm>
#include <cmath>

#include <tezla/dsp/Divisions.hpp>
#include <tezla/dsp/Ratio.hpp>
#include <tezla/dsp/StepSequencer.hpp>
#include <tezla/dsp/Tuning.hpp>

namespace tezla::stryda {

namespace dsp = tezla::dsp;

/// How an operator's ratio is quantised. **Append-only**: the choice parameter
/// stores an index, and `free` must stay 0 so a project saved before F6
/// reopens with its ratios untouched.
enum class RatioMode
{
    free = 0,     ///< continuous, exactly as set
    harmonic,     ///< the nearest simple p:q -- Chowning's N1:N2
    scale         ///< the nearest degree of the loaded scale, octave-extended
};

/// The quantiser, shared by the operator ratios and by the sequencer's steps
/// so a sequenced jump lands in the same places a knob does.
///
/// **Free returns the input bit-for-bit**, not a value that happens to equal
/// it: the mode is the default and every existing patch runs through here.
[[nodiscard]] inline double resolveRatio (double ratio, RatioMode mode,
                                          const dsp::Scale& scale) noexcept
{
    switch (mode)
    {
        case RatioMode::free:
            return ratio;

        case RatioMode::harmonic:
        {
            // Sixteen terms covers four octaves, which is where FM ratios stop
            // being thought of as ratios. Past that the match fails and the
            // continuous value is the honest answer -- a snap to nothing is
            // worse than no snap.
            //
            // **The tolerance is deliberately unreachable.** `nearestRatio`
            // returns the SIMPLEST p:q inside the tolerance, falling back to
            // the nearest one when none qualifies -- so a wide tolerance makes
            // everything qualify and 3.49 snaps to 3/1 rather than to 7/2. A
            // quantiser wants the nearest, always, so the fallback branch is
            // the one to take. Found by the test, which asked for 7/2 and got
            // a fifth below it.
            const auto match = dsp::nearestRatio (ratio, 16, 0.0);

            if (match.numerator <= 0 || match.denominator <= 0)
                return ratio;

            return static_cast<double> (match.numerator)
                 / static_cast<double> (match.denominator);
        }

        case RatioMode::scale:
            return scale.snapRatio (ratio);
    }

    return ratio;
}

/// Sixteen steps of ratio, free-running or locked to the transport.
class RatioSequencer
{
public:
    static constexpr int kMaxSteps = dsp::StepSequencer::kMaxSteps;

    void prepare (double sampleRate) noexcept
    {
        sampleRate_ = sampleRate > 0.0 ? sampleRate : 48000.0;
        steps_.prepare (sampleRate_);
        reset();
    }

    void reset() noexcept { steps_.reset(); }

    void setEnabled (bool enabled) noexcept { enabled_ = enabled; }
    [[nodiscard]] bool isEnabled() const noexcept { return enabled_; }

    /// Which operator the pattern drives, 0-based. Out of range disables it,
    /// which is how "no destination" is spelt without a magic value.
    void setTarget (int op) noexcept { target_ = op; }
    [[nodiscard]] int getTarget() const noexcept { return target_; }

    void setLength (int length) noexcept { steps_.setLength (length); }
    void setGlide (double glide) noexcept { steps_.setGlide (glide); }

    /// The lowest and highest ratio a step can hold. The same range as the
    /// operator Ratio parameter, so a sequenced ratio can reach anywhere a
    /// hand-set one can.
    static constexpr double kMinRatio = 0.25;
    static constexpr double kMaxRatio = 32.0;

    /// A step's ratio.
    ///
    /// ---------------------------------------------------------------------
    /// **Stored as a log, and not because logs are elegant**
    /// ---------------------------------------------------------------------
    ///
    /// `dsp::StepSequencer` is a modulation sequencer: `setStep` **clamps to
    /// -1..1**, because that is what a step driving a depth or a pan means.
    /// Handing it a ratio of 4 stores 1, and the first version of this class
    /// did exactly that -- every step above unity collapsed to unity and the
    /// sequencer was silently inert. It passed a block-size test, a
    /// phase-continuity test and an is-it-enabled test before a test that
    /// asked *where* the jump landed finally caught it.
    ///
    /// So the ratio is normalised into the range the sequencer is built for,
    /// logarithmically: `n = (log2(r) - centre) / halfSpan`. That is not merely
    /// a way to fit -- **glide then interpolates in the log domain**, which is
    /// the musically correct path between two ratios. A glide from 1 to 4 that
    /// passes through 2 at the halfway point is an octave a beat; one that
    /// passes through 2.5 is not a musical gesture at all.
    void setStep (int index, double ratio) noexcept
    {
        steps_.setStep (index, normalise (ratio));
    }

    [[nodiscard]] double getStep (int index) const noexcept
    {
        return denormalise (steps_.getStep (index));
    }

    /// Free-running rate, in steps per second.
    void setRateHz (double hz) noexcept { steps_.setRateHz (hz); }

    /// The same rate expressed as a tempo division, which is how it is
    /// actually used: a growl that is not in time with the drums is a mistake.
    void setDivision (int index, double bpm) noexcept
    {
        steps_.setRateHz (bpm > 0.0 ? dsp::divisionRateHz (index, bpm) : 0.0);
    }

    /// Re-anchor to the host's transport at the head of a block.
    ///
    /// The rate stays set, so `samplesToNextStep` is exact between anchors --
    /// which is where the caller does its cutting. Anchoring every block and
    /// free-running in between is what makes the pattern both locked to the
    /// bar and sample-accurate inside it.
    void anchorToPpq (double ppqPosition, double bpm, int division) noexcept
    {
        if (! (bpm > 0.0))
            return;

        setDivision (division, bpm);

        // One step per cycle of the division: at 1/16 that is four steps a
        // beat, which is the figure `setPhaseFromPpq` wants.
        const double stepsPerBeat = dsp::divisionRateHz (division, bpm) * 60.0 / bpm;

        (void) steps_.setPhaseFromPpq (ppqPosition, stepsPerBeat);
    }

    /// Samples until the next step edge, or -1 when the clock is stopped.
    [[nodiscard]] double samplesToNextStep() const noexcept
    {
        return steps_.samplesToNextStep();
    }

    void advance (int numSamples) noexcept { (void) steps_.advance (numSamples); }

    /// The ratio the pattern is asking for, already quantised.
    [[nodiscard]] double currentRatio (RatioMode mode, const dsp::Scale& scale) const noexcept
    {
        return resolveRatio (denormalise (steps_.getValue()), mode, scale);
    }

    [[nodiscard]] int getStepIndex() const noexcept { return steps_.getStepIndex(); }

private:
    static constexpr double kLogLow = -2.0;    ///< log2(kMinRatio)
    static constexpr double kLogHigh = 5.0;    ///< log2(kMaxRatio)
    static constexpr double kLogCentre = 0.5 * (kLogLow + kLogHigh);
    static constexpr double kLogHalfSpan = 0.5 * (kLogHigh - kLogLow);

    [[nodiscard]] static double normalise (double ratio) noexcept
    {
        const double clamped = std::clamp (ratio, kMinRatio, kMaxRatio);

        return (std::log2 (clamped) - kLogCentre) / kLogHalfSpan;
    }

    [[nodiscard]] static double denormalise (double normalised) noexcept
    {
        return std::exp2 (std::clamp (normalised, -1.0, 1.0) * kLogHalfSpan + kLogCentre);
    }

    dsp::StepSequencer steps_;
    double sampleRate_ { 48000.0 };
    int target_ { -1 };
    bool enabled_ { false };
};

} // namespace tezla::stryda
