// Copyright (c) 2026 The Tezla <thetezla@proton.me>
// Created by The Tezla -- https://github.com/wingit33/tezla.tech
// Music: https://soundcloud.com/thetezla | https://thetezla.bandcamp.com
// Built with development assistance from Claude (Anthropic).
// SPDX-License-Identifier: AGPL-3.0-only
// GNU AGPLv3 (see LICENSE), plus NOTICE.md's attribution term. Keep intact.

// The assembled strip.
//
// The stages are tested individually elsewhere. What only this file can ask is
// whether the *chain* holds together: that seven neutral stages in a row are
// still the identity to the bit, that the order is the order the header claims,
// that the two channels move together, and that none of it depends on how the
// host happens to cut its buffers.

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <numbers>
#include <vector>

#include <tezla/dsp/Decibels.hpp>
#include <tezla/dsp/Exact.hpp>

#include <SyrinxEngine.hpp>

#include "TestFramework.hpp"

using namespace tezla;
using namespace tezla::syrinx;

namespace {

constexpr double kRate = 48000.0;

/// A voice-shaped probe: a fundamental with a few harmonics, a sibilant band
/// on top, and a level that moves. Enough for every detector in the strip to
/// have something to decide about.
double voiceAt (std::size_t index, double levelDb = -12.0)
{
    const double t = static_cast<double> (index) / kRate;
    const double twoPi = 2.0 * std::numbers::pi;

    double value = 1.00 * std::sin (twoPi * 180.0 * t)
                 + 0.50 * std::sin (twoPi * 360.0 * t)
                 + 0.30 * std::sin (twoPi * 720.0 * t)
                 + 0.12 * std::sin (twoPi * 2400.0 * t);

    // A burst of sibilance every 400 ms, which is what the de-esser is for.
    if (std::fmod (t, 0.4) < 0.08)
        value += 0.9 * std::sin (twoPi * 7300.0 * t) * std::sin (twoPi * 5100.0 * t + 1.0);

    return dsp::dbToGain (levelDb) * value * 0.4;
}

SyrinxEngine::Settings working()
{
    SyrinxEngine::Settings s;
    s.highpassHz = 90.0;

    s.gate.thresholdDb = -40.0;
    s.gate.rangeDb = 18.0;

    s.deEss.rangeDb = 9.0;
    s.deEss.thresholdDb = -12.0;

    s.leveller.thresholdDb = -24.0;
    s.leveller.ratio = 2.5;
    s.leveller.attackMs = 30.0;
    s.leveller.releaseMs = 250.0;
    s.leveller.makeupDb = 3.0;
    s.leveller.programDependent = true;

    s.peak.thresholdDb = -12.0;
    s.peak.ratio = 6.0;
    s.peak.attackMs = 2.0;
    s.peak.releaseMs = 80.0;

    s.eq.lowShelfDb = -2.0;
    s.eq.midDb = 1.5;
    s.eq.highShelfDb = 2.5;

    s.outputTrimDb = -1.0;

    return s;
}

/// Renders the probe through a strip, in blocks of `blockSize`, pushing the
/// settings once per block the way a host does.
std::vector<double> render (const SyrinxEngine::Settings& settings, double seconds,
                            int blockSize, double levelDb = -12.0)
{
    SyrinxEngine engine;
    engine.prepare (kRate);

    const auto total = static_cast<std::size_t> (seconds * kRate);
    std::vector<double> left (total, 0.0);
    std::vector<double> right (total, 0.0);

    for (std::size_t n = 0; n < total; ++n)
        left[n] = right[n] = voiceAt (n, levelDb);

    for (std::size_t n = 0; n < total; n += static_cast<std::size_t> (blockSize))
    {
        engine.setSettings (settings);

        const auto count = std::min (static_cast<std::size_t> (blockSize), total - n);
        engine.process (left.data() + n, right.data() + n, static_cast<int> (count));
    }

    return left;
}

double peakOf (const std::vector<double>& x, std::size_t from = 0)
{
    double peak = 0.0;

    for (std::size_t n = from; n < x.size(); ++n)
        peak = std::max (peak, std::abs (x[n]));

    return peak;
}

} // namespace

// ---------------------------------------------------------------------------
// Section 7, seven times over
// ---------------------------------------------------------------------------

