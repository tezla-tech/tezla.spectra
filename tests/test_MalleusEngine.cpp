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

#include "MalleusEngine.hpp"

using namespace tezla::malleus;

namespace
{
/// Renders `seconds` of audio in `blockSize` chunks into one long vector.
[[nodiscard]] std::vector<double> renderSeconds (MalleusEngine& engine,
                                                 double seconds, int blockSize)
{
    const int total = static_cast<int> (seconds * engine.getSampleRate());
    std::vector<double> out (static_cast<std::size_t> (total), 0.0);

    int done = 0;

    while (done < total)
    {
        const int take = total - done < blockSize ? total - done : blockSize;
        engine.process (out.data() + done, take);
        done += take;
    }

    return out;
}

[[nodiscard]] double rmsOf (const std::vector<double>& x, std::size_t from,
                            std::size_t to)
{
    double sum = 0.0;

    for (std::size_t n = from; n < to && n < x.size(); ++n)
        sum += x[n] * x[n];

    return std::sqrt (sum / static_cast<double> (to - from));
}

/// Hann-windowed projection: the rectangular window's own sinc skirts sit
/// near -48 dB at these offsets, which would mask the floor being measured;
/// Hann's fall far below it. Same window on every probe, so ratios stand.
[[nodiscard]] double powerAt (const std::vector<double>& x, std::size_t from,
                              std::size_t to, double hz, double fs)
{
    double re = 0.0;
    double im = 0.0;
    const auto count = static_cast<double> (to - from);

    for (std::size_t n = from; n < to && n < x.size(); ++n)
    {
        const double along = static_cast<double> (n - from) / count;
        const double window = 0.5 - 0.5 * std::cos (2.0 * std::numbers::pi * along);
        const double phase = 2.0 * std::numbers::pi * hz
                           * static_cast<double> (n - from) / fs;
        re += window * x[n] * std::cos (phase);
        im += window * x[n] * std::sin (phase);
    }

    return (re * re + im * im) / (count * count);
}
} // namespace

TEZLA_TEST (a_struck_note_rings_then_measurably_dies)
{
    // The whole retirement contract in one scene: strike, release, and the
    // voice count must reach zero -- not the output getting quiet, the
    // VOICE DYING (the Sonitus zombie lesson, asserted from birth). Once
    // dead, the engine's output is bit-exact zero.
    MalleusEngine engine;
    engine.prepare (48000.0);
    engine.settings().exciter = Exciter::Mallet;
    engine.settings().dropSemitones = 12.0;
    engine.settings().decaySeconds = 1.5;

    engine.noteOn (57, 0.9);

    auto ring = renderSeconds (engine, 0.1, 480);

    CHECK (rmsOf (ring, 0, ring.size()) > 1.0e-4);
    CHECK (engine.activeVoiceCount() == 1);

    engine.noteOff (57);

    double diedAt = -1.0;

    for (int step = 0; step < 800; ++step)
    {
        (void) renderSeconds (engine, 0.01, 480);

        if (engine.activeVoiceCount() == 0)
        {
            diedAt = 0.1 + 0.01 * (step + 1);
            break;
        }
    }

    std::printf ("        [engine] voice measurably dead %.2f s after the strike\n",
                 diedAt);

    CHECK (diedAt > 0.0);
    CHECK (diedAt < 5.0);

    const auto after = renderSeconds (engine, 0.1, 480);

    for (const double y : after)
        CHECK (y == 0.0);
}

TEZLA_TEST (seventeen_notes_share_sixteen_voices_and_the_newest_survives)
{
    // Polyphony is 16 with oldest-first stealing: the seventeenth note
    // lands on the first note's voice, so note 40 is gone and note 56
    // rings.
    MalleusEngine engine;
    engine.prepare (48000.0);
    engine.settings().decaySeconds = 4.0;

    for (int note = 40; note <= 56; ++note)
    {
        engine.noteOn (note, 0.7);

        double sink[48];
        engine.process (sink, 48);
    }

    CHECK (engine.activeVoiceCount() == MalleusEngine::kMaxVoices);

    bool newestAlive = false;
    bool oldestAlive = false;

    for (int index = 0; index < MalleusEngine::kMaxVoices; ++index)
    {
        const auto& voice = engine.voiceForTest (index);

        if (voice.isActive() && voice.getNote() == 56)
            newestAlive = true;

        if (voice.isActive() && voice.getNote() == 40)
            oldestAlive = true;
    }

    CHECK (newestAlive);
    CHECK (! oldestAlive);
}

