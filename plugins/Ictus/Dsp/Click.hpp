// Copyright (c) 2026 The Tezla <thetezla@proton.me>
// Created by The Tezla -- https://github.com/wingit33/tezla.tech
// Music: https://soundcloud.com/thetezla | https://thetezla.bandcamp.com
// Built with development assistance from Claude (Anthropic).
// SPDX-License-Identifier: AGPL-3.0-only
// GNU AGPLv3 (see LICENSE), plus NOTICE.md's attribution term. Keep intact.

#pragma once

// The click pair -- a beater's contact, shared by the kick (its Click) and
// the snare (its Crack): one mode of a `ModalResonator` ringing for 3 ms, and
// a seeded noise burst through a one-pole high-pass at the same corner -- the
// 909's Attack knob and SOS's beater click (docs/DSP-REFERENCES.md, "Drum
// synthesis -- Ictus"). This is what velocity is most about, and what makes a
// drum read on a small speaker before its body arrives.
//
// Lifted out of the kick engine at I3 unchanged: the same constants, the same
// arithmetic, and `addTo` adds its two terms to the signal as two separate
// additions in the order the kick always used, so a kick with its click on
// renders bit-identically to the one that shipped at I2 (checked against a
// golden render when it moved).

#include <algorithm>
#include <cmath>
#include <cstdint>

#include <tezla/dsp/DcBlocker.hpp>
#include <tezla/dsp/Exact.hpp>
#include <tezla/dsp/ModalResonator.hpp>
#include <tezla/dsp/UnisonBank.hpp>

namespace tezla::ictus {

class ClickPair
{
public:
    /// The resonator's ring-down.
    static constexpr double kT60Seconds = 0.003;

    /// After four T60s (-240 dB) the resonator is cut exactly rather than
    /// left ringing below anything a double can express.
    static constexpr double kTailSeconds = 0.012;

    /// Below this the noise burst is over, exactly.
    static constexpr double kNoiseFloor = 1.0e-5;

    void prepare (double rate) noexcept
    {
        rate_ = rate > 0.0 ? rate : 48000.0;

        resonator_.prepare (rate_);
        resonator_.setModeCount (1);
        highpass_.prepare (rate_, 3000.0);

        reset();
    }

    void reset() noexcept
    {
        resonator_.reset();
        highpass_.reset();

        samplesLeft_ = 0;
        noiseOn_ = false;
        noiseEnv_ = 0.0;
    }

    /// Strikes. `level` and `noiseLevel` arrive already velocity-scaled, and
    /// exactly 0 means that half is not run at all; `toneHz` is the mode's
    /// pitch and the burst's high-pass corner; `noiseSeconds` the burst's
    /// fall to -60 dB; `seed` the burst's own random stream.
    void start (double level, double toneHz, double noiseLevel, double noiseSeconds,
                std::uint64_t seed) noexcept
    {
        const double hz = std::clamp (toneHz, 200.0, std::min (8000.0, rate_ * 0.4));

        if (! dsp::isExactlyZero (level))
        {
            resonator_.setMode (0, hz, kT60Seconds, 1.0);
            resonator_.excite (0, level);
            samplesLeft_ = static_cast<int> (std::ceil (kTailSeconds * rate_));
        }

        noiseLevel_ = noiseLevel;

        if (! dsp::isExactlyZero (noiseLevel_))
        {
            random_.seed (seed);
            noiseEnv_ = 1.0;
            noiseCoefficient_ = std::exp (-std::log (1000.0)
                                          / (std::clamp (noiseSeconds, 0.0005, 0.008) * rate_));
            highpass_.retune (rate_, hz);
            noiseOn_ = true;
        }
    }

    /// Adds the pair to `x` -- the resonator, then the burst, as two
    /// additions. Nothing is added once both are over.
    void addTo (double& x) noexcept
    {
        if (samplesLeft_ > 0)
        {
            x += resonator_.process();

            if (--samplesLeft_ == 0)
                resonator_.reset();
        }

        if (noiseOn_)
        {
            x += noiseLevel_ * noiseEnv_ * highpass_.process (random_.bipolar());
            noiseEnv_ *= noiseCoefficient_;

            if (noiseEnv_ < kNoiseFloor)
            {
                noiseEnv_ = 0.0;
                noiseOn_ = false;
            }
        }
    }

    [[nodiscard]] bool isActive() const noexcept { return samplesLeft_ > 0 || noiseOn_; }

private:
    double rate_ { 48000.0 };

    dsp::ModalResonator resonator_;
    int samplesLeft_ { 0 };

    bool noiseOn_ { false };
    double noiseLevel_ { 0.0 };
    double noiseEnv_ { 0.0 };
    double noiseCoefficient_ { 0.0 };
    dsp::SmallRandom random_;
    dsp::DcBlocker<double> highpass_;
};

} // namespace tezla::ictus