TEZLA_TEST (a_strip_with_every_stage_neutral_is_bit_exact_identity)
{
    // The claim that matters most for a channel strip, because a strip is by
    // definition permanently in the path: seven stages in a row, each of which
    // has to be the identity function or the whole thing is "nearly"
    // transparent seven times over.
    //
    // The defaults are the neutral setting -- trims 0 dB, high-pass off, gate
    // Range 0, de-ess Range 0, both ratios 1:1, every EQ band 0 dB -- and
    // 40001 sample values go through both channels and come back bit for bit.
    SyrinxEngine engine;
    engine.prepare (kRate);
    engine.setSettings (SyrinxEngine::Settings {});

    CHECK (engine.isIdentity());

    for (int i = -20000; i <= 20000; ++i)
    {
        double left = static_cast<double> (i) / 19997.0;   // not a round grid
        double right = -left * 0.37;

        const double originalLeft = left;
        const double originalRight = right;

        engine.process (&left, &right, 1);

        CHECK (dsp::isExactly (left, originalLeft));
        CHECK (dsp::isExactly (right, originalRight));
    }
}

TEZLA_TEST (each_stage_alone_can_break_the_identity_and_nothing_else_does)
{
    // The identity above is worth exactly as much as the list of things that
    // could break it. Seven stages, seven single-setting departures from
    // neutral, and each one has to be detectable on its own -- otherwise a
    // stage could be silently doing nothing and the test above would still
    // pass.
    //
    // This is the break-check written as a test rather than as an edit: each
    // row perturbs one control by the smallest musically real amount and
    // asserts the strip stops being the identity.
    //
    // Each row also pins `isIdentity()` against the arithmetic, because that
    // predicate is what the JUCE layer will trust when it claims a preset is
    // transparent, and a predicate that is never checked against samples is a
    // comment. The implication is deliberately **one way** -- `isIdentity()`
    // true must mean bit-exact -- because the predicate reads the settings and
    // not the signal: a Range of 12 dB with a threshold nothing reaches is
    // honestly "not neutral" even though this probe never triggers it. Drop
    // any one clause from `isIdentity()` and its row below goes red.
    const auto stillIdentity = [] (const SyrinxEngine::Settings& s)
    {
        SyrinxEngine engine;
        engine.prepare (kRate);
        engine.setSettings (s);

        bool exact = true;

        for (std::size_t n = 0; n < 4800; ++n)
        {
            double left = voiceAt (n);
            double right = left;
            const double original = left;

            engine.process (&left, &right, 1);

            if (! dsp::isExactly (left, original))
                exact = false;
        }

        CHECK (! engine.isIdentity() || exact);

        return exact;
    };

    CHECK (stillIdentity (SyrinxEngine::Settings {}));

    {
        auto s = SyrinxEngine::Settings {}; s.inputTrimDb = 0.1;
        CHECK (! stillIdentity (s));
    }
    {
        auto s = SyrinxEngine::Settings {}; s.highpassHz = 20.0;
        CHECK (! stillIdentity (s));
    }
    {
        auto s = SyrinxEngine::Settings {}; s.gate.rangeDb = 1.0;
        s.gate.thresholdDb = 0.0;   // so it is actually shut
        CHECK (! stillIdentity (s));
    }
    {
        auto s = SyrinxEngine::Settings {};
        s.deEss.rangeDb = 6.0; s.deEss.thresholdDb = -40.0;
        CHECK (! stillIdentity (s));
    }
    {
        auto s = SyrinxEngine::Settings {};
        s.leveller.ratio = 1.1; s.leveller.thresholdDb = -60.0;
        CHECK (! stillIdentity (s));
    }
    {
        auto s = SyrinxEngine::Settings {};
        s.peak.ratio = 1.1; s.peak.thresholdDb = -60.0;
        CHECK (! stillIdentity (s));
    }
    {
        auto s = SyrinxEngine::Settings {}; s.eq.lowShelfDb = 0.1;
        CHECK (! stillIdentity (s));
    }
    {
        auto s = SyrinxEngine::Settings {}; s.eq.midDb = 0.1;
        CHECK (! stillIdentity (s));
    }
    {
        auto s = SyrinxEngine::Settings {}; s.eq.highShelfDb = 0.1;
        CHECK (! stillIdentity (s));
    }
    {
        auto s = SyrinxEngine::Settings {}; s.outputTrimDb = 0.1;
        CHECK (! stillIdentity (s));
    }
}

