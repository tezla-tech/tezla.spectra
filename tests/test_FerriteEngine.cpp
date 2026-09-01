// Copyright (c) 2026 The Tezla <thetezla@proton.me>
// Created by The Tezla -- https://github.com/wingit33/tezla.tech
// Music: https://soundcloud.com/thetezla | https://thetezla.bandcamp.com
// Built with development assistance from Claude (Anthropic).
// SPDX-License-Identifier: AGPL-3.0-only
// GNU AGPLv3 (see LICENSE), plus NOTICE.md's attribution term. Keep intact.

#include "TestFramework.hpp"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <numbers>
#include <vector>

#include <tezla/measure/Harmonics.hpp>
#include <tezla/measure/Signals.hpp>

#include <FerriteEngine.hpp>

using namespace tezla;
using namespace tezla::ferrite;

namespace
{
constexpr double kRate = 48000.0;
constexpr std::size_t kFft = 1 << 14;

using Buffers = std::vector<std::vector<double>>;

Buffers stereoSine (double amplitude, double frequency, std::size_t n,
                    double rate = kRate)
{
    Buffers b (2, std::vector<double> (n));

    for (std::size_t i = 0; i < n; ++i)
        b[0][i] = b[1][i] = amplitude
                              * std::sin (2.0 * std::numbers::pi * frequency
                                          * static_cast<double> (i) / rate);

    return b;
}

Engine made (const Parameters& parameters, double rate = kRate, int block = 512)
{
    Engine engine;
    engine.prepare (rate, block, 2);
    engine.setParameters (parameters);
    engine.reset();   // snap the smoothers, so the run starts at the settings
    return engine;
}

Buffers render (Engine& engine, const Buffers& input, int blockSize = 271)
{
    Buffers output = input;
    const auto total = static_cast<int> (output[0].size());

    for (int at = 0; at < total; at += blockSize)
    {
        const int n = std::min (blockSize, total - at);
        double* pointers[2] { output[0].data() + at, output[1].data() + at };
        engine.process (pointers, 2, n);
    }

    return output;
}

/// A tape at rest: the wobble and hiss are silenced so a spectrum sees the
/// nonlinearity alone -- wow's FM sidebands are the machine's feature, not
/// aliasing, and they must not be scored as either harmonics or mush.
Parameters still (double drive, double inputDb = 0.0)
{
    Parameters p;
    p.drive = drive;
    p.inputDb = inputDb;
    p.wowDepth = 0.0;
    p.flutterDepth = 0.0;
    p.hissDb = -200.0;
    return p;
}

measure::HarmonicReport analysed (Engine& engine, double amplitude,
                                  double roughHz, double rate = kRate)
{
    const double hz = measure::binExactFrequency (roughHz, rate, kFft);
    const auto rendered = render (engine, stereoSine (amplitude, hz, 3 * kFft, rate));
    const std::vector<double> settled (rendered[0].begin() + 2 * kFft,
                                       rendered[0].begin() + 3 * kFft);

    return measure::analyseHarmonics (settled, rate, hz);
}

double rmsOfTail (const Buffers& b, std::size_t n)
{
    double sum = 0.0;

    for (std::size_t i = b[0].size() - n; i < b[0].size(); ++i)
        sum += b[0][i] * b[0][i];

    return std::sqrt (sum / static_cast<double> (n));
}
} // namespace

// ---------------------------------------------------------------------------
// House basics: silence, DC, bit-exact dry paths
// ---------------------------------------------------------------------------

TEZLA_TEST (ferrite_silence_in_is_exact_silence_out)
{
    // With hiss off (the default), an idle track is EXACTLY silent -- zero
    // state through the whole chain stays zero, wobble running or not.
    for (const double drive : { 0.0, 0.5, 1.0 })
        for (const int speed : { 0, 2 })
        {
            Parameters p;
            p.drive = drive;
            p.speedChoice = speed;

            auto engine = made (p);
            const auto out = render (engine, Buffers (2, std::vector<double> (8192, 0.0)));

            bool allZero = true;

            for (const auto& channel : out)
                for (const double sample : channel)
                    allZero = allZero && sample == 0.0;

            CHECK (allZero);
        }
}

