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
#include <complex>
#include <cstdio>
#include <memory>
#include <numbers>
#include <vector>

#include <tezla/dsp/Adsr.hpp>
#include <tezla/dsp/Exact.hpp>
#include <tezla/dsp/Fft.hpp>
#include <tezla/dsp/Scales.hpp>

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
    int sample;          ///< host sample the event lands on
    int note;
    double velocity;     ///< a negative velocity is a note-OFF for `note`
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
            {
                if (hit.velocity < 0.0)
                    engine.noteOff (hit.note);
                else
                    engine.noteOn (hit.note, hit.velocity);
            }

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

/// Every snare stage engaged: the drop, both upper modes, wires with the
/// rattle, and the crack pair.
SnareSettings snareEverythingOn()
{
    SnareSettings s;
    s.tuneHz = 190.0;
    s.spread = 0.9;
    s.tone = 0.7;
    s.decaySeconds = 0.3;
    s.startSemitones = 8.0;
    s.dropSeconds = 0.03;
    s.body = 0.8;
    s.wires = 0.7;
    s.snappyHz = 3500.0;
    s.snap = 0.4;
    s.wiresDecaySeconds = 0.18;
    s.rattle = 0.6;
    s.crack = 0.5;
    s.crackToneHz = 4000.0;
    s.crackNoise = 0.4;
    s.crackNoiseSeconds = 0.0015;
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
    // the same bits -- with both kicks, the snare and the perc sounding, a
    // retrigger, and every stage engaged (the snare's drop retuning its
    // bank per chunk, the rattle, the wires, the crack).
    constexpr double rate = 48000.0;
    constexpr int samples = 24000;

    EngineParameters parameters;
    parameters.kick1 = everythingOn();
    parameters.kick2 = everythingOn();
    parameters.kick2.tuneHz = 60.0;
    parameters.kick2.even = 1.0;
    parameters.snare1 = snareEverythingOn();
    parameters.perc = tomSettings();
    parameters.perc.rattle = 0.5;
    parameters.perc.wires = 0.3;
    parameters.masterDb = -3.0;

    const std::vector<Hit> hits { { 0, 36, 0.9 }, { 1000, 38, 1.0 }, { 2500, 35, 0.7 },
                                  { 3100, 37, 0.8 }, { 4000, 36, 1.0 }, { 4700, 38, 0.6 } };

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

TEZLA_TEST (a_kit_of_two_kicks_and_three_snares_fits_its_budget_and_idles_for_nothing)
{
    constexpr double rate = 48000.0;
    constexpr int block = 480;
    constexpr int blocks = 100;      // one second of audio

    EngineParameters parameters;
    parameters.kick1 = everythingOn();
    parameters.kick2 = everythingOn();
    parameters.snare1 = snareEverythingOn();
    parameters.snare2 = snareEverythingOn();
    parameters.snare2.tuneHz = 240.0;
    parameters.perc = tomSettings();

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
        // Eight hits a second on each kick, alternating; the snares and the
        // perc between them -- a busy break.
        if (b % 12 == 0)
            engine->noteOn (36, 1.0);

        if (b % 12 == 6)
            engine->noteOn (35, 0.8);

        if (b % 12 == 3)
            engine->noteOn (38, 1.0);

        if (b % 12 == 9)
            engine->noteOn (40, 0.9);

        if (b % 8 == 4)
            engine->noteOn (37, 0.7);

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

    std::printf ("        [engine cpu] two kicks and three snares at 48 kHz x%d: %.2f%% of a core; idle %.3f%% (sink %g)\n",
                 engine->getOversamplingFactor(), 100.0 * activeSeconds, 100.0 * idleSeconds, sink);

    CHECK_CPU_BUDGET (activeSeconds, 0.12, "two kicks and three snares, everything on, 48 kHz x4");
    CHECK_CPU_BUDGET (idleSeconds, 0.01, "idle instrument");
}

// ---------------------------------------------------------------------------
// Bass mode and the gate -- from the first ear round on the rig
// ---------------------------------------------------------------------------

namespace
{
int lastNonZeroSample (const std::vector<double>& x)
{
    int last = -1;

    for (std::size_t n = 0; n < x.size(); ++n)
        if (x[n] != 0.0)
            last = static_cast<int> (n);

    return last;
}

double peakFrom (const std::vector<double>& x, int from)
{
    double peak = 0.0;

    for (std::size_t n = static_cast<std::size_t> (from); n < x.size(); ++n)
        peak = std::max (peak, std::abs (x[n]));

    return peak;
}
} // namespace

TEZLA_TEST (bass_mode_plays_the_kick_on_every_key_at_that_keys_pitch)
{
    // With Bass mode on, every key strikes Kick 1 at the key's pitch through
    // the tuning -- 12-TET at A4 = 440 Hz until a scale is loaded: note 36
    // lands on 65.41 Hz, 43 on 98.00, 48 on 130.81 -- and the pad's own note
    // stops being special. With it off, a key that is not a pad's note is
    // silent, which is what the rig showed for "Follow key" alone.
    constexpr double rate = 48000.0;
    constexpr int samples = 24000;

    EngineParameters parameters;
    parameters.kick1 = neutralKick();
    parameters.kick1.decaySeconds = 0.6;
    parameters.bassMode = true;

    const std::pair<int, double> keys[] { { 36, 65.4064 }, { 43, 97.9989 }, { 48, 130.8128 } };

    for (const auto& [note, hz] : keys)
    {
        auto engine = heapEngine();
        engine->prepare (rate, 512);

        const auto out = render (parameters, rate, samples, { { 0, note, 1.0 } }, 256, {}, engine.get());
        const auto times = crossings (out, rate);

        CHECK (times.size() > 20);

        // A silent key has already failed the check above; measuring its
        // pitch would index an empty list.
        if (times.size() < 3)
            continue;

        // The mean period over the settled cycles, in cents against the
        // tuning's frequency for that key.
        const double measured = static_cast<double> (times.size() - 2) / (times.back() - times[1]);
        const double cents = 1200.0 * std::log2 (measured / hz);

        std::printf ("        [bass] note %d: %.3f Hz, %.3f cents from %.3f\n", note, measured, cents, hz);
        CHECK_NEAR (cents, 0.0, 0.5);
        CHECK (engine->activeHitCount() == 1);
    }

    // Off: note 43 strikes nothing at all.
    parameters.bassMode = false;

    auto engine = heapEngine();
    engine->prepare (rate, 512);

    const auto out = render (parameters, rate, 4800, { { 0, 43, 1.0 } }, 256, {}, engine.get());

    CHECK (engine->activeHitCount() == 0);
    CHECK (peakFrom (out, 0) == 0.0);
}

TEZLA_TEST (a_gated_kick_releases_at_note_off_and_a_one_shot_ignores_it)
{
    // The hold and decay are the hit's shape; the gate adds the early exit a
    // fill needs. Note-off at 200 ms into a 2 s decay: a one-shot pad plays
    // on regardless; a gated pad with a 50 ms release is exactly silent
    // shortly after 250 ms; with Release at 0 it is gone within 2 ms -- and
    // in neither case does the output step by more than a 1 ms ramp of the
    // level can, where a cut would step by the whole level.
    constexpr double rate = 48000.0;
    constexpr int samples = 48000;
    constexpr int off = 9600;

    EngineParameters parameters;
    parameters.kick1 = neutralKick();
    parameters.kick1.tuneHz = 100.0;
    parameters.kick1.decaySeconds = 2.0;

    const std::vector<Hit> events { { 0, 36, 1.0 }, { off, 36, -1.0 } };

    // ---- one-shot ----
    parameters.kick1.gate = false;
    {
        auto engine = heapEngine();
        engine->prepare (rate, 512);
        const auto out = render (parameters, rate, samples, events, 256, {}, engine.get());

        CHECK (engine->activeHitCount() == 1);
        CHECK (peakFrom (out, 43200) > 0.05);
    }

    // The body's own largest step, for the click bound below.
    const double bodyStep = maxStep (render (parameters, rate, samples, { { 0, 36, 1.0 } }));

    // ---- gated, 50 ms release ----
    parameters.kick1.gate = true;
    parameters.kick1.releaseSeconds = 0.05;
    {
        auto engine = heapEngine();
        engine->prepare (rate, 512);
        const auto out = render (parameters, rate, samples, events, 256, {}, engine.get());
        const int last = lastNonZeroSample (out);

        std::printf ("        [gate] release 50 ms: note-off at %.1f ms, last non-zero at %.1f ms, max step %.4f (body %.4f)\n",
                     1000.0 * off / rate, 1000.0 * last / rate, maxStep (out), bodyStep);

        CHECK (engine->activeHitCount() == 0);
        CHECK (last > off);
        CHECK (last < off + static_cast<int> (0.05 * rate) + 400);
        CHECK (maxStep (out) <= bodyStep + 1.0e-3);
    }

    // ---- gated, release 0: the 1 ms cut ----
    parameters.kick1.releaseSeconds = 0.0;
    {
        auto engine = heapEngine();
        engine->prepare (rate, 512);
        const auto out = render (parameters, rate, samples, events, 256, {}, engine.get());
        const int last = lastNonZeroSample (out);

        std::printf ("        [gate] release 0: last non-zero at %.2f ms after the note-off, max step %.4f\n",
                     1000.0 * (last - off) / rate, maxStep (out));

        CHECK (engine->activeHitCount() == 0);
        CHECK (last > off);
        CHECK (last < off + static_cast<int> (0.002 * rate) + 100);

        // A 1 ms ramp from full level steps 1/48 per host sample at most; a
        // cut would step by the level itself.
        CHECK (maxStep (out) <= bodyStep + 1.0 / (0.001 * rate) + 1.0e-3);
        CHECK (maxStep (out) < 0.1);
    }

    // ---- a note-off after the hit has landed changes nothing ----
    parameters.kick1.decaySeconds = 0.1;
    parameters.kick1.releaseSeconds = 0.05;
    {
        const auto plain = render (parameters, rate, 24000, { { 0, 36, 1.0 } });
        const auto late = render (parameters, rate, 24000, { { 0, 36, 1.0 }, { 12000, 36, -1.0 } });

        double worst = 0.0;
        for (std::size_t n = 0; n < plain.size(); ++n)
            worst = std::max (worst, std::abs (plain[n] - late[n]));

        CHECK (worst == 0.0);
    }
}

TEZLA_TEST (in_bass_mode_a_note_off_releases_only_the_hit_its_key_started)
{
    // A bass line played legato: C held, D pressed (the pad retriggers, C's
    // hit fades in the crossfade), C released -- D must play on -- then D
    // released, and the pad is silent. The gate keys on the note, not the
    // pad.
    constexpr double rate = 48000.0;
    constexpr int samples = 48000;

    EngineParameters parameters;
    parameters.kick1 = neutralKick();
    parameters.kick1.decaySeconds = 2.0;
    parameters.kick1.gate = true;
    parameters.kick1.releaseSeconds = 0.02;
    parameters.bassMode = true;

    auto engine = heapEngine();
    engine->prepare (rate, 512);

    const auto out = render (parameters, rate, samples,
                             { { 0, 36, 1.0 }, { 4800, 38, 1.0 }, { 7200, 36, -1.0 }, { 24000, 38, -1.0 } },
                             256, {}, engine.get());

    // D still sounding well after C's release...
    CHECK (peakFrom (out, 12000) > 0.05);

    // ...and everything gone after D's.
    const int last = lastNonZeroSample (out);
    CHECK (last > 24000);
    CHECK (last < 24000 + static_cast<int> (0.02 * rate) + 400);
    CHECK (engine->activeHitCount() == 0);
}

// ---------------------------------------------------------------------------
// I3: the snare
// ---------------------------------------------------------------------------

namespace
{
/// Wires 0, crack 0, no drop, body and level 1 with every velocity amount at
/// 0: the shell at unit gain and nothing else.
SnareSettings neutralSnare()
{
    SnareSettings s;
    s.tuneHz = 200.0;
    s.spread = 1.0;
    s.tone = 1.0;
    s.decaySeconds = 1.0;
    s.startSemitones = 0.0;
    s.body = 1.0;
    s.wires = 0.0;
    s.rattle = 0.0;
    s.crack = 0.0;
    s.crackNoise = 0.0;
    s.level = 1.0;
    s.velocityLevel = 0.0;
    s.velocityWires = 0.0;
    s.velocityCrack = 0.0;
    s.velocityDrop = 0.0;
    return s;
}

/// The snare engine on its own at an internal rate, stepped on the engine's
/// control grid exactly as the engine steps it: `seconds` of one hit.
std::vector<double> renderSnareEngine (SnareEngine& engine, const SnareSettings& s, double rate,
                                       double seconds, double hz = 200.0)
{
    engine.prepare (rate);
    engine.start (s, hz, 1.0, 99u, 0);

    const int total = static_cast<int> (seconds * rate);
    std::vector<double> out (static_cast<std::size_t> (total));

    for (int n = 0; n < total; ++n)
    {
        if (n % Engine::kControlIntervalSamples == 0)
            engine.advanceControl (Engine::kControlIntervalSamples);

        out[static_cast<std::size_t> (n)] = engine.process();
    }

    return out;
}

/// The frequency of the largest spectral peak between `lo` and `hi` Hz, from
/// a zero-padded FFT of the whole signal: 262144 bins, 0.18 Hz each at 48 k.
double peakHzBetween (const std::vector<double>& x, double rate, double lo, double hi)
{
    std::vector<double> padded (1u << 18, 0.0);
    const std::size_t count = std::min (x.size(), padded.size());

    for (std::size_t n = 0; n < count; ++n)
        padded[n] = x[n];

    const auto spectrum = fftOfReal (padded);
    const double binWidth = rate / static_cast<double> (padded.size());

    std::size_t best = 0;
    double bestPower = -1.0;

    const auto first = static_cast<std::size_t> (lo / binWidth);
    const auto last = std::min (static_cast<std::size_t> (hi / binWidth), spectrum.size() - 1);

    for (std::size_t bin = first; bin <= last; ++bin)
    {
        const double power = std::norm (spectrum[bin]);

        if (power > bestPower)
        {
            bestPower = power;
            best = bin;
        }
    }

    return static_cast<double> (best) * binWidth;
}

double rmsOfDifference (const std::vector<double>& x, const std::vector<double>& minus,
                        std::size_t from, std::size_t to)
{
    double sum = 0.0;

    for (std::size_t n = from; n < to; ++n)
    {
        const double d = x[n] - minus[n];
        sum += d * d;
    }

    return std::sqrt (sum / static_cast<double> (to - from));
}
} // namespace

TEZLA_TEST (the_snare_shell_rings_at_the_sos_mode_ratios)
{
    // Spread 1, Tone 1, no wires, no crack, no drop: the three modes at
    // 1 : 1.6 : 2.2 of a 200 Hz fundamental (Reid's measured snare, rounded;
    // docs/DSP-REFERENCES.md), read off a zero-padded FFT of the hit at
    // 0.18 Hz per bin, through the whole engine at Auto oversampling.
    constexpr double rate = 48000.0;

    EngineParameters parameters;
    parameters.snare1 = neutralSnare();

    const auto out = render (parameters, rate, 96000, { { 0, 38, 1.0 } });

    const double f0 = peakHzBetween (out, rate, 150.0, 250.0);
    const double f1 = peakHzBetween (out, rate, 280.0, 360.0);
    const double f2 = peakHzBetween (out, rate, 400.0, 480.0);

    std::printf ("        [snare modes] %.2f / %.2f / %.2f Hz -- ratios 1 : %.3f : %.3f\n",
                 f0, f1, f2, f1 / f0, f2 / f0);

    CHECK_NEAR (f0, 200.0, 0.4);
    CHECK_NEAR (f1 / f0, 1.6, 0.004);
    CHECK_NEAR (f2 / f0, 2.2, 0.004);
}

TEZLA_TEST (a_spread_zero_shell_is_one_tone_at_its_pitch_at_every_host_rate)
{
    // Spread 0 puts all three modes on the fundamental: one decaying sine.
    // Its period from zero crossings at 44.1 / 48 / 96 / 192 kHz (Auto
    // oversampling: x4, x4, x2, x1), cycles 2 to 100, against 200 Hz. The
    // resonator's pole is built from the internal rate, so the tone is the
    // same tone at every rate -- and this is the assertion that says so.
    EngineParameters parameters;
    parameters.snare1 = neutralSnare();
    parameters.snare1.spread = 0.0;
    parameters.snare1.decaySeconds = 2.0;

    for (const double rate : { 44100.0, 48000.0, 96000.0, 192000.0 })
    {
        const auto out = render (parameters, rate, static_cast<int> (rate), { { 0, 38, 1.0 } });
        const auto times = crossings (out, rate);

        CHECK (times.size() > 101);

        if (times.size() < 102)
            continue;

        double worst = 0.0;

        for (std::size_t k = 1; k < 100; ++k)
        {
            const double period = times[k + 1] - times[k];
            worst = std::max (worst, std::abs (1200.0 * std::log2 (200.0 * period)));
        }

        const double mean = 99.0 / (times[100] - times[1]);

        std::printf ("        [snare tone] %6.0f Hz host: mean %.4f Hz, worst single cycle %.3f cents\n",
                     rate, mean, worst);

        CHECK_NEAR (1200.0 * std::log2 (mean / 200.0), 0.0, 0.1);
        CHECK (worst < 1.0);
    }
}

TEZLA_TEST (the_snare_drop_retunes_the_bank_while_it_moves_and_never_once_it_has_landed)
{
    // The plan's cost claim: `setMode` per control chunk while the
    // TensionDrop moves, one last time as it snaps to exactly 1.0, then
    // never again -- the landed drum costs no transcendentals. The drop
    // moves past its stated 50 ms landing (where 0.67% of the depth is
    // left) until the remaining cents fall under `kSnapCents`: from 12 st
    // that is 50 ms / 5 * ln (1200 / 0.01) = 117 ms, or 702 chunks of 32
    // samples at 192 kHz, plus the strike's own and the landing's.
    constexpr double rate = 192000.0;

    SnareSettings s = neutralSnare();
    s.startSemitones = 12.0;
    s.dropSeconds = 0.05;
    s.decaySeconds = 2.0;

    const double snapSeconds = s.dropSeconds / TensionDrop::kLandFactor
                             * std::log (100.0 * s.startSemitones / TensionDrop::kSnapCents);
    const int expected = static_cast<int> (std::ceil (snapSeconds * rate / Engine::kControlIntervalSamples));

    SnareEngine engine;
    engine.prepare (rate);
    engine.start (s, 200.0, 1.0, 7u, 0);

    double sink = 0.0;

    const auto run = [&] (double seconds)
    {
        const int total = static_cast<int> (seconds * rate);

        for (int n = 0; n < total; ++n)
        {
            if (n % Engine::kControlIntervalSamples == 0)
                engine.advanceControl (Engine::kControlIntervalSamples);

            sink += engine.process();
        }
    };

    run (0.02);
    const int during = engine.getRetuneCount();
    const double duringHz = engine.currentHz();

    run (0.58);
    const int landed = engine.getRetuneCount();
    const double landedHz = engine.currentHz();

    run (0.5);
    const int later = engine.getRetuneCount();

    std::printf ("        [snare drop] retunes: %d in the first 20 ms (at %.2f Hz), %d by 600 ms, %d by 1.1 s; "
                 "the snap predicts %d chunks; landed on %.9g Hz (sink %g)\n",
                 during, duringHz, landed, later, expected, landedHz, sink);

    CHECK (during > 100);
    CHECK (duringHz > 200.0);
    CHECK (landed >= expected);
    CHECK (landed <= expected + 3);
    CHECK (later == landed);
    CHECK (isExactly (landedHz, 200.0));
}

TEZLA_TEST (rattle_at_zero_is_exact_and_at_one_drives_the_wires_from_the_shell)
{
    // Body and Wires are a sum with nothing between them at Rattle 0: the
    // hit with both is, bit for bit, the shell alone plus the wires alone --
    // so the shell's motion cannot be leaking into the wires -- and the
    // follower reads exactly 0.0 because it was never evaluated. At Rattle 1
    // the same wires are louder while the shell is loud, and they are still
    // there at 100 ms, following the shell, where the stick's own 50 ms
    // burst has landed and the unrattled wires are exactly over.
    constexpr double rate = 192000.0;
    constexpr double seconds = 0.4;

    SnareSettings s = neutralSnare();
    s.decaySeconds = 0.3;
    s.wires = 1.0;
    s.snappyHz = 3000.0;
    s.wiresDecaySeconds = 0.05;

    SnareEngine engine;

    const auto both = renderSnareEngine (engine, s, rate, seconds);

    auto shellOnly = s;
    shellOnly.wires = 0.0;
    const auto shell = renderSnareEngine (engine, shellOnly, rate, seconds);

    auto wiresOnly = s;
    wiresOnly.body = 0.0;
    const auto wires = renderSnareEngine (engine, wiresOnly, rate, seconds);

    std::size_t mismatches = 0;

    for (std::size_t n = 0; n < both.size(); ++n)
        if (! isExactly (both[n], shell[n] + wires[n]))
            ++mismatches;

    CHECK (mismatches == 0);

    // The follower, read mid-hit -- before the hit retires and resets it.
    const auto followerAfter = [&] (const SnareSettings& settings)
    {
        SnareEngine probe;
        probe.prepare (rate);
        probe.start (settings, 200.0, 1.0, 99u, 0);

        for (int n = 0; n < 4000; ++n)
        {
            if (n % Engine::kControlIntervalSamples == 0)
                probe.advanceControl (Engine::kControlIntervalSamples);

            (void) probe.process();
        }

        return probe.getFollowerLevel();
    };

    auto rattled = s;
    rattled.rattle = 1.0;

    const double followerOff = followerAfter (s);
    const double followerOn = followerAfter (rattled);

    CHECK (isExactlyZero (followerOff));
    CHECK (followerOn > 0.0);

    // The wires at Rattle 1 against Rattle 0 (the shell subtracted from
    // both): over the first 20 ms, where the shell is loud; and over
    // 100-120 ms, where the 50 ms burst is exactly over and only the shell,
    // 20 dB down by then, is driving them.
    const auto c = renderSnareEngine (engine, rattled, rate, seconds);

    const auto early = static_cast<std::size_t> (0.02 * rate);
    const auto late0 = static_cast<std::size_t> (0.10 * rate);
    const auto late1 = static_cast<std::size_t> (0.12 * rate);

    const double plainEarly = rmsOfDifference (both, shell, 0, early);
    const double rattledEarly = rmsOfDifference (c, shell, 0, early);
    const double plainLate = rmsOfDifference (both, shell, late0, late1);
    const double rattledLate = rmsOfDifference (c, shell, late0, late1);

    std::printf ("        [rattle] wires at rattle 1 against 0: x%.3f over the first 20 ms; at 100 ms %.1f dB re "
                 "their start where the plain wires are %s; follower %.4f (rattle 0: %g)\n",
                 rattledEarly / plainEarly, 20.0 * std::log10 (rattledLate / rattledEarly),
                 isExactlyZero (plainLate) ? "exactly over" : "still sounding", followerOn, followerOff);

    CHECK (rattledEarly / plainEarly > 1.3);
    CHECK (isExactlyZero (plainLate));
    CHECK (rattledLate > 0.01 * rattledEarly);
}

TEZLA_TEST (a_snare_hit_retires_exactly_and_leaves_exact_zeros)
{
    // Everything on: the shell is cut once it is 120 dB down (a 0.3 s T60
    // gets there at 0.6 s), the wires' envelope is killed as it lands, the
    // crack is over in 12 ms -- and the activity count says 0, not merely
    // the output.
    constexpr double rate = 48000.0;

    EngineParameters parameters;
    parameters.snare1 = snareEverythingOn();

    auto engine = heapEngine();
    engine->prepare (rate, 512);

    const auto out = render (parameters, rate, 96000, { { 0, 38, 1.0 } }, 256, {}, engine.get());
    const int last = lastNonZeroSample (out);

    std::printf ("        [snare retire] last non-zero sample at %.3f s (%.2e), active hits %d\n",
                 last / rate, std::abs (out[static_cast<std::size_t> (last)]), engine->activeHitCount());

    CHECK (engine->activeHitCount() == 0);
    CHECK (last > 4800);
    CHECK (last < 48000);

    // The cut lands at the floor, not on an audible ring.
    CHECK (std::abs (out[static_cast<std::size_t> (last)]) < 1.0e-5);
}

TEZLA_TEST (a_gated_snare_fades_out_at_note_off_and_a_one_shot_rings_on)
{
    // A tom with a 2 s ring, note-off at 100 ms: the one-shot pad plays on;
    // gated with a 50 ms release it is exactly silent shortly after 150 ms
    // with no step larger than the strike's own; with Release at 0 it is
    // gone within 2 ms, ramped rather than cut.
    constexpr double rate = 48000.0;
    constexpr int off = 4800;

    EngineParameters parameters;
    parameters.snare1 = neutralSnare();
    parameters.snare1.decaySeconds = 2.0;
    parameters.snare1.wires = 0.5;
    parameters.snare1.wiresDecaySeconds = 0.4;

    const std::vector<Hit> events { { 0, 38, 1.0 }, { off, 38, -1.0 } };

    // ---- one-shot ----
    parameters.snare1.gate = false;
    {
        auto engine = heapEngine();
        engine->prepare (rate, 512);
        const auto out = render (parameters, rate, 48000, events, 256, {}, engine.get());

        CHECK (engine->activeHitCount() == 1);
        CHECK (peakFrom (out, 24000) > 0.01);
    }

    const double bodyStep = maxStep (render (parameters, rate, 24000, { { 0, 38, 1.0 } }));

    // ---- gated, 50 ms release ----
    parameters.snare1.gate = true;
    parameters.snare1.releaseSeconds = 0.05;
    {
        auto engine = heapEngine();
        engine->prepare (rate, 512);
        const auto out = render (parameters, rate, 48000, events, 256, {}, engine.get());
        const int last = lastNonZeroSample (out);

        std::printf ("        [snare gate] release 50 ms: last non-zero at %.1f ms, max step %.4f (strike %.4f)\n",
                     1000.0 * last / rate, maxStep (out), bodyStep);

        CHECK (engine->activeHitCount() == 0);
        CHECK (last > off);
        CHECK (last < off + static_cast<int> (0.05 * rate) + 400);
        CHECK (maxStep (out) <= bodyStep + 1.0e-3);
    }

    // ---- gated, release 0: the 1 ms ramp ----
    parameters.snare1.releaseSeconds = 0.0;
    {
        auto engine = heapEngine();
        engine->prepare (rate, 512);
        const auto out = render (parameters, rate, 48000, events, 256, {}, engine.get());
        const int last = lastNonZeroSample (out);

        std::printf ("        [snare gate] release 0: last non-zero at %.2f ms after the note-off, max step %.4f\n",
                     1000.0 * (last - off) / rate, maxStep (out));

        CHECK (engine->activeHitCount() == 0);
        CHECK (last > off);
        CHECK (last < off + static_cast<int> (0.002 * rate) + 100);
        CHECK (maxStep (out) <= bodyStep + 1.0 / (0.001 * rate) + 1.0e-3);
    }
}

// ---------------------------------------------------------------------------
// Note snap: the drums in the key of the bass line
// ---------------------------------------------------------------------------

namespace
{
/// The mean frequency over cycles 3..N from the zero crossings.
double meanHzOfCycles (const std::vector<double>& out, double rate, std::size_t cycles)
{
    const auto times = crossings (out, rate);

    if (times.size() < cycles + 3)
        return 0.0;

    return static_cast<double> (cycles - 2) / (times[cycles] - times[2]);
}
} // namespace

TEZLA_TEST (note_snap_lands_the_drums_on_the_nearest_degree_of_the_tuning)
{
    // Tune 52 Hz with Note lit lands on G#1, 51.913 Hz -- the nearest note
    // of 12-TET at A4 = 440 -- and the neutral snare at 205 Hz on G#3,
    // 207.652 Hz; with Note dark they land where Tune says. With a five-tone
    // scale swapped in, the kick lands on that scale's nearest degree
    // instead, which is not a keyboard note at all: the snap is the
    // tuning's, so a drum snapped in a microtuned project sits in it.
    constexpr double rate = 48000.0;

    EngineParameters parameters;
    parameters.kick1 = neutralKick();
    parameters.kick1.tuneHz = 52.0;
    parameters.kick1.decaySeconds = 0.8;
    parameters.snare1 = neutralSnare();
    parameters.snare1.spread = 0.0;
    parameters.snare1.tuneHz = 205.0;
    parameters.snare1.decaySeconds = 2.0;

    const auto cents = [] (double measured, double expected)
    {
        return 1200.0 * std::log2 (measured / expected);
    };

    // ---- dark: Tune as set ----
    {
        const double kick = meanHzOfCycles (render (parameters, rate, 24000, { { 0, 36, 1.0 } }), rate, 20);
        const double snare = meanHzOfCycles (render (parameters, rate, 24000, { { 0, 38, 1.0 } }), rate, 60);

        CHECK_NEAR (cents (kick, 52.0), 0.0, 0.5);
        CHECK_NEAR (cents (snare, 205.0), 0.0, 0.5);
    }

    // ---- lit: the nearest 12-TET note ----
    parameters.kick1.noteSnap = true;
    parameters.snare1.noteSnap = true;

    const double kickSnapped = meanHzOfCycles (render (parameters, rate, 24000, { { 0, 36, 1.0 } }), rate, 20);
    const double snareSnapped = meanHzOfCycles (render (parameters, rate, 24000, { { 0, 38, 1.0 } }), rate, 60);

    std::printf ("        [note snap] kick 52 Hz -> %.3f Hz (G#1 is 51.913), snare 205 Hz -> %.3f Hz (G#3 is 207.652)\n",
                 kickSnapped, snareSnapped);

    CHECK_NEAR (cents (kickSnapped, 51.9131), 0.0, 0.5);
    CHECK_NEAR (cents (snareSnapped, 207.6523), 0.0, 0.5);

    // ---- a five-tone scale: the snap follows the tuning ----
    {
        auto engine = heapEngine();
        engine->prepare (rate, 512);

        auto scale = scales::fiveToneEqual();
        engine->swapScale (scale);

        const double expected = engine->tuning().nearestScaleHz (52.0);
        const double kick = meanHzOfCycles (render (parameters, rate, 24000, { { 0, 36, 1.0 } }, 256, {}, engine.get()), rate, 20);

        std::printf ("        [note snap] 5-TET: kick 52 Hz -> %.3f Hz (the scale's nearest degree is %.3f)\n", kick, expected);

        CHECK_NEAR (cents (kick, expected), 0.0, 0.5);
        CHECK (std::abs (cents (kick, 51.9131)) > 10.0);
    }
}
