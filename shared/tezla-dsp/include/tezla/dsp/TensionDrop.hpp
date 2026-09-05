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
// Shared between Malleus (where it was born, gliding a modal bank) and Ictus
// (where a kick body carries two of them: the fast drop of the attack and the
// slow sigh of the decay, the two effects the TR-808 analysis keeps apart).
// A drop is pure bookkeeping in cents: it knows nothing about what it
// retunes, which is why one class serves both.
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
//
// The CURVE (Ictus I4.5) bends the landing's shape without touching the
// exponential: 0 is the glide above, bit for bit, by branch. Towards -1 the
// cents blend to a straight line that lands at exactly the stated time -- the
// laser, falling at one rate from the first sample to the last. Towards +1
// the exponent's time is raised to a power, 1 + 3 curve, so the drop holds
// near its start and then falls away -- the snap. A curved glide counts its
// elapsed samples (an exact integer sum) rather than multiplying a
// coefficient, so it too lands within rounding of the same place whatever
// the block size.

#include <algorithm>
#include <cmath>

#include "Exact.hpp"

namespace tezla::dsp {

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
        curve_ = 0.0;
        depthCents_ = 0.0;
        elapsedSamples_ = 0.0;
        lengthSamples_ = 1.0;
    }

    /// Starts a glide of `depthSemitones` (signed) landing over
    /// `timeSeconds`, shaped by `curve` (-1..+1, see the header; 0 is the
    /// exponential exactly). Called at note-on.
    void trigger (double depthSemitones, double timeSeconds, double curve = 0.0) noexcept
    {
        const double time = timeSeconds < 0.001 ? 0.001
                          : timeSeconds > 5.0 ? 5.0 : timeSeconds;

        cents_ = 100.0 * depthSemitones;
        depthCents_ = cents_;
        decayPerSample_ = std::exp (-kLandFactor / (time * sampleRate_));
        curve_ = std::clamp (curve, -1.0, 1.0);
        lengthSamples_ = time * sampleRate_;
        elapsedSamples_ = 0.0;
    }

    /// Advances the glide by a block of samples.
    void advance (int numSamples) noexcept
    {
        if (numSamples <= 0 || isExactlyZero (cents_))
            return;

        if (isExactlyZero (curve_))
        {
            cents_ *= std::pow (decayPerSample_, static_cast<double> (numSamples));
        }
        else
        {
            elapsedSamples_ += static_cast<double> (numSamples);
            cents_ = depthCents_ * shapeAt (elapsedSamples_ / lengthSamples_);
        }

        if (std::abs (cents_) < kSnapCents)
            cents_ = 0.0;
    }

    /// The curved landing's remaining fraction at `u` = elapsed / stated
    /// time: the exponential blended to a line towards -1, the exponential
    /// of a power of time towards +1. 1.0 at u = 0 for every curve.
    [[nodiscard]] double shapeAt (double u) const noexcept
    {
        const double t = u < 0.0 ? 0.0 : u;

        if (curve_ < 0.0)
        {
            const double exponential = std::exp (-kLandFactor * t);
            const double line = t < 1.0 ? 1.0 - t : 0.0;
            return (1.0 + curve_) * exponential + (-curve_) * line;
        }

        return std::exp (-kLandFactor * std::pow (t, 1.0 + 3.0 * curve_));
    }

    [[nodiscard]] double getCurve() const noexcept { return curve_; }

    /// The current frequency multiplier for every mode. Exactly 1.0 at
    /// rest, so the resonator's retune guard sees a true no-op.
    [[nodiscard]] double multiplier() const noexcept
    {
        return isExactlyZero (cents_) ? 1.0 : std::exp2 (cents_ / 1200.0);
    }

    [[nodiscard]] double remainingCents() const noexcept { return cents_; }

    [[nodiscard]] bool isActive() const noexcept { return ! isExactlyZero (cents_); }

private:
    double sampleRate_ { 44100.0 };
    double cents_ { 0.0 };
    double decayPerSample_ { 1.0 };

    // the curved landings
    double curve_ { 0.0 };
    double depthCents_ { 0.0 };
    double elapsedSamples_ { 0.0 };
    double lengthSamples_ { 1.0 };
};

} // namespace tezla::dsp