TEZLA_TEST (a_transient_leaves_no_dc_behind)
{
    // Remanence is the tape stage's memory: after a loud one-sided burst the
    // magnetisation parks off zero, which is DC downstream. The blocker sits
    // directly after the hysteresis so a second of silence drains it all.
    Parameters p = still (0.9, 12.0);

    auto engine = made (p);

    Buffers input (2, std::vector<double> (56000, 0.0));

    for (int i = 0; i < 4800; ++i)
    {
        const double ramp = 0.5 - 0.5 * std::cos (std::numbers::pi * i / 4800.0);
        input[0][static_cast<std::size_t> (i)] =
            input[1][static_cast<std::size_t> (i)] = 0.9 * ramp;
    }

    const auto out = render (engine, input);

    double worst = 0.0, mean = 0.0;
    constexpr std::size_t tail = 4800;

    for (std::size_t i = out[0].size() - tail; i < out[0].size(); ++i)
    {
        worst = std::max (worst, std::abs (out[0][i]));
        mean += out[0][i];
    }

    CHECK (worst < 1.0e-6);
    CHECK (std::abs (mean / static_cast<double> (tail)) < 1.0e-7);
}

TEZLA_TEST (mix_zero_is_the_input_delayed_by_the_reported_latency_bit_for_bit)
{
    // The neutral-setting contract, honoured through the latency
    // declaration: at mix 0 the output IS the input, delayed by exactly
    // getLatencySamples(), whatever the drive and however hard the wobble
    // works. This is the test that catches the dry ring's indexing being
    // off by one.
    Parameters p;
    p.drive = 1.0;
    p.inputDb = 18.0;
    p.wowDepth = 1.0;
    p.flutterDepth = 1.0;
    p.mix = 0.0;

    auto engine = made (p);
    const int latency = engine.getLatencySamples();

    Buffers input (2, std::vector<double> (16384));

    for (std::size_t i = 0; i < input[0].size(); ++i)
    {
        input[0][i] = std::sin (0.1 * static_cast<double> (i * i % 977))
                        * std::cos (0.037 * static_cast<double> (i));
        input[1][i] = -input[0][i];
    }

    const auto out = render (engine, input);

    bool exact = true;

    for (std::size_t c = 0; c < 2; ++c)
        for (std::size_t i = static_cast<std::size_t> (latency); i < out[c].size(); ++i)
            exact = exact && out[c][i] == input[c][i - static_cast<std::size_t> (latency)];

    CHECK (exact);
}

TEZLA_TEST (bypass_is_the_delayed_input_from_the_first_sample)
{
    // Bypassed from the start (reset() jumps the crossfade to its end
    // state): the output equals the latency-matched input bit for bit, so
    // the host's A/B comparison is honest.
    Parameters p;
    p.drive = 1.0;
    p.wowDepth = 1.0;
    p.flutterDepth = 1.0;
    p.hissDb = -50.0;
    p.bypassed = true;

    auto engine = made (p);
    const int latency = engine.getLatencySamples();

    Buffers input (2, std::vector<double> (16384));

    for (std::size_t i = 0; i < input[0].size(); ++i)
    {
        input[0][i] = std::sin (0.11 * static_cast<double> (i));
        input[1][i] = std::cos (0.07 * static_cast<double> (i));
    }

    const auto out = render (engine, input);

    bool exact = true;

    for (std::size_t c = 0; c < 2; ++c)
        for (std::size_t i = static_cast<std::size_t> (latency); i < out[c].size(); ++i)
            exact = exact && out[c][i] == input[c][i - static_cast<std::size_t> (latency)];

    CHECK (exact);
}

// ---------------------------------------------------------------------------
// The aliasing gate -- the thing that decides whether this ships
// ---------------------------------------------------------------------------

