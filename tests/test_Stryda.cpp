// Copyright (c) 2026 The Tezla <thetezla@proton.me>
// Created by The Tezla -- https://github.com/wingit33/tezla.tech
// Music: https://soundcloud.com/thetezla | https://thetezla.bandcamp.com
// Built with development assistance from Claude (Anthropic).
// SPDX-License-Identifier: AGPL-3.0-only
// GNU AGPLv3 (see LICENSE), plus NOTICE.md's attribution term. Keep intact.

#include "TestFramework.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <limits>
#include <numbers>
#include <vector>

#include <tezla/dsp/Fft.hpp>
#include <tezla/dsp/FmOperator.hpp>
#include <tezla/dsp/PhaseShaper.hpp>

#include "StrydaEngine.hpp"

using namespace tezla::dsp;
using namespace tezla::stryda;

namespace
{
constexpr double kTwoPi = 2.0 * std::numbers::pi;

/// A voice parameter set with one carrier at full level and nothing else --
/// the neutral patch every bit-exactness claim is measured against.
[[nodiscard]] VoiceParameters singleCarrier()
{
    VoiceParameters parameters;

    for (auto& op : parameters.operators)
    {
        op.level = 0.0;
        op.attack = 0.0;
        op.hold = 0.0;
        op.decay = 10.0;
        op.sustain = 1.0;
        op.release = 0.1;
    }

    parameters.operators[0].level = 1.0;
    parameters.operators[0].ratio = 1.0;

    return parameters;
}

std::vector<double> renderEngine (StrydaEngine& engine, int frames, int blockSize)
{
    std::vector<double> left (static_cast<std::size_t> (frames), 0.0);
    std::vector<double> right (static_cast<std::size_t> (frames), 0.0);

    for (int i = 0; i < frames; i += blockSize)
    {
        const int run = std::min (blockSize, frames - i);
        engine.process (left.data() + i, right.data() + i, run);
    }

    return left;
}
} // namespace

// ---------------------------------------------------------------------------
// The operator
// ---------------------------------------------------------------------------

TEZLA_TEST (character_zero_is_a_sine_bit_for_bit)
{
    // CLAUDE.md section 7: a stage permanently in the path must be bit-exactly
    // its neutral setting, not almost. With no modulation and no Character, the
    // operator IS sin(2 pi phase) and the comparison is exact, not near.
    FmOperator op;
    op.prepare (48000.0);
    op.setFrequency (440.0);

    double phase = 0.0;
    const double increment = 440.0 / 48000.0;

    for (int i = 0; i < 48000; ++i)
    {
        const double produced = op.advance (0.0);
        const double expected = std::sin (kTwoPi * phase);

        CHECK (produced == expected);

        phase += increment;
        if (phase >= 1.0)
            phase -= 1.0;
    }
}

TEZLA_TEST (character_zero_matches_classic_phase_modulation_exactly)
{
    // With Character 0 the operator must reproduce sin(wc t + k sin(wm t))
    // to the bit -- the eighties operator, not an approximation of it.
    FmOperator carrier;
    FmOperator modulator;
    carrier.prepare (48000.0);
    modulator.prepare (48000.0);
    carrier.setFrequency (220.0);
    modulator.setFrequency (660.0);

    double carrierPhase = 0.0;
    double modulatorPhase = 0.0;
    constexpr double indexCycles = 0.75;

    double worst = 0.0;

    for (int i = 0; i < 20000; ++i)
    {
        const double modulatorOut = modulator.advance (0.0);
        const double produced = carrier.advance (indexCycles * modulatorOut);

        const double reference = std::sin (kTwoPi * (carrierPhase
                                                     + indexCycles * std::sin (kTwoPi * modulatorPhase)));
        worst = std::max (worst, std::abs (produced - reference));

        carrierPhase += 220.0 / 48000.0;
        if (carrierPhase >= 1.0)
            carrierPhase -= 1.0;

        modulatorPhase += 660.0 / 48000.0;
        if (modulatorPhase >= 1.0)
            modulatorPhase -= 1.0;
    }

    std::printf ("        [operator] worst difference from closed-form PM: %.3e\n", worst);
    CHECK (worst < 1.0e-12);
}

TEZLA_TEST (full_character_is_the_modfm_closed_form)
{
    // r = 1, s = 0: e^(k cos(wm t) - k) sin(wc t), the DAFx-08 bandlimited
    // pulse multiplied onto an unmodulated carrier.
    FmOperator carrier;
    FmOperator modulator;
    carrier.prepare (48000.0);
    modulator.prepare (48000.0);
    carrier.setFrequency (220.0);
    modulator.setFrequency (220.0);
    carrier.setCharacter (1.0);
    carrier.setTilt (0.0);

    constexpr double indexCycles = 0.5;
    const double k = kTwoPi * indexCycles;

    double carrierPhase = 0.0;
    double modulatorPhase = 0.0;
    double worst = 0.0;

    for (int i = 0; i < 20000; ++i)
    {
        (void) modulator.advance (0.0);
        const double produced = carrier.advance (indexCycles * modulator.getOutput(),
                                                 indexCycles * modulator.getQuadrature(),
                                                 indexCycles);

        const double reference = std::exp (k * std::cos (kTwoPi * modulatorPhase) - k)
                               * std::sin (kTwoPi * carrierPhase);

        worst = std::max (worst, std::abs (produced - reference));

        carrierPhase += 220.0 / 48000.0;
        if (carrierPhase >= 1.0)
            carrierPhase -= 1.0;

        modulatorPhase += 220.0 / 48000.0;
        if (modulatorPhase >= 1.0)
            modulatorPhase -= 1.0;
    }

    std::printf ("        [operator] worst difference from closed-form ModFM: %.3e\n", worst);
    CHECK (worst < 1.0e-12);
}

TEZLA_TEST (modfm_never_exceeds_full_scale_however_hard_it_is_driven)
{
    // The `- k` in the exponent is the normalisation. Without it the output is
    // e^k, which at index 4 cycles is e^25 -- and it would not look like a bug
    // until something downstream clipped.
    FmOperator carrier;
    FmOperator modulator;
    carrier.prepare (48000.0);
    modulator.prepare (48000.0);
    carrier.setCharacter (1.0);
    carrier.setTilt (0.0);

    double peak = 0.0;

    for (double indexCycles = 0.0; indexCycles <= 8.0; indexCycles += 0.05)
    {
        carrier.reset();
        modulator.reset();
        carrier.setFrequency (110.0);
        modulator.setFrequency (110.0 * 3.0);

        for (int i = 0; i < 4000; ++i)
        {
            (void) modulator.advance (0.0);
            const double value = carrier.advance (indexCycles * modulator.getOutput(),
                                                  indexCycles * modulator.getQuadrature(),
                                                  indexCycles);
            CHECK (std::isfinite (value));
            peak = std::max (peak, std::abs (value));
        }
    }

    std::printf ("        [operator] ModFM peak over index 0..8 cycles: %.6f\n", peak);
    CHECK (peak <= 1.0);
}

// ---------------------------------------------------------------------------
// The matrix
// ---------------------------------------------------------------------------

