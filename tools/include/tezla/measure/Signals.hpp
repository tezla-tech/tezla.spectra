// Copyright (c) 2026 The Tezla <thetezla@proton.me>
// Created by The Tezla -- https://github.com/wingit33/tezla.tech
// Music: https://soundcloud.com/thetezla | https://thetezla.bandcamp.com
// Built with development assistance from Claude (Anthropic).
// SPDX-License-Identifier: AGPL-3.0-only
// GNU AGPLv3 (see LICENSE), plus NOTICE.md's attribution term. Keep intact.

#pragma once

#include <cmath>
#include <cstddef>
#include <numbers>
#include <vector>

namespace tezla::measure {

/// The nearest frequency to `desiredHz` that lands exactly on an FFT bin.
///
/// Using a bin-exact sine means the analysis needs no window and has no
/// spectral leakage, so a measured -110 dB aliasing floor is real rather than
/// an artefact of the measurement. Always generate test tones this way.
[[nodiscard]] inline double binExactFrequency (double desiredHz, double sampleRate, std::size_t fftSize)
{
    const double binWidth = sampleRate / static_cast<double> (fftSize);
    const double bin      = std::round (desiredHz / binWidth);
    return std::max (1.0, bin) * binWidth;
}

[[nodiscard]] inline std::vector<double> sine (double frequencyHz, double amplitude,
                                               double sampleRate, std::size_t numSamples)
{
    std::vector<double> output (numSamples);
    const double omega = 2.0 * std::numbers::pi * frequencyHz / sampleRate;

    for (std::size_t i = 0; i < numSamples; ++i)
        output[i] = amplitude * std::sin (omega * static_cast<double> (i));

    return output;
}

/// Linear sine sweep, for aliasing tests where the artefacts of interest sweep
/// downwards as the fundamental sweeps up.
[[nodiscard]] inline std::vector<double> linearSweep (double startHz, double endHz, double amplitude,
                                                      double sampleRate, std::size_t numSamples)
{
    std::vector<double> output (numSamples);
    double phase = 0.0;

    for (std::size_t i = 0; i < numSamples; ++i)
    {
        const double t = static_cast<double> (i) / static_cast<double> (numSamples);
        const double f = startHz + (endHz - startHz) * t;

        output[i] = amplitude * std::sin (phase);
        phase += 2.0 * std::numbers::pi * f / sampleRate;
    }

    return output;
}

} // namespace tezla::measure