TEZLA_TEST (ferrite_aliases_below_sixty_dbfs_at_maximum_drive_swept)
{
    // CLAUDE.md section 7: no inharmonic component above -60 dBFS in the
    // audible band at maximum drive. Swept, bin-exact, non-divisor probes --
    // the Anvil lesson: a single 1 kHz probe flatters the figure by tens of
    // decibels, and a divisor probe cannot see aliasing at all.
    //
    // The figure asserted is absolute dBFS of the audible-band inharmonic
    // energy: fundamentalDbFs + audibleAliasingDb.
    Parameters p = still (1.0, 24.0);
    p.saturation = 1.0;
    p.bias = 0.0;   // the widest, dirtiest loop the panel can dial

    double worst = -300.0;

    for (const double frequency : { 82.0, 330.0, 1000.0, 4400.0 })
    {
        auto engine = made (p);
        const auto report = analysed (engine, 0.5, frequency);
        worst = std::max (worst, report.fundamentalDbFs + report.audibleAliasingDb);
    }

    CHECK (worst < -60.0);
}

TEZLA_TEST (the_auto_factor_is_chosen_by_measurement_not_habit)
{
    // The evidence behind Auto picking x4 at 48 kHz: the same brutal
    // setting measured at x1, x2 and x4 on the worst probe of the sweep.
    // The numbers are printed so the choice stays auditable.
    Parameters p = still (1.0, 24.0);
    p.saturation = 1.0;
    p.bias = 0.0;

    double at[3] = {};
    const dsp::OversamplingMode modes[3] { dsp::OversamplingMode::Off,
                                           dsp::OversamplingMode::X2,
                                           dsp::OversamplingMode::X4 };

    for (int m = 0; m < 3; ++m)
    {
        p.oversampling = modes[m];

        double worst = -300.0;

        for (const double frequency : { 1000.0, 4400.0 })
        {
            auto engine = made (p);
            const auto report = analysed (engine, 0.5, frequency);
            worst = std::max (worst, report.fundamentalDbFs + report.audibleAliasingDb);
        }

        at[m] = worst;
    }

    std::printf ("        [ferrite aliasing dBFS] x1 %.1f | x2 %.1f | x4 %.1f\n",
                 at[0], at[1], at[2]);

    // Each doubling must buy real suppression, and x4 must clear the gate
    // with margin to spare for material nastier than a sine.
    CHECK (at[1] < at[0]);
    CHECK (at[2] < at[1]);
    CHECK (at[2] < -60.0);
}

TEZLA_TEST (the_aliasing_gate_holds_at_every_host_rate)
{
    // CLAUDE.md section 6 at the engine's front door. The harmonic
    // structure itself needs no cross-rate test here: the Jiles-Atherton
    // recursion is quasi-static -- dM scales with dH, so the loop depends
    // on the field's trajectory, not on time -- and its 315 Hz THD measures
    // identical across 44.1-192 kHz even UNOVERSAMPLED (and even with the
    // integrator deliberately prepared at the wrong rate: both break-checks
    // stayed green, which is why no such test is kept). What DOES change
    // with rate is where the harmonics fold, so the claim with teeth is
    // this one: Auto's factor keeps the audible alias floor under the
    // -60 dBFS gate at every session rate the rig uses.
    Parameters p = still (1.0, 24.0);
    p.saturation = 1.0;
    p.bias = 0.0;

    for (const double rate : { 44100.0, 96000.0, 192000.0 })
    {
        double worst = -300.0;

        for (const double frequency : { 1000.0, 4400.0 })
        {
            auto engine = made (p, rate);
            const auto report = analysed (engine, 0.5, frequency, rate);
            worst = std::max (worst, report.fundamentalDbFs + report.audibleAliasingDb);
        }

        CHECK (worst < -60.0);
    }
}

// ---------------------------------------------------------------------------
// Auto-trim: the user judges tone, not loudness
// ---------------------------------------------------------------------------

