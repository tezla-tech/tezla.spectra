// Copyright (c) 2026 The Tezla <thetezla@proton.me>
// Created by The Tezla -- https://github.com/wingit33/tezla.tech
// Music: https://soundcloud.com/thetezla | https://thetezla.bandcamp.com
// Built with development assistance from Claude (Anthropic).
// SPDX-License-Identifier: AGPL-3.0-only
// GNU AGPLv3 (see LICENSE), plus NOTICE.md's attribution term. Keep intact.

#pragma once

// How the object gets hit.
//
// The mallet and the pluck never exist as time-domain pulses at all: a modal
// bank is excited per mode, so the strike injects each mode with the CLOSED
// FORM of what a contact pulse would have given it -- the Hann pulse's own
// spectrum for a mallet, the plucked string's 1/k^2 displacement series for
// a pluck, each combed by the strike position. Spectrally exact, band-limited
// by construction, and nothing to alias: the excitation cannot contain a
// frequency that was never computed.
//
//   Mallet   force pulse F(t) = (1 - cos(2 pi t / w)) / 2 over a contact
//            time w set by Hardness. Its transform's magnitude, normalised
//            to 1 at DC, is |sinc(u) / (1 - u^2)| with u = f w -- the
//            removable point at u = 1 is 1/2, the true nulls start at
//            u = 2, and the envelope falls as u^-3. Soft felt is a long
//            contact (little above 1/w); a metal beater is a short one.
//
//   Pluck    a displacement start rather than a velocity one: the classic
//            plucked-string series b_k ~ sin(k pi p) / k^2 -- inherently
//            darker than any mallet, which is the audible difference
//            between the two.
//
//   Position both are combed by sin(k pi p): strike the middle of a string
//            and every even mode sits on a node and gets EXACTLY nothing.
//            The same law is applied across every material as the voicing
//            rule -- the 2D shapes of membranes and plates would need a
//            two-dimensional strike point, and one knob that behaves the
//            same everywhere beats two that behave differently per
//            material. Stated as a choice, not smuggled as physics.
//
//   Noise    the scrape: a seeded, Hann-enveloped, hardness-darkened noise
//            burst driven through the bank's continuous input. Finite, and
//            exactly silent once spent.
//
//   Roll     a bouncing-ball retrigger clock: each interval is the last
//            times a ratio, floored at a minimum -- a dropped mallet
//            accelerates into a buzz, which is exactly what this is.
//            Seeded humanise on timing and velocity, deterministic replay.

#include <cmath>
#include <cstdint>
#include <numbers>

namespace tezla::malleus {

/// The mallet's contact-pulse spectrum, normalised to 1 at DC.
/// u = frequency * contactSeconds.
[[nodiscard]] inline double hannPulseSpectrum (double u) noexcept
{
    const double magnitude = std::abs (u);

    if (magnitude < 1.0e-9)
        return 1.0;

    if (std::abs (magnitude - 1.0) < 1.0e-9)
        return 0.5;   // the removable point: sinc's zero cancels the pole

    const double sinc = std::sin (std::numbers::pi * magnitude)
                          / (std::numbers::pi * magnitude);

    return std::abs (sinc / (1.0 - magnitude * magnitude));
}

/// Strike-position comb for spatial mode k (1-based): sin(k pi p).
[[nodiscard]] inline double positionWeight (int k, double position) noexcept
{
    return std::sin (static_cast<double> (k) * std::numbers::pi * position);
}

/// Hardness 0..1 to contact time: 8 ms of felt down to 0.15 ms of brass,
/// log-spaced so the knob moves brightness evenly.
[[nodiscard]] inline double contactSeconds (double hardness) noexcept
{
    const double h = hardness < 0.0 ? 0.0 : hardness > 1.0 ? 1.0 : hardness;
    return 0.008 * std::pow (0.15e-3 / 0.008, h);
}

/// Fills per-mode excitation amounts for one mallet hit.
inline void malletWeights (double* amounts, const double* modeFrequencies,
                           int modeCount, double position, double hardness,
                           double velocity) noexcept
{
    const double contact = contactSeconds (hardness);

    for (int mode = 0; mode < modeCount; ++mode)
        amounts[mode] = velocity
                      * positionWeight (mode + 1, position)
                      * hannPulseSpectrum (modeFrequencies[mode] * contact);
}

/// Fills per-mode excitation amounts for a pluck.
inline void pluckWeights (double* amounts, int modeCount, double position,
                          double velocity) noexcept
{
    for (int mode = 0; mode < modeCount; ++mode)
    {
        const double k = static_cast<double> (mode + 1);
        amounts[mode] = velocity * positionWeight (mode + 1, position) / (k * k);
    }
}

/// The scrape component: a finite, seeded, darkened noise burst.
class NoiseBurst
{
public:
    void prepare (double sampleRate) noexcept
    {
        sampleRate_ = sampleRate > 0.0 ? sampleRate : 44100.0;
        remaining_ = 0;
    }

    void setSeed (std::uint64_t seed) noexcept { seed_ = seed == 0 ? 1 : seed; }