TEZLA_TEST (a_lower_numbered_modulator_arrives_one_sample_late)
{
    // The ordering rule, asserted numerically in both directions rather than
    // described in a comment. 5 -> 0 is instantaneous; 0 -> 5 is one sample
    // old, and the two must not produce the same output.
    const auto render = [] (int from, int to)
    {
        OperatorMatrix matrix;
        matrix.prepare (48000.0);
        matrix.setFrequency (from, 700.0);
        matrix.setFrequency (to, 300.0);
        matrix.setIndex (to, from, 1.0);
        matrix.setOutputLevel (to, 1.0);

        std::array<double, OperatorMatrix::kNumOperators> gains {};
        gains.fill (1.0);

        std::vector<double> out (2000, 0.0);
        for (std::size_t i = 0; i < out.size(); ++i)
        {
            double l = 0.0;
            double r = 0.0;
            matrix.process (gains.data(), 0.0, l, r);
            out[i] = l;
        }
        return out;
    };

    const auto instantaneous = render (5, 0);
    const auto delayed = render (0, 5);

    double difference = 0.0;
    for (std::size_t i = 0; i < instantaneous.size(); ++i)
        difference = std::max (difference, std::abs (instantaneous[i] - delayed[i]));

    std::printf ("        [matrix] 5->0 against 0->5, worst sample difference: %.6f\n", difference);

    // Not merely different: the delayed path is the instantaneous one shifted,
    // so at 700 Hz and 48 kHz the two disagree substantially.
    CHECK (difference > 0.01);
}

TEZLA_TEST (an_unmodulated_matrix_is_exactly_its_carriers)
{
    OperatorMatrix matrix;
    matrix.prepare (48000.0);
    matrix.setFrequency (0, 440.0);
    matrix.setOutputLevel (0, 1.0);

    std::array<double, OperatorMatrix::kNumOperators> gains {};
    gains.fill (1.0);

    double phase = 0.0;

    for (int i = 0; i < 10000; ++i)
    {
        double l = 0.0;
        double r = 0.0;
        matrix.process (gains.data(), 0.0, l, r);

        // A centred operator is panned by cos(pi/4) and scaled back by sqrt(2),
        // so a lone carrier arrives at full scale. Not bit-exact -- the pan is
        // two transcendentals -- but it must land on the sine to well within a
        // part in 10^12, and left must equal right.
        const double expected = std::sin (kTwoPi * phase);
        CHECK_NEAR (l, expected, 1.0e-12);
        CHECK_NEAR (r, expected, 1.0e-12);

        phase += 440.0 / 48000.0;
        if (phase >= 1.0)
            phase -= 1.0;
    }
}

// ---------------------------------------------------------------------------
// The engine
// ---------------------------------------------------------------------------

TEZLA_TEST (the_engine_renders_the_same_audio_whatever_the_block_size)
{
    // CLAUDE.md section 7: control changes land on a sample-counted chunk
    // boundary, so the host's buffer size must not reach the output at all.
    const auto render = [] (int blockSize)
    {
        StrydaEngine engine;
        engine.prepare (48000.0, 512);
        engine.setParameters (singleCarrier());
        engine.noteOn (69, 1.0);
        return renderEngine (engine, 8192, blockSize);
    };

    const auto small = render (64);
    const auto large = render (512);
    const auto odd = render (97);

    for (std::size_t i = 0; i < small.size(); ++i)
    {
        CHECK (small[i] == large[i]);
        CHECK (small[i] == odd[i]);
    }
}

TEZLA_TEST (a_voice_retires_exactly_and_the_count_says_so)
{
    // Assert **activity**, not silence: the Sonitus zombie ran at zero level
    // for seconds while every silence-based test passed and the CPU meter
    // pinned at 100 %.
    StrydaEngine engine;
    engine.prepare (48000.0, 512);

    auto parameters = singleCarrier();
    for (auto& op : parameters.operators)
    {
        op.decay = 0.05;
        op.sustain = 0.0;
        op.release = 0.05;
    }
    engine.setParameters (parameters);

    engine.noteOn (60, 1.0);
    CHECK (engine.getActiveVoiceCount() == 1);

    engine.noteOff (60);
    renderEngine (engine, 48000, 512);

    std::printf ("        [engine] active voices one second after note-off: %d\n",
                 engine.getActiveVoiceCount());
    CHECK (engine.getActiveVoiceCount() == 0);
}

TEZLA_TEST (the_index_cap_is_exactly_inert_when_it_is_off_and_when_it_is_not_binding)
{
    StrydaEngine engine;
    engine.prepare (48000.0, 512);

    auto parameters = singleCarrier();
    parameters.operators[1].ratio = 2.0;
    parameters.indices[0][1] = 1.0;

    // Off.
    parameters.indexCap = 0.0;
    engine.setParameters (parameters);
    engine.noteOn (48, 1.0);
    renderEngine (engine, 512, 512);

    const auto withoutCap = renderEngine (engine, 4096, 512);

    // On, but nowhere near binding at a low note and a modest index.
    engine.reset();
    parameters.indexCap = 1.0;
    engine.setParameters (parameters);
    engine.noteOn (48, 1.0);
    renderEngine (engine, 512, 512);

    const auto withCap = renderEngine (engine, 4096, 512);

    for (std::size_t i = 0; i < withoutCap.size(); ++i)
        CHECK (withoutCap[i] == withCap[i]);
}

TEZLA_TEST (the_index_cap_bites_on_a_screaming_patch_and_lets_go_lower_down)
{
    StrydaEngine engine;
    engine.prepare (48000.0, 512);

    auto parameters = singleCarrier();
    parameters.operators[1].ratio = 11.0;
    parameters.indices[0][1] = 6.0;
    parameters.indexCap = 1.0;
    engine.setParameters (parameters);

    engine.noteOn (96, 1.0);          // C7, four octaves up
    renderEngine (engine, 256, 256);
    const double high = engine.getFactor() > 0 ? 1.0 : 1.0;
    (void) high;

    StrydaVoice probe;
    probe.prepare (192000.0);
    probe.noteOn (96, 2093.0, 1.0);
    const double capHigh = probe.capScaleFor (parameters, 192000.0);

    probe.noteOn (36, 65.4, 1.0);
    const double capLow = probe.capScaleFor (parameters, 192000.0);

    std::printf ("        [cap] index scale at C7 %.4f, at C2 %.4f\n", capHigh, capLow);

    CHECK (capHigh < 1.0);
    CHECK (capLow == 1.0);
}