TEZLA_TEST (auto_trim_holds_loudness_across_the_whole_panel)
{
    // Drive, saturation and input gain all move the level through the tape;
    // the measured trim must hand back the same loudness within +-1.5 dB
    // everywhere, so what the user hears changing is the tone.
    const double hz = measure::binExactFrequency (315.0, kRate, kFft);

    double reference = 0.0;
    double worstDb = 0.0;

    for (const double drive : { 0.0, 0.5, 1.0 })
        for (const double saturation : { 0.0, 0.5, 1.0 })
            for (const double inputDb : { 0.0, 12.0, 24.0 })
            {
                Parameters p = still (drive, inputDb);
                p.saturation = saturation;

                auto engine = made (p);
                const auto out = render (engine, stereoSine (0.1, hz, 3 * kFft));
                const double rms = rmsOfTail (out, kFft);

                if (reference == 0.0)
                {
                    reference = rms;
                    continue;
                }

                const double db = std::abs (20.0 * std::log10 (rms / reference));
                worstDb = std::max (worstDb, db);
            }

    CHECK (worstDb < 1.5);
}

// ---------------------------------------------------------------------------
// Stereo, block size, and the control grid
// ---------------------------------------------------------------------------

TEZLA_TEST (the_two_channels_are_one_tape_even_through_automation)
{
    // Same seed, same wobble: identical inputs must give bit-identical
    // channels, including WHILE parameters ramp -- the per-sample control
    // curves are computed once and shared, and this is the test that
    // catches any per-channel smoother divergence.
    Parameters a;
    a.drive = 0.4;
    a.wowDepth = 0.8;
    a.flutterDepth = 0.8;

    auto engine = made (a);

    Parameters b = a;
    b.inputDb = 12.0;
    b.drive = 0.9;
    b.mix = 0.6;

    Buffers block (2, std::vector<double> (256));
    bool identical = true;

    for (int n = 0; n < 96; ++n)
    {
        if (n == 24)
            engine.setParameters (b);

        for (std::size_t i = 0; i < block[0].size(); ++i)
            block[0][i] = block[1][i] =
                0.4 * std::sin (2.0 * std::numbers::pi * 220.0
                                * static_cast<double> (n * 256 + static_cast<int> (i)) / kRate);

        double* pointers[2] { block[0].data(), block[1].data() };
        engine.process (pointers, 2, 256);

        for (std::size_t i = 0; i < block[0].size(); ++i)
            identical = identical && block[0][i] == block[1][i];
    }

    CHECK (identical);
}

TEZLA_TEST (the_host_block_size_cannot_bend_the_output)
{
    // Wobble and hiss running, a parameter step mid-stream, three block
    // sizes -- one deliberately off the 32-sample grid. Every state in the
    // engine advances per sample and the control timer carries its
    // remainder, so the rendered audio must be identical.
    const auto settle = [] (int blockSize)
    {
        Parameters a;
        a.drive = 0.3;
        a.wowDepth = 0.5;
        a.flutterDepth = 0.5;
        a.hissDb = -70.0;

        Engine engine;
        engine.prepare (kRate, 1024, 2);
        engine.setParameters (a);
        engine.reset();

        Parameters b = a;
        b.drive = 0.9;
        b.inputDb = 9.0;
        b.saturation = 0.8;

        constexpr int total = 24576;
        std::vector<double> left, right;
        left.reserve (total);
        right.reserve (total);

        std::vector<double> l (static_cast<std::size_t> (blockSize)),
                            r (static_cast<std::size_t> (blockSize));

        for (int written = 0; written < total; written += blockSize)
        {
            const int span = std::min (blockSize, total - written);

            for (int i = 0; i < span; ++i)
            {
                const double t = static_cast<double> (written + i) / kRate;
                l[static_cast<std::size_t> (i)] = r[static_cast<std::size_t> (i)] =
                    0.4 * std::sin (2.0 * std::numbers::pi * 110.0 * t)
                  + 0.2 * std::sin (2.0 * std::numbers::pi * 1300.0 * t);
            }

            double* pointers[2] { l.data(), r.data() };
            engine.setParameters (b);
            engine.process (pointers, 2, span);

            for (int i = 0; i < span; ++i)
            {
                left.push_back (l[static_cast<std::size_t> (i)]);
                right.push_back (r[static_cast<std::size_t> (i)]);
            }
        }

        left.insert (left.end(), right.begin(), right.end());
        return left;
    };

    const auto small = settle (64);
    const auto large = settle (512);
    const auto odd = settle (271);

    double worstLarge = 0.0, worstOdd = 0.0;

    for (std::size_t i = 0; i < small.size(); ++i)
    {
        worstLarge = std::max (worstLarge, std::abs (small[i] - large[i]));
        worstOdd = std::max (worstOdd, std::abs (small[i] - odd[i]));
    }

    CHECK (worstLarge < 1.0e-9);
    CHECK (worstOdd < 1.0e-9);
}