TEZLA_TEST (per_hit_seeds_vary_but_the_take_replays)
{
    // Two hits of the same key must differ -- the roll's humanise and the
    // scrape's grain are seeded per hit -- while reset() plus the same
    // playing replays the first take bit-exactly. Variation without
    // un-mixability.
    MalleusEngine engine;
    engine.prepare (48000.0);
    engine.settings().exciter = Exciter::Roll;
    engine.settings().rollHumanise = 0.7;
    engine.settings().noiseAmount = 0.5;
    engine.settings().decaySeconds = 1.0;

    const auto playOnce = [&engine]
    {
        engine.noteOn (50, 0.8);
        auto held = renderSeconds (engine, 0.7, 512);
        engine.noteOff (50);
        const auto tail = renderSeconds (engine, 0.3, 512);
        held.insert (held.end(), tail.begin(), tail.end());
        return held;
    };

    const auto firstHit = playOnce();
    const auto secondHit = playOnce();

    double hitDifference = 0.0;

    for (std::size_t n = 0; n < firstHit.size(); ++n)
        hitDifference = std::max (hitDifference,
                                  std::abs (firstHit[n] - secondHit[n]));

    std::printf ("        [seeds] hit-to-hit worst difference %.4g\n",
                 hitDifference);

    CHECK (hitDifference > 1.0e-3);   // audibly a different performance

    engine.reset();

    const auto replayFirst = playOnce();
    const auto replaySecond = playOnce();

    CHECK (replayFirst.size() == firstHit.size());

    for (std::size_t n = 0; n < firstHit.size(); ++n)
    {
        CHECK (replayFirst[n] == firstHit[n]);
        CHECK (replaySecond[n] == secondHit[n]);
    }
}

TEZLA_TEST (block_size_cannot_bend_the_take)
{
    // The section 7 block rule on the whole instrument, in its hardest
    // posture: a humanised roll re-striking sample-accurately, a tension
    // drop retuning at control rate, the taraf ringing under it -- rendered
    // in 64-sample blocks and in 512-sample blocks, the two takes must be
    // BIT-IDENTICAL, because every control cut lands on the 48-sample
    // boundary wherever the host cuts its buffers.
    const auto play = [] (int blockSize)
    {
        MalleusEngine engine;
        engine.prepare (48000.0);
        engine.settings().exciter = Exciter::Roll;
        engine.settings().rollHumanise = 0.5;
        engine.settings().noiseAmount = 0.4;
        engine.settings().dropSemitones = 7.0;
        engine.settings().decaySeconds = 1.2;
        engine.setSympathetic (7, 0.5, 0.8, 0.4, 4.0, 0.6);

        engine.noteOn (45, 0.9);
        auto held = renderSeconds (engine, 0.8, blockSize);
        engine.noteOff (45);
        const auto tail = renderSeconds (engine, 0.2, blockSize);
        held.insert (held.end(), tail.begin(), tail.end());
        return held;
    };

    const auto small = play (64);
    const auto large = play (512);

    CHECK (small.size() == large.size());

    double worst = 0.0;

    for (std::size_t n = 0; n < small.size(); ++n)
        worst = std::max (worst, std::abs (small[n] - large[n]));

    std::printf ("        [blocks] 64 vs 512: worst difference %g\n", worst);

    CHECK (worst == 0.0);
}

TEZLA_TEST (silence_is_exact_at_rest_and_after_the_taraf_fades)
{
    // Silence in, silence out, bit-exactly: a never-played engine, and a
    // played one after its voices die and the sympathetic ring drops below
    // the audibility floor and is snapped home.
    MalleusEngine engine;
    engine.prepare (48000.0);
    engine.setSympathetic (7, 0.6, 1.0, 0.0, 0.5, 0.6);

    const auto rest = renderSeconds (engine, 0.5, 480);

    for (const double y : rest)
        CHECK (y == 0.0);

    engine.settings().decaySeconds = 0.8;
    engine.noteOn (57, 0.9);
    (void) renderSeconds (engine, 0.1, 480);
    engine.noteOff (57);

    double exactAt = -1.0;

    for (int step = 0; step < 1000; ++step)
    {
        (void) renderSeconds (engine, 0.01, 480);

        if (engine.activeVoiceCount() == 0 && engine.sympatheticEnergy() == 0.0)
        {
            exactAt = 0.1 + 0.01 * (step + 1);
            break;
        }
    }

    std::printf ("        [silence] output exactly zero again %.2f s after the strike\n",
                 exactAt);

    CHECK (exactAt > 0.0);

    const auto after = renderSeconds (engine, 0.1, 480);

    for (const double y : after)
        CHECK (y == 0.0);
}

