// Copyright (c) 2026 The Tezla <thetezla@proton.me>
// Created by The Tezla -- https://github.com/wingit33/tezla.tech
// Music: https://soundcloud.com/thetezla | https://thetezla.bandcamp.com
// Built with development assistance from Claude (Anthropic).
// SPDX-License-Identifier: AGPL-3.0-only
// GNU AGPLv3 (see LICENSE), plus NOTICE.md's attribution term. Keep intact.

#include "TestFramework.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <vector>

#include <AnvilEngine.hpp>
#include <tezla/measure/Fft.hpp>
#include <tezla/measure/Harmonics.hpp>
#include <tezla/measure/Signals.hpp>

using namespace tezla;
namespace measure = tezla::measure;

namespace
{
constexpr double kRate = 48000.0;
constexpr std::size_t kFft = 1 << 14;

using Buffers = std::vector<std::vector<double>>;

Buffers render (anvil::Engine& engine, const Buffers& input, int blockSize)
{
    Buffers output = input;

    std::vector<double*> pointers (output.size());
    const auto total = static_cast<int> (output[0].size());

    for (int i = 0; i < total; i += blockSize)
    {
        const int n = std::min (blockSize, total - i);

        for (std::size_t c = 0; c < output.size(); ++c)
            pointers[c] = output[c].data() + i;

        engine.process (pointers.data(), static_cast<int> (pointers.size()), n);
    }

    return output;
}

Buffers sine (double amplitude, double frequency, std::size_t n, double rate = kRate)
{
    Buffers b (2, std::vector<double> (n));

    for (std::size_t i = 0; i < n; ++i)
        b[0][i] = b[1][i] = amplitude * std::sin (2.0 * std::numbers::pi * frequency
                                                  * static_cast<double> (i) / rate);

    return b;
}

anvil::Engine made (const anvil::Parameters& parameters, double rate = kRate, int block = 128)
{
    anvil::Engine engine;
    engine.prepare (rate, block, 2);
    engine.setParameters (parameters);
    engine.reset();
    return engine;
}

/// The loudest inharmonic component in the audible band, in absolute dBFS.
///
/// Absolute, because that is what CLAUDE.md section 7 specifies, and the
/// difference matters: an amplifier's fundamental is not at 0 dBFS by the time
/// the cabinet has had it.
struct Alias { double hz; double dbFs; };

Alias worstAlias (anvil::Engine& engine, double amplitude, double frequency, double rate = kRate)
{
    const double bin = measure::binExactFrequency (frequency, rate, kFft);

    const auto rendered = render (engine, sine (amplitude, bin, 3 * kFft, rate), 128);
    const std::vector<double> settled (rendered[0].begin() + 2 * kFft,
                                       rendered[0].begin() + 3 * kFft);

    const auto spectrum = measure::fftOfReal (settled);
    const double binWidth = rate / static_cast<double> (kFft);
    const auto fundamental = static_cast<std::size_t> (std::llround (bin / binWidth));

    Alias worst { 0.0, -300.0 };

    for (std::size_t k = 2; k + 1 < kFft / 2; ++k)
    {
        const double hz = static_cast<double> (k) * binWidth;

        if (hz < 20.0 || hz > 18000.0)
            continue;

        // Harmonics and the one bin either side of them are not aliasing.
        const long harmonic = std::lround (static_cast<double> (k) / static_cast<double> (fundamental));

        if (harmonic >= 1 && std::llabs (static_cast<long> (k) - harmonic * static_cast<long> (fundamental)) <= 1)
            continue;

        const double amp = 2.0 * std::abs (spectrum[k]) / static_cast<double> (kFft);
        const double db = 20.0 * std::log10 (std::max (amp, 1.0e-15));

        if (db > worst.dbFs)
            worst = { hz, db };
    }

    return worst;
}

double peakOf (const Buffers& b)
{
    double peak = 0.0;

    for (const auto& channel : b)
        for (const double v : channel)
            peak = std::max (peak, std::abs (v));

    return peak;
}
} // namespace

// ---------------------------------------------------------------------------
// The thing that decides whether this is a plugin or a toy
// ---------------------------------------------------------------------------