    /// Starts a burst: length tracks the mallet's contact time (a scrape is
    /// the contact heard directly), darkness tracks hardness.
    void trigger (double hardness, double amount) noexcept
    {
        const double seconds = 4.0 * contactSeconds (hardness) + 0.002;
        length_ = static_cast<int> (seconds * sampleRate_);
        remaining_ = length_;
        amount_ = amount;

        // One-pole darkening: soft mallets scrape dull, hard ones bright.
        const double h = hardness < 0.0 ? 0.0 : hardness > 1.0 ? 1.0 : hardness;
        const double corner = 400.0 + 12000.0 * h;
        coefficient_ = 1.0 - std::exp (-2.0 * std::numbers::pi * corner / sampleRate_);

        state_ = seed_;
        lowpass_ = 0.0;
    }

    /// Exactly 0.0 once the burst is spent -- silence is silence.
    [[nodiscard]] double next() noexcept
    {
        if (remaining_ <= 0)
            return 0.0;

        state_ ^= state_ >> 12;
        state_ ^= state_ << 25;
        state_ ^= state_ >> 27;

        const double white = static_cast<double> ((state_ * std::uint64_t { 0x2545F4914F6CDD1D }) >> 11)
                               / static_cast<double> (std::uint64_t { 1 } << 52) - 1.0;

        lowpass_ += coefficient_ * (white - lowpass_);

        // Hann envelope over the burst, so it neither clicks in nor out.
        const double phase = 1.0 - static_cast<double> (remaining_)
                                     / static_cast<double> (length_);
        const double envelope = 0.5 - 0.5 * std::cos (2.0 * std::numbers::pi * phase);

        --remaining_;

        return amount_ * envelope * lowpass_;
    }

    [[nodiscard]] bool isActive() const noexcept { return remaining_ > 0; }

private:
    double sampleRate_ { 44100.0 };
    std::uint64_t seed_ { 0x5CA1AB1E0DDBA11ULL };
    std::uint64_t state_ { 0x5CA1AB1E0DDBA11ULL };
    int length_ { 1 };
    int remaining_ { 0 };
    double amount_ { 0.0 };
    double coefficient_ { 0.1 };
    double lowpass_ { 0.0 };
};

/// The bouncing-ball retrigger clock behind a mallet roll.
class RollClock
{
public:
    void prepare (double sampleRate) noexcept
    {
        sampleRate_ = sampleRate > 0.0 ? sampleRate : 44100.0;
        countdown_ = -1;
    }

    void setSeed (std::uint64_t seed) noexcept { seed_ = seed == 0 ? 1 : seed; }

    /// Drops the ball: first re-strike after `startSeconds`, each following
    /// interval times `ratio` (< 1 accelerates, like a real bounce), floored
    /// at `minimumSeconds` where the roll settles into its buzz. `humanise`
    /// 0..1 jitters both timing and velocity, deterministically per seed.
    void trigger (double startSeconds, double ratio, double minimumSeconds,
                  double humanise) noexcept
    {
        interval_ = startSeconds < 1.0e-3 ? 1.0e-3 : startSeconds;
        ratio_ = ratio < 0.1 ? 0.1 : ratio > 2.0 ? 2.0 : ratio;
        minimum_ = minimumSeconds < 1.0e-3 ? 1.0e-3 : minimumSeconds;
        humanise_ = humanise < 0.0 ? 0.0 : humanise > 1.0 ? 1.0 : humanise;

        state_ = seed_;
        countdown_ = static_cast<int> (interval_ * sampleRate_);
        if (countdown_ < 1)
            countdown_ = 1;
    }

    void stop() noexcept { countdown_ = -1; }

    /// Advances one sample. Returns a re-strike velocity in (0, 1] on the
    /// sample a bounce lands, 0.0 otherwise. Pre-decrement, so an interval of
    /// N samples fires on exactly the Nth call -- the gap between bounces IS
    /// the scheduled count, and the test can assert it to the sample.
    [[nodiscard]] double next() noexcept
    {
        if (countdown_ < 0)
            return 0.0;

        if (--countdown_ > 0)
            return 0.0;

        // The bounce fires; schedule the next one. The floor is where a real
        // bounce settles into its buzz; the ceiling only matters for a
        // decelerating roll (ratio > 1), whose interval would otherwise grow
        // without bound and eventually overflow the sample countdown.
        interval_ = interval_ * ratio_;
        if (interval_ < minimum_)
            interval_ = minimum_;
        if (interval_ > 30.0)
            interval_ = 30.0;

        const double jitter = 1.0 + humanise_ * 0.25 * nextNoise();
        countdown_ = static_cast<int> (interval_ * jitter * sampleRate_);
        if (countdown_ < 1)
            countdown_ = 1;

        const double velocity = 1.0 + humanise_ * 0.3 * nextNoise();
        return velocity < 0.05 ? 0.05 : velocity > 1.0 ? 1.0 : velocity;
    }

    [[nodiscard]] bool isRunning() const noexcept { return countdown_ >= 0; }

private:
    [[nodiscard]] double nextNoise() noexcept
    {
        state_ ^= state_ >> 12;
        state_ ^= state_ << 25;
        state_ ^= state_ >> 27;

        return static_cast<double> ((state_ * std::uint64_t { 0x2545F4914F6CDD1D }) >> 11)
                 / static_cast<double> (std::uint64_t { 1 } << 52) - 1.0;
    }

    double sampleRate_ { 44100.0 };
    std::uint64_t seed_ { 0xB0C1B0C1B0C1B0C1ULL };
    std::uint64_t state_ { 0xB0C1B0C1B0C1B0C1ULL };

    double interval_ { 0.1 };
    double ratio_ { 0.8 };
    double minimum_ { 0.02 };
    double humanise_ { 0.0 };
    int countdown_ { -1 };
};

} // namespace tezla::malleus