TEZLA_TEST (nothing_rings_between_the_modes_at_maximum_hardness)
{
    // The aliasing posture of a modal synth, measured: every partial is
    // computed, injected in closed form, and muted rather than folded if
    // it would pass 0.45 fs -- so at maximum hardness with all 64 partials
    // of an inharmonic bar, the sustained output must contain the MODES
    // and nothing else. Probes at the geometric midpoints between adjacent
    // audible modes sit more than 60 dB under the loudest mode (the
    // section 7 gate), and the same note at 96 kHz keeps the same tail
    // level -- the character does not follow the host rate. This
    // measurement IS the oversampling decision: nothing here needs one.
    //
    // The window starts at 0.7 s deliberately: the vactrol's fast early
    // decay amplitude-modulates real sidebands around every mode (that is
    // the ping, not aliasing -- first measured at -38 dB when this window
    // opened at 0.1 s), and by 0.7 s the gate's tail is a ~1 Hz-bandwidth
    // exponential, so what this measures is the modal path itself.
    const auto play = [] (double fs)
    {
        MalleusEngine engine;
        engine.prepare (fs);
        engine.settings().exciter = Exciter::Mallet;
        engine.settings().material = 1.0;   // the bar: inharmonic
        engine.settings().partials = 64;
        engine.settings().hardness = 1.0;
        engine.settings().decaySeconds = 3.0;

        engine.noteOn (60, 1.0);   // ~262 Hz: a dense audible stack

        struct Take
        {
            std::vector<double> audio;
            std::vector<double> modes;
        };

        Take take;
        take.audio = renderSeconds (engine, 1.6, 480);

        for (int mode = 0; mode < 64; ++mode)
            if (engine.voiceForTest (0).modeGain (mode) > 0.0)
                take.modes.push_back (engine.voiceForTest (0).modeFrequency (mode));

        return take;
    };

    const auto at48 = play (48000.0);

    CHECK (at48.modes.size() > 10);

    const auto from = static_cast<std::size_t> (0.7 * 48000.0);
    const auto to = static_cast<std::size_t> (1.5 * 48000.0);

    double onMode = 0.0;
    double between = 0.0;

    for (std::size_t m = 0; m < at48.modes.size(); ++m)
    {
        onMode = std::max (onMode,
                           powerAt (at48.audio, from, to, at48.modes[m], 48000.0));

        if (m + 1 < at48.modes.size()
            && at48.modes[m + 1] / at48.modes[m] > 1.1)
            between = std::max (between,
                                powerAt (at48.audio, from, to,
                                         std::sqrt (at48.modes[m] * at48.modes[m + 1]),
                                         48000.0));
    }

    const double gapDb = 10.0 * std::log10 (between / onMode);

    std::printf ("        [inharmonic gate] between-modes floor %.1f dB below the loudest mode\n",
                 gapDb);

    CHECK (gapDb < -60.0);

    // Same instrument at 96 kHz: same modes, same tail level.
    const auto at96 = play (96000.0);

    const double tail48 = rmsOf (at48.audio, from, to);
    const double tail96 = rmsOf (at96.audio, 2 * from, 2 * to);

    std::printf ("        [inharmonic gate] tail rms 48k %.3g vs 96k %.3g\n",
                 tail48, tail96);

    CHECK_NEAR (tail96 / tail48, 1.0, 0.05);
}

TEZLA_TEST (sixteen_voices_cost_what_the_plan_budgeted_and_the_dead_cost_nothing)
{
    // The plan budgeted 10-15% of a core for the full instrument. Sixteen
    // BOWED voices (the sustained worst case: friction loop + 64 modes +
    // vactrol each) plus a twelve-string taraf, measured; then everything
    // released, and the dead engine's cost must return to the idle
    // baseline -- the other half of the zombie-voice lesson.
    MalleusEngine engine;
    engine.prepare (48000.0);
    engine.settings().exciter = Exciter::Bow;
    engine.settings().partials = 64;
    engine.settings().bowPressure = 0.5;
    engine.settings().decaySeconds = 2.0;
    engine.setSympathetic (12, 0.5, 0.8, 0.3, 8.0, 0.6);

    for (int note = 0; note < 16; ++note)
        engine.noteOn (36 + 3 * note, 0.8);

    (void) renderSeconds (engine, 0.1, 480);   // let everything speak

    double sink = 0.0;
    std::vector<double> buffer (480, 0.0);

    auto start = std::chrono::steady_clock::now();

    for (int block = 0; block < 100; ++block)
    {
        engine.process (buffer.data(), 480);
        sink += buffer[0];
    }

    const double activeSeconds = std::chrono::duration<double> (
        std::chrono::steady_clock::now() - start).count();

    engine.allNotesOff();

    for (int step = 0; step < 1200 && engine.activeVoiceCount() > 0; ++step)
        engine.process (buffer.data(), 480);

    CHECK (engine.activeVoiceCount() == 0);

    start = std::chrono::steady_clock::now();

    for (int block = 0; block < 100; ++block)
    {
        engine.process (buffer.data(), 480);
        sink += buffer[0];
    }

    const double idleSeconds = std::chrono::duration<double> (
        std::chrono::steady_clock::now() - start).count();

    std::printf ("        [engine cpu] 16 bowed voices %.1f%%, dead engine %.2f%% of a core (sink %g)\n",
                 100.0 * activeSeconds, 100.0 * idleSeconds, sink);

    CHECK (activeSeconds < 0.5);    // under half a core, fully loaded
    CHECK (idleSeconds < 0.02);     // dead means baseline, not busy
}