TEZLA_TEST (six_operators_at_eight_voices_fit_the_budget)
{
    // Two figures, because the two ends of Character cost very different
    // amounts and quoting only one of them would be misleading.
    //
    // A **classic FM** operator is one `std::sin` per sample. A **ModFM** one
    // is three -- sin, cos for the quadrature the next operator's exponential
    // reads, and the exponential itself. At six operators, eight voices and a
    // x4 internal rate that is 9.2 million operator-samples a second, so the
    // difference is the whole cost of the instrument.
    const auto measure = [] (double character, double& outElapsed, int& outFactor)
    {
        StrydaEngine engine;
        engine.prepare (48000.0, 512);
        engine.setPolyphony (8);

        VoiceParameters parameters;
        for (int op = 0; op < StrydaVoice::kNumOperators; ++op)
        {
            auto& settings = parameters.operators[static_cast<std::size_t> (op)];
            settings.ratio = 1.0 + static_cast<double> (op);
            settings.level = op == 0 ? 1.0 : 0.0;
            settings.character = character;
            settings.decay = 4.0;
            settings.sustain = 0.8;
            settings.attack = 0.005;
        }
        parameters.operators[0].feedback = 0.2;

        for (int op = 0; op + 1 < StrydaVoice::kNumOperators; ++op)
            parameters.indices[static_cast<std::size_t> (op)][static_cast<std::size_t> (op + 1)] = 1.5;

        engine.setParameters (parameters);

        for (int v = 0; v < 8; ++v)
            engine.noteOn (48 + v * 3, 1.0);

        const auto start = std::chrono::steady_clock::now();
        renderEngine (engine, 48000 * 2, 512);
        outElapsed = std::chrono::duration<double> (std::chrono::steady_clock::now() - start).count();
        outFactor = engine.getFactor();
    };

    double classicSeconds = 0.0;
    double modfmSeconds = 0.0;
    int factor = 0;

    measure (0.0, classicSeconds, factor);
    measure (0.5, modfmSeconds, factor);

    std::printf ("        [cpu] 8 voices, 6 operators, x%d, 2 s of audio:\n", factor);
    std::printf ("        [cpu]   classic FM (Character 0): %.3f s  (%.1f %% of a core)\n",
                 classicSeconds, 100.0 * classicSeconds / 2.0);
    std::printf ("        [cpu]   half ModFM (Character 0.5): %.3f s  (%.1f %% of a core)\n",
                 modfmSeconds, 100.0 * modfmSeconds / 2.0);
    std::printf ("        [cpu]   the exponential and its quadrature cost %.2fx\n",
                 classicSeconds > 0.0 ? modfmSeconds / classicSeconds : 0.0);

    // **Classic FM measures slower than ModFM, repeatably**, which is the
    // opposite of what an extra exponential and an extra cosine predict. Over
    // ten interleaved runs: Character 0 at 47-57 % of a core, Character 0.5 at
    // 41-50 %. The likeliest cause is argument reduction -- at full tilt the
    // phase handed to `std::sin` reaches tens of radians, and at half tilt it
    // is halved -- but wrapping the argument with `std::floor` first was tried
    // and made *both* figures worse, so the cause is recorded as unconfirmed
    // rather than asserted. It is not worth more container time: this machine
    // is shared and noisy, and the rig is the measurement that decides.
    //
    // The budgets therefore sit well above the observed range: tight enough
    // that doubling the per-sample work fails, loose enough that a busy
    // container does not.
    CHECK_CPU_BUDGET (classicSeconds, 1.60, "8 Stryda voices, 6 classic FM operators");
    CHECK_CPU_BUDGET (modfmSeconds, 1.60, "8 Stryda voices, 6 operators at half ModFM");
}

TEZLA_TEST (the_index_cap_costs_almost_nothing_to_keep_engaged)
{
    // ---------------------------------------------------------------------
    // **The test that was missing when the cap froze FL Studio**
    // ---------------------------------------------------------------------
    //
    // The budget test above runs with `indexCap` at its default of 0, so the
    // cap never resolved and its cost was never on any scale. On the rig a
    // Neuro Growl patch with the cap at Soft pinned the CPU meter past 100 %
    // and locked the DAW the moment a knob moved (reported 2026-09-04). Three
    // things had gone wrong together and each is now guarded:
    //
    //   1. `fm::significantOrder` evaluated a Bessel integral per order, which
    //      is quadratic -- 5.7 ms at an index of 64 radians.
    //   2. `fm::feedbackOrder` walked 512 orders whenever beta was **below**
    //      1 radian, so the cost hid at low feedback and, worse, appeared
    //      halfway through every bisection because bisecting scales beta down.
    //      30-55 ms per call, twelve calls per prediction, thirty-three
    //      predictions per resolution: two to three seconds.
    //   3. The engine resolved all of it per voice per 32-sample chunk.
    //
    // This asserts the whole of that: the same patch as the budget test, with
    // feedback below the boundary and the cap hard on, must cost no more than
    // a modest fraction over the uncapped figure. It is deliberately a *ratio*
    // rather than an absolute, so a busy container moves both numbers together.
    const auto measure = [] (double capAmount, double& outElapsed)
    {
        StrydaEngine engine;
        engine.prepare (48000.0, 512);
        engine.setPolyphony (8);

        VoiceParameters parameters;
        parameters.indexCap = capAmount;

        for (int op = 0; op < StrydaVoice::kNumOperators; ++op)
        {
            auto& settings = parameters.operators[static_cast<std::size_t> (op)];
            settings.ratio = 1.0 + static_cast<double> (op);
            settings.level = op == 0 ? 1.0 : 0.0;
            settings.decay = 4.0;
            settings.sustain = 0.8;
            settings.attack = 0.005;
        }

        // 0.1 cycles is 0.63 radians -- the side of the boundary that used to
        // take the slow path, which is the case the user actually hit.
        parameters.operators[0].feedback = 0.1;

        for (int op = 0; op + 1 < StrydaVoice::kNumOperators; ++op)
            parameters.indices[static_cast<std::size_t> (op)][static_cast<std::size_t> (op + 1)] = 1.5;

        engine.setParameters (parameters);

        for (int v = 0; v < 8; ++v)
            engine.noteOn (48 + v * 3, 1.0);

        const auto start = std::chrono::steady_clock::now();
        renderEngine (engine, 48000 * 2, 512);
        outElapsed = std::chrono::duration<double> (std::chrono::steady_clock::now() - start).count();
    };

    double offSeconds = 0.0;
    double hardSeconds = 0.0;

    measure (0.0, offSeconds);
    measure (1.0, hardSeconds);

    const double overhead = offSeconds > 0.0 ? hardSeconds / offSeconds : 0.0;

    std::printf ("        [cap] 8 voices, 2 s of audio: cap off %.3f s, cap hard %.3f s (%.2fx)\n",
                 offSeconds, hardSeconds, overhead);

    // The bound was 1.5 first and caught nothing: resolving the cap every chunk
    // again -- the rig behaviour exactly -- measured 1.30 and sailed through.
    // Three baseline runs give 0.84 / 0.85 / 0.87, so 1.10 separates them
    // cleanly with a quarter of headroom over the noisiest of the three.
    //
    // Below 1 rather than a little above it, and that is not noise: the cap
    // scales the indices *down*, so a capped voice does slightly less work than
    // an uncapped one. The cost of deciding is smaller than the work it saves.
    CHECK (overhead < 1.10);

    CHECK_CPU_BUDGET (hardSeconds, 1.60, "8 Stryda voices with the index cap hard on");
}

TEZLA_TEST (the_cap_resolves_on_its_own_sub_grid_and_not_per_chunk)
{
    // ---------------------------------------------------------------------
    // **The clock cannot assert this; the count can**
    // ---------------------------------------------------------------------
    //
    // The wall-clock test above is a coarse net: on a shared container the two
    // renders it compares move together by tens of per cent, so the threshold
    // has to be loose enough to miss a 46 % regression -- which is exactly the
    // size of the one that reaching for `refreshIndexCap` every chunk would
    // reintroduce. So assert the thing itself. This counts the expensive
    // operation and is exact on any machine at any load.
    StrydaEngine engine;
    engine.prepare (48000.0, 512);
    engine.setPolyphony (4);

    VoiceParameters parameters = singleCarrier();
    parameters.indexCap = 1.0;
    engine.setParameters (parameters);

    engine.noteOn (60, 1.0);
    engine.noteOn (64, 1.0);

    const long long afterNotes = engine.getCapResolutionCount();
    CHECK (afterNotes == 2);   // one each, immediately, so neither starts uncapped

    constexpr int kHostFrames = 4800;
    renderEngine (engine, kHostFrames, 512);

    const long long during = engine.getCapResolutionCount() - afterNotes;

    const int internalSamples = kHostFrames * engine.getFactor();
    const int chunks = internalSamples / StrydaEngine::kControlChunk;
    const long long expected = 2LL * (chunks / StrydaEngine::kCapChunks);

    std::printf ("        [cap] %d internal samples, %d chunks: %lld resolutions "
                 "(one per voice every %d chunks would be %lld; every chunk, %lld)\n",
                 internalSamples, chunks, during, StrydaEngine::kCapChunks, expected,
                 2LL * chunks);

    // Within one sub-grid step of the ideal -- the grid is anchored to the
    // stream, so where the render happens to start and stop can add or drop a
    // single boundary.
    CHECK (std::llabs (during - expected) <= 2);

    // And emphatically not the per-chunk figure that froze the rig.
    CHECK (during < chunks);
}

