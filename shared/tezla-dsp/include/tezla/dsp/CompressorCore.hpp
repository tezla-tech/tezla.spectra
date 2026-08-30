// Copyright (c) 2026 The Tezla <thetezla@proton.me>
// Created by The Tezla -- https://github.com/wingit33/tezla.tech
// Music: https://soundcloud.com/thetezla | https://thetezla.bandcamp.com
// Built with development assistance from Claude (Anthropic).
// SPDX-License-Identifier: AGPL-3.0-only
// GNU AGPLv3 (see LICENSE), plus NOTICE.md's attribution term. Keep intact.

#pragma once

// A feed-forward compressor: the three pieces this library already has, wired
// the way the textbook wires them, plus the two things a channel strip needs
// that a limiter does not.
//
//     level -> GainComputer -> EnvelopeFollower -> makeup -> parallel mix
//
// `GainComputer` is the static curve (threshold, knee, ratio) and
// `EnvelopeFollower` is the attack and release, including the correct
// program-dependent release. Neither is re-implemented here. What this class
// adds is the wiring, and four properties that the wiring has to guarantee.
//
// ---------------------------------------------------------------------------
// Four things that must be exact, not merely close
// ---------------------------------------------------------------------------
//
// CLAUDE.md section 7: a stage permanently in the signal path has to be the
// identity at its neutral setting. A channel strip has two compressors always
// in the path, so "nearly identity" would mean every project changes the day
// the plugin updates -- twice.
//
//  1. **Ratio 1:1 is bit-exact bypass.** `GainComputer`'s slope is exactly 0
//     there, so every branch returns a signed zero, `dbToGain` of which is
//     exactly 1.0.
//  2. **Makeup at 0 dB is exactly 1.0**, for the same reason, so it multiplies
//     nothing.
//  3. **Mix is exact at BOTH ends.** The form is `mix * wet + (1 - mix) * dry`,
//     which returns `wet` bit-for-bit at 1 and `dry` bit-for-bit at 0. The
//     algebraically identical `dry + mix * (wet - dry)` is exact at 0 and a
//     unit in the last place off at 1 -- and 1 is the default, so that form
//     would have made the ordinary case the inexact one.
//  4. **A sidechain filter at 0 Hz is not a filter.** The detector sees the
//     signal itself, through no biquad at all, rather than through one
//     designed at some very low corner.
//
// ---------------------------------------------------------------------------
// Peak detection, and why that is the right choice here
// ---------------------------------------------------------------------------
//
// The level fed to the curve is `20*log10(|x|)`: peak, not RMS. For a vocal
// strip that is the useful one, and the distinction matters less than it looks
// like it should -- the attack and release *are* the averaging. A 100 ms
// attack on a peak detector behaves like an RMS detector with a 100 ms window,
// because the follower cannot chase anything faster than its own attack. What
// peak detection buys is that a fast setting really is fast, which is what the
// second compressor in a strip is for.
//
// Zero latency, deliberately. A vocal strip that reported latency would make
// every take arrive early in the monitor path, and lookahead belongs at the
// end of the chain where Capstone already has it.

#include <algorithm>
#include <cmath>

#include "Biquad.hpp"
#include "Decibels.hpp"
#include "EnvelopeFollower.hpp"
#include "Exact.hpp"
#include "GainComputer.hpp"

namespace tezla::dsp {

class CompressorCore
{
public:
    /// The level a silent input reports. Low enough to sit under any threshold
    /// a control can reach, finite so nothing downstream sees an infinity.
    static constexpr double kSilentDb = -160.0;

    /// **Ratio 1, not `GainComputer`'s infinity.** That class defaults to a
    /// limiter because Capstone is what it was written for; a compressor
    /// defaulting to a limiter would be a surprise, and section 8 wants a
    /// freshly built stage to be neutral. A default-constructed core is the
    /// identity function and `isIdentity()` says so.
    CompressorCore() noexcept
    {
        computer_.setRatio (1.0);
    }

    void prepare (double sampleRate) noexcept
    {
        sampleRate_ = sampleRate > 0.0 ? sampleRate : 44100.0;

        envelope_.prepare (sampleRate_);
        envelope_.setAttackMs (attackMs_);
        envelope_.setReleaseMs (releaseMs_);

        designSidechain();
        reset();
    }

    void reset() noexcept
    {
        envelope_.reset();
        sidechainFilter_.reset();
        reductionDb_ = 0.0;
    }

    // -----------------------------------------------------------------------
    // The curve
    // -----------------------------------------------------------------------

    void setThresholdDb (double db) noexcept { computer_.setThresholdDb (db); }
    void setRatio (double ratio) noexcept { computer_.setRatio (ratio); }
    void setKneeDb (double db) noexcept { computer_.setKneeDb (db); }

    [[nodiscard]] double getThresholdDb() const noexcept { return computer_.getThresholdDb(); }
    [[nodiscard]] double getRatio() const noexcept { return computer_.getRatio(); }
    [[nodiscard]] double getKneeDb() const noexcept { return computer_.getKneeDb(); }

    // -----------------------------------------------------------------------
    // The times
    // -----------------------------------------------------------------------

    void setAttackMs (double ms) noexcept
    {
        attackMs_ = std::max (ms, 0.0);
        envelope_.setAttackMs (attackMs_);
    }

    void setReleaseMs (double ms) noexcept
    {
        releaseMs_ = std::max (ms, 0.0);
        envelope_.setReleaseMs (releaseMs_);
    }