TEZLA_TEST (silence_in_is_exactly_silence_out_with_the_strip_working_hard)
{
    // Nothing in, everything switched on: a gate that leaked its range, a
    // compressor with a makeup applied to a dry path, or an EQ ringing on its
    // own initial state would all show up as a non-zero here.
    SyrinxEngine engine;
    engine.prepare (kRate);
    engine.setSettings (working());

    std::vector<double> left (4096, 0.0);
    std::vector<double> right (4096, 0.0);

    for (int block = 0; block < 24; ++block)
    {
        engine.process (left.data(), right.data(), static_cast<int> (left.size()));

        for (std::size_t n = 0; n < left.size(); ++n)
        {
            CHECK (dsp::isExactlyZero (left[n]));
            CHECK (dsp::isExactlyZero (right[n]));
        }
    }
}

// ---------------------------------------------------------------------------
// The chain behaves like a chain
// ---------------------------------------------------------------------------

TEZLA_TEST (the_strip_does_not_depend_on_the_block_size)
{
    // CLAUDE.md section 7's block rule. Nothing in the strip has a control
    // boundary of its own -- every detector, filter and smoother counts
    // samples -- so 64 and 512 have to agree **to the bit**, with the settings
    // pushed once per block in both runs as a host does.
    //
    // Break-checked with the bug section 7 names by name: a stage reset from
    // inside `setSettings`, which is how "apply the change by re-preparing"
    // gets written. One `deEsser_.reset()` there and the two block sizes
    // diverge by **0.0697 of full scale** -- seven percent of the signal,
    // decided by nothing but how the host happens to cut its buffers.
    const auto small = render (working(), 2.0, 64);
    const auto large = render (working(), 2.0, 512);

    CHECK (small.size() == large.size());

    double worst = 0.0;

    for (std::size_t n = 0; n < small.size(); ++n)
        worst = std::max (worst, std::abs (small[n] - large[n]));

    CHECK (dsp::isExactlyZero (worst));
}

TEZLA_TEST (the_two_channels_stay_locked_together)
{
    // Section 7's stereo rule. Every detector in the strip runs once, on the
    // linked signal, so a signal that is louder in one channel must still be
    // reduced by the same amount in both -- otherwise the image moves every
    // time a doubled vocal leans one way.
    //
    // Measured: with the right channel 6 dB down and the strip compressing
    // hard -- 8:1 then 10:1 -- the output ratio between the channels stays
    // 6.000 dB from start to finish, to within **9.2e-09 dB**.
    //
    // Break-checked by giving each channel its own `computeGain` call, which is
    // the shape the bug actually takes: the deviation goes to **0.357 dB** and
    // this test goes red. That is the image walking a third of a decibel toward
    // the quiet side and back on every syllable, which on a doubled vocal is
    // audible as the double wandering rather than sitting where it was panned.
    SyrinxEngine engine;
    engine.prepare (kRate);

    auto s = working();
    s.leveller.ratio = 8.0;
    s.leveller.thresholdDb = -30.0;
    s.peak.ratio = 10.0;
    s.peak.thresholdDb = -20.0;
    engine.setSettings (s);

    const auto total = static_cast<std::size_t> (1.5 * kRate);
    std::vector<double> left (total, 0.0);
    std::vector<double> right (total, 0.0);

    for (std::size_t n = 0; n < total; ++n)
    {
        left[n] = voiceAt (n);
        right[n] = left[n] * dsp::dbToGain (-6.0);
    }

    engine.process (left.data(), right.data(), static_cast<int> (total));

    // Over the settled second, every non-trivial sample keeps the 6 dB offset.
    double worstDeviationDb = 0.0;

    for (std::size_t n = static_cast<std::size_t> (0.5 * kRate); n < total; ++n)
    {
        if (std::abs (left[n]) < 1.0e-6)
            continue;

        const double ratioDb = 20.0 * std::log10 (std::abs (right[n] / left[n]));
        worstDeviationDb = std::max (worstDeviationDb, std::abs (ratioDb + 6.0));
    }

    CHECK (worstDeviationDb < 0.001);
}

TEZLA_TEST (the_strip_levels_what_it_is_asked_to_level)
{
    // The whole point of the thing: a signal that swings 18 dB comes out
    // swinging much less. Measured on the voice probe rendered at -6 and at
    // -24 dBFS, through the working settings:
    //
    //     input level    output peak
    //        -6 dBFS       0.3030
    //       -24 dBFS       0.0826
    //
    // **18 dB in becomes 11.29 dB out**, which is 1.6:1 overall -- less than
    // either compressor's own ratio, and correctly so: the quiet take barely
    // reaches the leveller's threshold and never reaches the peak catcher's,
    // so most of the range is passed through untouched. A strip that levelled
    // harder than that would be flattening the performance rather than
    // steadying it.
    const double loud = peakOf (render (working(), 1.5, 64, -6.0),
                                static_cast<std::size_t> (0.5 * kRate));
    const double quiet = peakOf (render (working(), 1.5, 64, -24.0),
                                 static_cast<std::size_t> (0.5 * kRate));

    const double outputRangeDb = 20.0 * std::log10 (loud / quiet);

    CHECK (outputRangeDb > 6.0);    // it is not a limiter
    CHECK (outputRangeDb < 13.0);   // and it is not doing nothing
}

