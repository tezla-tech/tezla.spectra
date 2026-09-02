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
#include <memory>
#include <numbers>
#include <vector>

#include <tezla/dsp/Adsr.hpp>

#include <IctusEngine.hpp>

using namespace tezla::ictus;
using namespace tezla::dsp;

namespace
{
/// The engine holds an oversampler with every stage built; it lives on the
/// heap in tests for the same reason Sonitus's does.
[[nodiscard]] auto heapEngine()
{
    return std::make_unique<Engine>();
}

struct Hit
{
    int sample;          ///< host sample the note lands on
    int note;
    double velocity;
};

struct Choke
{
    int sample;
    PadIndex pad;
};

/// Renders `samples` host samples with the notes and chokes landing on their
/// exact samples whatever the block size: a block is cut short at every
/// event so the same sample sees the same note at 64 and at 512.
std::vector<double> render (const EngineParameters& parameters, double rate, int samples,
                            const std::vector<Hit>& hits, int blockSize = 256,
                            const std::vector<Choke>& chokes = {},
                            Engine* reuse = nullptr)
{
    std::unique_ptr<Engine> owned;

    if (reuse == nullptr)
    {
        owned = heapEngine();
        reuse = owned.get();
        reuse->prepare (rate, 512);
    }

    Engine& engine = *reuse;
    engine.setParameters (parameters);

    std::vector<double> left (static_cast<std::size_t> (samples), 0.0);
    std::vector<double> right (static_cast<std::size_t> (samples), 0.0);

    int done = 0;

    while (done < samples)
    {
        for (const auto& hit : hits)
            if (hit.sample == done)
                engine.noteOn (hit.note, hit.velocity);

        for (const auto& choke : chokes)
            if (choke.sample == done)
                engine.choke (choke.pad);

        int take = std::min (blockSize, samples - done);

        for (const auto& hit : hits)
            if (hit.sample > done)
                take = std::min (take, hit.sample - done);

        for (const auto& choke : chokes)
            if (choke.sample > done)
                take = std::min (take, choke.sample - done);

        double* block[2] = { left.data() + done, right.data() + done };
        engine.process (block, take);

        done += take;
    }

    return left;
}

/// Start 0, sigh 0, phase 0, harmonics 0, tail 0, tone off, click 0, noise 0:
/// the body under its envelope and nothing else.
KickSettings neutralKick()
{
    KickSettings s;
    s.tuneHz = 50.0;
    s.startSemitones = 0.0;
    s.sighSemitones = 0.0;
    s.phaseDegrees = 0.0;
    s.harmonics = 0.0;
    s.toneEnabled = false;
    s.click = 0.0;
    s.clickNoise = 0.0;
    s.attackSeconds = 0.0;
    s.holdSeconds = 0.0;
    s.decaySeconds = 0.2;
    s.shape = 0.0;
    s.tailMix = 0.0;
    s.level = 1.0;
    s.velocityLevel = 1.0;
    s.velocityClick = 0.0;
    s.velocityDrop = 0.0;
    s.velocityDecay = 0.0;
    return s;
}

/// Every stage engaged.
KickSettings everythingOn()
{
    KickSettings s;
    s.tuneHz = 48.0;
    s.startSemitones = 30.0;
    s.dropSeconds = 0.03;
    s.sighSemitones = 1.5;
    s.sighSeconds = 0.5;
    s.phaseDegrees = 45.0;
    s.harmonics = 0.7;
    s.even = 0.5;
    s.toneEnabled = true;
    s.toneRatio = 6.0;
    s.click = 0.5;
    s.clickToneHz = 3000.0;
    s.clickNoise = 0.4;
    s.clickNoiseSeconds = 0.002;
    s.attackSeconds = 0.0;
    s.holdSeconds = 0.005;
    s.decaySeconds = 0.3;
    s.shape = 0.3;
    s.tailMix = 0.5;
    s.tailSeconds = 1.0;
    s.level = 0.8;
    return s;
}

double maxStep (const std::vector<double>& x)
{
    double worst = 0.0;

    for (std::size_t n = 1; n < x.size(); ++n)
        worst = std::max (worst, std::abs (x[n] - x[n - 1]));

    return worst;
}

/// Positive-going zero-crossing times in seconds, linearly interpolated.
std::vector<double> crossings (const std::vector<double>& x, double rate)
{
    std::vector<double> out;

    // Only from inside the first lobe: a linear-phase decimator pre-rings
    // before the hit -- at -100 dB far out, at -40 dB right at the onset --
    // and that ripple crosses zero. Counting it misaligned every cycle that
    // followed (10,000 cents on the first run, one whole cycle on the
    // second, while the undecimated 192 kHz case read 0.015). Detection
    // starts where the signal first reaches a tenth of its peak, which is
    // inside the body's first half-cycle, so the first crossing counted is
    // the end of the first cycle at every rate.
    double peak = 0.0;
    for (const double v : x)
        peak = std::max (peak, std::abs (v));

    std::size_t onset = x.size();
    for (std::size_t n = 0; n < x.size(); ++n)
        if (std::abs (x[n]) > 0.1 * peak)
        {
            onset = n;
            break;
        }

    for (std::size_t n = onset + 1; n < x.size(); ++n)
        if (x[n - 1] < 0.0 && x[n] >= 0.0)
        {
            const double frac = -x[n - 1] / (x[n] - x[n - 1]);
            out.push_back ((static_cast<double> (n - 1) + frac) / rate);
        }

    return out;
}

/// The body's pitch from the two drops' closed forms.
double closedFormHz (const KickSettings& s, double t)
{
    const double dropTau = s.dropSeconds / TensionDrop::kLandFactor;
    const double sighTau = s.sighSeconds / TensionDrop::kLandFactor;
    const double cents = 100.0 * s.startSemitones * std::exp (-t / dropTau)
                       + 100.0 * s.sighSemitones * std::exp (-t / sighTau);
    return s.tuneHz * std::exp2 (cents / 1200.0);
}

/// Where the closed-form body crosses zero going up: the phase integral of
/// the closed-form pitch reaching each whole cycle.
std::vector<double> predictedCrossings (const KickSettings& s, double seconds)
{
    std::vector<double> out;
    constexpr double dt = 1.0e-6;

    double phase = 0.0;
    double next = 1.0;
    double previous = closedFormHz (s, 0.0);

    for (double t = 0.0; t < seconds; t += dt)
    {
        const double hz = closedFormHz (s, t + dt);
        phase += 0.5 * (previous + hz) * dt;
        previous = hz;

        if (phase >= next)
        {
            const double over = (phase - next) / (hz * dt);
            out.push_back (t + dt - over * dt);
            next += 1.0;
        }
    }

    return out;
}
} // namespace

