// Copyright (c) 2026 The Tezla <thetezla@proton.me>
// Created by The Tezla -- https://github.com/wingit33/tezla.tech
// Music: https://soundcloud.com/thetezla | https://thetezla.bandcamp.com
// Built with development assistance from Claude (Anthropic).
// SPDX-License-Identifier: AGPL-3.0-only
// GNU AGPLv3 (see LICENSE), plus NOTICE.md's attribution term. Keep intact.

#pragma once

// The per-hit tension drop -- the membrane physics behind a tabla gliss and
// an 808 drop, reduced to what it audibly is: strike at raised tension,
// tension relaxes, every mode glides down to rest.
//
// The glide is exponential in CENTS (log-frequency), because pitch is heard
// in log-frequency: a drop that is linear in Hz sags unmusically. Depth is
// signed semitones (positive strikes sharp and falls; negative strikes flat
// and rises -- the reverse-drop). `timeSeconds` is the audible landing
// time: the internal time constant is a fifth of it, so at the stated time
// less than 1% of the depth remains, and shortly after that the remaining
// cents SNAP to exactly zero -- the multiplier becomes exactly 1.0, the
// resonator's no-op retune guard takes over, and a resting voice spends
// nothing on pole rebuilds (the same silence-is-silence discipline as
// everywhere else).
//
// advance() is multiplicative in the sample count, so any division of the
// same span into blocks lands within rounding of the same place -- host
// buffer size cannot bend the glide (CLAUDE.md section 7's block rule, held
// by construction).

#include <cmath>

#include <tezla/dsp/Exact.hpp>

namespace tezla::malleus {

class TensionDrop
{
public:
    /// The stated landing time leaves e^-5 (0.67%) of the depth.
    static constexpr double kLandFactor = 5.0;

    /// Below this many cents the drop snaps home to exactly 1.0.
    static constexpr double kSnapCents = 0.01;

    void prepare (double sampleRate) noexcept
    {
        sampleRate_ = sampleRate > 0.0 ? sampleRate : 44100.0;
        reset();
    }

    void reset() noexcept
    {
        cents_ = 0.0;
        decayPerSample_ = 1.0;
    }

    /// Starts a glide of `depthSemitones` (signed) landing over
    /// `timeSeconds`. Called at note-on.
    void trigger (double depthSemitones, double timeSeconds) noexcept
    {
        const double time = timeSeconds < 0.001 ? 0.001
                          : timeSeconds > 5.0 ? 5.0 : timeSeconds;

        cents_ = 100.0 * depthSemitones;
        decayPerSample_ = std::exp (-kLandFactor / (time * sampleRate_));
    }

    /// Advances the glide by a block of samples.
    void advance (int numSamples) noexcept
    {
        if (numSamples <= 0 || dsp::isExactlyZero (cents_))
            return;

        cents_ *= std::pow (decayPerSample_, static_cast<double> (numSamples));

        if (std::abs (cents_) < kSnapCents)
            cents_ = 0.0;
    }

    /// The current frequency multiplier for every mode. Exactly 1.0 at
    /// rest, so the resonator's retune guard sees a true no-op.
    [[nodiscard]] double multiplier() const noexcept
    {
        return dsp::isExactlyZero (cents_) ? 1.0 : std::exp2 (cents_ / 1200.0);
    }

    [[nodiscard]] double remainingCents() const noexcept { return cents_; }

    [[nodiscard]] bool isActive() const noexcept { return ! dsp::isExactlyZero (cents_); }

private:
    double sampleRate_ { 44100.0 };
    double cents_ { 0.0 };
    double decayPerSample_ { 1.0 };
};

} // namespace tezla::malleus
