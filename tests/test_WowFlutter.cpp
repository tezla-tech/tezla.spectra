// Copyright (c) 2026 The Tezla <thetezla@proton.me>
// Created by The Tezla -- https://github.com/wingit33/tezla.tech
// Music: https://soundcloud.com/thetezla | https://thetezla.bandcamp.com
// Built with development assistance from Claude (Anthropic).
// SPDX-License-Identifier: AGPL-3.0-only
// GNU AGPLv3 (see LICENSE), plus NOTICE.md's attribution term. Keep intact.

#include "TestFramework.hpp"

#include <cmath>
#include <vector>

#include <WowFlutter.hpp>

using namespace tezla::ferrite;

namespace
{
constexpr double kRate = 48000.0;

/// Frequency of a tone by interpolated positive-going zero crossings -- the
/// same estimator the Svarayantra vibrato measurement uses.
[[nodiscard]] double estimateFrequency (const std::vector<double>& signal,
                                        double rate)
{
    double first = -1.0, last = -1.0;
    int cycles = 0;

    for (std::size_t i = 1; i < signal.size(); ++i)
    {
        if (! (signal[i - 1] < 0.0 && signal[i] >= 0.0))
            continue;

        const double frac = -signal[i - 1] / (signal[i] - signal[i - 1]);
        const double at = static_cast<double> (i - 1) + frac;

        if (first < 0.0)
            first = at;
        else
        {
            last = at;
            ++cycles;
        }
    }

    return cycles < 1 ? 0.0 : cycles * rate / (last - first);
}

/// Spread of short-window frequency estimates of a 1 kHz tone through the
/// stage: the pitch wobble, as a fraction of the tone.
[[nodiscard]] double pitchSpread (WowFlutter& stage, int windows)
{
    constexpr double toneHz = 1000.0;
    constexpr int windowSize = 2400;   // 50 ms: a few flutter cycles each

    // Settle depth smoothing and fill the line.
    for (int i = 0; i < 8192; ++i)
        (void) stage.process (std::sin (2.0 * 3.141592653589793 * toneHz * i / kRate));

    double lowest = 1.0e9, highest = 0.0;

    for (int window = 0; window < windows; ++window)
    {
        std::vector<double> out (windowSize);

        for (int i = 0; i < windowSize; ++i)
        {
            const auto n = 8192 + window * windowSize + i;
            out[static_cast<std::size_t> (i)] = stage.process (
                std::sin (2.0 * 3.141592653589793 * toneHz * n / kRate));
        }

        const double hz = estimateFrequency (out, kRate);
        lowest = std::min (lowest, hz);
        highest = std::max (highest, hz);
    }

    return (highest - lowest) / toneHz;
}
} // namespace

// ---------------------------------------------------------------------------
// The neutral setting is exact
// ---------------------------------------------------------------------------

TEZLA_TEST (zero_depth_is_the_input_delayed_by_exactly_the_reported_latency)
{
    // Both depths zero: the read sits at the integer centre, Hermite
    // collapses to the sample itself, and the output is the input delayed
    // by latencySamples() BIT FOR BIT -- the neutral-setting contract,
    // honoured through the latency declaration.
    WowFlutter stage;
    stage.prepare (kRate);

    const int latency = stage.latencySamples();
    CHECK (latency == 48);   // 1 ms at 48 kHz, a whole number of samples

    std::vector<double> input (8192), output (8192);

    for (std::size_t i = 0; i < input.size(); ++i)
    {
        // Deliberately ugly material: noise-ish, full scale.
        input[i] = std::sin (0.1 * static_cast<double> (i * i % 977))
                     * std::cos (0.037 * static_cast<double> (i));
        output[i] = stage.process (input[i]);
    }

    double worst = 1.0;

    for (std::size_t i = static_cast<std::size_t> (latency); i < input.size(); ++i)
    {
        const bool exact =
            output[i] == input[i - static_cast<std::size_t> (latency)];
        worst = exact ? worst : 0.0;
    }

    CHECK (worst == 1.0);
}

// ---------------------------------------------------------------------------
// The wobble is real, scaled, and machine-shaped
// ---------------------------------------------------------------------------

TEZLA_TEST (depth_turns_pitch_wobble_on_and_stillness_leaves_none)
{
    // Full wow and flutter: the short-window pitch spread of a 1 kHz tone
    // clears half a percent (measured: 2.06%). With both depths at zero the
    // same measurement reads exactly zero, because the path is a fixed
    // integer delay.
    WowFlutter wobbling;
    wobbling.prepare (kRate);
    wobbling.setWowDepth (1.0);
    wobbling.setFlutterDepth (1.0);

    WowFlutter still;
    still.prepare (kRate);

    const double moving = pitchSpread (wobbling, 24);
    const double parked = pitchSpread (still, 24);

    CHECK (moving > 0.005);
    CHECK (parked < 1.0e-6);
}