// ---------------------------------------------------------------------------
// The neutral kick
// ---------------------------------------------------------------------------

TEZLA_TEST (a_neutral_kick_is_exactly_a_sine_under_its_envelope)
{
    // Not "nearly": with every stage at its neutral setting the engine's
    // output must be sin(2 pi phase) times the envelope and nothing else --
    // no filter, no blocker, no shaper, no smoothing anywhere in the path.
    // Oversampling is off so the internal rate is the host rate and the
    // comparison can be bit for bit; the reference is the same Adsr and the
    // same accumulator, written out here.
    constexpr double rate = 48000.0;
    constexpr int samples = 24000;

    EngineParameters parameters;
    parameters.kick1 = neutralKick();
    parameters.oversampling = OversamplingMode::Off;

    const auto out = render (parameters, rate, samples, { { 0, 36, 1.0 } });

    Adsr envelope;
    envelope.prepare (rate);
    envelope.setAttackSeconds (0.0);
    envelope.setAttackTension (0.0);
    envelope.setHoldSeconds (0.0);
    envelope.setDecaySeconds (0.2);
    envelope.setDecayTension (1.0);
    envelope.setSustain (0.0);
    envelope.setReleaseSeconds (0.0);
    envelope.noteOn();

    const double inc = 50.0 / rate;
    double phase = 0.0;
    double worst = 0.0;
    int lastNonZero = -1;

    for (int n = 0; n < samples; ++n)
    {
        const double body = std::sin (KickEngine::kTwoPi * phase);
        phase += inc;
        if (phase >= 1.0)
            phase -= 1.0;

        const double env = envelope.process();
        if (envelope.getStage() == AdsrStage::sustain)
            envelope.kill();

        const double expected = body * env;
        const double actual = out[static_cast<std::size_t> (n)];

        worst = std::max (worst, std::abs (actual - expected));

        if (actual != 0.0)
            lastNonZero = n;
    }

    CHECK (worst == 0.0);

    // The envelope lands at 0.2 s, and from there the output is exactly
    // zero -- the hit retired rather than decaying below hearing.
    CHECK (lastNonZero > 0);
    CHECK (lastNonZero < static_cast<int> (0.2 * rate) + 2);

    std::printf ("        [neutral] bit-exact over %d samples, last non-zero at %.4f s\n",
                 samples, lastNonZero / rate);
}