TEZLA_TEST (anvil_aliases_below_sixty_dbfs_at_maximum_gain_in_every_lane)
{
    // CLAUDE.md section 7: no inharmonic component above -60 dBFS in the
    // audible band, at maximum drive.
    //
    // **Swept, not sampled.** The worst case is always the highest probe
    // frequency, and a single 1 kHz probe cannot see it -- measured, that probe
    // reads x4 at -68.7 dBFS where a sweep to 4.4 kHz reads -46.5. CLAUDE.md
    // section 7 asks for a sweep for exactly this reason.
    //
    // **And the probe has to be bin-exact but must not divide the host rate.**
    // 1500 Hz at 48 kHz is both bin-exact and a divisor, so every alias of it
    // lands on a harmonic bin and is scored as a harmonic -- the engine reads
    // -174 dBFS on such a probe, which is a blind instrument rather than a good
    // result. binExactFrequency gives a probe that is bin-exact and is not a
    // divisor, which is what is wanted.
    for (const auto voicing : { anvil::Voicing::clean,
                                anvil::Voicing::vintage,
                                anvil::Voicing::modern })
    {
        anvil::Parameters parameters;
        parameters.voicing = voicing;
        parameters.cabinet = anvil::CabinetChoice::british;
        parameters.gainDb = 48.0;
        parameters.masterDb = 0.0;
        parameters.extraStages = 2;      // the worst case the plugin can build

        double worst = -300.0;

        for (const double frequency : { 82.0, 330.0, 1000.0, 4400.0 })
        {
            auto engine = made (parameters);
            worst = std::max (worst, worstAlias (engine, 0.5, frequency).dbFs);
        }

        CHECK (worst < -60.0);
    }
}

TEZLA_TEST (a_probe_that_divides_the_host_rate_cannot_see_aliasing_at_all)
{
    // The instrument, checked before it is trusted -- CLAUDE.md section 10.
    //
    // 1500 Hz at 48 kHz divides the rate exactly, so every alias of it lands on
    // a multiple of 1500 and is counted as a harmonic. The engine reads a
    // spotless figure through that probe at a factor the sweep shows is not
    // spotless at all. If this test ever fails it means the harness changed and
    // the aliasing test above may have gone blind with it.
    anvil::Parameters parameters;
    parameters.voicing = anvil::Voicing::modern;
    parameters.cabinet = anvil::CabinetChoice::british;
    parameters.gainDb = 48.0;
    parameters.masterDb = 0.0;
    parameters.extraStages = 2;
    parameters.oversampling = dsp::OversamplingMode::X4;

    // Both bin-exact, so neither leaks. Only one of them divides 48000.
    const double blind = 1500.0;
    const double honest = measure::binExactFrequency (4400.0, kRate, kFft);

    CHECK_NEAR (std::fmod (kRate, blind), 0.0, 1.0e-9);
    CHECK (std::fmod (kRate, honest) > 1.0);

    auto a = made (parameters);
    auto b = made (parameters);

    const double throughBlind = worstAlias (a, 0.5, blind).dbFs;
    const double throughHonest = worstAlias (b, 0.5, 4400.0).dbFs;

    CHECK (throughBlind < -100.0);        // spotless, and untrue
    CHECK (throughHonest > -60.0);        // the same amplifier, actually measured
    CHECK (throughHonest - throughBlind > 40.0);
}

TEZLA_TEST (anvil_auto_holds_that_figure_at_every_host_rate)
{
    // The reason Anvil's Auto targets 384 kHz rather than the house table's
    // 192: a cascade compounds, and x4 measured -47 dBFS on the three-valve
    // lane where x8 measures -72. The table is in AnvilEngine.hpp.
    CHECK (anvil::Engine::autoFactorFor (44100.0) == 8);
    CHECK (anvil::Engine::autoFactorFor (48000.0) == 8);
    CHECK (anvil::Engine::autoFactorFor (96000.0) == 4);
    CHECK (anvil::Engine::autoFactorFor (192000.0) == 2);

    // Which is the point of it: the effective rate is the same everywhere.
    for (const double rate : { 44100.0, 48000.0, 96000.0, 192000.0 })
    {
        const double effective = rate * anvil::Engine::autoFactorFor (rate);

        CHECK (effective > 340000.0);
        CHECK (effective < 400000.0);
    }

    for (const double rate : { 44100.0, 48000.0, 96000.0 })
    {
        anvil::Parameters parameters;
        parameters.voicing = anvil::Voicing::modern;
        parameters.cabinet = anvil::CabinetChoice::british;
        parameters.gainDb = 48.0;
        parameters.masterDb = 0.0;
        parameters.extraStages = 2;

        auto engine = made (parameters, rate);
        CHECK (worstAlias (engine, 0.5, 4400.0, rate).dbFs < -60.0);
    }
}

