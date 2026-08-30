// Copyright (c) 2026 The Tezla <thetezla@proton.me>
// Created by The Tezla -- https://github.com/wingit33/tezla.tech
// Music: https://soundcloud.com/thetezla | https://thetezla.bandcamp.com
// Built with development assistance from Claude (Anthropic).
// SPDX-License-Identifier: AGPL-3.0-only
// GNU AGPLv3 (see LICENSE), plus NOTICE.md's attribution term. Keep intact.

#pragma once

// The de-esser: SibilanceDetector deciding, a crossfade doing.
//
// THE REDUCTION IS A CROSSFADE TOWARD THE LOW-PASSED SIGNAL, and the form is
// chosen for what it guarantees rather than for what it resembles:
//
//     out = g * x + (1 - g) * lowpass(x)
//
//   * g == 1 returns x BIT-EXACTLY. Not almost: the two multiplies are by
//     exactly 1.0 and exactly 0.0, so an inactive de-esser is the identity
//     function (CLAUDE.md section 7 -- a stage permanently in the path needs
//     a bit-exact bypass at its neutral setting, and this one is in the path
//     of every vocal that ever passes through the strip).
//   * The body is untouched BY CONSTRUCTION rather than by care. Below the
//     corner, lowpass(x) is x, so the crossfade has nothing to move there.
//     "It does not lisp" stops being a hope and becomes arithmetic -- and
//     the test measures it anyway.
//   * No band-splitting phase to explain. A Linkwitz-Riley pair sums to an
//     allpass, not to unity, so a split-duck-recombine de-esser cannot be
//     bit-exact when idle however carefully it is written. This one is
//     complementary by definition: the high part IS x minus the low part.
//
// Range caps how far g may fall. A de-esser that can reach g = 0 removes the
// entire top of the voice on a hard /s/, which is the sound everyone means
// when they say a de-esser lisps. 6 to 12 dB is the useful territory.
//
// Listen solos what is being removed -- x - out, the sibilance and nothing
// else -- because the only way to set a de-esser honestly is to hear what it
// is taking. Halo's `listen` is the same idea.

#include <algorithm>
#include <cmath>

#include <tezla/dsp/Biquad.hpp>
#include <tezla/dsp/Decibels.hpp>
#include <tezla/dsp/EnvelopeFollower.hpp>
#include <tezla/dsp/Exact.hpp>
#include <tezla/dsp/GainComputer.hpp>

#include "SibilanceDetector.hpp"

namespace tezla::syrinx {

class DeEsser
{
public:
    void prepare (double sampleRate) noexcept
    {
        sampleRate_ = sampleRate > 0.0 ? sampleRate : 44100.0;

        detector_.prepare (sampleRate_);
        envelope_.prepare (sampleRate_);
        envelope_.setAttackMs (attackMs_);
        envelope_.setReleaseMs (releaseMs_);

        setCornerHz (cornerHz_);
        reset();
    }

    void reset() noexcept
    {
        detector_.reset();
        envelope_.reset();
        shelf_.reset();
        reductionDb_ = 0.0;
    }

    /// Where the voice stops and the sibilance starts, in Hz. Drives both
    /// the detector's split and the crossfade's low-pass, so the thing being
    /// measured and the thing being ducked are the same band.
    void setCornerHz (double hz) noexcept
    {
        detector_.setCornerHz (hz);
        cornerHz_ = detector_.getCornerHz();
        shelf_.setCoefficients (dsp::design::lowpass (cornerHz_, 0.707, sampleRate_));
    }

    /// How sibilant the voice has to be before anything happens, in dB of
    /// high-to-body ratio. Not a level: see SibilanceDetector.
    void setThresholdDb (double db) noexcept
    {
        computer_.setThresholdDb (db);
    }

    void setRatio (double ratio) noexcept { computer_.setRatio (ratio); }
    void setKneeDb (double db) noexcept { computer_.setKneeDb (db); }

    /// The most the sibilance may be ducked, in dB. Caps the crossfade so a
    /// hard /s/ cannot take the whole top of the voice with it.
    void setRangeDb (double db) noexcept { rangeDb_ = std::clamp (db, 0.0, 36.0); }

    void setAttackMs (double ms) noexcept
    {
        attackMs_ = ms;
        envelope_.setAttackMs (ms);
    }

    void setReleaseMs (double ms) noexcept
    {
        releaseMs_ = ms;
        envelope_.setReleaseMs (ms);
    }

    void setListen (bool shouldListen) noexcept { listen_ = shouldListen; }

    /// One sample through. `sidechain` is what the detector measures, which
    /// is normally the same signal -- the strip passes the post-gate vocal.
    [[nodiscard]] double process (double input, double sidechain) noexcept
    {
        const double sibilanceDb = detector_.process (sidechain);
        lastSibilanceDb_ = sibilanceDb;

        // Clamped to the Range, then smoothed. Clamping BEFORE the envelope
        // rather than after means the smoother is chasing a target it can
        // actually reach, so Range changes the depth of the duck rather than
        // flattening the top off a trajectory already in flight.
        const double target = std::max (computer_.computeGainReductionDb (sibilanceDb),
                                        -rangeDb_);

        reductionDb_ = envelope_.process (target);

        const double low = shelf_.process (input);

        // Exactly 1.0 when nothing is being reduced -- and dbToGain of a
        // signed zero is exactly 1.0, so this really is the identity.
        const double g = dsp::dbToGain (reductionDb_);
        const double out = g * input + (1.0 - g) * low;

        return listen_ ? input - out : out;
    }

    /// How much the sibilance is being ducked right now, in dB (<= 0).
    [[nodiscard]] double getReductionDb() const noexcept { return reductionDb_; }

    /// The detector's current reading, for the editor's display.
    [[nodiscard]] double getSibilanceDb() const noexcept
    {
        return lastSibilanceDb_;
    }

    [[nodiscard]] double getCornerHz() const noexcept { return cornerHz_; }

private:
    double sampleRate_ { 44100.0 };
    double cornerHz_ { 6000.0 };
    double rangeDb_ { 9.0 };
    double attackMs_ { 0.5 };
    double releaseMs_ { 40.0 };
    bool listen_ { false };

    SibilanceDetector detector_;
    dsp::GainComputer computer_;
    dsp::EnvelopeFollower envelope_;
    dsp::Biquad<double> shelf_;

    double reductionDb_ { 0.0 };
    double lastSibilanceDb_ { SibilanceDetector::kSilentDb };
};

} // namespace tezla::syrinx
