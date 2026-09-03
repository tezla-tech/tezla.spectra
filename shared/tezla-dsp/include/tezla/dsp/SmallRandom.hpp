// Copyright (c) 2026 The Tezla <thetezla@proton.me>
// Created by The Tezla -- https://github.com/wingit33/tezla.tech
// Music: https://soundcloud.com/thetezla | https://thetezla.bandcamp.com
// Built with development assistance from Claude (Anthropic).
// SPDX-License-Identifier: AGPL-3.0-only
// GNU AGPLv3 (see LICENSE), plus NOTICE.md's attribution term. Keep intact.

#pragma once

// The house random generator: small, deterministic, and the same everywhere.
//
// Lifted out of `UnisonBank.hpp`, where it was first written and where it still
// arrives from for every existing includer, because a third thing now wants it
// and "include the oscillator bank to get a random number" is not a dependency
// anybody should have to explain. The class itself is unchanged.

#include <cstdint>

namespace tezla::dsp {

/// A small deterministic generator, so a test can rely on what it produces.
///
/// xorshift64*, which is cheap, has no visible structure at this scale, and --
/// unlike rand() -- gives the same stream on every platform. That last part is
/// what makes "the drift is bounded" a testable claim rather than a hope.
class SmallRandom
{
public:
    explicit SmallRandom (std::uint64_t seed = 0x9e3779b97f4a7c15ull) noexcept
        : state_ (seed | 1ull)
    {
    }

    void seed (std::uint64_t value) noexcept { state_ = value | 1ull; }

    /// Uniform in [0, 1).
    [[nodiscard]] double next() noexcept
    {
        state_ ^= state_ >> 12;
        state_ ^= state_ << 25;
        state_ ^= state_ >> 27;

        const std::uint64_t value = state_ * 0x2545f4914f6cdd1dull;

        // The top 53 bits, which is exactly a double's mantissa.
        return static_cast<double> (value >> 11) * (1.0 / 9007199254740992.0);
    }

    /// Uniform in [-1, 1).
    [[nodiscard]] double bipolar() noexcept { return next() * 2.0 - 1.0; }

private:
    std::uint64_t state_;
};

} // namespace tezla::dsp
