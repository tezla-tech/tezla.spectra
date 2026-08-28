// Copyright (c) 2026 The Tezla <thetezla@proton.me>
// Created by The Tezla -- https://github.com/wingit33/tezla.tech
// Music: https://soundcloud.com/thetezla | https://thetezla.bandcamp.com
// Built with development assistance from Claude (Anthropic).
// SPDX-License-Identifier: AGPL-3.0-only
// GNU AGPLv3 (see LICENSE), plus NOTICE.md's attribution term. Keep intact.

#pragma once

// Harmonic and aliasing analysis for nonlinear stages.
//
// The two numbers that decide whether a saturator is any good:
//
//   thdDb       -- how much distortion it makes (a target, not a fault)
//   aliasingDb  -- how much of that distortion landed on the wrong frequency
//
// A saturator is supposed to generate harmonics. It is not supposed to generate
// inharmonic mush. Anything above -60 dB in aliasingDb is audible on the kind
// of sustained bass this repository exists to process.

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

#include "Fft.hpp"

namespace tezla::measure {

struct HarmonicReport
{
    double fundamentalDbFs {};       ///< level of the fundamental, dBFS
    std::vector<double> harmonicsDb; ///< index 0 = 2nd harmonic, relative to the fundamental, dB
    double thdDb {};                 ///< total harmonic distortion, dB relative to the fundamental
    double thdPercent {};
    double aliasingDb {};            ///< everything that is neither fundamental nor harmonic, dB
    double audibleAliasingDb {};     ///< the same, restricted to the audible band
    int    harmonicsCounted {};      ///< how many harmonics fitted below Nyquist
};

/// Upper edge of the band that actually matters. Aliasing above this is real
/// but nobody hears it, and reporting it as if it mattered hides the number
/// that does: a saturator can read -79 dB overall while being -145 dB
/// everywhere below 18 kHz, because the whole residual is one harmonic sitting
/// in the decimator's transition band at 22 kHz.
inline constexpr double kAudibleUpperHz = 18000.0;
inline constexpr double kAudibleLowerHz = 20.0;

/// Analyses a steady-state signal produced by feeding a bin-exact sine at
/// `fundamentalHz` through the process under test.
///
/// `signal.size()` must be a power of two, and `fundamentalHz` must have come
/// from binExactFrequency() with the same size and sample rate -- otherwise
/// leakage swamps the aliasing figure and the result is meaningless.
[[nodiscard]] inline HarmonicReport analyseHarmonics (const std::vector<double>& signal,
                                                      double sampleRate,
                                                      double fundamentalHz,
                                                      int maxReportedHarmonics = 12,
                                                      double audibleUpperHz = kAudibleUpperHz)
{
    HarmonicReport report;

    const std::size_t n = signal.size();
    if (n < 8 || ! isPowerOfTwo (n))
        return report;

    const auto spectrum = fftOfReal (signal);
    const std::size_t halfSize = n / 2;

    std::vector<double> power (halfSize, 0.0);
    for (std::size_t k = 0; k < halfSize; ++k)
        power[k] = std::norm (spectrum[k]);

    const double binWidth       = sampleRate / static_cast<double> (n);
    const auto fundamentalBin   = static_cast<std::size_t> (std::llround (fundamentalHz / binWidth));
    if (fundamentalBin == 0 || fundamentalBin >= halfSize)
        return report;

    // A bin-exact tone still spills a little into its neighbours through
    // rounding; count one bin either side as belonging to the component.
    constexpr std::size_t skirt = 1;
    std::vector<bool> accountedFor (halfSize, false);

    const auto collect = [&] (std::size_t centre)
    {
        double total = 0.0;
        const std::size_t first = centre > skirt ? centre - skirt : 0;
        const std::size_t last  = std::min (centre + skirt, halfSize - 1);

        for (std::size_t k = first; k <= last; ++k)
        {
            total += power[k];
            accountedFor[k] = true;
        }
        return total;
    };

    // DC is never a harmonic, but asymmetric shapers put energy there and it
    // must not be counted as aliasing.
    for (std::size_t k = 0; k <= skirt && k < halfSize; ++k)
        accountedFor[k] = true;

    const double fundamentalPower = collect (fundamentalBin);
    if (fundamentalPower <= 0.0)
        return report;

    // Every multiple of the fundamental that fits below Nyquist is a harmonic.
    // Stopping at an arbitrary count and calling the rest "aliasing" is how a
    // high-sample-rate run gets scored as badly as a low one -- at 192 kHz there
    // are simply more real harmonics in band, not more aliasing.
    double harmonicPower = 0.0;
    for (int h = 2;; ++h)
    {
        const std::size_t bin = fundamentalBin * static_cast<std::size_t> (h);
        if (bin + skirt >= halfSize)
            break;

        const double p = collect (bin);
        harmonicPower += p;
        ++report.harmonicsCounted;

        if (static_cast<int> (report.harmonicsDb.size()) < maxReportedHarmonics)
            report.harmonicsDb.push_back (10.0 * std::log10 (std::max (p / fundamentalPower, 1.0e-30)));
    }

    double aliasPower = 0.0;
    double audibleAliasPower = 0.0;

    for (std::size_t k = 0; k < halfSize; ++k)
    {
        if (accountedFor[k])
            continue;

        aliasPower += power[k];

        const double frequency = static_cast<double> (k) * binWidth;
        if (frequency >= kAudibleLowerHz && frequency <= audibleUpperHz)
            audibleAliasPower += power[k];
    }

    // A real sine of peak amplitude A at a bin-exact frequency has |X[k]| = A*N/2,
    // so power * 4 / N^2 recovers A^2 and 0 dBFS means a full-scale sine.
    const double scale = 4.0 / (static_cast<double> (n) * static_cast<double> (n));
    report.fundamentalDbFs = 10.0 * std::log10 (std::max (fundamentalPower * scale, 1.0e-30));
    report.thdDb           = 10.0 * std::log10 (std::max (harmonicPower / fundamentalPower, 1.0e-30));
    report.thdPercent      = 100.0 * std::sqrt (harmonicPower / fundamentalPower);
    report.aliasingDb        = 10.0 * std::log10 (std::max (aliasPower / fundamentalPower, 1.0e-30));
    report.audibleAliasingDb = 10.0 * std::log10 (std::max (audibleAliasPower / fundamentalPower, 1.0e-30));

    return report;
}

} // namespace tezla::measure