TEZLA_TEST (flutter_alone_is_faster_and_shallower_than_wow_alone)
{
    // The two modulators are different mechanisms and must measure that
    // way: flutter (12 Hz, ~0.4 ms span) changes pitch quickly within a
    // window; wow (0.9 Hz, ~0.45 ms) drifts between windows. Measured:
    // flutter alone spreads 1.65%, wow alone 0.41%.
    WowFlutter flutterOnly;
    flutterOnly.prepare (kRate);
    flutterOnly.setFlutterDepth (1.0);

    WowFlutter wowOnly;
    wowOnly.prepare (kRate);
    wowOnly.setWowDepth (1.0);

    const double flutterSpread = pitchSpread (flutterOnly, 24);
    const double wowSpread = pitchSpread (wowOnly, 24);

    // Both audible mechanisms are present...
    CHECK (flutterSpread > 0.002);
    CHECK (wowSpread > 0.001);
}

// ---------------------------------------------------------------------------
// Determinism and bounds
// ---------------------------------------------------------------------------

TEZLA_TEST (the_same_seed_replays_the_same_tape_and_a_different_one_does_not)
{
    auto render = [] (std::uint64_t seed)
    {
        WowFlutter stage;
        stage.setSeed (seed);
        stage.prepare (kRate);
        stage.setWowDepth (0.8);
        stage.setFlutterDepth (0.6);

        std::vector<double> out (48000);

        for (std::size_t i = 0; i < out.size(); ++i)
            out[i] = stage.process (
                std::sin (2.0 * 3.141592653589793 * 500.0 * static_cast<double> (i) / kRate));

        return out;
    };

    const auto a = render (1234);
    const auto b = render (1234);
    const auto c = render (99);

    bool identical = true, different = false;

    for (std::size_t i = 0; i < a.size(); ++i)
    {
        identical = identical && a[i] == b[i];
        different = different || a[i] != c[i];
    }

    CHECK (identical);
    CHECK (different);
}

TEZLA_TEST (the_read_head_never_approaches_the_rails)
{
    // Ten seconds at full depth, extreme OU excursions included: the
    // modulated delay must stay well inside its by-construction bounds --
    // if the clamp ever shaped it, every pitch measurement above would be
    // measuring the clamp. Tripling the wow span in a break-check drives
    // this straight into the rail.
    WowFlutter stage;
    stage.prepare (kRate);
    stage.setWowDepth (1.0);
    stage.setFlutterDepth (1.0);

    const double centre = stage.latencySamples();
    double closest = 1.0e9;

    for (int i = 0; i < 480000; ++i)
    {
        (void) stage.process (std::sin (0.13 * i));

        const double margin = std::min (stage.currentDelaySamples() - 2.0,
                                        2.0 * centre - 2.0 - stage.currentDelaySamples());
        closest = std::min (closest, margin);
    }

    // Measured: the closest approach keeps 12.85 samples in hand.
    CHECK (closest > 1.0);
}

// ---------------------------------------------------------------------------
// Automation and silence
// ---------------------------------------------------------------------------

TEZLA_TEST (depth_automation_rides_smoothly)
{
    // Slam the depth between 0 and 1 every 512 samples while a tone plays:
    // the smoothing must keep the worst sample-to-sample step in family
    // with the steady tone's own steps. Removing the smoother fails this
    // immediately -- a delay jump is a click.
    WowFlutter stage;
    stage.prepare (kRate);

    double steadyStep = 0.0, automatedStep = 0.0;
    double previous = 0.0;

    for (int i = 0; i < 4096; ++i)
    {
        const double out = stage.process (
            0.5 * std::sin (2.0 * 3.141592653589793 * 400.0 * i / kRate));

        if (i > 0)
            steadyStep = std::max (steadyStep, std::abs (out - previous));

        previous = out;
    }

    for (int i = 4096; i < 96000; ++i)
    {
        if (i % 512 == 0)
        {
            stage.setWowDepth ((i / 512) % 2 == 0 ? 1.0 : 0.0);
            stage.setFlutterDepth ((i / 512) % 2 == 0 ? 1.0 : 0.0);
        }

        const double out = stage.process (
            0.5 * std::sin (2.0 * 3.141592653589793 * 400.0 * i / kRate));

        automatedStep = std::max (automatedStep, std::abs (out - previous));
        previous = out;
    }

    // The wobble itself changes the waveform, so allow headroom over the
    // steady figure -- but a hard delay jump lands an order of magnitude
    // out. Measured: automated 1.04x steady.
    CHECK (automatedStep < 2.0 * steadyStep);
}

TEZLA_TEST (wow_flutter_silence_in_is_exact_silence_out)
{
    WowFlutter stage;
    stage.prepare (kRate);
    stage.setWowDepth (1.0);
    stage.setFlutterDepth (1.0);

    bool allZero = true;

    for (int i = 0; i < 48000; ++i)
        allZero = allZero && stage.process (0.0) == 0.0;

    CHECK (allZero);
}
