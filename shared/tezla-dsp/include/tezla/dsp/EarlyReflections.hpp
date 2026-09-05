// Copyright (c) 2026 The Tezla <thetezla@proton.me>
// Created by The Tezla -- https://github.com/wingit33/tezla.tech
// Music: https://soundcloud.com/thetezla | https://thetezla.bandcamp.com
// Built with development assistance from Claude (Anthropic).
// SPDX-License-Identifier: AGPL-3.0-only
// GNU AGPLv3 (see LICENSE), plus NOTICE.md's attribution term. Keep intact.

#pragma once

// Early reflections as a sparse room: the first tens of milliseconds of a
// room's answer to a drum, before the tail -- the thing a sampled kick or
// snare carries from the room it was recorded in and a circuit model has
// nowhere to get.
//
// The room is a FIR of a few dozen unit impulses at irregular delays with
// random signs, decaying over its length -- the *velvet noise* construction
// (Karjalainen and Jarvelainen, "Reverberation modeling using velvet noise",
// AES 30th conference, 2007; known second-hand, the paper itself was not
// fetched here, and nothing numeric is taken from it -- see
// docs/DSP-REFERENCES.md). One impulse per cell, placed anywhere within it,
// so the pattern has no periodicity and no gaps; two channels on two
// independent draws, so the reflections differ left and right the way a
// room's do and the pair decorrelates without any phase trick. Each
// channel's gains are normalised to unit energy, so the room's level is the
// caller's one number and not a function of its length.
//
// A FINITE reflection pattern is a gated room by construction: the last tap
// is the last thing heard, and the classic gated snare of the eighties falls
// out of the length control rather than needing a gate. Ninety-six taps a
// sample while the line holds anything; nothing once it has drained, and the
// drain is counted rather than assumed so the pad's activity stays honest.
//
// Real-time safe: prepare() allocates the line once; design() and process()
// never allocate, so a room can be re-drawn at the internal rate from the
// audio thread when the oversampling factor changes.

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <numbers>
#include <vector>

#include "Exact.hpp"
#include "SmallRandom.hpp"

namespace tezla::dsp {

class EarlyReflections
{
public:
    /// Taps a channel: one per cell of the room's length.
    static constexpr int kTapsPerChannel = 48;

    /// The longest room the line is sized for by default, and the shortest
    /// a design will draw.
    static constexpr double kMaxSeconds = 0.25;
    static constexpr double kMinSeconds = 0.01;

    /// The last tap sits this far under the first: a room's answer falling
    /// away, and the gate at its end is then a small step rather than a cut.
    static constexpr double kDecayDb = 30.0;

    /// Below this the tone filter's memory is cleared to exact zero once the
    /// line has drained, so a finished room leaves exact zeros.
    static constexpr double kQuiet = 1.0e-12;

    /// Allocates the line for `maxSeconds` at `sampleRate`. The one call that
    /// allocates; design() and process() never do.
    void prepare (double sampleRate, double maxSeconds = kMaxSeconds)
    {
        sampleRate_ = sampleRate > 0.0 ? sampleRate : 48000.0;
        maxSeconds_ = std::clamp (maxSeconds, kMinSeconds, 2.0);

        const auto length = static_cast<std::size_t> (std::ceil (maxSeconds_ * sampleRate_)) + 1;
        line_.assign (length, 0.0);

        toneHz_ = -1.0;
        setToneHz (0.0);
        design (std::min (0.1, maxSeconds_), 1u);
        reset();
    }

    /// The rate the taps and the tone are designed at, when it differs from
    /// the rate the line was sized for: a caller that allocated for the
    /// highest internal rate it can run at re-aims the room here when the
    /// oversampling factor changes, with nothing allocated. The tone's
    /// coefficient is redesigned for the new rate on its next setToneHz.
    void setSampleRate (double sampleRate) noexcept
    {
        const double wanted = sampleRate > 0.0 ? sampleRate : 48000.0;

        if (isExactly (wanted, sampleRate_))
            return;

        sampleRate_ = wanted;

        const double tone = toneHz_;
        toneHz_ = -1.0;
        setToneHz (tone);
    }

    /// Clears the line and the tone filters. The design is kept.
    void reset() noexcept
    {
        std::fill (line_.begin(), line_.end(), 0.0);
        write_ = 0;
        lowLeft_ = 0.0;
        lowRight_ = 0.0;
        sinceInput_ = lengthSamples_ + 1;
    }

    /// Draws the room: `seconds` long (clamped to what the line holds), from
    /// `seed`. Per channel, the length is cut into kTapsPerChannel cells and
    /// one tap is placed at a uniformly random point of each, with a random
    /// sign and a gain that falls exponentially to -kDecayDb at the end;
    /// the gains are then normalised so a unit impulse returns unit energy.
    /// No allocation; safe to call between hits from the audio thread.
    void design (double seconds, std::uint64_t seed) noexcept
    {
        seconds_ = std::clamp (seconds, kMinSeconds, maxSeconds_);

        const int longest = static_cast<int> (line_.size()) - 1;
        lengthSamples_ = std::clamp (static_cast<int> (seconds_ * sampleRate_), 2 * kTapsPerChannel, longest);

        SmallRandom random (seed);
        const double cell = static_cast<double> (lengthSamples_) / static_cast<double> (kTapsPerChannel);

        for (int channel = 0; channel < 2; ++channel)
        {
            double energy = 0.0;

            for (int tap = 0; tap < kTapsPerChannel; ++tap)
            {
                const double position = (static_cast<double> (tap) + random.next()) * cell;
                const int delay = std::clamp (static_cast<int> (position), 0, lengthSamples_ - 1);
                const double fall = std::pow (10.0, -kDecayDb / 20.0 * static_cast<double> (delay)
                                                        / static_cast<double> (lengthSamples_));
                const double sign = random.next() < 0.5 ? -1.0 : 1.0;

                delay_[channel][tap] = delay;
                gain_[channel][tap] = sign * fall;
                energy += fall * fall;
            }

            const double normalise = 1.0 / std::sqrt (energy);

            for (int tap = 0; tap < kTapsPerChannel; ++tap)
                gain_[channel][tap] *= normalise;
        }

        if (sinceInput_ > lengthSamples_)
            sinceInput_ = lengthSamples_ + 1;
    }

