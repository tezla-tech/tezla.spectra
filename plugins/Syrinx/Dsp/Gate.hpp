// Copyright (c) 2026 The Tezla <thetezla@proton.me>
// Created by The Tezla -- https://github.com/wingit33/tezla.tech
// Music: https://soundcloud.com/thetezla | https://thetezla.bandcamp.com
// Built with development assistance from Claude (Anthropic).
// SPDX-License-Identifier: AGPL-3.0-only
// GNU AGPLv3 (see LICENSE), plus NOTICE.md's attribution term. Keep intact.

#pragma once

// The gate at the head of the strip, and the two mechanisms that stop it
// chattering.
//
// ---------------------------------------------------------------------------
// One threshold is not enough
// ---------------------------------------------------------------------------
//
// A gate with a single threshold has a failure mode you can hear from the
// other side of the room: a signal sitting *at* the threshold makes it open
// and shut over and over, and a vocal tail sits at whatever threshold you set
// it to, because that is how you set it. Every breath between lines lands
// there.
//
// Two mechanisms, and both are needed because they fail differently:
//
//  1. **Hysteresis.** It opens above the threshold and shuts only below
//     `threshold - hysteresis`, so once the gate is open the signal has to
//     drop a stated amount before it closes. This is the one that fixes
//     threshold-sitting, and measurement says it is the *only* one that does:
//     a 400 Hz tone at exactly the threshold wobbling +/-0.5 dB at 5 Hz flips
//     the gate **1600 times** in two seconds with neither mechanism, **20**
//     with a 40 ms hold and no hysteresis, and **once** with 3 dB of
//     hysteresis. A 40 ms hold cannot bridge a 100 ms excursion.
//  2. **Hold.** Once open, it stays open for at least the hold time after the
//     level drops. Hysteresis cannot help a signal that genuinely falls a long
//     way between syllables; hold is what carries a gate across the gap inside
//     a word. Measured: a 100 ms hold carries the gate across a 60 ms gap and
//     lets it shut across a 300 ms one.
//
// So they are not two settings for one problem. They are two problems.
//
// ---------------------------------------------------------------------------
// The detector holds peaks
// ---------------------------------------------------------------------------
//
// The level fed to the thresholds is a peak follower with an instant attack
// and a short fixed decay, not the bare `|x|`. Without it the gate sees a
// 200 Hz vowel drop to nothing twice every cycle and no amount of hysteresis
// helps, because the signal really is at zero there. Five milliseconds: long
// enough to ride the lowest note a voice makes, short enough that the gate
// still shuts inside the hold time you set.
//
// ---------------------------------------------------------------------------
// Range attenuates; it does not mute
// ---------------------------------------------------------------------------
//
// A gate that closes to silence removes room tone as well as noise, and the
// result is a vocal that breathes in a vacuum between lines. Range says how
// far down closed goes, and the useful setting on a vocal is 10 to 20 dB
// rather than infinity.
//
// **Range 0 is bit-exact identity** (CLAUDE.md section 7): the target is then
// exactly 0 dB at both ends of the smoother, `dbToGain` of which is exactly
// 1.0, so a gate with nothing to do multiplies by nothing.

#include <algorithm>
#include <cmath>

#include <tezla/dsp/Biquad.hpp>
#include <tezla/dsp/Decibels.hpp>
#include <tezla/dsp/Denormals.hpp>
#include <tezla/dsp/Exact.hpp>

namespace tezla::syrinx {

namespace dsp = tezla::dsp;

class Gate
{
public:
    /// What a silent input reports, finite so nothing downstream sees an
    /// infinity and low enough to sit under any threshold a control reaches.
    static constexpr double kSilentDb = -160.0;

    /// How fast the peak detector lets go. Fixed rather than exposed: it is
    /// not a musical choice, it is what stops the gate seeing a low vowel's
    /// own zero crossings as silence.
    static constexpr double kDetectorDecayMs = 5.0;

    void prepare (double sampleRate) noexcept
    {
        sampleRate_ = sampleRate > 0.0 ? sampleRate : 44100.0;

        detectorDecay_ = std::exp (-1.0 / (kDetectorDecayMs * 0.001 * sampleRate_));

        refreshTimes();
        designSidechain();
        reset();
    }

    void reset() noexcept
    {
        detector_ = 0.0;
        sidechainFilter_.reset();
        open_ = false;
        holdRemaining_ = 0;

        // Starts closed, so silence at the top of a take stays gated rather
        // than fading in over the first attack.
        gainDb_ = -rangeDb_;
    }

    // -----------------------------------------------------------------------
    // Thresholds
    // -----------------------------------------------------------------------

    void setThresholdDb (double db) noexcept { thresholdDb_ = db; }

    /// How far below the threshold the gate has to fall before it shuts. See
    /// the header: this is the mechanism, not a refinement.
    void setHysteresisDb (double db) noexcept { hysteresisDb_ = std::clamp (db, 0.0, 24.0); }

    /// How far down "closed" is. 0 is bit-exact identity; infinity would be a
    /// mute, which is not what a vocal wants.
    void setRangeDb (double db) noexcept { rangeDb_ = std::clamp (db, 0.0, 80.0); }

    [[nodiscard]] double getThresholdDb() const noexcept { return thresholdDb_; }
    [[nodiscard]] double getHysteresisDb() const noexcept { return hysteresisDb_; }
    [[nodiscard]] double getRangeDb() const noexcept { return rangeDb_; }

    // -----------------------------------------------------------------------
    // Times
    // -----------------------------------------------------------------------