TEZLA_TEST (the_cap_sub_grid_is_anchored_to_the_stream_not_the_block)
{
    // The same property `getChunkCountdown` exists to prove, one level down:
    // if the cap grid were counted per callback rather than per control chunk,
    // a host at 64 samples would resolve it eight times as often as one at 512
    // and the two would drift apart in what they cap to. Same stream, three
    // buffer sizes, same count.
    const auto count = [] (int blockSize)
    {
        StrydaEngine engine;
        engine.prepare (48000.0, 512);
        engine.setPolyphony (4);

        VoiceParameters parameters = singleCarrier();
        parameters.indexCap = 1.0;
        engine.setParameters (parameters);

        engine.noteOn (60, 1.0);
        renderEngine (engine, 8192, blockSize);

        return engine.getCapResolutionCount();
    };

    const long long small = count (64);
    const long long large = count (512);
    const long long odd = count (97);

    std::printf ("        [cap] resolutions at 64/512/97-sample blocks: %lld / %lld / %lld\n",
                 small, large, odd);

    CHECK (small == large);
    CHECK (odd == large);
}

TEZLA_TEST (an_idle_instrument_costs_almost_nothing)
{
    StrydaEngine engine;
    engine.prepare (48000.0, 512);
    engine.setParameters (singleCarrier());

    const auto start = std::chrono::steady_clock::now();
    renderEngine (engine, 48000 * 4, 512);
    const auto elapsed = std::chrono::duration<double> (std::chrono::steady_clock::now() - start).count();

    std::printf ("        [cpu] idle, 4 s of audio in %.4f s (%.3f %% of a core)\n",
                 elapsed, 100.0 * elapsed / 4.0);

    CHECK_CPU_BUDGET (elapsed, 0.08, "idle Stryda");
}

// ---------------------------------------------------------------------------
// The three properties whose first tests turned out to be decorations
// ---------------------------------------------------------------------------

TEZLA_TEST (a_classic_operator_ignores_the_modfm_inputs_entirely)
{
    // The first version of the Character-0 test asserted only the sine
    // identity, and the break-check that removed the `character_ > 0` branch
    // stayed green -- because `std::exp (0.0 * anything)` is 1.0 whenever
    // "anything" is finite, so the branch made no numerical difference on the
    // values that test used.
    //
    // It does make one on the values that matter: `0.0 * infinity` is NaN, and
    // `std::exp (NaN)` is NaN. A carrier at Character 0 whose modulators have
    // driven the normalisation somewhere absurd must still be a clean sine, and
    // without the branch it is silence-shaped garbage.
    FmOperator op;
    op.prepare (48000.0);
    op.setFrequency (440.0);

    double phase = 0.0;
    const double increment = 440.0 / 48000.0;
    const double huge = std::numeric_limits<double>::infinity();

    for (int i = 0; i < 4800; ++i)
    {
        const double produced = op.advance (0.0, huge, huge);
        const double expected = std::sin (kTwoPi * phase);

        CHECK (std::isfinite (produced));
        CHECK (produced == expected);

        phase += increment;
        if (phase >= 1.0)
            phase -= 1.0;
    }
}

TEZLA_TEST (the_reverse_path_is_exactly_one_sample_old)
{
    // The first version of this compared 5->0 against 0->5 and asserted only
    // that they differ. The break-check that made both directions read the
    // current sample stayed green -- because `outputs_` already holds the
    // previous value for an operator that has not run yet, so removing the
    // choice changed nothing. The property worth asserting is the exact one:
    // a lower-numbered modulator's contribution is the same signal, delayed by
    // exactly one sample, and nothing else.
    const auto renderWithExplicitDelay = [] (int delaySamples)
    {
        FmOperator carrier;
        FmOperator modulator;
        carrier.prepare (48000.0);
        modulator.prepare (48000.0);
        carrier.setFrequency (300.0);
        modulator.setFrequency (700.0);

        double history = 0.0;
        std::vector<double> out (1500, 0.0);

        for (std::size_t i = 0; i < out.size(); ++i)
        {
            const double m = modulator.advance (0.0);
            const double feed = delaySamples > 0 ? history : m;
            out[i] = carrier.advance (1.0 * feed);
            history = m;
        }

        return out;
    };

    const auto matrixRender = [] (int from, int to)
    {
        OperatorMatrix matrix;
        matrix.prepare (48000.0);
        matrix.setFrequency (from, 700.0);
        matrix.setFrequency (to, 300.0);
        matrix.setIndex (to, from, 1.0);
        matrix.setOutputLevel (to, 1.0);

        std::array<double, OperatorMatrix::kNumOperators> gains {};
        gains.fill (1.0);

        std::vector<double> out (1500, 0.0);
        for (std::size_t i = 0; i < out.size(); ++i)
        {
            double l = 0.0;
            double r = 0.0;
            matrix.process (gains.data(), 0.0, l, r);
            out[i] = l;
        }
        return out;
    };

    const auto instantaneous = matrixRender (5, 0);
    const auto delayed = matrixRender (0, 5);
    const auto referenceNow = renderWithExplicitDelay (0);
    const auto referenceLate = renderWithExplicitDelay (1);

    double worstNow = 0.0;
    double worstLate = 0.0;

    for (std::size_t i = 0; i < instantaneous.size(); ++i)
    {
        worstNow = std::max (worstNow, std::abs (instantaneous[i] - referenceNow[i]));
        worstLate = std::max (worstLate, std::abs (delayed[i] - referenceLate[i]));
    }

    std::printf ("        [matrix] 5->0 against the instantaneous reference: %.3e\n", worstNow);
    std::printf ("        [matrix] 0->5 against the one-sample-late reference: %.3e\n", worstLate);

    CHECK (worstNow < 1.0e-12);
    CHECK (worstLate < 1.0e-12);
}