// ---------------------------------------------------------------------------
// Hiss and latency
// ---------------------------------------------------------------------------

TEZLA_TEST (hiss_reads_as_its_stated_dbfs_and_the_channels_do_not_share_it)
{
    // The hiss parameter is calibrated: the filtered generator's RMS is
    // 1/sqrt(21) of full scale and the gain carries the correction, so
    // -60 dB of hiss measures -60 dBFS at the output. Real tape hiss is
    // per-track noise, so the channels must be decorrelated.
    Parameters p;
    p.wowDepth = 0.0;
    p.flutterDepth = 0.0;
    p.hissDb = -60.0;
    p.autoTrim = false;

    auto engine = made (p);
    const auto out = render (engine, Buffers (2, std::vector<double> (1 << 17, 0.0)));

    constexpr std::size_t window = 1 << 16;
    double sumL = 0.0, sumR = 0.0, cross = 0.0;

    for (std::size_t i = out[0].size() - window; i < out[0].size(); ++i)
    {
        sumL += out[0][i] * out[0][i];
        sumR += out[1][i] * out[1][i];
        cross += out[0][i] * out[1][i];
    }

    const double dbL = 10.0 * std::log10 (sumL / static_cast<double> (window));
    const double dbR = 10.0 * std::log10 (sumR / static_cast<double> (window));
    const double correlation = cross / std::sqrt (sumL * sumR);

    CHECK (std::abs (dbL + 60.0) < 1.0);
    CHECK (std::abs (dbR + 60.0) < 1.0);
    CHECK (std::abs (correlation) < 0.05);
}

TEZLA_TEST (latency_follows_the_oversampling_factor)
{
    // Oversampler round trip (0/47/63/71) plus the wow/flutter centre
    // (1 ms), reported so the host's PDC and both dry paths agree.
    const auto latencyAt = [] (dsp::OversamplingMode mode, double rate)
    {
        Parameters p;
        p.oversampling = mode;

        Engine engine;
        engine.prepare (rate, 512, 2);
        engine.setParameters (p);
        engine.reset();
        return engine.getLatencySamples();
    };

    CHECK (latencyAt (dsp::OversamplingMode::Off, 48000.0) == 48);
    CHECK (latencyAt (dsp::OversamplingMode::X2, 48000.0) == 95);
    CHECK (latencyAt (dsp::OversamplingMode::X4, 48000.0) == 111);
    CHECK (latencyAt (dsp::OversamplingMode::X8, 48000.0) == 119);

    // Auto lands near 192 kHz: x4 at 48 k, x2 at 96 k, off at 192 k.
    CHECK (latencyAt (dsp::OversamplingMode::Auto, 48000.0) == 111);
    CHECK (latencyAt (dsp::OversamplingMode::Auto, 96000.0) == 47 + 96);
    CHECK (latencyAt (dsp::OversamplingMode::Auto, 192000.0) == 192);
}

// ---------------------------------------------------------------------------
// The loss stage and the wobble are wired through
// ---------------------------------------------------------------------------