    void setAttackMs (double ms) noexcept
    {
        const double clamped = std::max (ms, 0.0);

        if (dsp::isExactly (clamped, attackMs_))
            return;

        attackMs_ = clamped;
        refreshTimes();
    }

    /// How long the gate stays open after the level drops below the closing
    /// threshold. Counted in samples, so it is the same duration at every rate.
    void setHoldMs (double ms) noexcept
    {
        const double clamped = std::max (ms, 0.0);

        if (dsp::isExactly (clamped, holdMs_))
            return;

        holdMs_ = clamped;
        refreshTimes();
    }

    void setReleaseMs (double ms) noexcept
    {
        const double clamped = std::max (ms, 0.0);

        if (dsp::isExactly (clamped, releaseMs_))
            return;

        releaseMs_ = clamped;
        refreshTimes();
    }

    [[nodiscard]] double getAttackMs() const noexcept { return attackMs_; }
    [[nodiscard]] double getHoldMs() const noexcept { return holdMs_; }
    [[nodiscard]] double getReleaseMs() const noexcept { return releaseMs_; }

    /// A high-pass on the **detector only**, so a rumbling room does not hold
    /// the gate open through every pause. Zero switches it off entirely, and
    /// then the detector sees the signal itself.
    void setSidechainHighpassHz (double hz) noexcept
    {
        const double clamped = std::clamp (hz, 0.0, 1000.0);

        if (dsp::isExactly (clamped, sidechainHz_))
            return;

        sidechainHz_ = clamped;
        designSidechain();
    }

    [[nodiscard]] double getSidechainHighpassHz() const noexcept { return sidechainHz_; }

    // -----------------------------------------------------------------------
    // Processing
    // -----------------------------------------------------------------------

    [[nodiscard]] double process (double input) noexcept { return process (input, input); }

    [[nodiscard]] double process (double input, double sidechain) noexcept
    {
        const double detected = sidechainHz_ > 0.0 ? sidechainFilter_.process (sidechain)
                                                   : sidechain;

        // Instant attack, short decay: a peak hold rather than a rectifier.
        const double magnitude = std::abs (detected);
        detector_ = magnitude > detector_ ? magnitude
                                          : dsp::snapToZero (detectorDecay_ * detector_);

        const double levelDb = detector_ > 0.0 ? 20.0 * std::log10 (detector_) : kSilentDb;

        updateState (levelDb);

        // Attack is travelling toward open (0 dB) and release toward closed,
        // which is the opposite convention to a compressor's -- hence a
        // smoother of its own rather than `EnvelopeFollower`, whose attack is
        // "further into reduction".
        const double target = open_ ? 0.0 : -rangeDb_;
        const double coefficient = target > gainDb_ ? attackCoefficient_ : releaseCoefficient_;

        gainDb_ = target + coefficient * (gainDb_ - target);

        return input * dsp::dbToGain (gainDb_);
    }

    /// How far down the gate is holding the signal right now, in dB (<= 0).
    [[nodiscard]] double getReductionDb() const noexcept { return gainDb_; }

    /// Whether the gate is currently letting the signal through, which is the
    /// state the chatter test counts transitions of.
    [[nodiscard]] bool isOpen() const noexcept { return open_; }

    /// True when this stage is the identity, bit for bit.
    [[nodiscard]] bool isIdentity() const noexcept { return dsp::isExactlyZero (rangeDb_); }

private:
    void updateState (double levelDb) noexcept
    {
        if (! open_)
        {
            if (levelDb > thresholdDb_)
            {
                open_ = true;
                holdRemaining_ = holdSamples_;
            }

            return;
        }

        // The closing threshold, which is the whole of the hysteresis: once
        // open, the level has to fall this far before the hold even starts
        // counting.
        if (levelDb > thresholdDb_ - hysteresisDb_)
        {
            holdRemaining_ = holdSamples_;
            return;
        }

        if (holdRemaining_ > 0)
            --holdRemaining_;
        else
            open_ = false;
    }

    void refreshTimes() noexcept
    {
        attackCoefficient_ = coefficientFor (attackMs_);
        releaseCoefficient_ = coefficientFor (releaseMs_);
        holdSamples_ = static_cast<int> (std::lround (holdMs_ * 0.001 * sampleRate_));
    }

    [[nodiscard]] double coefficientFor (double milliseconds) const noexcept
    {
        if (milliseconds <= 0.0)
            return 0.0;   // instantaneous

        return std::exp (-1.0 / (milliseconds * 0.001 * sampleRate_));
    }

    void designSidechain() noexcept
    {
        if (sidechainHz_ > 0.0)
            sidechainFilter_.setCoefficients (
                dsp::design::highpass (sidechainHz_, 0.707, sampleRate_));
    }

    double sampleRate_ { 44100.0 };

    double thresholdDb_ { -45.0 };
    double hysteresisDb_ { 3.0 };
    double rangeDb_ { 0.0 };

    double attackMs_ { 1.0 };
    double holdMs_ { 40.0 };
    double releaseMs_ { 120.0 };

    double attackCoefficient_ { 0.0 };
    double releaseCoefficient_ { 0.0 };
    int holdSamples_ { 0 };

    double detector_ { 0.0 };
    double detectorDecay_ { 0.0 };
    dsp::Biquad<double> sidechainFilter_;
    double sidechainHz_ { 0.0 };

    bool open_ { false };
    int holdRemaining_ { 0 };
    double gainDb_ { 0.0 };
};

} // namespace tezla::syrinx