TEZLA_TEST (the_control_chunk_grid_is_anchored_to_the_stream_not_the_block)
{
    // The first version rendered a fixed patch at two block sizes and compared
    // the audio, and the break-check that cut the loop at the block boundary
    // stayed green -- because with nothing changing between chunks the two cuts
    // produce the same samples. The mechanism only shows once a parameter
    // actually moves, so assert the mechanism directly: after the same number
    // of samples, the chunk countdown must be the same whatever route was taken
    // to get there.
    const auto countdownAfter = [] (int blockSize, int frames)
    {
        StrydaEngine engine;
        engine.prepare (48000.0, 512);
        engine.setParameters (singleCarrier());
        engine.noteOn (60, 1.0);
        renderEngine (engine, frames, blockSize);
        return engine.getChunkCountdown();
    };

    constexpr int frames = 2048;

    const int viaSmall = countdownAfter (64, frames);
    const int viaLarge = countdownAfter (512, frames);
    const int viaOdd = countdownAfter (97, frames);

    std::printf ("        [engine] chunk countdown after %d samples at 64 / 512 / 97: %d / %d / %d\n",
                 frames, viaSmall, viaLarge, viaOdd);

    CHECK (viaSmall == viaLarge);
    CHECK (viaSmall == viaOdd);
}

// ---------------------------------------------------------------------------
// Phase distortion
// ---------------------------------------------------------------------------

TEZLA_TEST (the_phase_shaper_is_the_identity_at_zero_bit_for_bit)
{
    PhaseShaper shaper;
    shaper.setAmount (0.0);

    for (int i = 0; i < 100000; ++i)
    {
        const double phase = static_cast<double> (i) / 100000.0;
        CHECK (shaper.map (phase) == phase);
    }

    // And with the branch removed it would still have to hold, because the
    // arithmetic is exact: 0.5 / 0.5 is 1.0 in binary, both sides.
    CHECK (shaper.getKnee() == 0.5);
}

TEZLA_TEST (the_phase_shaper_is_monotonic_and_stays_in_range)
{
    // A phase map that goes backwards plays the waveform backwards for part of
    // the cycle, which is a click, not a timbre.
    for (double amount = 0.0; amount <= 1.0; amount += 0.01)
    {
        PhaseShaper shaper;
        shaper.setAmount (amount);

        double previous = -1.0;

        for (int i = 0; i <= 10000; ++i)
        {
            const double mapped = shaper.map (static_cast<double> (i) / 10000.0);

            CHECK (mapped >= previous);
            CHECK (mapped >= 0.0);
            CHECK (mapped <= 1.0);

            previous = mapped;
        }
    }
}

TEZLA_TEST (the_phase_shaper_adds_harmonics_monotonically)
{
    // The control has to *do* something, and the something has to be ordered:
    // more fold, more upper harmonic energy, no reversals.
    std::printf ("        [fold] amount   harmonic energy above the 4th (dBc)\n");

    // Below the -300 dB clamp the measurement floors at, so amount 0 -- which is
    // exactly a sine and therefore has no upper harmonics at all -- counts as a
    // genuine step up rather than as a tie against the floor.
    double previous = -400.0;

    for (double amount : { 0.0, 0.25, 0.5, 0.75, 1.0 })
    {
        PhaseShaper shaper;
        shaper.setAmount (amount);

        constexpr std::size_t kFrames = 1u << 14;
        constexpr double kRate = 48000.0;
        const double f0 = std::round (200.0 / (kRate / kFrames)) * (kRate / kFrames);

        std::vector<double> rendered (kFrames, 0.0);
        double phase = 0.0;

        for (std::size_t i = 0; i < kFrames; ++i)
        {
            rendered[i] = std::sin (kTwoPi * shaper.map (phase));
            phase += f0 / kRate;
            if (phase >= 1.0)
                phase -= 1.0;
        }

        const auto spectrum = fftOfReal (rendered);
        const double binWidth = kRate / static_cast<double> (kFrames);
        const auto fundamentalBin = static_cast<std::size_t> (std::llround (f0 / binWidth));

        double fundamental = std::norm (spectrum[fundamentalBin]);
        double upper = 0.0;

        for (int h = 5; h < 60; ++h)
        {
            const std::size_t bin = fundamentalBin * static_cast<std::size_t> (h);
            if (bin >= kFrames / 2)
                break;
            upper += std::norm (spectrum[bin]);
        }

        const double db = 10.0 * std::log10 (std::max (upper / fundamental, 1.0e-30));
        std::printf ("        [fold] %6.2f   %+8.1f\n", amount, db);

        CHECK (db > previous);
        previous = db;
    }
}

// ---------------------------------------------------------------------------
// The formant operator
// ---------------------------------------------------------------------------

TEZLA_TEST (the_formant_operator_puts_its_peak_where_it_was_asked_to)
{
    // The claim the two carriers exist for: the resonance sits at the requested
    // frequency **whatever the note**, continuously, rather than jumping from
    // harmonic to harmonic. A single carrier could only land on a multiple of
    // the fundamental, so this is the test that says the crossfade works.
    constexpr double kRate = 96000.0;
    constexpr std::size_t kFrames = 1u << 15;
    const double binWidth = kRate / static_cast<double> (kFrames);

    std::printf ("        [formant] note(Hz)  asked      found    error\n");

    double worstCents = 0.0;

    // **The formant must sit at least eight harmonics up, and the reason is
    // worth writing down** because the vowel lane will need it.
    //
    // The exponential generates sidebands at +/- m harmonics around each
    // carrier, with amplitudes I_m(k)/e^k -- symmetric in m, so the centroid
    // of the pair should be exactly `n + a` harmonics, which is exactly the
    // requested formant. It is, until the skirt reaches below harmonic zero:
    // those sidebands **fold through DC and add to their positive twins**
    // (Chowning), which breaks the symmetry and drags the centre upward. At a
    // depth of 0.5 cycles the skirt is roughly six harmonics wide, so the
    // formant needs about eight harmonics of room.
    //
    // Measured, with the formant at 1200 Hz: 0 cents at 55 Hz (n = 21.8),
    // +11 at 110 (n = 10.9), +52 at 146.8 (n = 8.2), +105 at 220 (n = 5.5).
    // It is a property of the technique, not of this implementation, and it is
    // why a vowel stops sounding like a vowel on a very high note.
    for (double f0 : { 55.0, 82.4, 110.0, 146.8, 220.0 })
    {
        for (double formant : { 700.0, 1200.0, 2400.0 })
        {
            if (formant / f0 < 8.0)
                continue;

            FmOperator op;
            op.prepare (kRate);
            op.setFrequency (std::round (f0 / binWidth) * binWidth);
            op.setMode (FmOperator::Mode::formant);
            op.setFormantHz (formant);
            op.setFormantDepth (0.5);

            std::vector<double> rendered (kFrames, 0.0);
            for (std::size_t i = 0; i < kFrames; ++i)
                rendered[i] = op.advance (0.0);

            const auto spectrum = fftOfReal (rendered);

            // **The strongest bin is the wrong instrument here, and it took a
            // failing test to see it.** With two carriers crossfaded by the
            // fractional part, the formant centre is their weighted *centroid*,
            // not whichever of them happens to be louder. Measured by peak bin,
            // a 1200 Hz formant on a 220 Hz note reads 1100 Hz -- 153 cents
            // flat -- because the lower carrier carries 0.545 of the blend and
            // the peak simply snaps to it. That is the measurement snapping,
            // not the formant moving.
            //
            // The magnitude-weighted centroid is what the design actually
            // claims, and it is what a listener hears as the resonance.
            // Windowed to an octave either side of where the formant was
            // asked for. Over the whole spectrum the exponential's own skirt
            // drags the centroid -- it is wide in absolute Hz and the spectrum
            // is not symmetric in log frequency -- and at 220 Hz with a formant
            // at 700 that reads 312 cents sharp. The skirt is real and audible;
            // it is just not the answer to "where is the resonance".
            double weighted = 0.0;
            double total = 0.0;

            const auto low = static_cast<std::size_t> (0.5 * formant / binWidth);
            const auto high = std::min (static_cast<std::size_t> (2.0 * formant / binWidth),
                                        kFrames / 2 - 1);

            for (std::size_t k = low; k <= high; ++k)
            {
                const double magnitude = std::abs (spectrum[k]);
                weighted += magnitude * static_cast<double> (k) * binWidth;
                total += magnitude;
            }

            const double found = total > 0.0 ? weighted / total : 0.0;
            const double cents = 1200.0 * std::log2 (found / formant);
            worstCents = std::max (worstCents, std::abs (cents));

            if (formant == 1200.0)
                std::printf ("        [formant] %8.1f  %6.0f Hz  %6.0f Hz  %+6.0f cents\n",
                             f0, formant, found, cents);
        }
    }

    std::printf ("        [formant] worst placement error over 15 combinations: %.0f cents\n",
                 worstCents);

    // A quarter of a semitone at eight harmonics of room, which is the edge of
    // the condition; the trend in the printout is the useful part -- 0 cents at
    // n = 21.8, +3 at n = 10.9, +20 at n = 8.2. Give a formant ten harmonics
    // and it is exact for any purpose.
    CHECK (worstCents < 25.0);
}