TEZLA_TEST (speed_and_bump_reach_the_output)
{
    // Not a re-measurement of TapeLoss -- its own tests own the curves --
    // just proof the engine actually wires the controls to the stage: slow
    // tape loses treble that fast tape keeps, and the bump lifts the low
    // shelf region when asked.
    const double trebleHz = measure::binExactFrequency (12000.0, kRate, kFft);

    Parameters slow = still (0.2);
    slow.speedChoice = 0;    // 3.75 ips
    slow.autoTrim = false;

    Parameters fast = slow;
    fast.speedChoice = 3;    // 30 ips

    auto slowEngine = made (slow);
    auto fastEngine = made (fast);

    const double slowRms = rmsOfTail (render (slowEngine, stereoSine (0.1, trebleHz, 3 * kFft)), kFft);
    const double fastRms = rmsOfTail (render (fastEngine, stereoSine (0.1, trebleHz, 3 * kFft)), kFft);

    CHECK (20.0 * std::log10 (fastRms / slowRms) > 6.0);

    const double bumpHz = measure::binExactFrequency (42.0, kRate, kFft);

    Parameters flat = still (0.2);
    flat.bumpAmount = 0.0;
    flat.autoTrim = false;

    Parameters bumped = flat;
    bumped.bumpAmount = 2.0;

    auto flatEngine = made (flat);
    auto bumpedEngine = made (bumped);

    const double flatRms = rmsOfTail (render (flatEngine, stereoSine (0.1, bumpHz, 3 * kFft)), kFft);
    const double bumpedRms = rmsOfTail (render (bumpedEngine, stereoSine (0.1, bumpHz, 3 * kFft)), kFft);

    CHECK (20.0 * std::log10 (bumpedRms / flatRms) > 2.0);
}

TEZLA_TEST (the_default_wobble_reaches_the_output)
{
    // The factory setting has the tape moving. A 1 kHz tone's short-window
    // pitch estimates must spread with the default depths and hold still
    // with the wobble parked -- proof the depth controls arrive at the
    // stage, not a re-measurement of WowFlutter.
    const auto spreadWith = [] (double depth)
    {
        Parameters p = still (0.1);
        p.wowDepth = depth;
        p.flutterDepth = depth;
        p.autoTrim = false;

        auto engine = made (p);
        const auto out = render (engine, stereoSine (0.5, 1000.0, 1 << 17));

        constexpr int window = 4800;
        double lowest = 1.0e9, highest = 0.0;

        for (int w = 8; w < 24; ++w)
        {
            double first = -1.0, last = -1.0;
            int cycles = 0;

            for (int i = w * window + 1; i < (w + 1) * window; ++i)
            {
                const auto a = out[0][static_cast<std::size_t> (i - 1)];
                const auto b = out[0][static_cast<std::size_t> (i)];

                if (! (a < 0.0 && b >= 0.0))
                    continue;

                const double at = static_cast<double> (i - 1) - a / (b - a);

                if (first < 0.0)
                    first = at;
                else
                {
                    last = at;
                    ++cycles;
                }
            }

            const double hz = cycles < 1 ? 0.0 : cycles * kRate / (last - first);
            lowest = std::min (lowest, hz);
            highest = std::max (highest, hz);
        }

        return (highest - lowest) / 1000.0;
    };

    CHECK (spreadWith (0.15) > 3.0e-4);
    CHECK (spreadWith (0.0) < 1.0e-6);
}

// ---------------------------------------------------------------------------
// CPU
// ---------------------------------------------------------------------------

TEZLA_TEST (one_stereo_instance_is_affordable)
{
    // These run twenty at a time (CLAUDE.md section 1). One second of
    // stereo at 48 kHz through the default Auto x4 chain, timed; the
    // figure is printed so regressions are visible in the log.
    Parameters p;
    p.drive = 0.6;

    auto engine = made (p);

    Buffers block (2, std::vector<double> (512));
    const auto start = std::chrono::steady_clock::now();

    for (int n = 0; n < 94; ++n)
    {
        for (std::size_t i = 0; i < block[0].size(); ++i)
            block[0][i] = block[1][i] =
                0.4 * std::sin (2.0 * std::numbers::pi * 110.0
                                * static_cast<double> (n * 512 + static_cast<int> (i)) / kRate);

        double* pointers[2] { block[0].data(), block[1].data() };
        engine.process (pointers, 2, 512);
    }

    const auto elapsed = std::chrono::duration<double> (
        std::chrono::steady_clock::now() - start).count();
    const double perSecond = elapsed / (94.0 * 512.0 / kRate);

    std::printf ("        [ferrite cpu] %.1f%% of one core per stereo instance\n",
                 100.0 * perSecond);

    CHECK_CPU_BUDGET (perSecond, 0.5, "ferrite, one stereo instance");
}