TEZLA_TEST (the_stage_meters_report_the_stage_that_is_working)
{
    // A strip's display exists to say *which* stage is doing the work, so the
    // readouts have to be per stage and have to be right. Three settings, each
    // of which should light exactly one meter.
    const auto metersFor = [] (const SyrinxEngine::Settings& s, double levelDb)
    {
        SyrinxEngine engine;
        engine.prepare (kRate);
        engine.setSettings (s);

        SyrinxEngine::Meters worst;

        for (std::size_t n = 0; n < static_cast<std::size_t> (1.2 * kRate); ++n)
        {
            double left = voiceAt (n, levelDb);
            double right = left;

            engine.process (&left, &right, 1);

            const auto m = engine.getMeters();
            worst.gateDb = std::min (worst.gateDb, m.gateDb);
            worst.deEssDb = std::min (worst.deEssDb, m.deEssDb);
            worst.levellerDb = std::min (worst.levellerDb, m.levellerDb);
            worst.peakDb = std::min (worst.peakDb, m.peakDb);
        }

        return worst;
    };

    // Only the gate: a signal well under its threshold.
    {
        auto s = SyrinxEngine::Settings {};
        s.gate.thresholdDb = 0.0;
        s.gate.rangeDb = 20.0;

        const auto m = metersFor (s, -30.0);
        CHECK (m.gateDb < -19.0);
        CHECK (dsp::isExactlyZero (m.deEssDb));
        CHECK (dsp::isExactlyZero (m.levellerDb));
        CHECK (dsp::isExactlyZero (m.peakDb));
    }

    // Only the de-esser: the probe's sibilant bursts.
    {
        auto s = SyrinxEngine::Settings {};
        s.deEss.rangeDb = 12.0;
        s.deEss.thresholdDb = -18.0;

        const auto m = metersFor (s, -12.0);
        CHECK (dsp::isExactlyZero (m.gateDb));
        CHECK (m.deEssDb < -1.0);
        CHECK (dsp::isExactlyZero (m.levellerDb));
        CHECK (dsp::isExactlyZero (m.peakDb));
    }

    // Only the leveller.
    {
        auto s = SyrinxEngine::Settings {};
        s.leveller.thresholdDb = -30.0;
        s.leveller.ratio = 4.0;

        const auto m = metersFor (s, -12.0);
        CHECK (dsp::isExactlyZero (m.gateDb));
        CHECK (dsp::isExactlyZero (m.deEssDb));
        CHECK (m.levellerDb < -3.0);
        CHECK (dsp::isExactlyZero (m.peakDb));
    }
}

TEZLA_TEST (de_essing_before_compression_is_not_the_same_as_after)
{
    // The claim the chain order rests on, and the reason it is worth a test
    // rather than a comment. A compressor that ducks on an /s/ pulls down the
    // word after it too, so the sibilance ends up *louder* relative to what
    // follows -- which is why a strip that compresses first needs a harder
    // de-esser to repair damage it caused itself.
    //
    // Measured here as the difference the order makes at all: the same
    // settings with the de-esser's Range removed leave the compressor to meet
    // the raw sibilance, and it works **0.691 dB harder** on the burst --
    // 8.892 dB of reduction against 8.201. If the order made no difference
    // that number would be zero.
    //
    // 0.691 dB is a modest figure and worth being honest about: this probe's
    // sibilance is a synthetic burst two harmonics wide, where a real /s/
    // carries far more energy relative to the vowel around it. What the test
    // establishes is the *sign and the mechanism* -- the de-esser really is
    // taking work off the compressor -- rather than how much it is worth on
    // a particular take.
    //
    // Break-checked by swapping DE-ESS and COMP 1 in the engine, which is the
    // ordering this test exists to rule out. The difference then reads
    // **exactly 0.000 dB**: with the compressor in front, the de-esser is
    // downstream of it and cannot take any work off it at all, however hard it
    // is driven. That zero is the whole argument for the order.
    const auto worstLevellerReduction = [] (double deEssRangeDb)
    {
        SyrinxEngine engine;
        engine.prepare (kRate);

        auto s = SyrinxEngine::Settings {};
        s.deEss.rangeDb = deEssRangeDb;
        s.deEss.thresholdDb = -18.0;
        s.leveller.thresholdDb = -26.0;
        s.leveller.ratio = 4.0;
        s.leveller.attackMs = 2.0;
        s.leveller.releaseMs = 120.0;
        engine.setSettings (s);

        double worst = 0.0;

        for (std::size_t n = 0; n < static_cast<std::size_t> (1.2 * kRate); ++n)
        {
            double left = voiceAt (n, -10.0);
            double right = left;

            engine.process (&left, &right, 1);
            worst = std::min (worst, engine.getMeters().levellerDb);
        }

        return worst;
    };

    const double withDeEss = worstLevellerReduction (12.0);
    const double without = worstLevellerReduction (0.0);

    // Without the de-esser in front, the compressor works harder on the same
    // material -- which is the compressor reacting to sibilance rather than to
    // the voice.
    CHECK (without < withDeEss - 0.4);
    CHECK_NEAR (withDeEss - without, 0.691, 0.05);
}