TEZLA_TEST (the_formant_operator_stays_bounded_and_harmonic)
{
    // The exponential is normalised the same way ModFM is, so however wide the
    // formant is asked to be, the output stays inside full scale.
    double peak = 0.0;

    for (double depth = 0.0; depth <= 16.0; depth += 0.25)
    {
        FmOperator op;
        op.prepare (48000.0);
        op.setFrequency (110.0);
        op.setMode (FmOperator::Mode::formant);
        op.setFormantHz (1500.0);
        op.setFormantDepth (depth);

        for (int i = 0; i < 4000; ++i)
        {
            const double value = op.advance (0.0);
            CHECK (std::isfinite (value));
            peak = std::max (peak, std::abs (value));
        }
    }

    std::printf ("        [formant] peak over depth 0..16: %.6f\n", peak);
    CHECK (peak <= 1.0);
}

// ---------------------------------------------------------------------------
// Key scaling and velocity
// ---------------------------------------------------------------------------

TEZLA_TEST (the_break_point_is_inert_while_both_depths_are_flat)
{
    // What this actually proves, stated precisely rather than generously: that
    // **moving the break point changes nothing while both depths are zero**.
    // That is a real property -- a patch with the break somewhere odd and no
    // depth must sound identical to one with the default break -- and it is
    // the one a user would notice breaking.
    //
    // It is deliberately not claimed as a test of the zero-depth guards
    // themselves. `pow (2, 0 * x)` is exactly 1.0 and `(1 - 0) + 0 * v` is
    // exactly 1.0, so those guards are speed, not correctness, and a
    // break-check that removes them stays green. The Character branch is the
    // one case in this file where such a guard *is* load-bearing, and that test
    // drives the inputs to infinity to prove it.
    const auto render = [] (bool withScalingControlsPresent)
    {
        StrydaEngine engine;
        engine.prepare (48000.0, 512);

        auto parameters = singleCarrier();
        parameters.operators[1].ratio = 2.0;
        parameters.indices[0][1] = 1.0;

        if (withScalingControlsPresent)
        {
            // Present, and set to neutral: break point somewhere far from the
            // note, both depths flat, both velocity amounts zero.
            for (auto& op : parameters.operators)
            {
                op.keyBreak = 24.0;
                op.keyLeft = 0.0;
                op.keyRight = 0.0;
                op.velLevel = 0.0;
                op.velIndex = 0.0;
            }
        }

        engine.setParameters (parameters);
        engine.noteOn (72, 0.4);          // a note far above the break, quiet
        return renderEngine (engine, 8192, 512);
    };

    const auto without = render (false);
    const auto with = render (true);

    for (std::size_t i = 0; i < without.size(); ++i)
        CHECK (without[i] == with[i]);
}

TEZLA_TEST (key_scaling_leans_the_way_it_is_pointed)
{
    // And it must actually do something when it is not neutral, in the stated
    // direction: positive above the break is louder as you play up.
    const auto peakAt = [] (int note, double right)
    {
        StrydaEngine engine;
        engine.prepare (48000.0, 512);

        auto parameters = singleCarrier();
        parameters.operators[0].keyBreak = 60.0;
        parameters.operators[0].keyRight = right;
        engine.setParameters (parameters);
        engine.noteOn (note, 1.0);

        const auto rendered = renderEngine (engine, 8192, 512);

        double peak = 0.0;
        for (double value : rendered)
            peak = std::max (peak, std::abs (value));
        return peak;
    };

    const double flat = peakAt (84, 0.0);
    const double up = peakAt (84, 0.5);
    const double down = peakAt (84, -0.5);

    std::printf ("        [keyscale] two octaves above the break, depth -0.5 / 0 / +0.5: "
                 "%.4f / %.4f / %.4f\n", down, flat, up);

    // Two octaves up at depth 0.5 is 2^(0.5 * 2) = exactly 2x, clipped by
    // nothing here, and 0.5 the other way.
    CHECK_NEAR (up / flat, 2.0, 0.02);
    CHECK_NEAR (down / flat, 0.5, 0.02);
}

// ---------------------------------------------------------------------------
// F5 groundwork: the filter, the sub lane and the unison offsets
// ---------------------------------------------------------------------------
//
// The DSP for phase 5 sits in `StrydaVoice` ahead of the parameters that will
// drive it, so every one of its defaults has to be **exactly** neutral -- not
// nearly. Until F5's JUCE layer lands this is the only thing standing between
// a patch and a silently different sound, and CLAUDE.md section 7 is explicit
// that "almost identity" means every existing project changes on update.

TEZLA_TEST (the_filter_is_skipped_bit_exactly_above_its_bypass_corner)
{
    // ---------------------------------------------------------------------
    // **The first version of this test was a decoration, and here is why**
    // ---------------------------------------------------------------------
    //
    // It rendered a reference at the defaults and compared other spellings of
    // "neutral" against it. Forcing the filter to run unconditionally -- the
    // exact regression it was meant to catch -- made it run for the reference
    // too, so all four renders still agreed and the test passed. Found by
    // breaking it (CLAUDE.md section 10).
    //
    // What actually has teeth is the *boundary*. Every cutoff at or above
    // `kFilterBypassHz` takes the skip, so they must all be bit-identical to
    // each other however far apart they are set; if the skip were removed they
    // would each get their own coefficients and diverge. And a cutoff just
    // below the corner must differ, or the filter is not in the path at all.
    const auto render = [] (double cutoffHz)
    {
        StrydaEngine engine;
        engine.prepare (48000.0, 512);
        engine.setPolyphony (1);

        VoiceParameters parameters = singleCarrier();
        parameters.extras.cutoffHz = cutoffHz;
        engine.setParameters (parameters);
        engine.noteOn (60, 1.0);

        return renderEngine (engine, 4096, 512);
    };

    const auto reference = render (20000.0);

    double energy = 0.0;
    for (const double sample : reference)
        energy += sample * sample;

    // Worthless against silence, so prove there is a signal to compare first.
    std::printf ("        [f5] reference energy %.4f over %zu samples\n",
                 energy, reference.size());
    CHECK (energy > 1.0);

    for (double cutoffHz : { 19000.0, 24000.0, 40000.0 })
    {
        const auto candidate = render (cutoffHz);

        std::size_t differing = 0;
        for (std::size_t i = 0; i < candidate.size(); ++i)
            if (! (candidate[i] == reference[i]))
                ++differing;

        std::printf ("        [f5] cutoff %.0f Hz vs 20 kHz: %zu of %zu samples differ\n",
                     cutoffHz, differing, candidate.size());

        CHECK (differing == 0);
    }

    // And the filter is really there when it is asked for.
    const auto engaged = render (18000.0);

    std::size_t engagedDiffering = 0;
    for (std::size_t i = 0; i < engaged.size(); ++i)
        if (! (engaged[i] == reference[i]))
            ++engagedDiffering;

    std::printf ("        [f5] cutoff 18 kHz (below the corner): %zu samples differ\n",
                 engagedDiffering);

    CHECK (engagedDiffering > 0);
}

