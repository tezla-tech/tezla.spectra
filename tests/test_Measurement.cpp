// Copyright (c) 2026 The Tezla <thetezla@proton.me>
// Created by The Tezla -- https://github.com/wingit33/tezla.tech
// Music: https://soundcloud.com/thetezla | https://thetezla.bandcamp.com
// Built with development assistance from Claude (Anthropic).
// SPDX-License-Identifier: AGPL-3.0-only
// GNU AGPLv3 (see LICENSE), plus NOTICE.md's attribution term. Keep intact.

#include "TestFramework.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

#include <tezla/measure/Harmonics.hpp>
#include <tezla/measure/Signals.hpp>

using namespace tezla::measure;

TEZLA_TEST (analysis_reads_a_clean_sine_as_clean)
{
    // If the measuring instrument has a distortion floor of its own, every
    // number it produces about a saturator is suspect. Check the instrument.
    constexpr std::size_t fftSize = 32768;
    constexpr double fs = 48000.0;

    const double frequency = binExactFrequency (1000.0, fs, fftSize);
    const auto report = analyseHarmonics (sine (frequency, 0.5, fs, fftSize), fs, frequency);

    CHECK_NEAR (report.fundamentalDbFs, -6.02, 0.05);
    CHECK (report.thdDb < -200.0);
    CHECK (report.aliasingDb < -200.0);
}

TEZLA_TEST (analysis_reports_full_scale_as_zero_dbfs)
{
    constexpr std::size_t fftSize = 32768;
    constexpr double fs = 48000.0;

    const double frequency = binExactFrequency (1000.0, fs, fftSize);

    for (const auto& [amplitude, expectedDb] : { std::pair { 1.0, 0.0 },
                                                std::pair { 0.5, -6.02 },
                                                std::pair { 0.25, -12.04 } })
    {
        const auto report = analyseHarmonics (sine (frequency, amplitude, fs, fftSize), fs, frequency);
        CHECK_NEAR (report.fundamentalDbFs, expectedDb, 0.05);
    }
}

TEZLA_TEST (hard_clipper_produces_odd_harmonics_only)
{
    // A symmetric nonlinearity cannot make even harmonics. If this ever fails,
    // either the shaper picked up an offset or the analysis is misaligned.
    constexpr std::size_t fftSize = 65536;
    constexpr double fs = 48000.0;

    const double frequency = binExactFrequency (1000.0, fs, fftSize);
    auto signal = sine (frequency, 0.5, fs, fftSize);
    for (auto& sample : signal)
        sample = std::clamp (sample * 4.0, -1.0, 1.0);

    const auto report = analyseHarmonics (signal, fs, frequency);

    CHECK (report.harmonicsDb.size() >= 6);
    CHECK (report.harmonicsDb[0] < -100.0);   // 2nd
    CHECK (report.harmonicsDb[1] > -20.0);    // 3rd, strong
    CHECK (report.harmonicsDb[2] < -100.0);   // 4th
    CHECK (report.harmonicsDb[3] > -35.0);    // 5th
}

TEZLA_TEST (running_a_clipper_faster_reduces_aliasing)
{
    // The measured justification for this repository's oversampling policy.
    // Identical naive hard clipper, identical drive, different sample rate:
    // 4x the rate buys about 18 dB less inharmonic rubbish. It never reaches
    // zero -- a hard clipper has infinite bandwidth, so some folding always
    // survives, which is why oversampling alone is not the whole answer.
    constexpr std::size_t fftSize = 65536;

    const auto aliasingAt = [] (double sampleRate)
    {
        const double frequency = binExactFrequency (1000.0, sampleRate, fftSize);
        auto signal = sine (frequency, 0.5, sampleRate, fftSize);
        for (auto& sample : signal)
            sample = std::clamp (sample * 4.0, -1.0, 1.0);

        return analyseHarmonics (signal, sampleRate, frequency).aliasingDb;
    };

    const double at48  = aliasingAt (48000.0);
    const double at192 = aliasingAt (192000.0);

    CHECK_NEAR (at48,  -47.1, 1.0);
    CHECK_NEAR (at192, -65.1, 1.0);
    CHECK (at192 < at48 - 12.0);
}
