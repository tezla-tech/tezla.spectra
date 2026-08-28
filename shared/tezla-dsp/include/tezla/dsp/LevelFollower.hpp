// Copyright (c) 2026 The Tezla <thetezla@proton.me>
// Created by The Tezla -- https://github.com/wingit33/tezla.tech
// Music: https://soundcloud.com/thetezla | https://thetezla.bandcamp.com
// Built with development assistance from Claude (Anthropic).
// SPDX-License-Identifier: AGPL-3.0-only
// GNU AGPLv3 (see LICENSE), plus NOTICE.md's attribution term. Keep intact.

#pragma once

// How loud the input is, right now, as a number between 0 and 1.
//
// **This is a level detector, not an envelope generator.** There is no trigger,
// no note-on and no MIDI: the audio is what drives it, exactly as it is for a
// compressor's detector or an auto-wah. The two things share the word
// "envelope" and are otherwise unrelated, and the confusion is common enough to
// be worth the paragraph.
//
// Halo already contains one of these -- Punch is a fast follower minus a slow
// one -- hard-wired to a single destination. This is the same idea with the
// wire removed, so it can be pointed anywhere.
//
// The mapping to 0..1 is in decibels rather than in amplitude, because that is
// how level is heard and how a "sensitivity" control is expected to behave: a
// signal 20 dB below the sensitivity point should read half way, not at a
// hundredth. In amplitude it would read 0.1, which makes the top of the control
// useless.
//
// Detection is stereo-linked. An independent follower per channel makes a
// modulated stereo image wander, which is the same argument CLAUDE.md section 7
// makes about per-channel nonlinearity.

#include <algorithm>
#include <cmath>

#include "Decibels.hpp"
#include "Denormals.hpp"

namespace tezla::dsp {

class LevelFollower
{
public:
    /// How far below the sensitivity point reads as zero. Fixed rather than
    /// exposed: two controls that both mean "how much level" is one too many,
    /// and 40 dB covers the useful span of any programme material.
    static constexpr double kRangeDb = 40.0;

    void prepare (double sampleRate) noexcept
    {
        sampleRate_ = sampleRate > 0.0 ? sampleRate : 44100.0;
        updateCoefficients();
        reset();
    }

    void reset() noexcept
    {
        state_ = 0.0;
        value_ = 0.0;
    }

    void setAttackMs (double milliseconds) noexcept
    {
        attackMs_ = std::clamp (milliseconds, 0.1, 500.0);
        updateCoefficients();
    }

    void setReleaseMs (double milliseconds) noexcept
    {
        releaseMs_ = std::clamp (milliseconds, 1.0, 5000.0);
        updateCoefficients();
    }

    /// The input level, in dBFS, that reads as a full 1.0.
    void setSensitivityDb (double db) noexcept
    {
        sensitivityDb_ = std::clamp (db, -60.0, 12.0);
    }

    [[nodiscard]] double getAttackMs() const noexcept  { return attackMs_; }
    [[nodiscard]] double getReleaseMs() const noexcept { return releaseMs_; }

    /// Feeds a block and returns the value at its end.
    ///
    /// The detector runs per sample -- an attack of 1 ms means nothing if it is
    /// only sampled once a block -- but the conversion to decibels happens once
    /// at the end, because a log per sample at 192 kHz is real money for a
    /// number nothing reads until the block is over.
    [[nodiscard]] double process (const double* const* channels, int numChannels,
                                  int numSamples) noexcept
    {
        const ScopedNoDenormals noDenormals;

        if (channels == nullptr || numChannels <= 0 || numSamples <= 0)
            return value_;

        for (int i = 0; i < numSamples; ++i)
        {
            double magnitude = 0.0;

            for (int channel = 0; channel < numChannels; ++channel)
                magnitude = std::max (magnitude, std::abs (channels[channel][i]));

            const double coefficient = magnitude > state_ ? attackCoeff_ : releaseCoeff_;
            state_ += coefficient * (magnitude - state_);
        }

        value_ = mapToUnit (state_);
        return value_;
    }

    [[nodiscard]] double getValue() const noexcept { return value_; }

    /// The linear amplitude the detector currently holds, before the mapping.
    /// For a meter, and for tests that want to check the time constants without
    /// the sensitivity curve in the way.
    [[nodiscard]] double getMagnitude() const noexcept { return state_; }

private:
    [[nodiscard]] double mapToUnit (double magnitude) const noexcept
    {
        const double db = gainToDb (magnitude, -200.0);
        return std::clamp ((db - (sensitivityDb_ - kRangeDb)) / kRangeDb, 0.0, 1.0);
    }

    [[nodiscard]] double coefficientFor (double milliseconds) const noexcept
    {
        const double seconds = milliseconds * 0.001;

        if (seconds <= 0.0)
            return 1.0;

        return 1.0 - std::exp (-1.0 / (seconds * sampleRate_));
    }

    void updateCoefficients() noexcept
    {
        attackCoeff_  = coefficientFor (attackMs_);
        releaseCoeff_ = coefficientFor (releaseMs_);
    }

    double sampleRate_    { 44100.0 };
    double attackMs_      { 10.0 };
    double releaseMs_     { 150.0 };
    double sensitivityDb_ { -6.0 };

    double attackCoeff_  { 0.0 };
    double releaseCoeff_ { 0.0 };

    double state_ { 0.0 };
    double value_ { 0.0 };
};

} // namespace tezla::dsp
