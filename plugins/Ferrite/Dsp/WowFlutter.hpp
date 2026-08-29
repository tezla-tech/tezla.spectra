// Copyright (c) 2026 The Tezla <thetezla@proton.me>
// Created by The Tezla -- https://github.com/wingit33/tezla.tech
// Music: https://soundcloud.com/thetezla | https://thetezla.bandcamp.com
// Built with development assistance from Claude (Anthropic).
// SPDX-License-Identifier: AGPL-3.0-only
// GNU AGPLv3 (see LICENSE), plus NOTICE.md's attribution term. Keep intact.

#pragma once

// Wow and flutter: the tape never moves at quite the speed it should.
//
// One fractional-delay line, read at a centre offset the modulators wobble.
// The FLUTTER shape is the reference's TC-260 fit, taken with attribution
// (technical references/ferrite/FlutterProcess.*): three partials at f, 2f
// and 3f with fixed phases (0, 13*pi/4, -pi/10) and amplitudes -230, -80
// and -99 microseconds around a +350 us centre -- a motor's signature, not
// a synthesised vibrato, which is why it sounds like a machine and not an
// LFO. The WOW is derived: one slow sine whose rate drifts and whose
// amplitude breathes through an Ornstein-Uhlenbeck process (mean-reverting
// noise -- the reference wraps the same idea in its OHProcess), seeded and
// deterministic so every test can be exact.
//
// ---------------------------------------------------------------------------
// Latency is the honest price of a modulated delay
// ---------------------------------------------------------------------------
//
// The read head needs room on both sides of the write head, so the centre
// offset -- one millisecond, rounded to a whole sample at the current rate
// -- is unavoidable delay. It is REPORTED (latencySamples()) rather than
// hidden: the engine's dry path and the host's PDC both align to it, and at
// zero depth the output is the input delayed by exactly that integer --
// bit-for-bit, because 4-point Hermite collapses to the sample itself at
// integer positions. That is the neutral-setting contract of CLAUDE.md
// section 7, honoured through the latency declaration.
//
// Depths are smoothed over ~50 ms; every state advances strictly per
// sample, so the host's block size cannot bend any of it.

#include <cmath>
#include <cstdint>
#include <vector>

#include <tezla/dsp/SmoothedValue.hpp>

namespace tezla::ferrite {

class WowFlutter
{
public:
    void prepare (double sampleRate)
    {
        sampleRate_ = sampleRate;
        centre_ = static_cast<int> (std::lround (0.001 * sampleRate));

        // Room for the centre plus every modulator at full depth, doubled.
        ring_.assign (static_cast<std::size_t> (4 * centre_ + 8), 0.0);

        wowDepthSmoothed_.prepare (sampleRate, 0.05);
        flutterDepthSmoothed_.prepare (sampleRate, 0.05);
        wowDepthSmoothed_.setCurrentAndTarget (wowDepth_);
        flutterDepthSmoothed_.setCurrentAndTarget (flutterDepth_);

        reset();
    }

    void reset() noexcept
    {
        std::fill (ring_.begin(), ring_.end(), 0.0);
        write_ = 0;
        wowPhase_ = 0.0;
        flutterPhase_ = 0.0;
        ouAmplitude_ = 0.0;
        ouRate_ = 0.0;
        noiseState_ = seed_;
        wowDepthSmoothed_.setCurrentAndTarget (wowDepth_);
        flutterDepthSmoothed_.setCurrentAndTarget (flutterDepth_);
    }

    /// Depths 0..1. Smoothed; zero is the bit-exact (latency-matched)
    /// neutral.
    void setWowDepth (double depth) noexcept
    {
        wowDepth_ = depth;
        wowDepthSmoothed_.setTarget (depth);
    }

    void setFlutterDepth (double depth) noexcept
    {
        flutterDepth_ = depth;
        flutterDepthSmoothed_.setTarget (depth);
    }

    void setWowRateHz (double hz) noexcept { wowRateHz_ = hz; }
    void setFlutterRateHz (double hz) noexcept { flutterRateHz_ = hz; }

    /// The random stream's identity. Takes effect at the next reset() (or
    /// prepare()), so two instances seeded alike replay identically.
    void setSeed (std::uint64_t seed) noexcept { seed_ = seed == 0 ? 1 : seed; }

    [[nodiscard]] int latencySamples() const noexcept { return centre_; }

    /// The current read offset in samples -- the margin claim, measurable.
    [[nodiscard]] double currentDelaySamples() const noexcept { return lastDelay_; }

