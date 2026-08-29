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
#include <vector>

#include <tezla/dsp/ModalResonator.hpp>

using namespace tezla::dsp;

namespace
{
/// Frequency by interpolated positive-going zero crossings -- the house
/// estimator (RMS-safe, works on decaying rings).
[[nodiscard]] double ringFrequency (const std::vector<double>& signal, double rate)
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

/// Peak absolute value inside [from, from + span).
[[nodiscard]] double peakIn (const std::vector<double>& signal,
                             std::size_t from, std::size_t span)
{
    double peak = 0.0;

    for (std::size_t i = from; i < from + span && i < signal.size(); ++i)
        peak = std::max (peak, std::abs (signal[i]));

    return peak;
}
} // namespace

TEZLA_TEST (a_mode_rings_at_the_frequency_it_was_given_at_every_rate)
{
    // The bank's first duty, held at 48 and 192 kHz alike: the pole angle is
    // computed from the actual rate, never from a baked-in one (CLAUDE.md
    // section 6).
    for (const double rate : { 48000.0, 192000.0 })
    {
        ModalResonator bank;
        bank.prepare (rate);
        bank.setModeCount (1);
        bank.setMode (0, 440.0, 1.0, 1.0);
        bank.excite (0, 1.0);

        std::vector<double> out (static_cast<std::size_t> (rate / 2.0));
        for (auto& sample : out)
            sample = bank.process();

        CHECK_NEAR (ringFrequency (out, rate), 440.0, 0.5);
    }
}

TEZLA_TEST (t60_is_the_time_it_says)
{
    // Excite, then compare the ring's peak envelope half a T60 apart: the
    // drop must be 30 dB within half a decibel. Getting r's formula wrong by
    // a factor of the sample rate -- the classic slip -- lands hundreds of
    // decibels away, not half of one.
    constexpr double rate = 48000.0;
    constexpr double t60 = 0.5;

    ModalResonator bank;
    bank.prepare (rate);
    bank.setModeCount (1);
    bank.setMode (0, 300.0, t60, 1.0);
    bank.excite (0, 1.0);

    const auto total = static_cast<std::size_t> (rate * t60);
    std::vector<double> out (total);
    for (auto& sample : out)
        sample = bank.process();

    // Windows one cycle wide would ripple; 50 ms holds many cycles of 300 Hz.
    const auto window = static_cast<std::size_t> (rate * 0.05);
    const double early = peakIn (out, 0, window);
    const double late = peakIn (out, total / 2, window);

    const double dropDb = 20.0 * std::log10 (early / late);

    CHECK_NEAR (dropDb, 30.0, 0.5);
}

TEZLA_TEST (retune_mid_ring_is_amplitude_continuous_and_lands)
{
    // The property the complex form was chosen for, and the one a two-pole
    // implementation fails: spinning the pole to a new angle keeps the
    // phasor's length, so a ringing mode glides without a click. This is
    // what the per-hit tension Drop stands on.
    constexpr double rate = 48000.0;

    ModalResonator bank;
    bank.prepare (rate);
    bank.setModeCount (1);
    bank.setMode (0, 200.0, 2.0, 1.0);
    bank.excite (0, 1.0);

    std::vector<double> out;
    out.reserve (24000);

    double steadyStep = 0.0, previous = 0.0;

    // 12100 samples is 50.42 cycles of 200 Hz: the retune lands mid-swing,
    // where a discontinuity is largest. Landing it on a cycle boundary
    // (12000 = exactly 50 cycles) let a state-zeroing break slip past the
    // step check, caught only by the frequency check -- measured, then moved.
    for (int i = 0; i < 12100; ++i)
    {
        const double sample = bank.process();

        if (i > 0)
            steadyStep = std::max (steadyStep, std::abs (sample - previous));

        previous = sample;
        out.push_back (sample);
    }

    // The retune, mid-ring.
    bank.setMode (0, 300.0, 2.0, 1.0);

    double retuneStep = 0.0;
    std::vector<double> after;
    after.reserve (12000);

    for (int i = 0; i < 12000; ++i)
    {
        const double sample = bank.process();
        retuneStep = std::max (retuneStep, std::abs (sample - previous));
        previous = sample;
        after.push_back (sample);
    }

    // A 300 Hz sine steps at most 1.5x as far per sample as a 200 Hz one of
    // the same amplitude, so anything under 2x steady is glide, not click.
    // Break-checked: zeroing the state on retune fails BOTH checks -- the
    // mid-swing stop is a step far outside this bound, and the dead ring
    // reads 0 Hz below.
    CHECK (retuneStep < 2.0 * steadyStep);

    CHECK_NEAR (ringFrequency (after, rate), 300.0, 1.0);
}