// ---------------------------------------------------------------------------
// What it costs
// ---------------------------------------------------------------------------

TEZLA_TEST (the_strip_costs_little_enough_to_go_on_every_take)
{
    // The budget for a channel strip is not one instance. CLAUDE.md's opening
    // premise is twenty plugins in a project, and this is the plugin that goes
    // on the lead, both doubles and every ad-lib -- so what matters is what a
    // rack of them costs, not what one does.
    //
    // One second of stereo audio at 48 kHz through the working settings, in
    // 480-sample blocks with the settings pushed once per block as a host does,
    // with every stage actually working: the gate opening and shutting, the
    // de-esser tracking the bursts, both compressors reducing, three EQ bands
    // and both trims.
    //
    // The probe is generated **once**, outside the timing, and copied into the
    // block each pass -- otherwise four sines and a ring-modulated sibilance
    // burst per sample get counted as the strip's cost, and they are not. That
    // hoist is worth less than it looks: 1.66% with the probe inline against
    // **1.31% of a core** for one stereo instance without it, so the harness
    // was about a fifth of the reading rather than most of it.
    //
    // 1.31% is the number to plan with. Eight vocal tracks -- a lead, two
    // doubles and five ad-libs, which is an ordinary rap arrangement -- cost
    // about 10.5%, and CLAUDE.md's twenty-plugin project is comfortable if a
    // fair share of those twenty are strips.
    //
    // For scale, Malleus's sixteen bowed voices cost 24% of a core. A strip is
    // cheap because nothing in it is oversampled, and that is the correct
    // design rather than a saving: there is no nonlinearity in the chain to
    // alias (CLAUDE.md section 6), so oversampling a dynamics-only path would
    // buy latency and CPU for nothing at all.
    SyrinxEngine engine;
    engine.prepare (kRate);

    constexpr int kBlock = 480;
    constexpr int kBlocks = 100;   // one second at 48 kHz

    std::vector<double> source (static_cast<std::size_t> (kBlock * kBlocks), 0.0);

    for (std::size_t n = 0; n < source.size(); ++n)
        source[n] = voiceAt (n);

    std::vector<double> left (kBlock, 0.0);
    std::vector<double> right (kBlock, 0.0);

    const auto settings = working();
    double sink = 0.0;

    const auto run = [&] (int blocks)
    {
        for (int block = 0; block < blocks; ++block)
        {
            const auto* from = source.data() + static_cast<std::size_t> (block) * kBlock;

            std::copy (from, from + kBlock, left.begin());
            std::copy (from, from + kBlock, right.begin());

            engine.setSettings (settings);
            engine.process (left.data(), right.data(), kBlock);
            sink += left[0];
        }
    };

    // A warm-up pass, so the measured second is steady state rather than a cold
    // cache and a gate still deciding whether it is open.
    run (10);

    const auto start = std::chrono::steady_clock::now();
    run (kBlocks);
    const double seconds = std::chrono::duration<double> (
        std::chrono::steady_clock::now() - start).count();

    std::printf ("        [syrinx cpu] full strip, stereo: %.2f%% of a core (sink %g)\n",
                 100.0 * seconds, sink);

    // Generous against the measured 1.31% because a shared CI runner is not a
    // quiet machine. What it rules out is the regression that matters: a filter
    // redesign or an allocation creeping into the per-sample path, either of
    // which costs orders of magnitude rather than percent.
    CHECK (seconds < 0.15);
}