TEZLA_TEST (the_sub_lane_is_silent_at_zero_level_whatever_else_it_is_set_to)
{
    // The sub's `isExactlyZero` guard saves work rather than changing the
    // answer -- multiplying by a level of zero would give zero anyway -- so
    // what is worth asserting is that no other sub setting can leak through it.
    // Octave and shape are the two that would, since both change the
    // oscillator rather than its gain.
    const auto render = [] (int octave, int shape)
    {
        StrydaEngine engine;
        engine.prepare (48000.0, 512);
        engine.setPolyphony (1);

        VoiceParameters parameters = singleCarrier();
        parameters.extras.subLevel = 0.0;
        parameters.extras.subOctave = octave;
        parameters.extras.subShape = shape;
        engine.setParameters (parameters);
        engine.noteOn (60, 1.0);

        return renderEngine (engine, 4096, 512);
    };

    const auto reference = render (-1, 0);

    for (int octave : { -2, -1, 0 })
        for (int shape : { 0, 1 })
        {
            const auto candidate = render (octave, shape);

            std::size_t differing = 0;
            for (std::size_t i = 0; i < candidate.size(); ++i)
                if (! (candidate[i] == reference[i]))
                    ++differing;

            CHECK (differing == 0);
        }
}

TEZLA_TEST (each_f5_stage_actually_does_something_when_asked)
{
    // The other half of the test above, and the half that makes it mean
    // anything: a stage that was never wired up would pass every bit-exactness
    // check ever written.
    const auto render = [] (const VoiceExtras& extras)
    {
        StrydaEngine engine;
        engine.prepare (48000.0, 512);
        engine.setPolyphony (1);

        VoiceParameters parameters = singleCarrier();
        parameters.extras = extras;
        engine.setParameters (parameters);
        engine.noteOn (60, 1.0);

        return renderEngine (engine, 4096, 512);
    };

    const auto rms = [] (const std::vector<double>& samples)
    {
        double sum = 0.0;
        for (const double sample : samples)
            sum += sample * sample;
        return std::sqrt (sum / static_cast<double> (samples.size()));
    };

    const double open = rms (render (VoiceExtras {}));

    VoiceExtras closed;
    closed.cutoffHz = 120.0;   // well below the 261 Hz note
    const double filtered = rms (render (closed));

    VoiceExtras withSub;
    withSub.subLevel = 0.8;
    const double subbed = rms (render (withSub));

    std::printf ("        [f5] rms open %.5f, filter at 120 Hz %.5f, sub at 0.8 %.5f\n",
                 open, filtered, subbed);

    CHECK (filtered < open * 0.5);    // the filter removes the note
    CHECK (subbed > open * 1.1);      // the sub lane adds to it
}

// ---------------------------------------------------------------------------
// F5: unison, the protected sub lane, and what keeps a voice alive
// ---------------------------------------------------------------------------

namespace
{
/// One note, rendered, with whatever the caller wants changed about it.
template <typename Configure>
[[nodiscard]] std::vector<double> renderNote (Configure&& configure,
                                              int frames = 4096,
                                              int polyphony = 8)
{
    StrydaEngine engine;
    engine.prepare (48000.0, 512);
    engine.setPolyphony (polyphony);

    VoiceParameters parameters = singleCarrier();
    configure (parameters);
    engine.setParameters (parameters);
    engine.noteOn (60, 1.0);

    return renderEngine (engine, frames, 512);
}

[[nodiscard]] double rmsOf (const std::vector<double>& samples)
{
    double sum = 0.0;
    for (const double sample : samples)
        sum += sample * sample;
    return std::sqrt (sum / static_cast<double> (samples.size()));
}

[[nodiscard]] std::size_t differingSamples (const std::vector<double>& a,
                                            const std::vector<double>& b)
{
    std::size_t differing = 0;
    const std::size_t count = std::min (a.size(), b.size());

    for (std::size_t i = 0; i < count; ++i)
        if (! (a[i] == b[i]))
            ++differing;

    return differing + (a.size() > count ? a.size() - count : b.size() - count);
}
} // namespace

TEZLA_TEST (a_unison_stack_of_one_is_bit_exactly_no_unison_at_all)
{
    // The three amounts have nothing to spread across at a count of one, so
    // they must not be able to move a single sample. This is the property that
    // lets unison default to 1 and leave every F4 project untouched.
    const auto reference = renderNote ([] (VoiceParameters&) {});

    CHECK (rmsOf (reference) > 0.01);

    const auto loud = renderNote ([] (VoiceParameters& parameters)
    {
        parameters.extras.unisonCount = 1;
        parameters.extras.unisonDetuneCents = 50.0;
        parameters.extras.unisonSpread = 1.0;
        parameters.extras.unisonIndexSpread = 2.0;
    });

    std::printf ("        [unison] count 1 with every amount at maximum: %zu of %zu differ\n",
                 differingSamples (reference, loud), reference.size());

    CHECK (differingSamples (reference, loud) == 0);
}

TEZLA_TEST (a_unison_stack_takes_one_voice_per_copy_and_they_are_distinct)
{
    StrydaEngine engine;
    engine.prepare (48000.0, 512);
    engine.setPolyphony (8);

    VoiceParameters parameters = singleCarrier();
    parameters.extras.unisonCount = 4;
    parameters.extras.unisonDetuneCents = 20.0;
    engine.setParameters (parameters);

    engine.noteOn (60, 1.0);

    std::printf ("        [unison] count 4 on one note: %d voices active\n",
                 engine.getActiveVoiceCount());

    CHECK (engine.getActiveVoiceCount() == 4);

    // A retrigger takes its own copies back rather than spending four more.
    engine.noteOn (60, 1.0);
    CHECK (engine.getActiveVoiceCount() == 4);

    // A second note takes four more, and the first note keeps its own.
    engine.noteOn (67, 1.0);
    CHECK (engine.getActiveVoiceCount() == 8);

    // The stack cannot exceed the polyphony it has to live inside: the copies
    // would otherwise be handed the same voice twice and silently collapse.
    StrydaEngine narrow;
    narrow.prepare (48000.0, 512);
    narrow.setPolyphony (3);

    VoiceParameters wide = singleCarrier();
    wide.extras.unisonCount = 8;
    narrow.setParameters (wide);
    narrow.noteOn (60, 1.0);

    std::printf ("        [unison] count 8 at 3 voices of polyphony: %d active\n",
                 narrow.getActiveVoiceCount());

    CHECK (narrow.getActiveVoiceCount() == 3);
}