TEZLA_TEST (energy_decays_monotonically_and_actually_reaches_the_floor)
{
    // The retirement number, asserted the way the zombie-voice lesson
    // demands: not "the output is quiet" but "the energy readout itself
    // falls, and falls far enough that a threshold will genuinely fire".
    constexpr double rate = 48000.0;

    ModalResonator bank;
    bank.prepare (rate);
    bank.setModeCount (8);

    for (int mode = 0; mode < 8; ++mode)
    {
        bank.setMode (mode, 100.0 * (mode + 1), 0.3, 1.0 / (mode + 1));
        bank.excite (mode, 1.0);
    }

    const double initial = bank.energy();
    CHECK (initial > 0.0);

    double last = initial;
    bool monotone = true;

    for (int step = 0; step < 20; ++step)
    {
        for (int i = 0; i < 2400; ++i)
            (void) bank.process();

        const double now = bank.energy();
        monotone = monotone && now < last;
        last = now;
    }

    CHECK (monotone);

    // One second is over three T60s: the ring must sit at least 100 dB below
    // where it started, or a retirement threshold could never fire.
    CHECK (last < initial * 1.0e-10);
}

TEZLA_TEST (modal_silence_in_is_exact_silence_out)
{
    ModalResonator bank;
    bank.prepare (48000.0);
    bank.setModeCount (ModalResonator::kMaxModes);

    for (int mode = 0; mode < ModalResonator::kMaxModes; ++mode)
        bank.setMode (mode, 50.0 * (mode + 1), 1.0, 1.0);

    bool allZero = true;

    for (int i = 0; i < 4096; ++i)
        allZero = allZero && bank.process() == 0.0;

    CHECK (allZero);
    CHECK (bank.energy() == 0.0);
}

TEZLA_TEST (modal_no_op_setters_do_not_disturb_the_ring)
{
    // The house rule: pushing the same settings every control tick must be
    // free of side effects, to the bit.
    ModalResonator quiet, pushed;

    for (auto* bank : { &quiet, &pushed })
    {
        bank->prepare (48000.0);
        bank->setModeCount (4);

        for (int mode = 0; mode < 4; ++mode)
        {
            bank->setMode (mode, 220.0 * (mode + 1), 0.8, 0.5);
            bank->excite (mode, 0.7);
        }
    }

    double worst = 0.0;

    for (int i = 0; i < 8192; ++i)
    {
        const double a = quiet.process();

        for (int mode = 0; mode < 4; ++mode)
            pushed.setMode (mode, 220.0 * (mode + 1), 0.8, 0.5);
        pushed.setModeCount (4);

        const double b = pushed.process();
        worst = std::max (worst, std::abs (a - b));
    }

    CHECK (worst == 0.0);
}

TEZLA_TEST (input_weights_scale_the_continuous_drive)
{
    // The bow's coupling path: the same input through weight 0.5 must ring
    // the mode at exactly half the amplitude of weight 1.0.
    ModalResonator full, half;

    for (auto* bank : { &full, &half })
    {
        bank->prepare (48000.0);
        bank->setModeCount (1);
        bank->setMode (0, 440.0, 0.5, 1.0);
    }

    full.setInputWeight (0, 1.0);
    half.setInputWeight (0, 0.5);

    double worst = 0.0;

    for (int i = 0; i < 4800; ++i)
    {
        const double input = std::sin (0.13 * i);
        const double a = full.process (input);
        const double b = half.process (input);

        worst = std::max (worst, std::abs (a - 2.0 * b));
    }

    CHECK (worst < 1.0e-12);
}

TEZLA_TEST (sixty_four_modes_cost_what_the_plan_budgeted)
{
    // The estimate the defaults will be chosen against: one voice's worth of
    // bank, one second of audio, printed so regressions show in the log.
    ModalResonator bank;
    bank.prepare (48000.0);
    bank.setModeCount (ModalResonator::kMaxModes);

    for (int mode = 0; mode < ModalResonator::kMaxModes; ++mode)
    {
        bank.setMode (mode, 40.0 + 90.0 * mode, 2.0, 1.0);
        bank.excite (mode, 0.1);
    }

    double sink = 0.0;
    const auto start = std::chrono::steady_clock::now();

    for (int i = 0; i < 48000; ++i)
        sink += bank.process();

    const double seconds = std::chrono::duration<double> (
        std::chrono::steady_clock::now() - start).count();

    std::printf ("        [modal cpu] 64 modes: %.2f%% of one core (sink %g)\n",
                 100.0 * seconds, sink);

    CHECK (seconds < 0.05);   // 5% of a core for one full-fat voice
}
