// Copyright (c) 2026 The Tezla <thetezla@proton.me>
// Created by The Tezla -- https://github.com/wingit33/tezla.tech
// Music: https://soundcloud.com/thetezla | https://thetezla.bandcamp.com
// Built with development assistance from Claude (Anthropic).
// SPDX-License-Identifier: AGPL-3.0-only
// GNU AGPLv3 (see LICENSE), plus NOTICE.md's attribution term. Keep intact.

#include "TestFramework.hpp"

#include <cmath>
#include <cstdint>
#include <vector>

#include <tezla/measure/Fft.hpp>
#include <tezla/measure/Harmonics.hpp>
#include <tezla/measure/Signals.hpp>

#include <SamplePlayer.hpp>

using namespace tezla::svarayantra;
namespace measure = tezla::measure;

namespace
{
constexpr double kRate = 48000.0;
constexpr std::size_t kFft = 1 << 14;

/// A pool holding `cycles` whole cycles of a sine of the given period, with
/// 8 samples of silence padding each side and POISON beyond the declared
/// bounds -- so any tap that escapes the sample is loud, not lucky.
struct SinePool
{
    std::vector<std::int16_t> data;
    std::uint32_t begin { 0 };
    std::uint32_t end { 0 };
    std::uint32_t loopBegin { 0 };
    std::uint32_t loopEnd { 0 };

    SinePool (int period, int cycles)
    {
        constexpr int kPoison = 64;
        const int body = period * cycles + 16;

        data.assign (static_cast<std::size_t> (body + 2 * kPoison), 32767);

        begin = kPoison;
        end = static_cast<std::uint32_t> (kPoison + body);
        loopBegin = begin + 8;
        loopEnd = loopBegin + static_cast<std::uint32_t> (period * cycles);

        for (int i = 0; i < body; ++i)
            data[static_cast<std::size_t> (kPoison + i)] = static_cast<std::int16_t> (
                std::lround (30000.0 * std::sin (2.0 * 3.141592653589793 * (i - 8) / period)));
    }
};

std::vector<double> render (SamplePlayer& player, std::size_t count)
{
    std::vector<double> out (count);

    for (auto& sample : out)
        sample = player.next();

    return out;
}
} // namespace

// ---------------------------------------------------------------------------
// Exactness where exactness is owed
// ---------------------------------------------------------------------------

TEZLA_TEST (rate_one_reproduces_the_sample_bit_for_bit)
{
    // At integer positions the Hermite polynomial collapses to the sample
    // itself, so playing at the recorded rate must reproduce the data exactly
    // -- quantised only by the int16 the file stores.
    SinePool pool (100, 4);

    SamplePlayer player;
    player.start (pool.data.data(), pool.begin, pool.end, pool.loopBegin, pool.loopEnd,
                  LoopMode::none);
    player.setRate (1.0);

    const auto out = render (player, static_cast<std::size_t> (pool.end - pool.begin));

    double worst = 0.0;

    for (std::size_t i = 0; i < out.size(); ++i)
    {
        const double expected = static_cast<double> (
            pool.data[pool.begin + static_cast<std::uint32_t> (i)]) / 32768.0;
        worst = std::max (worst, std::abs (out[i] - expected));
    }

    CHECK (worst == 0.0);

    // Delivering the final sample advanced the position past the end, so the
    // player is already finished -- eagerly, so an engine polling isFinished()
    // retires the voice without a wasted call. Further output is silence.
    CHECK (player.isFinished());
    CHECK (player.next() == 0.0);
}

TEZLA_TEST (a_looped_sine_is_the_sine_for_ever)
{
    // The loop spans whole cycles, so looped playback must equal the ideal
    // continuous sine to within the file's own quantisation -- across dozens
    // of seam crossings. A seam that steps would show here as a spray far
    // above one part in thirty thousand.
    //
    // Two lessons from this test's own first version are baked in:
    //
    //   * The material after the loop is deliberately NOT the sine's
    //     continuation -- in a real soundfont it is the release tail, and
    //     here it is corrupted outright. Whole sine cycles otherwise make an
    //     un-wrapped tap read bit-identical to a wrapped one, and removing
    //     the wrap entirely still passed.
    //   * The rate is fractional. At rate 1.0 every position is an integer,
    //     frac is 0, and the Hermite side taps carry zero weight -- the seam
    //     taps were never *used*. Only a fractional position weights them in.
    constexpr int period = 100;
    SinePool pool (period, 4);

    for (std::uint32_t i = pool.loopEnd; i < pool.end; ++i)
        pool.data[i] = 29000;

    constexpr double rate = 0.617;

    SamplePlayer player;
    player.start (pool.data.data(), pool.begin, pool.end, pool.loopBegin, pool.loopEnd,
                  LoopMode::continuous);
    player.setRate (rate);

    const auto out = render (player, 40000);

    // Skip the run-in: while the position is still in the pre-loop padding,
    // the left-hand taps legitimately clamp to the silence before the sample,
    // which the endless-sine formula below does not model.
    double worst = 0.0;

    for (std::size_t i = 24; i < out.size(); ++i)
    {
        const double ideal = (30000.0 / 32768.0)
                               * std::sin (2.0 * 3.141592653589793
                                             * (static_cast<double> (i) * rate - 8.0) / period);
        worst = std::max (worst, std::abs (out[i] - ideal));
    }

    // One int16 step of rounding plus the interpolator's own -100 dB error,
    // and nothing more. A missing seam wrap reads the corrupted tail instead
    // and lands four orders of magnitude above this.
    CHECK (worst < 1.5 / 32768.0);
    CHECK (! player.isFinished());
}