    /// A one-pole low-pass on each channel's reflections -- a room is duller
    /// than the drum that fills it. 0 (or a corner at the top of the band)
    /// is none, exactly, by branch. State-preserving: the corner can move
    /// under a ringing room without a step.
    void setToneHz (double hz) noexcept
    {
        const double wanted = (hz <= 0.0 || hz >= sampleRate_ * 0.45) ? 0.0 : hz;

        if (isExactly (wanted, toneHz_))
            return;

        toneHz_ = wanted;
        toneOn_ = ! isExactlyZero (wanted);
        toneCoefficient_ = toneOn_ ? 1.0 - std::exp (-2.0 * std::numbers::pi * wanted / sampleRate_) : 1.0;
    }

    /// One sample in, the two channels' reflections out. The line always
    /// takes the input, so a room turned up mid-hit hears what was already
    /// in it.
    void process (double input, double& left, double& right) noexcept
    {
        const int size = static_cast<int> (line_.size());

        line_[static_cast<std::size_t> (write_)] = input;

        if (! isExactlyZero (input))
            sinceInput_ = 0;
        else if (sinceInput_ <= lengthSamples_)
            ++sinceInput_;

        double l = 0.0;
        double r = 0.0;

        for (int tap = 0; tap < kTapsPerChannel; ++tap)
        {
            int indexLeft = write_ - delay_[0][tap];
            if (indexLeft < 0)
                indexLeft += size;

            int indexRight = write_ - delay_[1][tap];
            if (indexRight < 0)
                indexRight += size;

            l += gain_[0][tap] * line_[static_cast<std::size_t> (indexLeft)];
            r += gain_[1][tap] * line_[static_cast<std::size_t> (indexRight)];
        }

        if (++write_ >= size)
            write_ = 0;

        if (toneOn_)
        {
            lowLeft_ += toneCoefficient_ * (l - lowLeft_);
            lowRight_ += toneCoefficient_ * (r - lowRight_);
            l = lowLeft_;
            r = lowRight_;

            // Once the line has drained the filters' memory is all that is
            // left; below kQuiet it is cleared, so the room ends in exact
            // zeros rather than a denormal trickle.
            if (sinceInput_ > lengthSamples_
                && std::abs (lowLeft_) < kQuiet && std::abs (lowRight_) < kQuiet)
            {
                lowLeft_ = 0.0;
                lowRight_ = 0.0;
            }
        }

        left = l;
        right = r;
    }

    /// Whether the line still holds an input that can reach a tap, or the
    /// tone filters still hold anything. Exactly false once the room has
    /// finished, which is when a caller may stop running it.
    [[nodiscard]] bool isActive() const noexcept
    {
        return sinceInput_ <= lengthSamples_
            || ! isExactlyZero (lowLeft_) || ! isExactlyZero (lowRight_);
    }

    [[nodiscard]] int getLengthSamples() const noexcept { return lengthSamples_; }
    [[nodiscard]] double getLengthSeconds() const noexcept { return seconds_; }
    [[nodiscard]] double getToneHz() const noexcept { return toneHz_; }
    [[nodiscard]] double getSampleRate() const noexcept { return sampleRate_; }

    /// One tap's delay and gain, for a test to check the draw against the
    /// rule that made it.
    [[nodiscard]] int getTapDelay (int channel, int tap) const noexcept
    {
        return channel >= 0 && channel < 2 && tap >= 0 && tap < kTapsPerChannel ? delay_[channel][tap] : 0;
    }

    [[nodiscard]] double getTapGain (int channel, int tap) const noexcept
    {
        return channel >= 0 && channel < 2 && tap >= 0 && tap < kTapsPerChannel ? gain_[channel][tap] : 0.0;
    }

    /// The line's storage, so a test can assert that a redesign moved nothing.
    [[nodiscard]] const double* lineData() const noexcept { return line_.data(); }

private:
    double sampleRate_ { 48000.0 };
    double maxSeconds_ { kMaxSeconds };
    double seconds_ { 0.1 };
    int lengthSamples_ { 2 * kTapsPerChannel };

    std::vector<double> line_;
    int write_ { 0 };
    int sinceInput_ { 0 };

    int delay_[2][kTapsPerChannel] {};
    double gain_[2][kTapsPerChannel] {};

    bool toneOn_ { false };
    double toneHz_ { 0.0 };
    double toneCoefficient_ { 1.0 };
    double lowLeft_ { 0.0 };
    double lowRight_ { 0.0 };
};

} // namespace tezla::dsp
