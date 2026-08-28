// Copyright (c) 2026 The Tezla <thetezla@proton.me>
// Created by The Tezla -- https://github.com/wingit33/tezla.tech
// Music: https://soundcloud.com/thetezla | https://thetezla.bandcamp.com
// Built with development assistance from Claude (Anthropic).
// SPDX-License-Identifier: AGPL-3.0-only
// GNU AGPLv3 (see LICENSE), plus NOTICE.md's attribution term. Keep intact.

#pragma once

// A look-ahead brickwall limiter whose ceiling is a theorem.
//
//   level ── GainComputer ── EnvelopeFollower ── RunningMinimum ── BoxStack ── x
//   (true    (soft knee)     (release only)      (attack+hold)     (attack)    |
//    peak)                                                                     v
//   x ─────────────── delay(attack + detector) ──────────────────────────── clamp
//
// Every stage is chosen so the composition can be reasoned about rather than
// tuned, and the ordering is the argument:
//
//   GainComputer      gives g[n], the gain sample n needs. Everything after it
//                     may only ever produce something smaller.
//
//   EnvelopeFollower  holds the gain down longer. Its attack is pinned at zero
//                     -- not as a default, as a requirement. Any attack lag
//                     would put the gain *above* g[n] for a few samples and the
//                     proof would be void. The release is free to be as slow as
//                     it likes, including program-dependent, because holding
//                     the gain lower is always safe.
//
//   RunningMinimum    over a window that contains the sample being processed.
//   BoxStackSmoother  with a non-negative unit-sum kernel inside that window.
//                     Together: every term of the average is already below
//                     g[n], so the average is. That is the whole guarantee, and
//                     it is measured rather than asserted -- swept across the
//                     parameter space in tests/test_LimiterCore.cpp.
//
// The attack control is the smoother's length. There is no separate attack
// time constant, and there should not be: the look-ahead *is* the attack, and a
// second one downstream would be the thing that breaks the alignment.
//
// The delivered output is clamped to the ceiling once at the end. The chain
// above lands about 1e-14 above it -- rounding in the smoother's running sums,
// -280 dBFS -- and one clamp turns "within a few ULP" into "exactly", for the
// cost of a comparison on a value that is already correct.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <vector>

#include "BoxStackSmoother.hpp"
#include "Decibels.hpp"
#include "Denormals.hpp"
#include "EnvelopeFollower.hpp"
#include "GainComputer.hpp"
#include "RunningMinimum.hpp"
#include "TruePeakDetector.hpp"

namespace tezla::dsp {

class LimiterCore
{
public:
    static constexpr int kMaxChannels = 2;

    /// The longest look-ahead and hold the controls offer, which is what
    /// prepare() sizes everything for.
    static constexpr double kMaxAttackMs = 20.0;
    static constexpr double kMaxHoldMs   = 100.0;

    /// The most latency any setting of the controls can produce, at a given
    /// rate. Callers size their own delay lines from this, so it has to be
    /// derived from the same constants prepare() uses rather than guessed.
    [[nodiscard]] static int maximumLatencySamples (double sampleRate) noexcept
    {
        const double rate = sampleRate > 0.0 ? sampleRate : 44100.0;

        return static_cast<int> (std::lround (kMaxAttackMs * 0.001 * rate))
             + TruePeakDetector::kDesignedTaps / 2;
    }

    void prepare (double sampleRate, int numChannels)
    {
        sampleRate_  = sampleRate > 0.0 ? sampleRate : 44100.0;
        numChannels_ = std::clamp (numChannels, 1, kMaxChannels);

        const int maxAttack = millisecondsToSamples (kMaxAttackMs);
        const int maxWindow = maxAttack + millisecondsToSamples (kMaxHoldMs);

        // The delay has to cover the longest look-ahead plus the longest
        // detector, and one spare so the read index is never the write index.
        maxLatency_ = maxAttack + TruePeakDetector::kDesignedTaps / 2 + 1;

        for (int channel = 0; channel < kMaxChannels; ++channel)
        {
            auto& state = channels_[static_cast<std::size_t> (channel)];

            state.detector.prepare (TruePeakDetector::kMaxFactor);
            state.minimum.prepare (maxWindow + 1);
            state.smoother.prepare (maxAttack + 1);
            state.envelope.prepare (sampleRate_);
            state.delay.assign (static_cast<std::size_t> (maxLatency_ + 1), 0.0);
        }

        // Attack is the smoother's length, so it can never be zero: a support
        // of one sample is the shortest kernel there is, and it is what
        // "look-ahead off" means.
        applyTiming();
        reset();
    }

