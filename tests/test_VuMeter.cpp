// Copyright (c) 2026 The Tezla <thetezla@proton.me>
// Created by The Tezla -- https://github.com/wingit33/tezla.tech
// Music: https://soundcloud.com/thetezla | https://thetezla.bandcamp.com
// Built with development assistance from Claude (Anthropic).
// SPDX-License-Identifier: AGPL-3.0-only
// GNU AGPLv3 (see LICENSE), plus NOTICE.md's attribution term. Keep intact.

#include "TestFramework.hpp"

#include <cmath>
#include <vector>

#include <tezla/dsp/VuMeter.hpp>
#include <tezla/measure/Signals.hpp>

using namespace tezla::dsp;

TEZLA_TEST (vu_meter_reads_rms_of_a_steady_tone)
{
    constexpr double fs = 48000.0;

    VuMeter meter;
    meter.prepare (fs);

    // A full-scale sine is -3.01 dB RMS. A meter that reads 0 here is a peak
    // meter with a VU scale printed on it.
    const auto tone = tezla::measure::sine (1000.0, 1.0, fs, 96000);
    for (std::size_t offset = 0; offset < tone.size(); offset += 256)
        meter.processBlock (tone.data() + offset, 256);

    CHECK_NEAR (meter.getVuDb(), -3.01, 0.1);
    CHECK_NEAR (meter.getPeakDb(), 0.0, 0.1);
}

TEZLA_TEST (vu_meter_reaches_99_percent_in_300_milliseconds)
{
    constexpr double fs = 48000.0;

    VuMeter meter;
    meter.prepare (fs);

    const auto tone = tezla::measure::sine (1000.0, 1.0, fs, static_cast<std::size_t> (0.3 * fs));
    for (std::size_t offset = 0; offset + 64 <= tone.size(); offset += 64)
        meter.processBlock (tone.data() + offset, 64);

    // 99% of the final mean square, expressed in dB.
    const double expected = 10.0 * std::log10 (0.99 * 0.5);
    CHECK_NEAR (meter.getVuDb(), expected, 0.15);
}

TEZLA_TEST (vu_and_peak_disagree_on_transient_material)
{
    // The whole reason for showing both. A sparse click train has a high peak
    // and almost no average level.
    constexpr double fs = 48000.0;

    VuMeter meter;
    meter.prepare (fs);

    std::vector<double> clicks (static_cast<std::size_t> (fs), 0.0);
    for (std::size_t i = 0; i < clicks.size(); i += 4800)
        clicks[i] = 1.0;

    for (std::size_t offset = 0; offset + 256 <= clicks.size(); offset += 256)
        meter.processBlock (clicks.data() + offset, 256);

    CHECK (meter.getPeakDb() > -3.0);
    CHECK (meter.getVuDb() < -25.0);
}

TEZLA_TEST (vu_meter_ballistics_are_sample_rate_independent)
{
    for (const double fs : { 44100.0, 48000.0, 96000.0, 192000.0 })
    {
        VuMeter meter;
        meter.prepare (fs);

        const auto tone = tezla::measure::sine (1000.0, 1.0, fs, static_cast<std::size_t> (0.3 * fs));
        for (std::size_t offset = 0; offset + 128 <= tone.size(); offset += 128)
            meter.processBlock (tone.data() + offset, 128);

        CHECK_NEAR (meter.getVuDb(), 10.0 * std::log10 (0.99 * 0.5), 0.2);
    }
}
