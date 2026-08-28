// Copyright (c) 2026 The Tezla <thetezla@proton.me>
// Created by The Tezla -- https://github.com/wingit33/tezla.tech
// Music: https://soundcloud.com/thetezla | https://thetezla.bandcamp.com
// Built with development assistance from Claude (Anthropic).
// SPDX-License-Identifier: AGPL-3.0-only
// GNU AGPLv3 (see LICENSE), plus NOTICE.md's attribution term. Keep intact.

#pragma once

// Metering with real ballistics.
//
// Where a plugin claims analogue behaviour, the meter is part of how the unit
// gets used, not decoration. A VU meter and a peak meter disagree by 10 dB or
// more on percussive material, and that disagreement is exactly the information
// an engineer is reading: VU tells you how loud it is, peak tells you whether
// it is about to clip. Showing only one of them, or showing a peak meter with a
// VU scale printed on it, makes the plugin harder to use than having no meter.
//
// VU here follows the standard: an averaging meter that reaches 99% of its
// final deflection in 300 ms, with the same ballistics coming back down.

#include <algorithm>
#include <cmath>

#include "Decibels.hpp"

namespace tezla::dsp {

class VuMeter
{
public:
    void prepare (double sampleRate) noexcept
    {
        sampleRate_ = sampleRate > 0.0 ? sampleRate : 44100.0;
        reset();
    }

    void reset() noexcept
    {
        meanSquare_ = 0.0;
        peak_       = 0.0;
    }

    /// Feeds a block. Smoothing is applied once per block rather than per
    /// sample -- a meter updates at screen rate, and a per-sample envelope
    /// would cost more than the audio processing it is measuring.
    void processBlock (const double* samples, int numSamples) noexcept
    {
        if (numSamples <= 0)
            return;

        double sumOfSquares = 0.0;
        double blockPeak = 0.0;

        for (int i = 0; i < numSamples; ++i)
        {
            const double sample = samples[i];
            sumOfSquares += sample * sample;
            blockPeak = std::max (blockPeak, std::abs (sample));
        }

        const double blockMeanSquare = sumOfSquares / static_cast<double> (numSamples);
        const double blockSeconds = static_cast<double> (numSamples) / sampleRate_;

        const double vuCoefficient = std::exp (-blockSeconds / kVuTimeConstant);
        meanSquare_ = blockMeanSquare + vuCoefficient * (meanSquare_ - blockMeanSquare);

        // Peak rises instantly and falls slowly, so a transient stays readable
        // long enough to see it.
        if (blockPeak >= peak_)
            peak_ = blockPeak;
        else
            peak_ *= std::exp (-blockSeconds / kPeakDecay);
    }

    [[nodiscard]] double getVuDb()   const noexcept { return gainToDb (std::sqrt (meanSquare_)); }
    [[nodiscard]] double getPeakDb() const noexcept { return gainToDb (peak_); }

private:
    // 99% of full deflection in 300 ms is the VU standard; 1 - e^-4.6 = 0.99,
    // so the time constant is 300 ms / 4.6.
    static constexpr double kVuTimeConstant = 0.300 / 4.6;
    static constexpr double kPeakDecay      = 0.650;

    double sampleRate_  { 44100.0 };
    double meanSquare_  { 0.0 };
    double peak_        { 0.0 };
};

} // namespace tezla::dsp