// ---------------------------------------------------------------------------
// Sample rate and block size
// ---------------------------------------------------------------------------

TEZLA_TEST (anvil_is_the_same_amplifier_at_every_block_size)
{
    // Every network in the chain is rebuilt on a timer counted in samples, with
    // the sample loop cut at the timer's boundary rather than at the callback's
    // -- so the host's buffer size cannot reach the sound. CLAUDE.md section 7,
    // where Emberdrive measured 0.296 of full scale before the rule existed.
    //
    // **The parameter has to be moving for this to test anything**, and that is
    // the whole point of the rule. With the controls settled the rebuild is a
    // no-op whatever the block size, and cutting the loop at the callback's
    // boundary passes -- which is exactly how this test read green while the
    // bug it exists for was present.
    // Automation lands every kAutomationStride samples in both runs, which is a
    // multiple of both block sizes -- so the two receive the *same* parameter
    // trajectory at the same instants and differ only in where the callback
    // boundaries fall. Sending a new value per callback instead would be
    // comparing two different automations and proving nothing.
    constexpr int kAutomationStride = 1024;

    const auto sweep = [] (int blockSize)
    {
        anvil::Parameters parameters;
        parameters.voicing = anvil::Voicing::modern;
        parameters.gainDb = 30.0;
        parameters.bass = 0.0;
        parameters.treble = 1.0;

        auto engine = made (parameters, kRate, 512);

        const auto input = sine (0.5, 220.0, 8192);

        Buffers output = input;
        std::vector<double*> pointers (output.size());

        const auto total = static_cast<int> (output[0].size());

        for (int i = 0; i < total; i += blockSize)
        {
            const int n = std::min (blockSize, total - i);

            if (i % kAutomationStride == 0)
            {
                const double t = static_cast<double> (i) / static_cast<double> (total);

                parameters.bass = t;
                parameters.treble = 1.0 - t;
                parameters.middle = 0.5 + 0.4 * std::sin (12.0 * t);
                parameters.presence = t;
                (void) engine.setParameters (parameters);
            }

            for (std::size_t c = 0; c < output.size(); ++c)
                pointers[c] = output[c].data() + i;

            engine.process (pointers.data(), static_cast<int> (pointers.size()), n);
        }

        return output;
    };

    const auto at64 = sweep (64);
    const auto at512 = sweep (512);

    double worst = 0.0;

    for (std::size_t c = 0; c < at64.size(); ++c)
        for (std::size_t i = 0; i < at64[c].size(); ++i)
            worst = std::max (worst, std::abs (at64[c][i] - at512[c][i]));

    CHECK (worst < 1.0e-9);
}

TEZLA_TEST (anvil_reports_its_latency_and_it_is_a_whole_number_of_samples)
{
    for (const auto mode : { dsp::OversamplingMode::Off, dsp::OversamplingMode::X2,
                             dsp::OversamplingMode::X4, dsp::OversamplingMode::X8 })
    {
        anvil::Parameters parameters;
        parameters.oversampling = mode;

        auto engine = made (parameters);

        CHECK (engine.getLatencySamples()
               == dsp::Oversampler::latencyForFactor (engine.getOversamplingFactor()));
    }

    anvil::Parameters off;
    off.oversampling = dsp::OversamplingMode::Off;

    auto engine = made (off);
    CHECK (engine.getLatencySamples() == 0);
}

// ---------------------------------------------------------------------------
// The obligations every plugin here has
// ---------------------------------------------------------------------------

TEZLA_TEST (anvil_is_bit_exact_bypassed)
{
    anvil::Parameters parameters;
    parameters.bypass = true;
    parameters.gainDb = 48.0;

    auto engine = made (parameters);

    const auto input = sine (0.7, 330.0, 8192);
    const auto output = render (engine, input, 128);

    // Bit-exact *and latency-matched*, which is the whole point: A/B is only
    // honest if the two paths line up. CLAUDE.md section 2.2.
    const auto latency = static_cast<std::size_t> (engine.getLatencySamples());

    CHECK (latency > 0);

    bool exact = true;

    for (std::size_t c = 0; c < input.size(); ++c)
        for (std::size_t i = latency; i < input[c].size(); ++i)
            if (output[c][i] != input[c][i - latency])
                exact = false;

    CHECK (exact);
}