    void reset() noexcept
    {
        for (auto& state : channels_)
        {
            state.detector.reset();
            state.minimum.reset (1.0);
            state.smoother.reset (1.0);
            state.envelope.reset();
            std::fill (state.delay.begin(), state.delay.end(), 0.0);
            state.writePosition = 0;
        }

        gainReductionDb_ = 0.0;
        clampExcess_     = 0.0;
    }

    // ---- controls -----------------------------------------------------------

    /// The level the output is not allowed past. Above 0 dBFS is deliberate:
    /// in a floating-point chain there is no reason a limiter has to stop at
    /// full scale, and catching only the extremes is a real use.
    void setCeilingDb (double ceilingDb) noexcept
    {
        ceilingDb_ = ceilingDb;
        ceilingGain_ = dbToGain (ceilingDb_);

        for (auto& state : channels_)
            state.computer.setCeilingDb (ceilingDb_);
    }

    /// How far below the ceiling the curve starts bending. 0 is a hard corner.
    void setKneeDb (double kneeDb) noexcept
    {
        for (auto& state : channels_)
            state.computer.setKneeDb (kneeDb);
    }

    /// The look-ahead, which is also the attack and also the reported latency.
    void setAttackMs (double milliseconds) noexcept
    {
        attackMs_ = std::clamp (milliseconds, 0.0, kMaxAttackMs);
        applyTiming();
    }

    /// How long the gain stays down after a peak before the release starts.
    /// Free: it widens the minimum window backwards, which needs no more
    /// look-ahead and so costs no latency.
    void setHoldMs (double milliseconds) noexcept
    {
        holdMs_ = std::clamp (milliseconds, 0.0, kMaxHoldMs);
        applyTiming();
    }

    void setReleaseMs (double milliseconds) noexcept
    {
        for (auto& state : channels_)
        {
            state.envelope.setReleaseMs (std::max (milliseconds, 0.1));

            // Never anything else. See the header: an attack here would put the
            // gain above what the sample requires and void the guarantee.
            state.envelope.setAttackMs (0.0);
        }
    }

    void setAutoRelease (bool shouldBeProgramDependent) noexcept
    {
        for (auto& state : channels_)
            state.envelope.setProgramDependent (shouldBeProgramDependent);
    }

    /// 1 keeps the centre image still by giving every channel the same gain;
    /// 0 lets each follow its own peaks, which is wider and looser.
    void setStereoLink (double amount) noexcept
    {
        stereoLink_ = std::clamp (amount, 0.0, 1.0);
    }

    /// 1 for sample peak, 4 for the ITU filter, 16 for the strict one.
    void setTruePeakFactor (int factor) noexcept
    {
        for (auto& state : channels_)
            state.detector.setFactor (factor);

        applyTiming();
    }

    // ---- what the host and the panel need -----------------------------------

    [[nodiscard]] int getLatencySamples() const noexcept { return latency_; }

    /// The most reduction any channel reached in the last block, in dB.
    [[nodiscard]] double getGainReductionDb() const noexcept { return gainReductionDb_; }

    [[nodiscard]] double getCeilingDb() const noexcept { return ceilingDb_; }

    /// The most the final clamp had to remove in the last block, in linear
    /// units. Always >= 0.
    ///
    /// This is the number that says whether the guarantee is actually working,
    /// and it exists because checking the output peak does not. The clamp makes
    /// the ceiling true unconditionally -- so a ceiling test alone passes even
    /// with the minimum window deliberately misaligned by half the smoother's
    /// support, which was measured here and is why this is not a comment.
    /// A correct chain lands around 1e-14, which is the rounding the header
    /// describes; a broken one leaves the clamp clipping real signal, which
    /// reads correctly on a peak meter and sounds like distortion.
    [[nodiscard]] double getClampExcess() const noexcept { return clampExcess_; }

    // ---- audio --------------------------------------------------------------