// ---------------------------------------------------------------------------
// The pitch envelope
// ---------------------------------------------------------------------------

TEZLA_TEST (the_kick_pitch_envelope_follows_the_closed_form_at_every_host_rate)
{
    // The body's zero crossings are compared with the crossings the two
    // drops' closed forms predict, cycle by cycle, at four host rates under
    // Auto oversampling. The engine interpolates the increment linearly
    // between exact values at each control chunk's ends, so the residual is
    // the chord's miss of the exponential -- second order in the chunk
    // length -- plus the crossing interpolation, and the four rates must
    // agree with each other far more closely than with the closed form.
    KickSettings settings;
    settings.tuneHz = 50.0;
    settings.startSemitones = 30.0;
    settings.dropSeconds = 0.03;
    settings.sighSemitones = 1.5;
    settings.sighSeconds = 0.5;
    settings.decaySeconds = 1.0;
    settings.level = 1.0;
    settings.velocityDrop = 0.0;
    settings.click = 0.0;
    settings.clickNoise = 0.0;

    constexpr double seconds = 0.6;
    const auto predicted = predictedCrossings (settings, seconds);
    CHECK (predicted.size() > 30);

    std::vector<std::vector<double>> measured;
    double worstCents = 0.0;

    for (const double rate : { 44100.0, 48000.0, 96000.0, 192000.0 })
    {
        EngineParameters parameters;
        parameters.kick1 = settings;

        auto engine = heapEngine();
        engine->prepare (rate, 512);

        const auto out = render (parameters, rate, static_cast<int> (seconds * rate),
                                 { { 0, 36, 1.0 } }, 256, {}, engine.get());
        auto times = crossings (out, rate);

        // Rate-dependent latency: the decimators delay the host output by a
        // whole number of samples per factor, so the crossings are shifted
        // back by it before anything is compared.
        const double latency = static_cast<double> (engine->getLatencySamples()) / rate;

        for (auto& t : times)
            t -= latency;

        const std::size_t count = std::min (times.size(), predicted.size()) - 1;
        double worstHere = 0.0;

        for (std::size_t k = 1; k < count; ++k)
        {
            // Skip the first two cycles: they straddle the start, where the
            // closed form's phase and the engine's differ by the
            // interpolation of a single partial chunk.
            if (k < 3)
                continue;

            const double periodEngine = times[k + 1] - times[k];
            const double periodClosed = predicted[k + 1] - predicted[k];
            const double cents = 1200.0 * std::log2 (periodClosed / periodEngine);
            worstHere = std::max (worstHere, std::abs (cents));
        }

        std::printf ("        [pitch] %.0f Hz host x%d: worst %.3f cents against the closed form over %zu cycles\n",
                     rate, engine->getOversamplingFactor(), worstHere, count);

        worstCents = std::max (worstCents, worstHere);
        measured.push_back (std::move (times));
    }

    // Measured 2026-09-02: worst 0.016 / 0.015 / 0.015 / 0.015 cents at
    // 44.1 / 48 / 96 / 192 kHz over cycles 3-30 (cycle 1, which straddles
    // the drop's steepest part, reads 1.1 cents in the tool's per-cycle
    // table). The bound is six times the worst reading; a staircase
    // increment reads over 100 cents here.
    CHECK (worstCents < 0.1);

    // And the four rates against each other: the same crossing times to
    // within a few microseconds over the whole hit.
    double worstSpread = 0.0;
    const std::size_t common = std::min ({ measured[0].size(), measured[1].size(),
                                           measured[2].size(), measured[3].size() });

    for (std::size_t k = 3; k < common; ++k)
    {
        double lo = measured[0][k];
        double hi = measured[0][k];

        for (const auto& m : measured)
        {
            lo = std::min (lo, m[k]);
            hi = std::max (hi, m[k]);
        }

        worstSpread = std::max (worstSpread, hi - lo);
    }

    // Measured 2026-09-02: 1.95 us between the four rates over the whole
    // hit -- less than a tenth of an internal sample at 44.1 k x4.
    std::printf ("        [pitch] crossing-time spread between rates: %.2f us\n", worstSpread * 1.0e6);
    CHECK (worstSpread < 10.0e-6);
}