TEZLA_TEST (anvil_at_zero_mix_returns_the_input_delayed_and_untouched)
{
    // Not "nearly the input". The oversampler's round trip is a whole number of
    // base-rate samples by design, so the dry path is an integer delay and
    // comes through bit for bit -- no passband ripple, no filter, nothing.
    anvil::Parameters parameters;
    parameters.mix = 0.0;
    parameters.gainDb = 48.0;

    auto engine = made (parameters);

    const auto input = sine (0.7, 330.0, 8192);
    const auto output = render (engine, input, 128);

    const int latency = engine.getLatencySamples();

    CHECK (latency > 0);

    bool exact = true;

    // Past the smoother's settling and past the delay line filling.
    for (std::size_t i = 4096; i < input[0].size(); ++i)
        if (output[0][i] != input[0][i - static_cast<std::size_t> (latency)])
            exact = false;

    CHECK (exact);
}

TEZLA_TEST (anvil_is_silent_in_silence)
{
    // A chain with two feedback loops in it -- the power amplifier's, and every
    // valve's own bias -- must not be able to start itself.
    for (const auto voicing : { anvil::Voicing::clean,
                                anvil::Voicing::vintage,
                                anvil::Voicing::modern })
    {
        anvil::Parameters parameters;
        parameters.voicing = voicing;
        parameters.gainDb = 48.0;
        parameters.masterDb = 24.0;

        auto engine = made (parameters);

        const Buffers silence (2, std::vector<double> (16384, 0.0));

        CHECK (peakOf (render (engine, silence, 128)) == 0.0);
    }
}

TEZLA_TEST (anvil_cannot_be_made_to_run_away)
{
    // Every control at its limit, in every combination that matters, on every
    // lane. A feedback loop around a nonlinearity needs a bound that cannot be
    // defeated and a test that sweeps rather than samples -- CLAUDE.md 7.
    double worst = 0.0;

    for (const auto voicing : { anvil::Voicing::clean, anvil::Voicing::vintage,
                                anvil::Voicing::modern })
        for (const int extra : { 0, 2 })
            for (const double gain : { 48.0 })
                for (const double master : { 0.0 })
                    for (const double core : { 20.0, 400.0 })
                        for (const double damping : { 0.2, 20.0 })
                            for (const double presence : { 0.0, 1.0 })
                                for (const double resonance : { 0.0, 1.0 })
                                {
                                    anvil::Parameters p;
                                    p.voicing = voicing;
                                    p.extraStages = extra;
                                    p.gainDb = gain;
                                    p.masterDb = master;
                                    p.coreHz = core;
                                    p.damping = damping;
                                    p.presence = presence;
                                    p.resonance = resonance;
                                    p.sag = 2.0;
                                    p.oversampling = dsp::OversamplingMode::X2;

                                    auto engine = made (p);

                                    const auto out = render (engine, sine (1.0, 82.0, 6000), 128);
                                    worst = std::max (worst, peakOf (out));

                                    for (const auto& channel : out)
                                        for (const double v : channel)
                                            CHECK (std::isfinite (v));
                                }

    // Measured across the whole sweep: 1.166, on the vintage lane with two
    // extra valves. An amplifier at full tilt sits just over full scale by
    // design -- each lane's makeup is calibrated so it does -- and nothing in
    // the parameter space takes it past that.
    CHECK (worst < 4.0);
}

// ---------------------------------------------------------------------------
// That the lanes are actually different amplifiers
// ---------------------------------------------------------------------------