    [[nodiscard]] double process (double x) noexcept
    {
        ring_[static_cast<std::size_t> (write_)] = x;

        // --- the modulators, per sample.
        const double wowDepth = wowDepthSmoothed_.next();
        const double flutterDepth = flutterDepthSmoothed_.next();

        // Ornstein-Uhlenbeck pair: amplitude breath and rate drift, both
        // mean-reverting to zero. Euler-Maruyama with per-sample noise.
        const double dt = 1.0 / sampleRate_;
        ouAmplitude_ += -kOuReversion * ouAmplitude_ * dt
                          + kOuSigma * std::sqrt (dt) * nextNoise();
        ouRate_ += -kOuReversion * ouRate_ * dt
                     + kOuSigma * std::sqrt (dt) * nextNoise();

        const double wowRateNow = wowRateHz_ * (1.0 + 0.15 * ouRate_);
        wowPhase_ += kTwoPi * wowRateNow / sampleRate_;
        wowPhase_ -= wowPhase_ >= kTwoPi ? kTwoPi : 0.0;

        flutterPhase_ += kTwoPi * flutterRateHz_ / sampleRate_;
        flutterPhase_ -= flutterPhase_ >= kTwoPi ? kTwoPi : 0.0;

        // Microseconds of deviation, then samples.
        const double wowUs = wowDepth * kWowSpanUs
                               * std::sin (wowPhase_)
                               * (0.7 + 0.3 * ouAmplitude_);

        const double flutterUs = flutterDepth
            * (kFlutterAmp1Us * std::sin (flutterPhase_)
             + kFlutterAmp2Us * std::sin (2.0 * flutterPhase_ + kFlutterPhase2)
             + kFlutterAmp3Us * std::sin (3.0 * flutterPhase_ + kFlutterPhase3));

        double delay = centre_ + (wowUs + flutterUs) * 1.0e-6 * sampleRate_;

        // By construction the spans fit inside the centre; the clamp is the
        // last-resort rail the bounds test proves is never touched.
        const double limit = 2.0 * centre_ - 2.0;
        delay = delay < 2.0 ? 2.0 : delay > limit ? limit : delay;
        lastDelay_ = delay;

        // --- 4-point Hermite read around the fractional position.
        const double readPosition = static_cast<double> (write_) - delay;
        const auto size = static_cast<int> (ring_.size());

        auto at = [&] (int index) noexcept
        {
            index %= size;
            index += index < 0 ? size : 0;
            return ring_[static_cast<std::size_t> (index)];
        };

        const auto whole = static_cast<int> (std::floor (readPosition));
        const double frac = readPosition - whole;

        const double ym1 = at (whole - 1);
        const double y0 = at (whole);
        const double y1 = at (whole + 1);
        const double y2 = at (whole + 2);

        const double c1 = 0.5 * (y1 - ym1);
        const double c2 = ym1 - 2.5 * y0 + 2.0 * y1 - 0.5 * y2;
        const double c3 = 0.5 * (y2 - ym1) + 1.5 * (y0 - y1);
        const double out = ((c3 * frac + c2) * frac + c1) * frac + y0;

        write_ = write_ + 1 == size ? 0 : write_ + 1;

        return out;
    }

private:
    static constexpr double kTwoPi = 6.283185307179586;

    // The TC-260 flutter fit (attributed; see the header comment).
    static constexpr double kFlutterAmp1Us = -230.0;
    static constexpr double kFlutterAmp2Us = -80.0;
    static constexpr double kFlutterAmp3Us = -99.0;
    static constexpr double kFlutterPhase2 = 13.0 * 3.141592653589793 / 4.0;
    static constexpr double kFlutterPhase3 = -3.141592653589793 / 10.0;

    // Wow span and the OU processes' character. The span plus the flutter
    // sum (409 us) stays inside the 1 ms centre with margin.
    static constexpr double kWowSpanUs = 450.0;
    static constexpr double kOuReversion = 1.5;
    static constexpr double kOuSigma = 0.8;

    /// xorshift64*, mapped to roughly unit-variance zero-mean noise.
    [[nodiscard]] double nextNoise() noexcept
    {
        noiseState_ ^= noiseState_ >> 12;
        noiseState_ ^= noiseState_ << 25;
        noiseState_ ^= noiseState_ >> 27;

        const auto scaled = noiseState_ * 0x2545F4914F6CDD1DULL;
        return (static_cast<double> (scaled >> 11)
                  / static_cast<double> (1ULL << 53) - 0.5) * 3.464;
    }

    double sampleRate_ { 48000.0 };
    int centre_ { 48 };
    std::vector<double> ring_;
    int write_ { 0 };

    double wowDepth_ { 0.0 };
    double flutterDepth_ { 0.0 };
    double wowRateHz_ { 0.9 };
    double flutterRateHz_ { 12.0 };

    tezla::dsp::SmoothedValue<double> wowDepthSmoothed_;
    tezla::dsp::SmoothedValue<double> flutterDepthSmoothed_;

    double wowPhase_ { 0.0 };
    double flutterPhase_ { 0.0 };
    double ouAmplitude_ { 0.0 };
    double ouRate_ { 0.0 };

    std::uint64_t seed_ { 0x7E21A5F00D5EED01ULL };
    std::uint64_t noiseState_ { 0x7E21A5F00D5EED01ULL };

    double lastDelay_ { 0.0 };
};

} // namespace tezla::ferrite