// ---------------------------------------------------------------------------
// Latency
// ---------------------------------------------------------------------------

TEZLA_TEST (the_declared_latency_is_the_measured_one_at_every_factor)
{
    // The same neutral 200 Hz body rendered with oversampling off (no
    // decimator, no delay) and at x2, x4, x8. The oversampled render must be
    // the undecimated one delayed by exactly the integer the engine
    // declares: the residual at that integer is the halfband's passband
    // ripple, and at the neighbouring integers -- or at the half sample the
    // instrument would have without its alignment delay -- it is the
    // 200 Hz body shifted by 2 pi 200 / 48000 = 2.6 % of full scale per
    // sample, a thousand times larger. That is NOT the oversampler's
    // round-trip figure (an effect's: 47 / 63 / 71) but half of it plus
    // the half-sample alignment delay: 24 / 32 / 36.
    constexpr double rate = 48000.0;
    constexpr int samples = 4096;

    EngineParameters parameters;
    parameters.kick1 = neutralKick();
    parameters.kick1.tuneHz = 200.0;
    parameters.kick1.decaySeconds = 2.0;

    parameters.oversampling = OversamplingMode::Off;
    const auto reference = render (parameters, rate, samples, { { 0, 36, 1.0 } }, 128);

    for (const auto mode : { OversamplingMode::X2, OversamplingMode::X4, OversamplingMode::X8 })
    {
        parameters.oversampling = mode;

        auto engine = heapEngine();
        engine->prepare (rate, 512);

        const auto out = render (parameters, rate, samples, { { 0, 36, 1.0 } }, 128, {}, engine.get());
        const int declared = engine->getLatencySamples();

        const auto residualAt = [&] (int delay)
        {
            double sum = 0.0;
            int count = 0;

            // Past the onset's ringing, which the undecimated render does not
            // have, and short of the end.
            for (int n = 1000; n < samples - 100; ++n)
            {
                const double d = out[static_cast<std::size_t> (n)]
                               - reference[static_cast<std::size_t> (n - delay)];
                sum += d * d;
                ++count;
            }

            return std::sqrt (sum / count);
        };

        const double at = residualAt (declared);
        const double before = residualAt (declared - 1);
        const double after = residualAt (declared + 1);

        std::printf ("        [latency] x%d: declared %d; residual %.2e there, %.2e / %.2e one sample either side"
                     " (round trip would say %d)\n",
                     engine->getOversamplingFactor(), declared, at, before, after,
                     Oversampler::latencyForFactor (engine->getOversamplingFactor()));

        CHECK (at < 3.0e-5);
        CHECK (before > 100.0 * at);
        CHECK (after > 100.0 * at);
    }
}

// ---------------------------------------------------------------------------
// Retirement, retrigger, choke
// ---------------------------------------------------------------------------