TEZLA_TEST (until_release_loops_and_then_plays_the_tail_out)
{
    SinePool pool (64, 4);

    SamplePlayer player;
    player.start (pool.data.data(), pool.begin, pool.end, pool.loopBegin, pool.loopEnd,
                  LoopMode::untilRelease);
    player.setRate (1.0);

    // Held: still going strong after many loop lengths.
    (void) render (player, 20000);
    CHECK (! player.isFinished());

    // Released: the loop opens and the tail plays out within the distance
    // from the loop to the end, plus one loop length of worst-case position.
    player.release();

    const auto tailBudget = static_cast<std::size_t> (pool.end - pool.loopBegin + 8);
    (void) render (player, tailBudget);

    CHECK (player.isFinished());
}

TEZLA_TEST (a_degenerate_loop_is_demoted_to_no_loop)
{
    // A zero-length loop fed to the wrap arithmetic is a modulo by zero; one
    // to three samples long is a loop the four-tap interpolator cannot see
    // across. start() demotes both to LoopMode::none, so the sample plays
    // once and ends -- rather than crashing or buzzing at Nyquist.
    SinePool pool (64, 2);

    SamplePlayer player;
    player.start (pool.data.data(), pool.begin, pool.end, pool.loopBegin,
                  pool.loopBegin,   // loopEnd == loopBegin: zero-length
                  LoopMode::continuous);
    player.setRate (1.0);

    (void) render (player, static_cast<std::size_t> (pool.end - pool.begin) + 4);
    CHECK (player.isFinished());

    player.start (pool.data.data(), pool.begin, pool.end, pool.loopBegin,
                  pool.loopBegin + 3,   // three samples: under the taps' reach
                  LoopMode::continuous);
    player.setRate (1.0);

    (void) render (player, static_cast<std::size_t> (pool.end - pool.begin) + 4);
    CHECK (player.isFinished());
}

TEZLA_TEST (nothing_outside_the_sample_is_ever_read)
{
    // The pool is poisoned with full-scale values outside the declared
    // bounds. Play across the end without a loop: the tail must interpolate
    // into silence, never into poison -- and the same at the very first
    // samples, whose left-hand taps sit before the start.
    SinePool pool (64, 2);

    SamplePlayer player;
    player.start (pool.data.data(), pool.begin, pool.end, pool.loopBegin, pool.loopEnd,
                  LoopMode::none);
    player.setRate (0.71);   // fractional, so the taps straddle every boundary

    const auto out = render (player, static_cast<std::size_t> (
        static_cast<double> (pool.end - pool.begin) / 0.71) + 8);

    CHECK (player.isFinished());

    for (const double sample : out)
        CHECK (std::abs (sample) < 30001.0 / 32768.0);
}

// ---------------------------------------------------------------------------
// The interpolation, measured
// ---------------------------------------------------------------------------

TEZLA_TEST (hermite_interpolation_images_are_measured_and_pinned)
{
    // Resampling a sampled sine creates images of its spectral replicas, and
    // the interpolator's job is to bury them. Measured here at the pitches a
    // sampler actually gets asked for: up and down an octave from the root,
    // and the worst practical case of a bright source (a quarter of its own
    // rate) shifted up a fifth.
    //
    // Measured on this implementation (4-point Hermite, 48 kHz, 16384-point
    // window, inharmonic energy in the audible band relative to the tone):
    //
    //     480 Hz source,  up a major third   -105.9 dB
    //     480 Hz source,  down a fifth       -110.9 dB
    //     480 Hz source,  up an octave       -101.4 dB
    //     6 kHz source,   up a fifth          -47.4 dB
    //     6 kHz source,   down a fourth       -43.8 dB
    //
    // The bright-source figures are the honest cost of Hermite and the case
    // for the root-key-nearest zone selection every soundfont uses: a sampler
    // playing material near a quarter of its own rate is fighting physics,
    // whichever way it is pitched. The bounds below pin each measurement with
    // ~5 dB of margin.
    struct Case
    {
        int period;
        double targetHz;
        double bound;
    };

    const Case cases[] = {
        { 100, 604.7, -100.0 },   // up ~ a major third from 480
        { 100, 320.5, -105.0 },   // down a fifth
        { 100, 957.0, -95.0 },    // up an octave
        { 8, 9000.0, -42.0 },     // 6 kHz source up a fifth: Hermite's true face
        { 8, 4497.0, -38.0 },     // 6 kHz source down a fourth
    };

    for (const auto& testCase : cases)
    {
        SinePool pool (testCase.period, 2048 / testCase.period + 4);

        const double sourceHz = kRate / testCase.period;
        const double binExact = measure::binExactFrequency (testCase.targetHz, kRate, kFft);
        const double rate = binExact / sourceHz;

        SamplePlayer player;
        player.start (pool.data.data(), pool.begin, pool.end, pool.loopBegin, pool.loopEnd,
                      LoopMode::continuous);
        player.setRate (rate);

        // Let the position wander through many seams before measuring.
        (void) render (player, 4096);

        const auto signal = render (player, kFft);
        const auto analysis = measure::analyseHarmonics (signal, kRate, binExact);

        CHECK (analysis.audibleAliasingDb < testCase.bound);
    }
}
