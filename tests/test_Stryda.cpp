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

#include <tezla/dsp/FmOperator.hpp>

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