TEZLA_TEST (a_kick_hit_retires_exactly_and_leaves_exact_zeros)
{
    constexpr double rate = 48000.0;
    constexpr int samples = static_cast<int> (3.0 * rate);

    EngineParameters parameters;
    parameters.kick1 = everythingOn();

    auto engine = heapEngine();
    engine->prepare (rate, 512);

    const auto out = render (parameters, rate, samples, { { 0, 36, 1.0 } }, 256, {}, engine.get());

    // Sounding early, gone at the end -- by count, not by silence.
    CHECK (engine->activeHitCount() == 0);

    int lastNonZero = -1;

    for (int n = 0; n < samples; ++n)
        if (out[static_cast<std::size_t> (n)] != 0.0)
            lastNonZero = n;

    // The tail is 1 s at half mix; the decimators add their latency. Well
    // before 3 s the output is exactly 0.0 and stays there.
    CHECK (lastNonZero > static_cast<int> (0.5 * rate));
    CHECK (lastNonZero < static_cast<int> (2.0 * rate));

    std::printf ("        [retire] last non-zero sample at %.3f s, active hits %d\n",
                 lastNonZero / rate, engine->activeHitCount());
}

TEZLA_TEST (a_kick_retrigger_is_a_crossfade_not_a_cut)
{
    // Two hits 150 ms apart. The step between consecutive output samples
    // must stay at the signal's own maximum step (the drop's first cycles),
    // where a cut would step by the whole level the old hit was at. Then the
    // two orders of a choke and a retrigger inside each other's fade: no
    // NaN, bounded, and the right number of hits left sounding.
    constexpr double rate = 48000.0;
    constexpr int samples = static_cast<int> (0.6 * rate);
    constexpr int retriggerAt = static_cast<int> (0.15 * rate);

    EngineParameters parameters;
    parameters.kick1 = everythingOn();
    parameters.kick1.click = 0.0;        // the click IS a step; keep the body's own
    parameters.kick1.clickNoise = 0.0;
    parameters.kick1.phaseDegrees = 0.0;

    const auto single = render (parameters, rate, samples, { { 0, 36, 1.0 } });
    const auto pair = render (parameters, rate, samples, { { 0, 36, 1.0 }, { retriggerAt, 36, 1.0 } });

    const double singleStep = maxStep (single);
    const double pairStep = maxStep (pair);

    // The cut that the crossfade replaces: the old hit's level where the new
    // one lands.
    double cutStep = 0.0;
    for (int n = retriggerAt - 480; n < retriggerAt + 480; ++n)
        cutStep = std::max (cutStep, std::abs (single[static_cast<std::size_t> (n)]));

    std::printf ("        [retrigger] max step: single hit %.4f, retriggered %.4f, a cut would be %.4f\n",
                 singleStep, pairStep, cutStep);

    CHECK (pairStep <= 1.05 * singleStep + 1.0e-3);
    CHECK (pairStep < 0.5 * cutStep);

    for (const double x : pair)
        CHECK (std::isfinite (x));

    // Choke, then a retrigger 20 samples into the 5 ms fade.
    {
        auto engine = heapEngine();
        engine->prepare (rate, 512);
        const auto out = render (parameters, rate, samples,
                                 { { 0, 36, 1.0 }, { retriggerAt + 20, 36, 1.0 } }, 256,
                                 { { retriggerAt, PadIndex::kick1 } }, engine.get());

        for (const double x : out)
            CHECK (std::isfinite (x) && std::abs (x) < 1.5);

        CHECK (maxStep (out) <= 1.05 * singleStep + 1.0e-3);
    }

    // Retrigger, then a choke 20 samples into the 1 ms fade. Afterwards
    // nothing is sounding.
    {
        auto engine = heapEngine();
        engine->prepare (rate, 512);
        const auto out = render (parameters, rate, samples,
                                 { { 0, 36, 1.0 }, { retriggerAt, 36, 1.0 } }, 256,
                                 { { retriggerAt + 20, PadIndex::kick1 } }, engine.get());

        for (const double x : out)
            CHECK (std::isfinite (x) && std::abs (x) < 1.5);

        CHECK (engine->activeHitCount() == 0);
    }

    // And a choke with the pad idle is nothing at all.
    {
        auto engine = heapEngine();
        engine->prepare (rate, 512);
        engine->choke (PadIndex::kick1);
        CHECK (engine->activeHitCount() == 0);
    }
}