    void process (double* const* channels, int numChannels, int numSamples) noexcept
    {
        const ScopedNoDenormals noDenormals;

        if (numSamples <= 0)
            return;

        const int active = std::min (numChannels, numChannels_);
        double blockReductionDb = 0.0;
        double blockClampExcess = 0.0;

        for (int i = 0; i < numSamples; ++i)
        {
            // What each channel is about to do, and what the loudest is.
            double linked = 0.0;
            std::array<double, kMaxChannels> own {};

            for (int channel = 0; channel < active; ++channel)
            {
                auto& state = channels_[static_cast<std::size_t> (channel)];

                state.delay[static_cast<std::size_t> (state.writePosition)] = channels[channel][i];
                own[static_cast<std::size_t> (channel)] = state.detector.process (channels[channel][i]);
                linked = std::max (linked, own[static_cast<std::size_t> (channel)]);
            }

            for (int channel = 0; channel < active; ++channel)
            {
                auto& state = channels_[static_cast<std::size_t> (channel)];

                const double level = stereoLink_ * linked
                                   + (1.0 - stereoLink_) * own[static_cast<std::size_t> (channel)];

                // The gain this sample requires, and then three stages that may
                // only ever make it smaller.
                const double requiredDb = state.computer.computeGainReductionDb (gainToDb (level, -400.0));
                const double heldDb     = state.envelope.process (requiredDb);

                const double smoothed = state.smoother.process (state.minimum.process (dbToGain (heldDb)));

                const double delayed = state.delay[static_cast<std::size_t> (
                    readIndexFor (state.writePosition))];

                // The clamp that turns a few ULP into exactly. It is doing
                // nothing audible: everything above it is already correct to
                // -280 dBFS -- and how far above is recorded rather than
                // assumed, because this clamp is strong enough to hide a broken
                // chain from every peak measurement there is.
                const double raw = smoothed * delayed;

                blockClampExcess = std::max (blockClampExcess, std::abs (raw) - ceilingGain_);

                channels[channel][i] = std::clamp (raw, -ceilingGain_, ceilingGain_);

                if (++state.writePosition >= static_cast<int> (state.delay.size()))
                    state.writePosition = 0;

                blockReductionDb = std::min (blockReductionDb, gainToDb (smoothed, -100.0));
            }
        }

        gainReductionDb_ = blockReductionDb;
        clampExcess_     = std::max (0.0, blockClampExcess);
    }

private:
    struct ChannelState
    {
        TruePeakDetector  detector;
        GainComputer      computer;
        EnvelopeFollower  envelope;
        RunningMinimum    minimum;
        BoxStackSmoother  smoother;

        std::vector<double> delay;
        int writePosition { 0 };
    };

    [[nodiscard]] int millisecondsToSamples (double milliseconds) const noexcept
    {
        return std::max (0, static_cast<int> (std::lround (milliseconds * 0.001 * sampleRate_)));
    }

    /// Where in the delay line the sample this gain was computed for is sitting.
    [[nodiscard]] int readIndexFor (int writePosition) const noexcept
    {
        const int size = static_cast<int> (channels_[0].delay.size());
        int index = writePosition - latency_;

        while (index < 0)
            index += size;

        return index;
    }

    /// Turns the attack and hold controls into window lengths, and recomputes
    /// the latency the whole thing has to be aligned to.
    void applyTiming() noexcept
    {
        // The smoother's support. One sample means no smoothing and no
        // look-ahead, which is what an attack of zero has to mean.
        const int support = std::max (1, millisecondsToSamples (attackMs_));
        const int hold = millisecondsToSamples (holdMs_);

        int detectorLatency = 0;

        for (auto& state : channels_)
        {
            state.smoother.setLength (support);

            // At least the smoother's support, or the minimum would be taken
            // over a window that does not contain the sample being processed
            // and the guarantee would not hold. Hold only ever adds.
            state.minimum.setLength (support + hold);

            detectorLatency = std::max (detectorLatency, state.detector.getLatencySamples());
        }

        latency_ = channels_[0].smoother.getLatencySamples() + detectorLatency;
    }

    std::array<ChannelState, kMaxChannels> channels_ {};

    double sampleRate_  { 44100.0 };
    int    numChannels_ { 2 };
    int    latency_     { 0 };
    int    maxLatency_  { 1 };

    double attackMs_    { 5.0 };
    double holdMs_      { 0.0 };
    double stereoLink_  { 1.0 };
    double ceilingDb_   { -0.3 };
    double ceilingGain_ { 0.966 };

    double gainReductionDb_ { 0.0 };
    double clampExcess_     { 0.0 };
};

} // namespace tezla::dsp