TEZLA_TEST (detune_and_index_spread_each_change_the_stack_on_their_own)
{
    const auto flat = renderNote ([] (VoiceParameters& parameters)
    {
        parameters.extras.unisonCount = 4;
    });

    const auto detuned = renderNote ([] (VoiceParameters& parameters)
    {
        parameters.extras.unisonCount = 4;
        parameters.extras.unisonDetuneCents = 20.0;
    });

    // Index spread needs a live cell to offset, so give the patch one.
    const auto spreadIndex = renderNote ([] (VoiceParameters& parameters)
    {
        parameters.indices[0][1] = 1.2;
        parameters.operators[1].ratio = 2.0;
        parameters.extras.unisonCount = 4;
        parameters.extras.unisonIndexSpread = 0.6;
    });

    const auto spreadReference = renderNote ([] (VoiceParameters& parameters)
    {
        parameters.indices[0][1] = 1.2;
        parameters.operators[1].ratio = 2.0;
        parameters.extras.unisonCount = 4;
    });

    std::printf ("        [unison] detune moves %zu samples, index spread %zu\n",
                 differingSamples (flat, detuned),
                 differingSamples (spreadReference, spreadIndex));

    CHECK (differingSamples (flat, detuned) > 1000);
    CHECK (differingSamples (spreadReference, spreadIndex) > 1000);

    // And index spread cannot switch on a path the patch never asked for: with
    // no live cells there is nothing to offset, whatever the amount.
    const auto noCells = renderNote ([] (VoiceParameters& parameters)
    {
        parameters.extras.unisonCount = 4;
        parameters.extras.unisonIndexSpread = 2.0;
    });

    CHECK (differingSamples (flat, noCells) == 0);
}

TEZLA_TEST (only_one_copy_of_a_unison_stack_carries_the_sub)
{
    // Silence the matrix and listen to the sub lane alone. Four copies of the
    // stack must produce exactly what one does -- not four subs a few cents
    // apart, which is a chorused mush in the one octave that has to be solid.
    const auto subOnly = [] (int count)
    {
        return renderNote ([count] (VoiceParameters& parameters)
        {
            for (auto& settings : parameters.operators)
                settings.level = 0.0;

            parameters.extras.subLevel = 0.7;
            parameters.extras.unisonCount = count;
            parameters.extras.unisonDetuneCents = 25.0;
        });
    };

    const auto one = subOnly (1);
    const auto four = subOnly (4);

    std::printf ("        [sub] lane alone, rms %.5f at 1 copy and %.5f at 4; %zu differ\n",
                 rmsOf (one), rmsOf (four), differingSamples (one, four));

    CHECK (rmsOf (one) > 0.05);
    CHECK (differingSamples (one, four) == 0);
}

TEZLA_TEST (a_thicker_stack_settles_to_the_same_level_but_punches_harder)
{
    // ---------------------------------------------------------------------
    // **1/sqrt(n) is right, and the first version of this test was not**
    // ---------------------------------------------------------------------
    //
    // It rendered 85 ms and expected eight copies to land near one in level.
    // They measured 2.56x, which looked like a broken compensation and was not:
    // every copy starts at the same phase, so at the onset they sum
    // COHERENTLY. Measured raw sum factors, with the 1/sqrt(n) divided back
    // out (n means fully in phase, sqrt(n) means fully random):
    //
    //      window    5 cents      15 cents     40 cents
    //      85 ms     7.91 / 8     7.25 / 8     4.98 / 8
    //      500 ms    5.80 / 8     3.47 / 8     2.24 / 8
    //      2000 ms   3.03 / 8     3.03 / 8     2.84 / 8      (sqrt(8) = 2.83)
    //
    // So the copies drift apart over roughly a second and the steady state is
    // exactly the incoherent sum 1/sqrt(n) compensates for. The loud onset is
    // not a defect to flatten -- it is what makes a detuned stack punch, and
    // flattening it with 1/n would leave the sustain 8 dB quiet.
    //
    // The test therefore asserts both: the steady state lands near unity, and
    // the onset is measurably louder than it.
    const auto render = [] (int count, int frames)
    {
        return renderNote ([count] (VoiceParameters& parameters)
        {
            parameters.operators[0].sustain = 1.0;
            parameters.operators[0].decay = 8.0;
            parameters.extras.unisonCount = count;
            parameters.extras.unisonDetuneCents = 15.0;
        }, frames, 16);
    };

    const double settledOne = rmsOf (render (1, 96000));
    const double settledEight = rmsOf (render (8, 96000));
    const double settled = settledOne > 0.0 ? settledEight / settledOne : 0.0;

    const double onsetOne = rmsOf (render (1, 4096));
    const double onsetEight = rmsOf (render (8, 4096));
    const double onset = onsetOne > 0.0 ? onsetEight / onsetOne : 0.0;

    std::printf ("        [unison] 8 copies vs 1: %.2fx over 2 s, %.2fx over the first 85 ms\n",
                 settled, onset);

    CHECK (settled > 0.85);
    CHECK (settled < 1.35);

    // The onset has to be the louder of the two, or the copies are not starting
    // in phase and the stack has no transient of its own.
    CHECK (onset > settled * 1.5);
}

TEZLA_TEST (an_inaudible_filter_or_sub_envelope_never_holds_a_voice_open)
{
    // ---------------------------------------------------------------------
    // **The Sonitus zombie, in a new place**
    // ---------------------------------------------------------------------
    //
    // The filter envelope shapes something that must itself be sounding, and
    // the sub envelope only makes noise on the copy that carries the lane and
    // only while that lane has a level. Counting either towards "is this voice
    // still doing anything" keeps silent voices alive, costs a voice slot and a
    // sample loop each, and is invisible to every silence-based test -- which
    // is exactly how it pinned Sonitus's CPU meter at 100 %.
    //
    // So the assertion is on the voice COUNT, not on the audio.
    StrydaEngine engine;
    engine.prepare (48000.0, 512);
    engine.setPolyphony (8);

    VoiceParameters parameters = singleCarrier();

    // Short operator envelopes: the note is over quickly.
    for (auto& settings : parameters.operators)
    {
        settings.attack = 0.001;
        settings.decay = 0.02;
        settings.sustain = 0.0;
        settings.release = 0.01;
    }

    // A silent sub lane with a very long release, and a filter envelope with
    // one to match. Neither can be heard; neither may keep the voice.
    parameters.extras.subLevel = 0.0;
    parameters.extras.subRelease = 10.0;
    parameters.extras.subSustain = 1.0;
    parameters.extras.filterRelease = 10.0;
    parameters.extras.filterSustain = 1.0;

    engine.setParameters (parameters);
    engine.noteOn (60, 1.0);

    std::vector<double> left (512, 0.0);
    std::vector<double> right (512, 0.0);

    engine.process (left.data(), right.data(), 512);
    CHECK (engine.getActiveVoiceCount() == 1);

    engine.noteOff (60);

    // Half a second: far past the operators' 10 ms release, far short of the
    // ten seconds the two inaudible envelopes are set to.
    for (int block = 0; block < 47; ++block)
        engine.process (left.data(), right.data(), 512);

    std::printf ("        [zombie] 0.5 s after note-off, %d voices still active "
                 "(the silent envelopes are set to 10 s)\n",
                 engine.getActiveVoiceCount());

    CHECK (engine.getActiveVoiceCount() == 0);
}