// ---------------------------------------------------------------------------
// Block size
// ---------------------------------------------------------------------------

TEZLA_TEST (the_kick_output_is_block_size_independent)
{
    // CLAUDE.md section 7: the control grid is the engine's and the render
    // loop is cut at its boundary, so 64-, 97- and 512-sample blocks produce
    // the same bits -- with both kicks sounding, a retrigger, and every
    // stage engaged.
    constexpr double rate = 48000.0;
    constexpr int samples = 24000;

    EngineParameters parameters;
    parameters.kick1 = everythingOn();
    parameters.kick2 = everythingOn();
    parameters.kick2.tuneHz = 60.0;
    parameters.kick2.even = 1.0;
    parameters.masterDb = -3.0;

    const std::vector<Hit> hits { { 0, 36, 0.9 }, { 2500, 35, 0.7 }, { 4000, 36, 1.0 } };

    const auto small = render (parameters, rate, samples, hits, 64);
    const auto large = render (parameters, rate, samples, hits, 512);
    const auto odd = render (parameters, rate, samples, hits, 97);

    double worst = 0.0;

    for (std::size_t i = 0; i < small.size(); ++i)
    {
        worst = std::max (worst, std::abs (small[i] - large[i]));
        worst = std::max (worst, std::abs (small[i] - odd[i]));
    }

    CHECK (worst == 0.0);
}

// ---------------------------------------------------------------------------
// CPU
// ---------------------------------------------------------------------------

TEZLA_TEST (two_kicks_fit_their_budget_and_an_idle_instrument_costs_nothing)
{
    constexpr double rate = 48000.0;
    constexpr int block = 480;
    constexpr int blocks = 100;      // one second of audio

    EngineParameters parameters;
    parameters.kick1 = everythingOn();
    parameters.kick2 = everythingOn();

    auto engine = heapEngine();
    engine->prepare (rate, block);
    engine->setParameters (parameters);

    std::vector<double> left (block), right (block);
    double* buffers[2] = { left.data(), right.data() };
    double sink = 0.0;

    // Warm up: the first process builds nothing, but the first hit touches
    // every page.
    engine->noteOn (36, 1.0);
    engine->process (buffers, block);

    auto start = std::chrono::steady_clock::now();

    for (int b = 0; b < blocks; ++b)
    {
        // Eight hits a second on each kick, alternating.
        if (b % 12 == 0)
            engine->noteOn (36, 1.0);

        if (b % 12 == 6)
            engine->noteOn (35, 0.8);

        engine->process (buffers, block);
        sink += left[0];
    }

    const double activeSeconds = std::chrono::duration<double> (
        std::chrono::steady_clock::now() - start).count();

    // Let everything retire and the idle skip engage: 3 s of silence.
    for (int b = 0; b < 3 * blocks; ++b)
        engine->process (buffers, block);

    CHECK (engine->activeHitCount() == 0);

    start = std::chrono::steady_clock::now();

    for (int b = 0; b < blocks; ++b)
    {
        engine->process (buffers, block);
        sink += left[0];
    }

    const double idleSeconds = std::chrono::duration<double> (
        std::chrono::steady_clock::now() - start).count();

    std::printf ("        [engine cpu] two kicks at 48 kHz x%d: %.2f%% of a core; idle %.3f%% (sink %g)\n",
                 engine->getOversamplingFactor(), 100.0 * activeSeconds, 100.0 * idleSeconds, sink);

    CHECK_CPU_BUDGET (activeSeconds, 0.10, "two kicks, everything on, 48 kHz x4");
    CHECK_CPU_BUDGET (idleSeconds, 0.01, "idle instrument");
}