    /// A second, slower release running alongside the first, with whichever is
    /// holding more reduction winning. On a vocal it is what stops a held note
    /// pumping while still letting a consonant recover at the stated time.
    void setProgramDependent (bool shouldBe) noexcept
    {
        envelope_.setProgramDependent (shouldBe);
    }

    [[nodiscard]] double getAttackMs() const noexcept { return attackMs_; }
    [[nodiscard]] double getReleaseMs() const noexcept { return releaseMs_; }

    // -----------------------------------------------------------------------
    // The output
    // -----------------------------------------------------------------------

    void setMakeupDb (double db) noexcept
    {
        makeupDb_ = db;

        // Cached rather than converted per sample: `dbToGain` is a `pow`, and
        // a strip runs two of these on every voice. Exactly 1.0 at 0 dB, which
        // is what keeps the neutral setting bit-exact.
        makeupGain_ = dbToGain (db);
    }

    /// 1 is the compressed signal, 0 is the input untouched, and in between is
    /// parallel compression. Both ends are bit-exact -- see the header.
    void setMix (double mix) noexcept { mix_ = std::clamp (mix, 0.0, 1.0); }

    [[nodiscard]] double getMakeupDb() const noexcept { return makeupDb_; }
    [[nodiscard]] double getMix() const noexcept { return mix_; }

    // -----------------------------------------------------------------------
    // The sidechain
    // -----------------------------------------------------------------------

    /// A high-pass on the **detector only**, so a plosive or a kick bleeding
    /// into a vocal mic does not duck the whole word. Zero switches it off
    /// entirely rather than designing a filter at some very low corner: the
    /// detector then sees the signal itself, bit for bit.
    void setSidechainHighpassHz (double hz) noexcept
    {
        const double clamped = std::clamp (hz, 0.0, 1000.0);

        if (isExactly (clamped, sidechainHz_))
            return;

        sidechainHz_ = clamped;
        designSidechain();
    }

    [[nodiscard]] double getSidechainHighpassHz() const noexcept { return sidechainHz_; }

    // -----------------------------------------------------------------------
    // Processing
    // -----------------------------------------------------------------------

    /// Advances the detector one sample and returns the linear gain to apply.
    ///
    /// **Split from `applyTo` so a stereo pair can share one detector.**
    /// CLAUDE.md section 7 requires linked stereo by default: running two
    /// independent compressors on a stereo signal moves the image every time
    /// one channel is louder than the other, which on a doubled vocal is
    /// constantly. One detector fed the linked signal, one gain applied to
    /// both channels, and the image cannot move.
    [[nodiscard]] double computeGain (double sidechain) noexcept
    {
        const double detected = sidechainHz_ > 0.0 ? sidechainFilter_.process (sidechain)
                                                   : sidechain;

        const double magnitude = std::abs (detected);
        const double levelDb = magnitude > 0.0 ? 20.0 * std::log10 (magnitude) : kSilentDb;

        reductionDb_ = envelope_.process (computer_.computeGainReductionDb (levelDb));

        return dbToGain (reductionDb_);
    }

    /// Applies a gain from `computeGain` to one channel, with the makeup and
    /// the parallel mix. Stateless, so it can be called once per channel.
    [[nodiscard]] double applyTo (double input, double gain) const noexcept
    {
        const double wet = input * gain * makeupGain_;

        return mix_ * wet + (1.0 - mix_) * input;
    }

    /// One sample, detecting from the signal itself.
    [[nodiscard]] double process (double input) noexcept
    {
        return process (input, input);
    }

    /// One sample, detecting from `sidechain`.
    ///
    /// Two arguments rather than one because a channel strip's compressors
    /// detect from the post-gate, post-de-ess signal while passing whatever
    /// the stage before them produced -- and because it is the only honest way
    /// to test a detector separately from what it acts on.
    [[nodiscard]] double process (double input, double sidechain) noexcept
    {
        return applyTo (input, computeGain (sidechain));
    }

    /// How much this stage is pulling down right now, in dB (<= 0). What the
    /// meter reads.
    [[nodiscard]] double getReductionDb() const noexcept { return reductionDb_; }

    /// True when this stage is the identity, bit for bit: no ratio, no makeup,
    /// fully wet.
    ///
    /// Fully *wet* rather than any mix, because at ratio 1 the wet and dry
    /// paths are the same signal and the crossfade between them is exact only
    /// at its ends -- in the middle it is two multiplies and an add, which is
    /// a unit in the last place away from where it started.
    [[nodiscard]] bool isIdentity() const noexcept
    {
        return isExactly (computer_.getRatio(), 1.0)
                 && isExactlyZero (makeupDb_)
                 && isExactly (mix_, 1.0);
    }

private:
    void designSidechain() noexcept
    {
        if (sidechainHz_ > 0.0)
            sidechainFilter_.setCoefficients (
                design::highpass (sidechainHz_, 0.707, sampleRate_));
    }

    double sampleRate_ { 44100.0 };

    GainComputer computer_;
    EnvelopeFollower envelope_;
    Biquad<double> sidechainFilter_;

    double attackMs_ { 10.0 };
    double releaseMs_ { 120.0 };
    double makeupDb_ { 0.0 };
    double makeupGain_ { 1.0 };
    double mix_ { 1.0 };
    double sidechainHz_ { 0.0 };

    double reductionDb_ { 0.0 };
};

} // namespace tezla::dsp