TEZLA_TEST (anvil_clean_lane_is_genuinely_clean_and_the_others_are_not)
{
    // CLAUDE.md priority 2: a clean setting must be genuinely transparent, not
    // a quieter version of the dirty one.
    const auto thd = [] (anvil::Voicing voicing, double gain)
    {
        anvil::Parameters p;
        p.voicing = voicing;
        p.cabinet = anvil::CabinetChoice::none;
        p.gainDb = gain;

        auto engine = made (p);

        const double bin = measure::binExactFrequency (220.0, kRate, kFft);
        const auto out = render (engine, sine (0.25, bin, 3 * kFft), 128);

        return measure::analyseHarmonics (
                   std::vector<double> (out[0].begin() + 2 * kFft, out[0].begin() + 3 * kFft),
                   kRate, bin).thdDb;
    };

    // Measured at 220 Hz, -12 dBFS in, master at its default of -6 dB:
    //
    //     gain        -6      0      6     12     24
    //     clean    -56.3  -49.4  -42.9  -37.8  -26.2
    //     vintage  -37.3  -38.5  -19.0  -10.7   -7.5
    //     modern   -14.2   -9.5   -8.2   -6.4   -2.2
    //
    // The clean lane at the bottom of its range is 56 dB down, and it is still
    // 38 dB down where the high-gain lane is at 6.
    CHECK (thd (anvil::Voicing::clean, -6.0) < -50.0);
    CHECK (thd (anvil::Voicing::clean, 12.0) < -30.0);

    // And it is not merely quieter than the others. Gain means the same thing
    // on every lane -- each has an inputScale that makes it so -- and at the
    // same setting the clean lane is thirty decibels cleaner than the
    // high-gain one and twenty-five cleaner than the vintage one.
    CHECK (thd (anvil::Voicing::clean, 12.0) < thd (anvil::Voicing::modern, 12.0) - 25.0);
    CHECK (thd (anvil::Voicing::clean, 12.0) < thd (anvil::Voicing::vintage, 12.0) - 20.0);

    // The lanes are ordered, at every setting.
    for (const double gain : { -6.0, 0.0, 6.0, 12.0, 24.0 })
    {
        CHECK (thd (anvil::Voicing::clean, gain) < thd (anvil::Voicing::vintage, gain));
        CHECK (thd (anvil::Voicing::vintage, gain) < thd (anvil::Voicing::modern, gain));
    }
}

TEZLA_TEST (anvil_master_drives_a_valve_so_it_changes_the_balance_not_the_level)
{
    // The master volume is an attenuator in front of the phase inverter, and
    // the phase inverter is a valve. So turning it up does not simply make the
    // output stage louder -- it changes *which* harmonics arrive, because a
    // triode running out of swing trades its second for its third.
    //
    // Measured on the clean lane at 36 dB of gain, 220 Hz, cabinet off, as the
    // spacing between the second and third harmonics:
    //
    //     master       -36    -24    -12     -6      0
    //     with PI     16.73  16.76  15.88  12.24   1.91
    //     without     16.74  16.78  17.11  16.46   4.53
    //
    // Without the inverter the balance barely moves until the output valves
    // themselves clip. This is the test that catches its removal.
    const auto spacing = [] (double master)
    {
        anvil::Parameters p;
        p.voicing = anvil::Voicing::clean;
        p.cabinet = anvil::CabinetChoice::none;
        p.gainDb = 36.0;
        p.masterDb = master;

        auto engine = made (p);

        const double bin = measure::binExactFrequency (220.0, kRate, kFft);
        const auto out = render (engine, sine (0.5, bin, 3 * kFft), 128);

        const auto report = measure::analyseHarmonics (
            std::vector<double> (out[0].begin() + 2 * kFft, out[0].begin() + 3 * kFft), kRate, bin);

        return report.harmonicsDb[0] - report.harmonicsDb[1];
    };

    const double quiet = spacing (-36.0);
    const double loud = spacing (-6.0);

    CHECK (quiet > 14.0);              // second-harmonic dominant, as a triode is
    CHECK (quiet - loud > 3.0);        // and the balance moves as it is driven
}

TEZLA_TEST (anvil_lanes_build_different_amplifiers)
{
    // One valve, two, three -- and the tone stack in a different place in each,
    // because a passive stack in front of the distortion and one behind it are
    // different instruments.
    const auto shapeOf = [] (anvil::Voicing voicing)
    {
        anvil::Parameters p;
        p.voicing = voicing;

        auto engine = made (p);
        return std::pair { engine.getStageCount(), engine.getToneStackPosition() };
    };

    CHECK (shapeOf (anvil::Voicing::clean).first == 1);
    CHECK (shapeOf (anvil::Voicing::vintage).first == 2);
    CHECK (shapeOf (anvil::Voicing::modern).first == 3);

    // The high-gain lane has two valves in front of its stack; the vintage one
    // has the stack between its two.
    CHECK (shapeOf (anvil::Voicing::modern).second == 2);
    CHECK (shapeOf (anvil::Voicing::vintage).second == 1);

    // Extra valves cascade *after* the stack rather than moving it.
    anvil::Parameters p;
    p.voicing = anvil::Voicing::vintage;
    p.extraStages = 2;

    auto engine = made (p);

    CHECK (engine.getStageCount() == 4);
    CHECK (engine.getToneStackPosition() == 1);
}
