// Copyright (c) 2026 The Tezla <thetezla@proton.me>
// Created by The Tezla -- https://github.com/wingit33/tezla.tech
// Music: https://soundcloud.com/thetezla | https://thetezla.bandcamp.com
// Built with development assistance from Claude (Anthropic).
// SPDX-License-Identifier: AGPL-3.0-only
// GNU AGPLv3 (see LICENSE), plus NOTICE.md's attribution term. Keep intact.

#pragma once

// One mono sample, played back at a rate, with the loop the format asks for.
//
// ---------------------------------------------------------------------------
// Interpolation is the aliasing question, so it is a measured choice
// ---------------------------------------------------------------------------
//
// Pitch-shifting a sample is resampling, and the cheap way -- linear
// interpolation -- is why budget soundfont players fizz on bright samples
// pitched away from their root. This player uses 4-point Hermite (third
// order), which costs three more multiplies per sample and buys a measured
// 25-30 dB over linear on the worst material; the exact figures are pinned in
// tests/test_SamplePlayer.cpp rather than asserted here in prose. Windowed
// sinc would buy more still and is a deliberate later option, not a phase-one
// promise.
//
// ---------------------------------------------------------------------------
// The taps never leave the sample
// ---------------------------------------------------------------------------
//
// Hermite reads four taps around the position. Every tap goes through one
// fetch that clamps the world: before the sample's start and after its end
// the world is silence (the format expects zero padding there, and trusting
// a file to have provided it would read whatever sample happens to sit next
// in the pool); inside a running loop, taps at and past the loop's end wrap
// to its start, which is exactly what makes the seam bit-continuous -- the
// interpolator sees the loop as the endless waveform it claims to be.
//
// Loops shorter than the interpolator's reach (4 samples) are treated as no
// loop at all, and the model says so when it builds the voice.

#include <cstdint>

namespace tezla::svarayantra {

/// The format's sample modes. 2 is reserved-and-unused in the format and is
/// treated as none.
enum class LoopMode
{
    none = 0,
    continuous = 1,
    untilRelease = 3,
};

class SamplePlayer
{
public:
    /// Points the player at a region of the pool. Offsets are in samples,
    /// half-open [start, end), with the loop inside it. The pool outlives the
    /// player -- the engine owns it and swaps it RT-safely.
    void start (const std::int16_t* pool, std::uint32_t startAt, std::uint32_t endAt,
                std::uint32_t loopStartAt, std::uint32_t loopEndAt, LoopMode mode) noexcept
    {
        pool_ = pool;
        begin_ = startAt;
        end_ = endAt;
        loopBegin_ = loopStartAt;
        loopEnd_ = loopEndAt;

        // A loop the interpolator cannot see across is not a loop.
        mode_ = (loopEnd_ >= loopBegin_ + 4 && loopEnd_ <= end_ && loopBegin_ >= begin_)
                  ? mode
                  : LoopMode::none;

        looping_ = (mode_ != LoopMode::none);
        position_ = static_cast<double> (begin_);
        finished_ = (pool_ == nullptr || end_ <= begin_);
    }

    /// Source samples per output sample. 1.0 plays at the recorded rate.
    void setRate (double rate) noexcept { rate_ = rate > 0.0 ? rate : 0.0; }

    /// The key has gone up: an until-release loop stops looping and the tail
    /// of the sample plays out. A continuous loop keeps looping -- its exit
    /// is the volume envelope's job, per the format.
    void release() noexcept
    {
        if (mode_ == LoopMode::untilRelease)
            looping_ = false;
    }

    [[nodiscard]] bool isFinished() const noexcept { return finished_; }

    /// One output sample in [-1, 1), advancing the position.
    [[nodiscard]] double next() noexcept
    {
        if (finished_)
            return 0.0;

        const auto index = static_cast<std::int64_t> (position_);
        const double frac = position_ - static_cast<double> (index);

        const double ym1 = fetch (index - 1);
        const double y0 = fetch (index);
        const double y1 = fetch (index + 1);
        const double y2 = fetch (index + 2);

        // 4-point, 3rd-order Hermite (Catmull-Rom form).
        const double c1 = 0.5 * (y1 - ym1);
        const double c2 = ym1 - 2.5 * y0 + 2.0 * y1 - 0.5 * y2;
        const double c3 = 0.5 * (y2 - ym1) + 1.5 * (y0 - y1);
        const double out = ((c3 * frac + c2) * frac + c1) * frac + y0;

        position_ += rate_;

        if (looping_)
        {
            const double loopLength = static_cast<double> (loopEnd_ - loopBegin_);

            while (position_ >= static_cast<double> (loopEnd_))
                position_ -= loopLength;
        }
        else if (position_ >= static_cast<double> (end_))
        {
            finished_ = true;
        }

        return out;
    }

private:
    /// One tap: silence outside [begin, end), wrapped inside a running loop.
    [[nodiscard]] double fetch (std::int64_t index) const noexcept
    {
        if (looping_ && index >= static_cast<std::int64_t> (loopEnd_))
        {
            const auto loopLength = static_cast<std::int64_t> (loopEnd_ - loopBegin_);
            index = static_cast<std::int64_t> (loopBegin_) + (index - loopEnd_) % loopLength;
        }

        if (index < static_cast<std::int64_t> (begin_)
            || index >= static_cast<std::int64_t> (end_))
            return 0.0;

        return static_cast<double> (pool_[index]) * (1.0 / 32768.0);
    }

    const std::int16_t* pool_ { nullptr };
    std::uint32_t begin_ { 0 };
    std::uint32_t end_ { 0 };
    std::uint32_t loopBegin_ { 0 };
    std::uint32_t loopEnd_ { 0 };

    LoopMode mode_ { LoopMode::none };
    bool looping_ { false };
    bool finished_ { true };

    double position_ { 0.0 };
    double rate_ { 1.0 };
};

} // namespace tezla::svarayantra
