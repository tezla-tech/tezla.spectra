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
#include <tezla/dsp/Correlation.hpp>
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
    // the same bits -- with every pad sounding, a retrigger, a hat choke, and
    // every stage engaged (the snare's drop retuning its bank per chunk, the
    // rattle, the wires, the crack, the hat's six pulses and the clap's
    // burst pattern, which counts in samples and must not notice a block).
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
    parameters.hat = HatSettings {};
    parameters.hat.spread = 0.5;
    parameters.hat.air = 0.3;
    parameters.hat.harmonics = 1.4;
    parameters.clap = ClapSettings {};
    parameters.masterDb = -3.0;

    const std::vector<Hit> hits { { 0, 36, 0.9 }, { 1000, 38, 1.0 }, { 2500, 35, 0.7 },
                                  { 3100, 37, 0.8 }, { 4000, 36, 1.0 }, { 4700, 38, 0.6 },
                                  // the hats: open, then closed to choke it, then the clap
                                  { 1500, 46, 0.8 }, { 2200, 42, 1.0 }, { 3600, 42, 0.5 },
                                  { 5200, 39, 0.9 } };

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

TEZLA_TEST (a_kit_of_all_eight_pads_fits_its_budget_and_idles_for_nothing)
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
    parameters.hat = HatSettings {};
    parameters.hat.spread = 0.5;
    parameters.hat.air = 0.4;
    parameters.clap = ClapSettings {};

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

        // Sixteenth-note hats -- the busiest pad in a break -- an open hat
        // every bar, and a clap on the backbeat.
        if (b % 3 == 0)
            engine->noteOn (42, 0.8);

        if (b % 24 == 18)
            engine->noteOn (46, 0.9);

        if (b % 12 == 3)
            engine->noteOn (39, 0.85);

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

    std::printf ("        [engine cpu] all eight pads at 48 kHz x%d: %.2f%% of a core; idle %.3f%% (sink %g)\n",
                 engine->getOversamplingFactor(), 100.0 * activeSeconds, 100.0 * idleSeconds, sink);

    // Raised from 0.12 when the hats and the clap joined: the hats are six
    // band-limited pulses through three filters and they play sixteenths, so
    // they are the most expensive pad in the kit. Measured 2026-09-03 at
    // 6.04 % of a core against the 4.8-5.2 % the five-pad kit cost, and the
    // budget keeps the same two-and-a-half times' headroom the container's
    // noise needs. The figure is printed above on every run.
    CHECK_CPU_BUDGET (activeSeconds, 0.15, "all eight pads, everything on, 48 kHz x4");
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

// ---------------------------------------------------------------------------
// The hats and the clap (I4)
// ---------------------------------------------------------------------------

namespace
{
/// A hat with everything doing something: spread, air and both velocity
/// amounts live.
HatSettings hatEverythingOn()
{
    HatSettings s;
    s.tuneHz = 205.3;
    s.harmonics = 0.0;
    s.spread = 0.6;
    s.ring = 0.5;
    s.drive = 0.4;
    s.air = 0.5;
    s.airToneHz = 5000.0;
    s.airDecay = 1.2;
    s.sizzle = 0.7;
    s.colourHz = 3440.0;
    s.width = 0.5;
    s.highpassHz = 1200.0;
    s.damp = 0.5;
    s.strike = 0.5;
    s.level = 1.0;
    return s;
}

/// Nothing but the six pulses: no ring, no drive, no noise, no damping and
/// no stick, so a test can isolate one of them by turning it back on.
HatSettings bareHat()
{
    HatSettings s;
    s.spread = 0.0;
    s.ring = 0.0;
    s.drive = 0.0;
    s.air = 0.0;
    s.sizzle = 0.0;
    s.damp = 0.0;
    s.strike = 0.0;
    s.level = 1.0;
    s.velocityLevel = 0.0;
    s.velocityDecay = 0.0;
    s.velocityColour = 0.0;
    s.velocityStrike = 0.0;
    return s;
}

/// The hat engine on its own at an internal rate, stepped on the engine's
/// control grid: `seconds` of one hit.
std::vector<double> renderHatEngine (HatEngine& engine, const HatSettings& s, double rate,
                                     double seconds, bool open = false, double velocity = 1.0,
                                     std::uint64_t seed = 99u)
{
    engine.prepare (rate);
    engine.start (s, open, velocity, seed, 0);

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

std::vector<double> renderClapEngine (ClapEngine& engine, const ClapSettings& s, double rate,
                                      double seconds, std::uint64_t seed = 99u)
{
    engine.prepare (rate);
    engine.start (s, 1.0, seed, 0);

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

/// The first `seconds` of a signal, windowed over exactly that many samples
/// and zero-padded to a power of two.
///
/// The window has to cover the signal and nothing else. Windowing a fixed
/// 65536-sample buffer instead puts a 44.1 kHz hit in the taper's foot and a
/// 192 kHz one across its crown, and the two then differ by the window rather
/// than by the audio -- which is what made the first version of the
/// rate-independence test below read a factor of 345.
///
/// Blackman-Harris rather than Hann, and that is the difference between a
/// measurement and a decoration. A pulse bank puts a thousand strong
/// harmonics in the band; Hann's first sidelobe is only 31 dB down, so the
/// skirts of those harmonics pile up in every bin between them and the
/// aliasing floor read -57 dB no matter how good the generator was. This
/// window's sidelobes are 92 dB down, and the same bank then reads -95.
struct WindowedSignal
{
    std::vector<double> samples;   ///< the padded, windowed buffer
    std::size_t filled { 0 };      ///< how many of them carried signal
};

WindowedSignal windowFirst (const std::vector<double>& x, double rate, double seconds)
{
    const std::size_t n = std::min (x.size(), static_cast<std::size_t> (seconds * rate));

    std::size_t size = 1;
    while (size < n)
        size <<= 1;

    WindowedSignal out;
    out.samples.assign (size, 0.0);
    out.filled = n;

    for (std::size_t i = 0; i < n; ++i)
    {
        const double phase = 2.0 * std::numbers::pi * static_cast<double> (i)
                           / static_cast<double> (n - 1);

        const double w = 0.35875 - 0.48829 * std::cos (phase)
                       + 0.14128 * std::cos (2.0 * phase)
                       - 0.01168 * std::cos (3.0 * phase);

        out.samples[i] = x[i] * w;
    }

    return out;
}

/// The energy in bins that no partial's harmonic series can explain, in dB
/// below the whole signal's, counting only up to `maxHz`.
///
/// The bank's six partials are incommensurate, so their harmonics are a
/// sparse set even taken together -- a few thousand bins out of 32768 -- and
/// what is left over is folded-back. Hann-windowed, because a rectangular
/// window's leakage would be louder than the thing being measured.
double inharmonicFloorDb (const std::vector<double>& x, double rate,
                          const double* partials, int count, double maxHz)
{
    const auto windowed = windowFirst (x, rate, 1.2);
    const auto spectrum = fftOfReal (windowed.samples);
    const double binWidth = rate / static_cast<double> (windowed.samples.size());

    // fftOfReal returns the whole transform, so the upper half is the mirror
    // of the lower and counting it would put exactly half the energy in
    // "unexplained" whatever the signal is -- a measurement that reads -3 dB
    // for everything, which is what it did before this line existed.
    const std::size_t bins = windowed.samples.size() / 2;
    const auto last = std::min (static_cast<std::size_t> (maxHz / binWidth), bins);

    std::vector<bool> expected (bins + 1, false);

    // A Blackman-Harris main lobe is eight bins wide; +-8 covers it, so a
    // partial's own leakage is never counted as aliasing.
    for (int p = 0; p < count; ++p)
    {
        for (double hz = partials[p]; hz < rate * 0.5; hz += partials[p])
        {
            const auto centre = static_cast<long> (std::lround (hz / binWidth));

            for (long bin = centre - 8; bin <= centre + 8; ++bin)
                if (bin >= 0 && bin <= static_cast<long> (bins))
                    expected[static_cast<std::size_t> (bin)] = true;
        }
    }

    // DC and the first few bins are the window's own, not the signal's.
    for (std::size_t bin = 0; bin < 8 && bin < expected.size(); ++bin)
        expected[bin] = true;

    double total = 0.0;
    double inharmonic = 0.0;

    for (std::size_t bin = 0; bin <= last; ++bin)
    {
        const double power = std::norm (spectrum[bin]);
        total += power;

        if (! expected[bin])
            inharmonic += power;
    }

    return 10.0 * std::log10 (std::max (inharmonic, 1.0e-300) / std::max (total, 1.0e-300));
}

/// A bank of `HatEngine::kOscillators` pulses at `partials`, either
/// band-limited (`dsp::Oscillator`, polyBLEP) or naive.
std::vector<double> pulseBank (const double* partials, double rate, int samples, bool bandLimited)
{
    std::vector<double> out (static_cast<std::size_t> (samples), 0.0);

    if (bandLimited)
    {
        Oscillator oscillators[HatEngine::kOscillators];

        for (int i = 0; i < HatEngine::kOscillators; ++i)
        {
            oscillators[i].setShape (OscShape::pulse);
            oscillators[i].setWidth (HatEngine::kDutyCycle);
            oscillators[i].reset (0.0);
            oscillators[i].setIncrement (partials[i] / rate);
        }

        for (int n = 0; n < samples; ++n)
        {
            double sum = 0.0;

            for (auto& oscillator : oscillators)
                sum += oscillator.advance();

            out[static_cast<std::size_t> (n)] = sum / HatEngine::kOscillators;
        }

        return out;
    }

    double phase[HatEngine::kOscillators] {};

    for (int n = 0; n < samples; ++n)
    {
        double sum = 0.0;

        for (int i = 0; i < HatEngine::kOscillators; ++i)
        {
            sum += Oscillator::naiveShapeSample (OscShape::pulse, phase[i],
                                                 HatEngine::kDutyCycle, 0.0);
            phase[i] += partials[i] / rate;
            phase[i] -= std::floor (phase[i]);
        }

        out[static_cast<std::size_t> (n)] = sum / HatEngine::kOscillators;
    }

    return out;
}

/// The spectral centroid of a signal, in Hz -- where its weight sits, which
/// is what "the same metal at a different host rate" has to agree about.
double centroidHz (const std::vector<double>& x, double rate, double seconds)
{
    const auto windowed = windowFirst (x, rate, seconds);
    const auto spectrum = fftOfReal (windowed.samples);
    const double binWidth = rate / static_cast<double> (windowed.samples.size());

    double weighted = 0.0;
    double total = 0.0;

    // Only the audible band: above 20 kHz the four rates legitimately differ,
    // since 44.1 k has no such band to put anything in.
    const auto last = std::min (static_cast<std::size_t> (20000.0 / binWidth),
                                windowed.samples.size() / 2);

    for (std::size_t bin = 1; bin <= last; ++bin)
    {
        const double magnitude = std::abs (spectrum[bin]);
        weighted += magnitude * static_cast<double> (bin) * binWidth;
        total += magnitude;
    }

    return total > 0.0 ? weighted / total : 0.0;
}

/// The energy below 20 kHz, from the same windowed transform as the
/// centroid. Compared across host rates rather than the time-domain energy,
/// because 44.1 kHz has no band above 22 kHz to put anything in and counting
/// what 192 kHz puts there would be comparing two different questions.
double audibleEnergy (const std::vector<double>& x, double rate, double seconds)
{
    const auto windowed = windowFirst (x, rate, seconds);
    const auto spectrum = fftOfReal (windowed.samples);

    const double size = static_cast<double> (windowed.samples.size());
    const double binWidth = rate / size;
    const auto last = std::min (static_cast<std::size_t> (20000.0 / binWidth),
                                windowed.samples.size() / 2);

    double sum = 0.0;

    for (std::size_t bin = 1; bin <= last; ++bin)
        sum += std::norm (spectrum[bin]);

    // Parseval, for the unnormalised transform this tree uses: the sum of
    // |X|^2 over the whole transform is `size` times the sum of x^2. Halved
    // spectrum, so doubled; then per signal sample, so the answer is a mean
    // square and not a length.
    return 2.0 * sum / (size * static_cast<double> (windowed.filled));
}

/// Where each burst landed, in the first `withinSeconds` of the signal.
///
/// A clap is noise, so the sample-by-sample magnitude is useless as an
/// envelope -- a peak-hold on it found twenty-five "onsets" in four bursts,
/// which is what the first version of this did. What is stable is a moving
/// RMS: over a millisecond the noise averages out and the burst pattern is
/// what is left. Each burst then falls 17 dB per millisecond, so between two
/// bursts the level collapses completely and an upward threshold crossing
/// with hysteresis is unambiguous.
///
/// The window is the burst pattern's own -- the caller passes a little more
/// than the pattern's length. The tail that follows is a different question,
/// asked by a different test: it is band-passed noise at Q 1, so a
/// millisecond of it holds about one independent sample and its moving RMS
/// swings by tens of per cent. No level or slope test tells that apart from
/// a burst, and pretending otherwise would be a detector tuned until it
/// agreed.
std::vector<int> burstOnsets (const std::vector<double>& x, double rate, double withinSeconds)
{
    const auto window = static_cast<std::size_t> (0.001 * rate);
    const auto count = std::min (x.size(), static_cast<std::size_t> (withinSeconds * rate));

    std::vector<double> envelope (count, 0.0);
    double sum = 0.0;

    for (std::size_t n = 0; n < count; ++n)
    {
        sum += x[n] * x[n];

        if (n >= window)
            sum -= x[n - window] * x[n - window];

        // Clamped, because adding and subtracting a hundred thousand squares
        // leaves a residue that can be negative -- and sqrt of that is a NaN
        // that compares false against every threshold, so every later sample
        // reads as an onset. It did: 131 of them.
        envelope[n] = std::sqrt (std::max (sum, 0.0) / static_cast<double> (std::min (n + 1, window)));
    }

    double peak = 0.0;

    for (const double value : envelope)
        peak = std::max (peak, value);

    const double threshold = 0.15 * peak;
    const double rearm = 0.5 * threshold;

    std::vector<int> onsets;
    bool above = false;

    for (std::size_t n = 0; n < envelope.size(); ++n)
    {
        if (! above && envelope[n] >= threshold)
        {
            above = true;
            onsets.push_back (static_cast<int> (n));
        }
        else if (above && envelope[n] < rearm)
        {
            above = false;
        }
    }

    return onsets;
}
} // namespace

TEZLA_TEST (the_hat_ratio_sets_morph_geometrically_and_are_exact_at_a_set)
{
    // A set's own numbers, bit for bit, at every integer position: the morph
    // is branched out there rather than computed and rounded, so choosing a
    // set is choosing exactly what the paper (or the design) says.
    for (int set = 0; set < HatEngine::kSetCount; ++set)
    {
        double ratios[HatEngine::kOscillators] {};
        HatEngine::ratiosAt (static_cast<double> (set), ratios);

        for (int i = 0; i < HatEngine::kOscillators; ++i)
            CHECK (isExactly (ratios[i], HatEngine::kSets[set][i]));
    }

    // Halfway between two sets is the geometric mean, rank by rank: halfway
    // between 1.5 and 3.0 is an octave's midpoint, 2.121, not 2.25.
    double half[HatEngine::kOscillators] {};
    HatEngine::ratiosAt (1.5, half);

    for (int i = 0; i < HatEngine::kOscillators; ++i)
    {
        const double geometric = std::sqrt (HatEngine::kSets[1][i] * HatEngine::kSets[2][i]);
        CHECK (std::abs (half[i] - geometric) < 1.0e-12);
    }

    // Continuity across a set boundary: a step of one part in a thousand
    // moves no ratio by more than a thousandth of that rank's whole journey.
    for (double position = 0.0; position < HatEngine::kSetCount - 1; position += 0.25)
    {
        double before[HatEngine::kOscillators] {};
        double after[HatEngine::kOscillators] {};

        HatEngine::ratiosAt (position, before);
        HatEngine::ratiosAt (position + 0.001, after);

        const int lower = std::min (static_cast<int> (position), HatEngine::kSetCount - 2);

        for (int i = 0; i < HatEngine::kOscillators; ++i)
        {
            const double journey = std::abs (std::log2 (HatEngine::kSets[lower + 1][i]
                                                        / HatEngine::kSets[lower][i]));
            const double moved = std::abs (std::log2 (after[i] / before[i]));

            CHECK (moved <= journey * 0.0011 + 1.0e-12);
        }
    }

    // Past the last set the position clamps: every value from the last set
    // to the control's top is that set, so appending a set later gives those
    // positions a meaning without moving anything already saved.
    for (double position : { 3.0, 4.0, 5.5, 7.0, 9.0 })
    {
        double ratios[HatEngine::kOscillators] {};
        HatEngine::ratiosAt (position, ratios);

        for (int i = 0; i < HatEngine::kOscillators; ++i)
            CHECK (isExactly (ratios[i], HatEngine::kSets[HatEngine::kSetCount - 1][i]));
    }
}

TEZLA_TEST (the_hat_partials_sit_at_tune_times_the_sets_ratios)
{
    HatEngine engine;
    engine.prepare (192000.0);

    HatSettings s;
    s.tuneHz = 300.0;
    s.harmonics = 0.0;
    s.spread = 0.0;
    s.air = 0.0;

    engine.start (s, false, 1.0, 1u, 0);

    // Spread 0 is a branch, not a multiplication by exp2(0): the partials are
    // exactly Tune times the ratio.
    for (int i = 0; i < HatEngine::kOscillators; ++i)
        CHECK (isExactly (engine.getPartialHz (i), 300.0 * HatEngine::kSets[0][i]));

    // Spread pulls them apart along the fixed pattern, and the pattern sums
    // to zero so the set's centre of gravity in log frequency does not move.
    s.spread = 1.0;
    engine.start (s, false, 1.0, 1u, 0);

    double sumCents = 0.0;

    for (int i = 0; i < HatEngine::kOscillators; ++i)
    {
        const double nominal = 300.0 * HatEngine::kSets[0][i];
        const double cents = 1200.0 * std::log2 (engine.getPartialHz (i) / nominal);
        const double wanted = 100.0 * HatEngine::kSpreadSemitones * HatEngine::kSpreadPattern[i];

        CHECK (std::abs (cents - wanted) < 1.0e-9);
        sumCents += cents;
    }

    CHECK (std::abs (sumCents) < 1.0e-9);
}

TEZLA_TEST (the_hats_pulse_bank_is_band_limited_where_a_naive_one_folds)
{
    // The measurement CLAUDE.md section 7 asks for: inharmonic energy in the
    // AUDIBLE band, since that is where a fold-back is a defect. Two rates,
    // because they answer two different questions:
    //
    //   192 kHz  the rate the instrument actually generates at (Auto lands
    //            there from every host rate), and the number that has to
    //            clear the -60 dB gate.
    //   48 kHz   what choosing oversampling Off costs, and the contrast with
    //            a naive pulse bank that says the polyBLEP is doing the work.
    //
    // Tune 900 Hz: high enough that every set's top partial has harmonics
    // above Nyquist to fold at 48 kHz.
    constexpr double tune = 900.0;
    constexpr double seconds = 1.3;

    static const char* names[HatEngine::kSetCount] { "Metal", "Bell", "Trash", "Wide" };

    for (int set = 0; set < HatEngine::kSetCount; ++set)
    {
        double partials[HatEngine::kOscillators] {};

        for (int i = 0; i < HatEngine::kOscillators; ++i)
            partials[i] = tune * HatEngine::kSets[set][i];

        const auto internalRate = 192000.0;
        const auto internal = pulseBank (partials, internalRate,
                                         static_cast<int> (seconds * internalRate), true);
        const double internalDb = inharmonicFloorDb (internal, internalRate, partials,
                                                     HatEngine::kOscillators, 20000.0);

        const auto hostRate = 48000.0;
        const auto limited = pulseBank (partials, hostRate,
                                        static_cast<int> (seconds * hostRate), true);
        const auto naive = pulseBank (partials, hostRate,
                                      static_cast<int> (seconds * hostRate), false);

        const double limitedDb = inharmonicFloorDb (limited, hostRate, partials,
                                                    HatEngine::kOscillators, 20000.0);
        const double naiveDb = inharmonicFloorDb (naive, hostRate, partials,
                                                  HatEngine::kOscillators, 20000.0);

        std::printf ("        [hat alias] %-6s at %.0f Hz, audible band: 192k polyBLEP %.1f dB; 48k polyBLEP %.1f dB, naive %.1f dB (%.1f dB better)\n",
                     names[set], tune, internalDb, limitedDb, naiveDb, naiveDb - limitedDb);

        // Measured 2026-09-03. At the internal rate the four sets read
        // -77.2, -77.4, -76.5 and -74.2 dB, so the hat clears section 7's
        // -60 dB gate with 14 dB to spare. At 48 kHz with oversampling Off
        // they read -35.5, -35.5, -33.7 and -35.5 against a naive bank's
        // -15.9, -17.3, -14.6 and -12.3: the polyBLEP is worth 18 to 23 dB
        // there, and the remaining 35 dB is what Off costs -- which is why
        // Auto is the default and the tooltip says so.
        CHECK (internalDb < -70.0);
        CHECK (naiveDb - limitedDb > 15.0);
    }
}

TEZLA_TEST (a_hat_is_the_same_metal_at_every_host_rate)
{
    // CLAUDE.md section 6: the instrument must sound the same at 44.1, 48, 96
    // and 192 kHz. Through the WHOLE instrument with Auto oversampling, which
    // is what the policy promises: Auto lands the internal rate near 176-192
    // kHz from every host rate, so the two band-passes sit far below Fs/8 and
    // the bilinear warping section 6 warns about never gets near them.
    //
    // Rendering the hat engine at the raw host rate instead -- which is what
    // choosing oversampling Off does -- moves the upper band-pass at 7.1 kHz
    // above 44100/8 and the hit gets 21 % louder, measured. That is the
    // warping, not a bug, and it is why the hat lives inside the oversampled
    // section.
    EngineParameters parameters;
    parameters.hat = hatEverythingOn();
    parameters.hat.decayOpenSeconds = 0.35;
    parameters.oversampling = OversamplingMode::Auto;

    double centroids[4] {};
    double energies[4] {};
    int index = 0;

    for (const double rate : { 44100.0, 48000.0, 96000.0, 192000.0 })
    {
        const auto out = render (parameters, rate, static_cast<int> (0.5 * rate),
                                 { { 0, 46, 1.0 } });

        centroids[index] = centroidHz (out, rate, 0.5);
        energies[index] = audibleEnergy (out, rate, 0.5);
        ++index;
    }

    std::printf ("        [hat rates] centroid %.0f / %.0f / %.0f / %.0f Hz; audible energy %.4g / %.4g / %.4g / %.4g\n",
                 centroids[0], centroids[1], centroids[2], centroids[3],
                 energies[0], energies[1], energies[2], energies[3]);

    // Measured 2026-09-03 through the instrument at Auto (x4, x4, x2, x1).
    // They cannot be bit-identical -- three different internal rates and
    // three different decimators -- but the metal must not drift.
    for (int i = 1; i < 4; ++i)
    {
        CHECK (std::abs (centroids[i] - centroids[0]) / centroids[0] < 0.05);
        CHECK (std::abs (energies[i] - energies[0]) / energies[0] < 0.08);
    }
}

TEZLA_TEST (a_hat_with_air_at_zero_is_the_same_hit_whatever_its_seed)
{
    // What this proves is reproducibility, and the distinction cost a
    // break-check to find: skipping the noise draw at Air 0 is a COST branch,
    // not an exactness one. `0.0 * bipolar()` is already exactly 0 for any
    // finite draw, so removing the branch changes nothing in the output and
    // the test stayed green when it was removed. What the test does catch is
    // the metal itself becoming seed-dependent -- an oscillator started from
    // a hashed phase, say -- which would make two hits of a hat with no noise
    // in it different, and a drum machine's hats have to repeat.
    HatSettings s = hatEverythingOn();
    s.air = 0.0;

    HatEngine a, b;
    const auto first = renderHatEngine (a, s, 96000.0, 0.1, false, 1.0, 12345u);
    const auto second = renderHatEngine (b, s, 96000.0, 0.1, false, 1.0, 98765u);

    std::size_t mismatches = 0;

    for (std::size_t n = 0; n < first.size(); ++n)
        if (! isExactly (first[n], second[n]))
            ++mismatches;

    CHECK (mismatches == 0);

    // And with Air up they differ, which is what says the branch above is
    // the reason and not an accident of the two streams agreeing.
    s.air = 0.5;

    HatEngine c, d;
    const auto third = renderHatEngine (c, s, 96000.0, 0.1, false, 1.0, 12345u);
    const auto fourth = renderHatEngine (d, s, 96000.0, 0.1, false, 1.0, 98765u);

    CHECK (rmsOfDifference (third, fourth, 0, third.size()) > 1.0e-3);
}

TEZLA_TEST (a_hat_hit_retires_exactly_and_leaves_exact_zeros)
{
    HatSettings s = hatEverythingOn();
    s.decayOpenSeconds = 0.3;

    HatEngine engine;
    const auto out = renderHatEngine (engine, s, 96000.0, 1.2, true);

    CHECK (! engine.isActive());

    const int last = lastNonZeroSample (out);
    const double seconds = static_cast<double> (last) / 96000.0;

    std::printf ("        [hat retire] last non-zero at %.3f s (%.3g), active %d\n",
                 seconds, out[static_cast<std::size_t> (last)], engine.isActive() ? 1 : 0);

    // The envelope is killed the moment it reaches its zero sustain, so the
    // tail is the decay's own length and not a filter's ring: everything
    // after it is an exact zero, not a small number.
    CHECK (seconds < 0.6);

    for (std::size_t n = static_cast<std::size_t> (last) + 1; n < out.size(); ++n)
        CHECK (isExactlyZero (out[n]));
}

// ---------------------------------------------------------------------------
// The plate -- I4.3, the rig's "thin and tinny"
// ---------------------------------------------------------------------------

TEZLA_TEST (the_plates_modes_follow_the_cymbal_law_and_are_incommensurate)
{
    double hz[HatEngine::kPlateModes] {};
    double amplitude[HatEngine::kPlateModes] {};

    const int count = HatEngine::plateModesAt (205.3, 0.0, 192000.0 * 0.45, hz, amplitude);

    // At the default Tune the bank's whole capacity fits below the 18 kHz
    // cap, and the 64th mode lands near 9 kHz -- dense enough at the top to
    // read as a plate, low enough at the bottom to have body.
    CHECK (count == HatEngine::kPlateModes);
    CHECK (hz[count - 1] > 7000.0 && hz[count - 1] < 12000.0);

    std::printf ("        [plate] %d modes from %.1f to %.0f Hz at Tune 205.3\n", count, hz[0], hz[count - 1]);

    // The lowest mode IS Tune, exactly: it is the one mode never jittered.
    CHECK (isExactly (hz[0], 205.3));

    // The rim family follows f = c m^p with p = 1.47, the fit for the 14-inch
    // thick cymbal in Fletcher & Rossing's Table 20.1: the second mode is
    // (3,0), at 1.5^1.47 times the first, within the +-1.2 % jitter.
    const double law = std::pow (1.5, HatEngine::kPlateExponent);
    CHECK (std::abs (hz[1] / hz[0] - law) < law * 0.015);

    // Every mode is below the ceiling asked for, and the table is ascending
    // to within the jitter (a mode may be nudged past a neighbour closer than
    // the jitter is wide, which is the point of the jitter).
    for (int k = 0; k < count; ++k)
        CHECK (hz[k] < 192000.0 * 0.45);

    for (int k = 1; k < count; ++k)
        CHECK (hz[k] > hz[k - 1] * 0.97);

    // Incommensurate: no two modes coincide (a cymbal's modes are distinct
    // frequencies, Perrin et al.), and the low twelve -- the ones the ear can
    // still resolve as pitches -- are not a harmonic series of the lowest.
    // Counted rather than forbidden one by one, because the law itself puts
    // (6,0) at 3^1.47 = 5.02 times the fundamental, seven cents from the
    // fifth harmonic: a coincidence or two is what a real cymbal has (Perrin's
    // 18-inch has (9,0) on (2,1)); twelve of them would be a bell.
    for (int a = 0; a < count; ++a)
        for (int b = a + 1; b < count; ++b)
            CHECK (std::abs (1200.0 * std::log2 (hz[b] / hz[a])) > 3.0);

    int nearHarmonic = 0;

    for (int k = 1; k < 12; ++k)
        for (int harmonic = 2; harmonic <= 8; ++harmonic)
            if (std::abs (1200.0 * std::log2 (hz[k] / (harmonic * hz[0]))) < 30.0)
                ++nearHarmonic;

    std::printf ("        [plate] %d of the low twelve modes sit within 30 cents of a harmonic of the lowest\n",
                 nearHarmonic);
    CHECK (nearHarmonic <= 3);

    // The strike: amplitudes fall as (f / f0)^-0.5 and their squares sum to
    // the excitation level squared.
    double power = 0.0;
    for (int k = 0; k < count; ++k)
        power += amplitude[k] * amplitude[k];

    CHECK (std::abs (std::sqrt (power) - HatEngine::kPlateExcitation) < 1.0e-12);
    CHECK (std::abs (amplitude[count - 1] / amplitude[0]
                     - std::pow (hz[count - 1] / hz[0], -HatEngine::kPlateTilt)) < 1.0e-9);

    // A small plate has fewer modes: at Tune 1200 the cap at 18 kHz leaves
    // room for far fewer than 64, and every one is still below it.
    const int small = HatEngine::plateModesAt (1200.0, 0.0, 192000.0 * 0.45, hz, amplitude);

    CHECK (small < HatEngine::kPlateModes && small > 8);

    for (int k = 0; k < small; ++k)
        CHECK (hz[k] < HatEngine::kPlateTopHz);

    // Spread widens the jitter and never moves the lowest mode.
    double spread[HatEngine::kPlateModes] {};
    (void) HatEngine::plateModesAt (205.3, 1.0, 192000.0 * 0.45, spread, amplitude);

    CHECK (isExactly (spread[0], 205.3));

    // `hz` holds the Tune 1200 table from the check above; the Spread 0 table
    // at 205.3 again, to compare against.
    (void) HatEngine::plateModesAt (205.3, 0.0, 192000.0 * 0.45, hz, amplitude);

    double widest = 0.0;
    for (int k = 1; k < count; ++k)
        widest = std::max (widest, std::abs (1200.0 * std::log2 (spread[k] / hz[k])));

    std::printf ("        [plate] Spread 1 moves a mode by up to %.1f cents against Spread 0\n", widest);
    CHECK (widest > 10.0 && widest < 45.0);
}

TEZLA_TEST (plate_at_zero_never_builds_the_bank_and_grit_at_zero_is_sixteen_bits)
{
    // The proof that Plate 0 is the engine before the plate existed is a
    // golden render: five renders at three rates, 412 800 samples, byte for
    // byte identical before and after the change (the I4.3 notes in
    // plugins/Ictus/PLAN.md). What a test can hold permanently is the branch
    // that makes it so: at 0 the bank is never built, and Grit's crusher sits
    // at its exact 16-bit bypass.
    HatSettings s = hatEverythingOn();
    s.plate = 0.0;
    s.grit = 0.0;

    HatEngine off;
    off.prepare (192000.0);
    off.start (s, false, 1.0, 1u, 0);

    CHECK (off.getPlateModeCount() == 0);
    CHECK (isExactly (off.getGritBits(), tezla::dsp::Bitcrusher::kMaxBits));

    s.plate = 1.0;
    s.grit = 1.0;

    HatEngine on;
    on.prepare (192000.0);
    on.start (s, false, 1.0, 1u, 0);

    CHECK (on.getPlateModeCount() == HatEngine::kPlateModes);
    CHECK (std::abs (on.getGritBits() - HatEngine::kGritMinBits) < 1.0e-9);
    CHECK (isExactly (on.getPlateModeHz (0), s.tuneHz));
}

TEZLA_TEST (the_plate_is_a_cymbal_and_not_the_pulse_chord)
{
    // The measurement that separates a plate from six pulses: the share of
    // the energy that no pulse's harmonic series explains. The bare bank puts
    // everything it makes ON those series (-77 dB off them, the I4 figure);
    // a plate's modes are not harmonics of anything, so at Plate 1 most of
    // the energy is off them. Same rig as the Ring measurement in
    // tezla-measure -- Tune 900 so the series are far apart, the bands wide
    // open, nothing else in the mix -- with one difference: the high-pass at
    // 4 kHz. The plate's lowest mode sits AT Tune by design, which is the
    // first pulse's fundamental, and with the longest decay in the bank it
    // carried 77 % of the energy over 1.3 s and read as "on the series" --
    // the first version of this test measured that one shared line and
    // nothing else. Above 4 kHz the two sources have nothing in common.
    //
    // What this measure can and cannot catch: its mask is +-8 bins, about
    // +-6 Hz, around each harmonic, so a plate that degenerated into the
    // pulses' series would read on it -- the break-check that made the modes
    // harmonics of Tune with the jitter off went red -- while the same series
    // WITH the plate's +-1.2 % jitter reads off it. The small departures are
    // the mode-table test's job; this one guards the gross one.
    double partials[HatEngine::kOscillators] {};

    for (int i = 0; i < HatEngine::kOscillators; ++i)
        partials[i] = 900.0 * HatEngine::kSets[0][i];

    const auto offSeriesDb = [&] (double plate)
    {
        HatSettings s = bareHat();
        s.tuneHz = 900.0;
        s.width = 1.0;
        s.highpassHz = 4000.0;
        s.decayOpenSeconds = 1.2;
        s.plate = plate;

        HatEngine engine;
        const auto out = renderHatEngine (engine, s, 192000.0, 1.3, true, 1.0, 12u);
        return inharmonicFloorDb (out, 192000.0, partials, HatEngine::kOscillators, 20000.0);
    };

    const double metal = offSeriesDb (0.0);
    const double plate = offSeriesDb (1.0);

    std::printf ("        [plate] energy off the six harmonic series: metal %.1f dB, plate %.1f dB\n", metal, plate);

    CHECK (metal < -60.0);
    CHECK (plate > -3.0);
}

TEZLA_TEST (grit_puts_the_steps_in_the_sound_and_leaves_the_level_alone)
{
    // Geometric from 16 bits to 4: 0 is the crusher's exact bypass, 1 is
    // four bits, half way is eight.
    CHECK (isExactly (HatEngine::bitsForGrit (0.0), 16.0));
    CHECK (std::abs (HatEngine::bitsForGrit (1.0) - HatEngine::kGritMinBits) < 1.0e-9);
    CHECK (std::abs (HatEngine::bitsForGrit (0.5) - 8.0) < 1.0e-9);

    // A clean plate puts its energy on its own 64 modes; quantising it to
    // four bits spreads error across every bin. Measured as the share of
    // energy OFF the plate's own modes, which is what the steps are.
    HatSettings s = bareHat();
    s.tuneHz = 400.0;
    s.plate = 1.0;
    s.width = 1.0;
    s.highpassHz = 200.0;
    s.decayOpenSeconds = 1.2;

    HatEngine clean;
    const auto quiet = renderHatEngine (clean, s, 192000.0, 1.3, true, 1.0, 12u);

    double modes[HatEngine::kPlateModes] {};
    double amplitude[HatEngine::kPlateModes] {};
    const int count = HatEngine::plateModesAt (400.0, 0.0, 192000.0 * 0.45, modes, amplitude);

    s.grit = 1.0;

    HatEngine gritty;
    const auto crushed = renderHatEngine (gritty, s, 192000.0, 1.3, true, 1.0, 12u);

    const double cleanOff = inharmonicFloorDb (quiet, 192000.0, modes, count, 20000.0);
    const double crushedOff = inharmonicFloorDb (crushed, 192000.0, modes, count, 20000.0);

    double rmsClean = 0.0;
    double rmsCrushed = 0.0;

    for (std::size_t n = 0; n < quiet.size(); ++n)
    {
        rmsClean += quiet[n] * quiet[n];
        rmsCrushed += crushed[n] * crushed[n];
    }

    const double levelDb = 10.0 * std::log10 (rmsCrushed / rmsClean);

    std::printf ("        [grit] energy off the plate's modes: clean %.1f dB, 4 bits %.1f dB; level %+.2f dB\n",
                 cleanOff, crushedOff, levelDb);

    // The steps are the point: at least 10 dB more of the energy lands off
    // the modes. And it is texture, not a volume control.
    CHECK (crushedOff > cleanOff + 10.0);
    CHECK (std::abs (levelDb) < 3.0);
}

TEZLA_TEST (a_plate_hat_retires_exactly_and_leaves_exact_zeros)
{
    HatSettings s = hatEverythingOn();
    s.plate = 1.0;
    s.grit = 0.7;
    s.decayOpenSeconds = 0.3;

    HatEngine engine;
    const auto out = renderHatEngine (engine, s, 96000.0, 1.2, true);

    CHECK (! engine.isActive());

    const int last = lastNonZeroSample (out);
    const double seconds = static_cast<double> (last) / 96000.0;

    std::printf ("        [plate retire] last non-zero at %.3f s, active %d\n",
                 seconds, engine.isActive() ? 1 : 0);

    CHECK (seconds < 0.6);

    for (std::size_t n = static_cast<std::size_t> (last) + 1; n < out.size(); ++n)
        CHECK (isExactlyZero (out[n]));
}

TEZLA_TEST (a_plate_hat_is_the_same_cymbal_at_every_host_rate)
{
    // CLAUDE.md section 6, for the plate: its mode frequencies and decays are
    // computed from the internal rate, so through the instrument at Auto the
    // four host rates must agree.
    EngineParameters parameters;
    parameters.hat = hatEverythingOn();
    parameters.hat.plate = 1.0;
    parameters.hat.grit = 0.5;
    parameters.hat.decayOpenSeconds = 0.35;
    parameters.oversampling = OversamplingMode::Auto;

    double centroids[4] {};
    double energies[4] {};
    int index = 0;

    for (const double rate : { 44100.0, 48000.0, 96000.0, 192000.0 })
    {
        const auto out = render (parameters, rate, static_cast<int> (0.5 * rate),
                                 { { 0, 46, 1.0 } });

        centroids[index] = centroidHz (out, rate, 0.5);
        energies[index] = audibleEnergy (out, rate, 0.5);
        ++index;
    }

    std::printf ("        [plate rates] centroid %.0f / %.0f / %.0f / %.0f Hz; audible energy %.4g / %.4g / %.4g / %.4g\n",
                 centroids[0], centroids[1], centroids[2], centroids[3],
                 energies[0], energies[1], energies[2], energies[3]);

    for (int i = 1; i < 4; ++i)
    {
        CHECK (std::abs (centroids[i] - centroids[0]) / centroids[0] < 0.05);
        CHECK (std::abs (energies[i] - energies[0]) / energies[0] < 0.08);
    }
}

TEZLA_TEST (the_plate_costs_what_the_notes_say)
{
    // 64 modes at 192 kHz: measured 213 ns a sample against 125 for the hat
    // without it (2026-09-05, this container). Budget with the usual
    // headroom for a shared machine; fastest of three, like the other
    // budgets.
    HatSettings s = hatEverythingOn();
    s.plate = 1.0;
    s.grit = 0.6;
    s.decayOpenSeconds = 3.0;

    const auto elapsed = tezla::test::fastestOf (3, [&s] {
        HatEngine engine;
        engine.prepare (192000.0);
        engine.start (s, true, 1.0, 99u, 0);

        double sink = 0.0;

        for (int n = 0; n < 192000; ++n)
        {
            if (n % Engine::kControlIntervalSamples == 0)
                engine.advanceControl (Engine::kControlIntervalSamples);

            sink += engine.process();
        }

        if (sink == 12345.678)
            std::printf ("x");
    });

    std::printf ("        [plate cpu] one second at 192 kHz, plate 1 grit 0.6: %.1f ns/sample (%.2f%% of a core)\n",
                 elapsed * 1.0e9 / 192000.0, 100.0 * elapsed);

    CHECK_CPU_BUDGET (elapsed, 0.40, "hat with the plate, one second at 192 kHz");
}

TEZLA_TEST (a_closed_hat_chokes_the_open_one_and_the_fade_does_not_click)
{
    constexpr double rate = 96000.0;
    constexpr int block = 64;

    EngineParameters parameters;
    parameters.hat = hatEverythingOn();
    parameters.hat.decayOpenSeconds = 2.0;
    parameters.hat.choke = true;
    parameters.oversampling = OversamplingMode::Off;

    // The open hat alone, then the same with a closed hit 100 ms in.
    const int closedAt = static_cast<int> (0.1 * rate);

    auto engine = heapEngine();
    engine->prepare (rate, block);
    engine->setParameters (parameters);

    std::vector<double> left (block), right (block);
    double* buffers[2] = { left.data(), right.data() };

    engine->noteOn (46, 1.0);

    std::vector<double> out;
    out.reserve (static_cast<std::size_t> (0.3 * rate));

    for (int n = 0; n < static_cast<int> (0.3 * rate); n += block)
    {
        if (n <= closedAt && closedAt < n + block)
            engine->noteOn (42, 1.0);

        engine->process (buffers, block);

        for (int i = 0; i < block; ++i)
            out.push_back (left[static_cast<std::size_t> (i)]);
    }

    // The open pad is silent within the choke fade -- 5 ms -- and not before
    // it: a choke is a fade, not a cut.
    CHECK (engine->hatOpen().activeHits() == 0);

    const double step = maxStep (out);

    std::printf ("        [hat choke] open hits after the choke %d, max step %.4f\n",
                 engine->hatOpen().activeHits(), step);

    // With the choke off the open hat is still ringing at the same instant,
    // which is what says the assertion above is the choke's doing.
    parameters.hat.choke = false;

    auto unchoked = heapEngine();
    unchoked->prepare (rate, block);
    unchoked->setParameters (parameters);
    unchoked->noteOn (46, 1.0);

    for (int n = 0; n < static_cast<int> (0.3 * rate); n += block)
    {
        if (n <= closedAt && closedAt < n + block)
            unchoked->noteOn (42, 1.0);

        unchoked->process (buffers, block);
    }

    CHECK (unchoked->hatOpen().activeHits() == 1);
}

TEZLA_TEST (the_clap_fires_its_four_bursts_at_the_flam_spacing)
{
    // The scheduler first, exactly: counted in samples, so the spacing is
    // the same instant at every host rate rather than merely a similar one.
    for (const double rate : { 44100.0, 48000.0, 96000.0, 192000.0 })
    {
        const double flam = 0.011;
        const auto spacing = static_cast<int> (std::lround (flam * rate));

        BurstScheduler scheduler;
        scheduler.start (BurstScheduler::kMaxBursts, flam * rate);

        std::vector<int> fired;

        for (int n = 0; n < static_cast<int> (0.2 * rate); ++n)
            if (scheduler.advance() >= 0)
                fired.push_back (n);

        CHECK (fired.size() == static_cast<std::size_t> (BurstScheduler::kMaxBursts));

        for (std::size_t b = 0; b < fired.size(); ++b)
            CHECK (fired[b] == static_cast<int> (b) * spacing);

        CHECK (! scheduler.isPending());
    }

    // And in the audio: four peaks in the envelope, at the spacing asked for.
    constexpr double rate = 96000.0;

    ClapSettings s;
    s.flamSeconds = 0.012;
    s.tailSeconds = 0.2;

    ClapEngine engine;
    const auto out = renderClapEngine (engine, s, rate, 0.6);
    // A little more than the pattern's own length: four bursts three flams
    // apart, so a late or missing one still fails.
    const auto onsets = burstOnsets (out, rate, 4.5 * s.flamSeconds);

    std::printf ("        [clap bursts] %zu onsets at", onsets.size());

    for (const int onset : onsets)
        std::printf (" %.1f", 1000.0 * static_cast<double> (onset) / rate);

    std::printf (" ms (flam %.1f ms)\n", 1000.0 * s.flamSeconds);

    CHECK (onsets.size() == 4);

    for (std::size_t b = 1; b < onsets.size(); ++b)
    {
        const double gap = static_cast<double> (onsets[b] - onsets[b - 1]) / rate;
        CHECK (std::abs (gap - s.flamSeconds) < 0.001);
    }
}

TEZLA_TEST (a_clap_retires_exactly_and_leaves_exact_zeros)
{
    ClapSettings s;
    s.tailSeconds = 0.25;

    ClapEngine engine;
    const auto out = renderClapEngine (engine, s, 96000.0, 1.5);

    CHECK (! engine.isActive());

    const int last = lastNonZeroSample (out);
    const double seconds = static_cast<double> (last) / 96000.0;

    // The tail starts at the fourth burst, so the hit is three flams plus
    // the tail's own fall and no more.
    std::printf ("        [clap retire] last non-zero at %.3f s (%.3g), active %d\n",
                 seconds, out[static_cast<std::size_t> (last)], engine.isActive() ? 1 : 0);

    CHECK (seconds < 0.5);

    for (std::size_t n = static_cast<std::size_t> (last) + 1; n < out.size(); ++n)
        CHECK (isExactlyZero (out[n]));

    // A longer tail lasts longer, which is what says the number above is the
    // Tail control's and not a floor somewhere.
    s.tailSeconds = 0.6;

    ClapEngine longer;
    const auto out2 = renderClapEngine (longer, s, 96000.0, 2.0);
    const double seconds2 = static_cast<double> (lastNonZeroSample (out2)) / 96000.0;

    CHECK (seconds2 > seconds * 1.8);
}

// ---------------------------------------------------------------------------
// The depth the first hats and clap did not have (I4.1)
// ---------------------------------------------------------------------------

TEZLA_TEST (ring_modulation_is_band_limited_by_construction_and_rate_independent)
{
    // A product holds the SUM of every pair of its inputs' frequencies, so
    // ring-modulating two full-band signals puts energy above Nyquist and
    // folds it back. Both operands are low-passed first, and the guarantee is
    // a construction rather than a measurement: with both below a quarter of
    // the rate, no product can reach Nyquist. This asserts the construction.
    for (const double rate : { 44100.0, 48000.0, 96000.0, 176400.0, 192000.0, 384000.0 })
    {
        HatEngine engine;
        engine.prepare (rate);

        const double corner = engine.getRingOperandCutoffHz();

        CHECK (corner <= rate * 0.25 + 1.0e-9);

        // At every internal rate Auto actually produces -- 176.4 kHz from
        // 44.1 k, 192 kHz from the rest -- the limit is the same absolute
        // 20 kHz, so the ring products are the same frequencies whatever the
        // host is doing. It was a FRACTION of the rate first, and the same
        // patch then measured 6951 Hz at 192 kHz against 4849 Hz at 48 kHz.
        if (rate >= 176400.0)
            CHECK (isExactly (corner, HatEngine::kRingOperandHz));
    }

    // Rate independence where it is promised: through the whole instrument
    // with Auto, Ring at the top.
    EngineParameters parameters;
    parameters.hat = hatEverythingOn();
    parameters.hat.ring = 1.0;
    parameters.hat.decayOpenSeconds = 0.35;
    parameters.oversampling = OversamplingMode::Auto;

    double centroids[4] {};
    int index = 0;

    for (const double rate : { 44100.0, 48000.0, 96000.0, 192000.0 })
    {
        const auto out = render (parameters, rate, static_cast<int> (0.5 * rate),
                                 { { 0, 46, 1.0 } });
        centroids[index++] = centroidHz (out, rate, 0.5);
    }

    std::printf ("        [hat ring] centroid with Ring at 100%%: %.0f / %.0f / %.0f / %.0f Hz\n",
                 centroids[0], centroids[1], centroids[2], centroids[3]);

    for (int i = 1; i < 4; ++i)
        CHECK (std::abs (centroids[i] - centroids[0]) / centroids[0] < 0.05);

    // And it does something: the ring products are inharmonic against the six
    // series on purpose, so the share of energy they add is the effect.
    constexpr double rate = 192000.0;
    constexpr double tune = 900.0;

    HatSettings s = bareHat();
    s.tuneHz = tune;
    s.width = 1.0;
    s.highpassHz = 200.0;
    s.decayOpenSeconds = 1.2;

    double partials[HatEngine::kOscillators] {};

    for (int i = 0; i < HatEngine::kOscillators; ++i)
        partials[i] = tune * HatEngine::kSets[0][i];

    HatSettings ringed = s;
    ringed.ring = 1.0;

    HatEngine plain, dense;
    const auto without = renderHatEngine (plain, s, rate, 1.3, true);
    const auto with = renderHatEngine (dense, ringed, rate, 1.3, true);

    const double plainDb = inharmonicFloorDb (without, rate, partials, HatEngine::kOscillators, 20000.0);
    const double denseDb = inharmonicFloorDb (with, rate, partials, HatEngine::kOscillators, 20000.0);

    std::printf ("        [hat ring] energy off the six series: %.1f dB with no ring, %.1f dB with it\n",
                 plainDb, denseDb);

    // Measured 2026-09-03: -77.2 dB of the bare bank's energy is off its own
    // harmonic series; with the ring at full it is -0.7 dB, which is the
    // dense inharmonic wash the control exists to make.
    CHECK (denseDb > plainDb + 40.0);
}

TEZLA_TEST (damp_closes_the_top_as_the_hit_decays_and_is_out_of_the_path_at_zero)
{
    constexpr double rate = 192000.0;

    HatSettings s = bareHat();
    s.decayOpenSeconds = 0.8;
    s.damp = 0.0;

    // Damp 0: the corner never moves off its top, and the filter is skipped.
    {
        HatEngine engine;
        engine.prepare (rate);
        engine.start (s, true, 1.0, 3u, 0);

        for (int n = 0; n < static_cast<int> (0.4 * rate); ++n)
        {
            if (n % Engine::kControlIntervalSamples == 0)
                engine.advanceControl (Engine::kControlIntervalSamples);

            (void) engine.process();
        }

        CHECK (isExactly (engine.getDampCutoffHz(), HatEngine::kDampTopHz));
    }

    // Damp 1: the corner follows the envelope down.
    s.damp = 1.0;

    HatEngine engine;
    engine.prepare (rate);
    engine.start (s, true, 1.0, 3u, 0);

    double atStart = 0.0;
    double atHalf = 0.0;

    for (int n = 0; n < static_cast<int> (0.4 * rate); ++n)
    {
        if (n % Engine::kControlIntervalSamples == 0)
            engine.advanceControl (Engine::kControlIntervalSamples);

        (void) engine.process();

        if (n == 64)
            atStart = engine.getDampCutoffHz();

        if (n == static_cast<int> (0.2 * rate))
            atHalf = engine.getDampCutoffHz();
    }

    std::printf ("        [hat damp] corner at 0.3 ms %.0f Hz, at 200 ms %.0f Hz\n",
                 atStart, atHalf);

    // Measured 2026-09-03: 17.0 kHz falling to 1.3 kHz -- a hit that starts
    // bright and ends dark, which is the whole point of the control.
    CHECK (atStart > 12000.0);
    CHECK (atHalf < atStart * 0.25);

    // And it is audible, not merely a number: the second half of a damped hit
    // is darker than the second half of an undamped one.
    HatSettings bright = s;
    bright.damp = 0.0;

    HatEngine a, b;
    const auto damped = renderHatEngine (a, s, rate, 0.6, true);
    const auto open = renderHatEngine (b, bright, rate, 0.6, true);

    const auto half = damped.size() / 2;
    const std::vector<double> dampedTail (damped.begin() + static_cast<long> (half), damped.end());
    const std::vector<double> openTail (open.begin() + static_cast<long> (half), open.end());

    const double dampedCentroid = centroidHz (dampedTail, rate, 0.3);
    const double openCentroid = centroidHz (openTail, rate, 0.3);

    std::printf ("        [hat damp] tail centroid damped %.0f Hz, open %.0f Hz\n",
                 dampedCentroid, openCentroid);

    CHECK (dampedCentroid < openCentroid * 0.8);
}

TEZLA_TEST (sizzle_rings_the_hiss_where_the_metal_is_heard)
{
    // The claim: with Sizzle up, the noise is not beside the metal -- it is
    // ringing at the frequencies the metal is HEARD at. Measured through the
    // DEFAULT chain, which is where the first version failed: it rang the
    // hiss at the six fundamentals, 205 to 800 Hz, under a 1.2 kHz high-pass
    // and bands at 3.4 and 7.1 kHz, so at Sizzle 100 the hiss lost 16.5 dB
    // and 0.0 % of it landed within a semitone of a partial (I4.3). The bank
    // sits at the partials' harmonics nearest the two bands now.
    //
    // The hiss alone is the difference between a render with Air and one
    // without, same seed: the metal is deterministic and the chain is linear
    // with Drive at 0, so the difference is exactly the noise layer.
    constexpr double rate = 192000.0;

    HatSettings s = bareHat();          // the default chain: Colour 3440, Highpass 1200, Width 0.5
    s.decayOpenSeconds = 0.5;

    const auto hissAlone = [&] (double sizzle)
    {
        HatSettings dry = s;
        dry.air = 0.0;

        HatSettings wet = s;
        wet.air = 1.0;
        wet.airToneHz = 5000.0;
        wet.sizzle = sizzle;

        HatEngine a, b;
        const auto without = renderHatEngine (a, dry, rate, 0.5, true, 1.0, 7u);
        const auto with = renderHatEngine (b, wet, rate, 0.5, true, 1.0, 7u);

        std::vector<double> hiss (with.size());

        for (std::size_t n = 0; n < hiss.size(); ++n)
            hiss[n] = with[n] - without[n];

        double centres[HatEngine::kOscillators] {};

        for (int i = 0; i < HatEngine::kOscillators; ++i)
            centres[i] = b.getSizzleHz (i);

        return std::pair { hiss, std::vector<double> (centres, centres + HatEngine::kOscillators) };
    };

    const auto [flat, unusedCentres] = hissAlone (0.0);
    const auto [rung, centres] = hissAlone (1.0);

    // 1. The bank sits inside the bands: each centre within an octave of
    //    Colour or of the upper band, never under the high-pass.
    for (const double hz : centres)
    {
        const double toLower = std::abs (std::log2 (hz / 3440.0));
        const double toUpper = std::abs (std::log2 (hz / (3440.0 * HatEngine::kUpperBandRatio)));
        CHECK (std::min (toLower, toUpper) < 1.0);
        CHECK (hz > 1200.0);
    }

    // 2. Level: Sizzle is a placement, not an attenuator.
    double flatRms = 0.0;
    double rungRms = 0.0;

    for (std::size_t n = 0; n < flat.size(); ++n)
    {
        flatRms += flat[n] * flat[n];
        rungRms += rung[n] * rung[n];
    }

    const double levelDb = 10.0 * std::log10 (rungRms / flatRms);

    // 3. Placement: the share of the hiss within a semitone of a centre.
    const auto shareOnCentres = [&] (const std::vector<double>& x)
    {
        const auto windowed = windowFirst (x, rate, 0.4);
        const auto spectrum = fftOfReal (windowed.samples);
        const double binWidth = rate / static_cast<double> (windowed.samples.size());
        const auto last = std::min (static_cast<std::size_t> (20000.0 / binWidth),
                                    windowed.samples.size() / 2);

        double total = 0.0;
        double on = 0.0;

        for (std::size_t bin = 1; bin <= last; ++bin)
        {
            const double hz = static_cast<double> (bin) * binWidth;
            const double power = std::norm (spectrum[bin]);
            total += power;

            for (const double centre : centres)
                if (std::abs (1200.0 * std::log2 (hz / centre)) < 100.0)
                {
                    on += power;
                    break;
                }
        }

        return on / std::max (total, 1.0e-300);
    };

    const double flatShare = shareOnCentres (flat);
    const double rungShare = shareOnCentres (rung);

    std::printf ("        [hat sizzle] centres %.0f %.0f %.0f / %.0f %.0f %.0f Hz; level %+.1f dB; "
                 "within a semitone of a centre: %.1f%% flat, %.1f%% rung\n",
                 centres[0], centres[1], centres[2], centres[3], centres[4], centres[5],
                 levelDb, 100.0 * flatShare, 100.0 * rungShare);

    CHECK (std::abs (levelDb) < 3.0);
    CHECK (rungShare > flatShare * 2.5);
}

TEZLA_TEST (the_hats_strike_puts_the_loudest_part_at_the_front)
{
    constexpr double rate = 96000.0;

    HatSettings s = bareHat();
    s.decayOpenSeconds = 0.6;
    s.strike = 0.0;

    HatEngine plain, struck;
    const auto without = renderHatEngine (plain, s, rate, 0.3, true);

    s.strike = 1.0;
    const auto with = renderHatEngine (struck, s, rate, 0.3, true);

    const auto front = static_cast<std::size_t> (0.008 * rate);

    const auto peakOver = [] (const std::vector<double>& x, std::size_t from, std::size_t to)
    {
        double peak = 0.0;

        for (std::size_t n = from; n < std::min (to, x.size()); ++n)
            peak = std::max (peak, std::abs (x[n]));

        return peak;
    };

    const double plainFront = peakOver (without, 0, front);
    const double struckFront = peakOver (with, 0, front);
    const double plainBody = peakOver (without, front, without.size());
    const double struckBody = peakOver (with, front, with.size());

    std::printf ("        [hat strike] first 8 ms %.3f -> %.3f; after it %.3f -> %.3f\n",
                 plainFront, struckFront, plainBody, struckBody);

    // The stick lands at the front and nowhere else: it lifts the first 8 ms
    // and leaves the ring alone.
    CHECK (struckFront > plainFront * 1.4);
    CHECK (std::abs (struckBody - plainBody) < plainBody * 0.35);
}

TEZLA_TEST (a_gated_hat_fades_out_at_note_off_and_a_one_shot_rings_on)
{
    constexpr double rate = 96000.0;
    constexpr int block = 64;

    EngineParameters parameters;
    parameters.hat = hatEverythingOn();
    parameters.hat.decayOpenSeconds = 2.0;
    parameters.hat.gate = true;
    parameters.hat.releaseSeconds = 0.05;
    parameters.oversampling = OversamplingMode::Off;

    // A note-off 100 ms in: the whole hit is gone a release later.
    const auto gated = render (parameters, rate, static_cast<int> (0.5 * rate),
                               { { 0, 46, 1.0 }, { static_cast<int> (0.1 * rate), 46, -1.0 } },
                               block);

    const double gatedEnd = static_cast<double> (lastNonZeroSample (gated)) / rate;
    const double step = maxStep (gated);

    // The same hit with Gate dark ignores the note-off entirely.
    parameters.hat.gate = false;

    const auto oneShot = render (parameters, rate, static_cast<int> (0.5 * rate),
                                 { { 0, 46, 1.0 }, { static_cast<int> (0.1 * rate), 46, -1.0 } },
                                 block);

    const double oneShotEnd = static_cast<double> (lastNonZeroSample (oneShot)) / rate;

    std::printf ("        [hat gate] gated ends at %.3f s (max step %.4f); one-shot still going at %.3f s\n",
                 gatedEnd, step, oneShotEnd);

    // Measured 2026-09-03: the gated hat is silent by 0.152 s -- the note-off
    // at 100 ms plus the 50 ms release -- and the one-shot runs to the end of
    // the render.
    CHECK (gatedEnd < 0.17);
    CHECK (oneShotEnd > 0.45);

    // Release 0 is a 1 ms ramp, not a step.
    parameters.hat.gate = true;
    parameters.hat.releaseSeconds = 0.0;

    const auto cut = render (parameters, rate, static_cast<int> (0.5 * rate),
                             { { 0, 46, 1.0 }, { static_cast<int> (0.1 * rate), 46, -1.0 } },
                             block);

    const double cutEnd = static_cast<double> (lastNonZeroSample (cut)) / rate;

    std::printf ("        [hat gate] release 0 ends at %.3f s, max step %.4f against the hit's own %.4f\n",
                 cutEnd, maxStep (cut), maxStep (oneShot));

    CHECK (cutEnd < 0.105);
    CHECK (maxStep (cut) <= maxStep (oneShot) * 1.05);
}

TEZLA_TEST (the_clap_fires_the_bursts_it_is_asked_for_and_skews_their_spacing)
{
    // Every count, at every host rate, on exactly the samples the flam says.
    for (const double rate : { 44100.0, 192000.0 })
    {
        for (int count = 2; count <= BurstScheduler::kMaxBursts; ++count)
        {
            const double flam = 0.011;
            const auto spacing = static_cast<int> (std::lround (flam * rate));

            BurstScheduler scheduler;
            scheduler.start (count, flam * rate);

            std::vector<int> fired;

            for (int n = 0; n < static_cast<int> (0.4 * rate); ++n)
                if (scheduler.advance() >= 0)
                    fired.push_back (n);

            CHECK (fired.size() == static_cast<std::size_t> (count));

            for (std::size_t b = 0; b < fired.size(); ++b)
                CHECK (fired[b] == static_cast<int> (b) * spacing);
        }
    }

    // Skew compounds gap by gap, and is exactly even at the centre.
    CHECK (isExactly (ClapEngine::ratioForSkew (0.0), 1.0));

    constexpr double rate = 96000.0;
    const double flam = 0.010;

    for (const double skew : { -1.0, 0.0, 1.0 })
    {
        const double ratio = ClapEngine::ratioForSkew (skew);

        BurstScheduler scheduler;
        scheduler.start (4, flam * rate, ratio);

        std::vector<int> fired;

        for (int n = 0; n < static_cast<int> (0.5 * rate); ++n)
            if (scheduler.advance() >= 0)
                fired.push_back (n);

        CHECK (fired.size() == 4u);

        const double first = static_cast<double> (fired[1] - fired[0]);
        const double last = static_cast<double> (fired[3] - fired[2]);

        std::printf ("        [clap skew] skew %+.1f (ratio %.3f): first gap %.1f ms, last %.1f ms\n",
                     skew, ratio, 1000.0 * first / rate, 1000.0 * last / rate);

        if (skew < 0.0)
            CHECK (last < first * 0.8);
        else if (skew > 0.0)
            CHECK (last > first * 1.2);
        else
            CHECK (isExactly (first, last));
    }
}

TEZLA_TEST (the_claps_body_rings_at_its_pitch_and_retires_exactly)
{
    constexpr double rate = 96000.0;

    // The body alone: no hiss at all, so what is measured is the cavity.
    ClapSettings s;
    s.noise = 0.0;
    s.body = 1.0;
    s.bodyHz = 700.0;
    s.bodyRingSeconds = 0.3;
    s.tailSeconds = 0.05;
    s.width = 1.0;
    s.colourHz = 1500.0;

    ClapEngine engine;
    const auto out = renderClapEngine (engine, s, rate, 1.5);

    // The cavity is INHARMONIC, and that is the design rather than an
    // accident: cupped hands are not a tube with a harmonic series, and a
    // body whose modes were 1 : 2 : 3 would read as a pitched note rather
    // than a knock inside the clap. Asserted against the integers rather than
    // against the table, because a test that computes what it expects from
    // the table it is checking cannot fail when the table is wrong -- which
    // is what the first version of this did, and a break-check caught it.
    for (int mode = 1; mode < ClapEngine::kBodyModes; ++mode)
    {
        const double ratio = ClapEngine::kBodyRatios[mode];
        const double nearest = std::round (ratio);

        CHECK (std::abs (ratio - nearest) > 0.08 * nearest);
    }

    // Every one of the three modes, each in its own window, and measured on
    // the FREE ringing after the last burst.
    //
    // Not the whole render: four strikes 11 ms apart comb the spectrum every
    // 90 Hz, and the comb pulls a short mode's peak off its centre by a
    // couple of per cent -- which is the sound being made, not an error, and
    // measuring through it would be measuring the pattern rather than the
    // bank. Everything from 100 ms on is the cavity ringing alone.
    const auto freeFrom = static_cast<long> (0.1 * rate);
    const std::vector<double> ringing (out.begin() + freeFrom, out.end());

    for (int mode = 0; mode < ClapEngine::kBodyModes; ++mode)
    {
        const double wanted = 700.0 * ClapEngine::kBodyRatios[mode];
        const double found = peakHzBetween (ringing, rate, wanted * 0.93, wanted * 1.07);

        std::printf ("        [clap body] mode %d wanted %.1f Hz, measured %.1f Hz\n",
                     mode, wanted, found);

        CHECK (std::abs (found - wanted) < wanted * 0.02);
    }

    // And Pitch moves the whole cavity, not just its name.
    ClapSettings lower = s;
    lower.bodyHz = 350.0;

    ClapEngine deeper;
    const auto low = renderClapEngine (deeper, lower, rate, 1.5);

    const std::vector<double> lowRinging (low.begin() + freeFrom, low.end());
    const double lowFundamental = peakHzBetween (lowRinging, rate, 300.0, 420.0);

    std::printf ("        [clap body] pitch halved: fundamental %.1f Hz\n", lowFundamental);

    CHECK (std::abs (lowFundamental - 350.0) < 10.0);

    // And it retires: the bank is cut at its energy floor, so the hit ends.
    CHECK (! engine.isActive());

    const int last = lastNonZeroSample (out);

    for (std::size_t n = static_cast<std::size_t> (last) + 1; n < out.size(); ++n)
        CHECK (isExactlyZero (out[n]));

    std::printf ("        [clap body] last non-zero at %.3f s, active %d\n",
                 static_cast<double> (last) / rate, engine.isActive() ? 1 : 0);

    // With Body at 0 the bank is not run and the pad is the hiss alone.
    s.body = 0.0;
    s.noise = 1.0;

    ClapEngine hiss;
    const auto noiseOnly = renderClapEngine (hiss, s, rate, 1.0);

    CHECK (! hiss.isActive());
    CHECK (lastNonZeroSample (noiseOnly) > 0);
}

// ---------------------------------------------------------------------------
// Drive, and the clap's gate (I4.2 -- the rig's second round)
// ---------------------------------------------------------------------------

namespace
{
double rmsOf (const std::vector<double>& x)
{
    double sum = 0.0;

    for (const double sample : x)
        sum += sample * sample;

    return std::sqrt (sum / static_cast<double> (std::max<std::size_t> (x.size(), 1)));
}

double peakOf (const std::vector<double>& x)
{
    double peak = 0.0;

    for (const double sample : x)
        peak = std::max (peak, std::abs (sample));

    return peak;
}
} // namespace

TEZLA_TEST (drive_clips_the_hit_rather_than_replacing_it_with_its_own_residue)
{
    // The bug this pins, reported from the rig: `SoftClipExcess` evaluates to
    // what the clipper CHANGES -- clip(x) - x -- and not to the clipped
    // signal. Taking it alone leaves only the clipping residue, which is
    // EXACTLY ZERO below the knee. The clap went almost silent at low drive,
    // came back as a harsh remnant with no tail at full drive, and sounded
    // right only at exactly 0, where the branch skips the stage.
    //
    // So what is asserted is the shape of a working drive: unity for small
    // signals, a level that barely moves, and a peak that falls because the
    // clipping is real.
    constexpr double rate = 192000.0;

    {
        ClapSettings s;
        s.body = 0.25;

        double firstRms = 0.0;
        double firstPeak = 0.0;
        double lastPeak = 0.0;

        for (const double drive : { 0.0, 0.25, 0.5, 0.75, 1.0 })
        {
            s.drive = drive;

            ClapEngine engine;
            const auto out = renderClapEngine (engine, s, rate, 0.6);

            const double rms = rmsOf (out);
            const double peak = peakOf (out);

            std::printf ("        [clap drive] %3.0f%%: rms %.4f, peak %.3f\n",
                         100.0 * drive, rms, peak);

            if (isExactlyZero (drive))
            {
                firstRms = rms;
                firstPeak = peak;
            }
            else
            {
                // Measured 2026-09-03 across the whole control: the clap
                // rises 2.1 dB and the hat falls 2.7 dB, because a clipper's
                // output depends on where the signal already sat against the
                // threshold and one trim exponent cannot hold both exactly.
                // The broken version read a small fraction of the first at
                // 25 % and would fail this by an order of magnitude.
                CHECK (rms > firstRms * 0.6);
                CHECK (rms < firstRms * 1.7);
            }

            lastPeak = peak;
        }

        // The peak falls because the clipping is doing something: 0.320 to
        // 0.119, measured.
        CHECK (lastPeak < firstPeak * 0.6);
    }

    // The same shape on the hat, whose Drive had the identical bug and whose
    // presets ship with it up.
    {
        HatSettings s = bareHat();
        s.decayOpenSeconds = 0.5;

        double firstRms = 0.0;

        for (const double drive : { 0.0, 0.3, 0.6, 1.0 })
        {
            s.drive = drive;

            HatEngine engine;
            const auto out = renderHatEngine (engine, s, rate, 0.7, true);
            const double rms = rmsOf (out);

            std::printf ("        [hat drive] %3.0f%%: rms %.4f, peak %.3f\n",
                         100.0 * drive, rms, peakOf (out));

            if (isExactlyZero (drive))
                firstRms = rms;
            else
            {
                CHECK (rms > firstRms * 0.6);
                CHECK (rms < firstRms * 1.7);
            }
        }
    }
}

TEZLA_TEST (a_gated_clap_fades_out_at_note_off_and_a_one_shot_rings_on)
{
    constexpr double rate = 96000.0;
    constexpr int block = 64;

    EngineParameters parameters;
    parameters.clap = ClapSettings {};
    parameters.clap.tailSeconds = 0.8;
    parameters.clap.body = 0.4;
    parameters.clap.bodyRingSeconds = 0.4;
    parameters.clap.gate = true;
    parameters.clap.releaseSeconds = 0.05;
    parameters.oversampling = OversamplingMode::Off;

    const auto gated = render (parameters, rate, static_cast<int> (0.6 * rate),
                               { { 0, 39, 1.0 }, { static_cast<int> (0.1 * rate), 39, -1.0 } },
                               block);

    const double gatedEnd = static_cast<double> (lastNonZeroSample (gated)) / rate;

    parameters.clap.gate = false;

    const auto oneShot = render (parameters, rate, static_cast<int> (0.6 * rate),
                                 { { 0, 39, 1.0 }, { static_cast<int> (0.1 * rate), 39, -1.0 } },
                                 block);

    const double oneShotEnd = static_cast<double> (lastNonZeroSample (oneShot)) / rate;

    std::printf ("        [clap gate] gated ends at %.3f s, one-shot at %.3f s (max step %.4f)\n",
                 gatedEnd, oneShotEnd, maxStep (gated));

    // The note-off lands at 100 ms and the release is 50 ms, so the gated
    // clap is gone by 150-odd; the one-shot runs its tail out.
    CHECK (gatedEnd < 0.17);
    CHECK (oneShotEnd > 0.5);

    // It is a ramp, not a cut: no step larger than the hit's own.
    CHECK (maxStep (gated) <= maxStep (oneShot) * 1.05);

    // Release 0 is the 1 ms ramp.
    parameters.clap.gate = true;
    parameters.clap.releaseSeconds = 0.0;

    const auto cut = render (parameters, rate, static_cast<int> (0.6 * rate),
                             { { 0, 39, 1.0 }, { static_cast<int> (0.1 * rate), 39, -1.0 } },
                             block);

    CHECK (static_cast<double> (lastNonZeroSample (cut)) / rate < 0.105);
    CHECK (maxStep (cut) <= maxStep (oneShot) * 1.05);
}

TEZLA_TEST (the_wires_hold_at_full_level_before_they_start_to_fall)
{
    // A snare's wires are thrown against a head that is still moving and stay
    // there for a moment; they do not begin dying at the instant of the
    // strike. Hold is that moment, and what it buys is a buzz with a length
    // of its own -- the alternative being a long decay, which washes.
    constexpr double rate = 96000.0;

    // The wires alone: no shell, no crack, so the envelope IS the signal.
    SnareSettings s = neutralSnare();
    s.body = 0.0;
    s.decaySeconds = 0.06;   // a short shell, so the hit ends when the wires do
    s.wires = 1.0;
    s.wiresDecaySeconds = 0.1;
    s.snappyHz = 2000.0;
    s.velocityWires = 0.0;

    // A short-window RMS follows the noise's envelope; the raw samples do not.
    const auto envelopeAt = [] (const std::vector<double>& x, double seconds)
    {
        const auto centre = static_cast<std::size_t> (seconds * rate);
        const auto half = static_cast<std::size_t> (0.004 * rate);

        if (centre + half >= x.size())
            return 0.0;

        double sum = 0.0;

        for (std::size_t n = centre - half; n < centre + half; ++n)
            sum += x[n] * x[n];

        return std::sqrt (sum / static_cast<double> (2 * half));
    };

    SnareEngine plain, held;
    s.wiresHoldSeconds = 0.0;
    const auto without = renderSnareEngine (plain, s, rate, 0.6);
    s.wiresHoldSeconds = 0.12;
    const auto with = renderSnareEngine (held, s, rate, 0.6);

    // Measured at the strike, where both are still at full level. NOT at
    // 10 ms: a hundred-millisecond decay is already a third down by then, so
    // "the strike is unchanged" has to be asked at the strike -- the first
    // version of this test asked at 10 ms and failed on the control working.
    const double startWithout = envelopeAt (without, 0.005);
    const double startWith = envelopeAt (with, 0.005);
    const double lateWithout = envelopeAt (without, 0.1);
    const double lateWith = envelopeAt (with, 0.1);

    std::printf ("        [wires hold] at 5 ms %.4f -> %.4f; at 100 ms %.4f -> %.4f\n",
                 startWithout, startWith, lateWithout, lateWith);

    // The definition of a hold: the level at the END of it is the level at
    // the start. (Not "the strike is unchanged" -- an exponential decay is
    // already 14 % down by 5 ms, so that question cannot be asked of a
    // four-millisecond window, and the first version of this test failed on
    // the control working rather than on it being broken.)
    CHECK (std::abs (lateWith - startWith) < startWith * 0.15);

    // ...and 100 ms in, inside the hold, the wires are still at full level
    // where without it they have fallen most of the way. Measured
    // 2026-09-03: 0.0233 against 0.0009, a factor of 25.
    CHECK (lateWith > lateWithout * 5.0);
    CHECK (lateWith > startWith * 0.8);

    // The whole hit is longer by about the hold, and still retires exactly.
    const double endWithout = static_cast<double> (lastNonZeroSample (without)) / rate;
    const double endWith = static_cast<double> (lastNonZeroSample (with)) / rate;

    std::printf ("        [wires hold] the hit runs %.3f s -> %.3f s, active %d\n",
                 endWithout, endWith, held.isActive() ? 1 : 0);

    CHECK (endWith > endWithout + 0.1 * 0.8);
    CHECK (! held.isActive());

    for (std::size_t n = static_cast<std::size_t> (lastNonZeroSample (with)) + 1; n < with.size(); ++n)
        CHECK (isExactlyZero (with[n]));
}

// ---------------------------------------------------------------------------
// I4.4 -- the rig's fourth round: more say over the hiss, an open hold of its
// own, a thicker kick and snare, and the pads placed in the field
// ---------------------------------------------------------------------------

namespace
{
/// The kick engine on its own at an internal rate, stepped on the control
/// grid exactly as the engine steps it: `seconds` of one hit.
std::vector<double> renderKickEngine (KickEngine& engine, const KickSettings& s, double rate,
                                      double seconds, double endHz = 50.0, double velocity = 1.0)
{
    engine.prepare (rate);
    engine.start (s, endHz, velocity, 5u, 0);

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

/// Both channels of the engine, the notes on their exact samples. The mono
/// `render` returns the left channel only, which was the whole story until
/// the pads had pans.
struct StereoRender
{
    std::vector<double> left, right;
};

StereoRender renderBoth (const EngineParameters& parameters, double rate, int samples,
                         const std::vector<Hit>& hits, int blockSize = 256)
{
    auto engine = heapEngine();
    engine->prepare (rate, 512);
    engine->setParameters (parameters);

    StereoRender out;
    out.left.assign (static_cast<std::size_t> (samples), 0.0);
    out.right.assign (static_cast<std::size_t> (samples), 0.0);

    int done = 0;

    while (done < samples)
    {
        for (const auto& hit : hits)
            if (hit.sample == done)
                engine->noteOn (hit.note, hit.velocity);

        int take = std::min (blockSize, samples - done);

        for (const auto& hit : hits)
            if (hit.sample > done)
                take = std::min (take, hit.sample - done);

        double* block[2] = { out.left.data() + done, out.right.data() + done };
        engine->process (block, take);

        done += take;
    }

    return out;
}

/// The RMS of `x` between two instants, in seconds.
double rmsBetween (const std::vector<double>& x, double rate, double from, double to)
{
    const auto first = std::min (x.size(), static_cast<std::size_t> (from * rate));
    const auto last = std::min (x.size(), static_cast<std::size_t> (to * rate));

    double sum = 0.0;

    for (std::size_t n = first; n < last; ++n)
        sum += x[n] * x[n];

    return std::sqrt (sum / static_cast<double> (std::max<std::size_t> (last - first, 1)));
}

/// The hiss on its own: the hat with Air at 1 minus the same hit with Air at
/// 0. Same seed, same metal, and with Drive and Grit off the chain after the
/// layers meet is linear, so the difference IS the filtered noise and nothing
/// else -- there is no other way to hear the layer alone.
std::vector<double> hissOnly (HatSettings s, double rate, double seconds, bool open = false,
                              double velocity = 1.0)
{
    s.drive = 0.0;
    s.grit = 0.0;

    HatSettings quiet = s;
    quiet.air = 0.0;

    HatEngine withAir, withoutAir;
    const auto a = renderHatEngine (withAir, s, rate, seconds, open, velocity, 21u);
    const auto b = renderHatEngine (withoutAir, quiet, rate, seconds, open, velocity, 21u);

    std::vector<double> hiss (a.size());

    for (std::size_t n = 0; n < a.size(); ++n)
        hiss[n] = a[n] - b[n];

    return hiss;
}

/// A hat whose hiss can be heard whole: wide bands at 4 kHz, the high-pass
/// down at 200 Hz, Air tone at 1 kHz so there is hiss on both sides of the
/// tilt's pivot, and no Sizzle so the noise is not rung into lines.
HatSettings hissRig()
{
    HatSettings s = bareHat();
    s.air = 1.0;
    s.airToneHz = 1000.0;
    s.airDecay = 1.0;
    s.sizzle = 0.0;
    s.colourHz = 4000.0;
    s.width = 1.0;
    s.highpassHz = 200.0;
    s.decayClosedSeconds = 0.3;
    s.decayOpenSeconds = 0.5;
    return s;
}
} // namespace

TEZLA_TEST (open_hold_is_the_open_pads_own_when_link_is_dark)
{
    // One Hold has always fed both pads. Open hold is the open pad's own,
    // with a longer range, behind a LINK lamp lit by default: lit, the two
    // share Hold as they did; dark, the open pad plateaus for its own time.
    const double rate = 96000.0;

    HatSettings s = bareHat();
    s.decayOpenSeconds = 0.3;
    s.decayClosedSeconds = 0.1;
    s.holdSeconds = 0.0;
    s.holdOpenSeconds = 0.5;

    s.holdLink = true;
    HatEngine linked;
    linked.prepare (rate);
    linked.start (s, true, 1.0, 3u, 0);
    CHECK (isExactlyZero (linked.getHoldSeconds()));
    const auto linkedOpen = renderHatEngine (linked, s, rate, 1.0, true);
    const auto linkedClosed = renderHatEngine (linked, s, rate, 0.5, false);

    s.holdLink = false;
    HatEngine own;
    own.prepare (rate);
    own.start (s, true, 1.0, 3u, 0);
    CHECK (isExactly (own.getHoldSeconds(), 0.5));
    own.start (s, false, 1.0, 3u, 0);
    CHECK (isExactlyZero (own.getHoldSeconds()));

    const auto ownOpen = renderHatEngine (own, s, rate, 1.0, true);
    const auto ownClosed = renderHatEngine (own, s, rate, 0.5, false);

    // The closed pad never reads Open hold: byte for byte the same hit.
    CHECK (linkedClosed == ownClosed);

    // Inside the open pad's plateau, 0.4 s in, the unlinked hit is still at
    // its early level, and the linked one -- a 0.3 s decay -- is long gone.
    const double early = rmsBetween (ownOpen, rate, 0.05, 0.10);
    const double late = rmsBetween (ownOpen, rate, 0.40, 0.45);
    const double lateLinked = rmsBetween (linkedOpen, rate, 0.40, 0.45);

    std::printf ("        [open hold] open pad at 50 ms %.4f, at 400 ms %.4f with its own 0.5 s hold, %.6f linked\n",
                 early, late, lateLinked);

    CHECK (late > early * 0.8);
    CHECK (lateLinked < early * 0.05);
}

TEZLA_TEST (air_tilt_moves_the_hiss_dark_and_bright_and_is_out_of_the_path_at_zero)
{
    // Air tone is a high-pass and could only thin the hiss; Air tilt can
    // dull it. A low shelf and a high shelf of opposite sign about 4 kHz,
    // designed per hit, not run at 0.
    const double rate = 192000.0;

    HatSettings s = hissRig();

    // Under the bands' rails, as in the velocity test.
    s.air = 0.3;

    const auto centroidAt = [&] (double tilt)
    {
        HatSettings t = s;
        t.airTilt = tilt;
        return centroidHz (hissOnly (t, rate, 0.3), rate, 0.25);
    };

    const double dark = centroidAt (-1.0);
    const double flat = centroidAt (0.0);
    const double bright = centroidAt (1.0);

    std::printf ("        [air tilt] hiss centroid %.0f Hz dark, %.0f flat, %.0f bright\n", dark, flat, bright);

    CHECK (dark < flat * 0.85);
    CHECK (bright > flat * 1.15);

    HatEngine engine;
    engine.prepare (rate);
    engine.start (s, false, 1.0, 1u, 0);
    CHECK (! engine.isAirTiltOn());

    s.airTilt = 0.3;
    engine.start (s, false, 1.0, 1u, 0);
    CHECK (engine.isAirTiltOn());

    // Level: a slope, not a volume control. The two shelves pull in
    // opposite directions about the pivot, so the hiss stays within a few dB.
    s.airTilt = -1.0;
    const double darkRms = rmsOf (hissOnly (s, rate, 0.3));
    s.airTilt = 0.0;
    const double flatRms = rmsOf (hissOnly (s, rate, 0.3));
    s.airTilt = 1.0;
    const double brightRms = rmsOf (hissOnly (s, rate, 0.3));

    std::printf ("        [air tilt] hiss RMS %.4f dark, %.4f flat, %.4f bright\n", darkRms, flatRms, brightRms);

    CHECK (darkRms > flatRms * 0.25 && darkRms < flatRms * 4.0);
    CHECK (brightRms > flatRms * 0.25 && brightRms < flatRms * 4.0);
}

TEZLA_TEST (air_attack_brings_the_hiss_up_after_the_metal)
{
    const double rate = 96000.0;

    HatSettings s = hissRig();
    s.airAttackSeconds = 0.0;
    const auto instant = hissOnly (s, rate, 0.5);

    s.airAttackSeconds = 0.2;
    const auto swelled = hissOnly (s, rate, 0.5);

    HatEngine engine;
    engine.prepare (rate);
    engine.start (s, false, 1.0, 1u, 0);
    CHECK (isExactly (engine.getAirAttackSeconds(), 0.2));

    // The first 20 ms: a 200 ms linear rise is a tenth of the way up.
    const double earlyInstant = rmsBetween (instant, rate, 0.0, 0.02);
    const double earlySwelled = rmsBetween (swelled, rate, 0.0, 0.02);

    // ...and at 200 ms the swelled hiss has arrived where the instant one has
    // already fallen.
    const double lateInstant = rmsBetween (instant, rate, 0.19, 0.21);
    const double lateSwelled = rmsBetween (swelled, rate, 0.19, 0.21);

    std::printf ("        [air attack] first 20 ms %.4f -> %.4f with a 200 ms rise; at 200 ms %.4f -> %.4f\n",
                 earlyInstant, earlySwelled, lateInstant, lateSwelled);

    CHECK (earlySwelled < earlyInstant * 0.12);
    CHECK (lateSwelled > lateInstant);
}

TEZLA_TEST (grain_thins_the_hiss_to_a_crackle_and_says_how_many_a_second)
{
    // Per second, not per sample: the same knob is the same texture at every
    // rate. Exactly every sample at 0.
    CHECK (isExactly (HatEngine::grainDensityFor (0.0, 192000.0), 1.0));

    for (double rate : { 176400.0, 192000.0, 96000.0 })
        CHECK (std::abs (HatEngine::grainDensityFor (1.0, rate) * rate - HatEngine::kGrainMinEventsPerSecond) < 1.0e-9);

    CHECK (std::abs (HatEngine::grainDensityFor (0.5, 192000.0)
                     - std::sqrt (HatEngine::kGrainMinEventsPerSecond / 192000.0)) < 1.0e-12);

    const double rate = 192000.0;
    HatSettings s = hissRig();

    HatEngine engine;
    engine.prepare (rate);
    engine.start (s, false, 1.0, 1u, 0);
    CHECK (isExactly (engine.getGrainDensity(), 1.0));

    s.grain = 1.0;
    engine.start (s, false, 1.0, 1u, 0);
    CHECK (std::abs (engine.getGrainDensity() * rate - HatEngine::kGrainMinEventsPerSecond) < 1.0e-9);

    // A crackle is the same noise with most of its samples exactly zero: the
    // crest factor (peak over RMS) is what says so once the filters have
    // smeared each impulse into a few samples. And the level stays within
    // bounds -- p^-0.25 of make-up, see kGrainMakeupExponent.
    s.grain = 0.0;
    const auto dense = hissOnly (s, rate, 0.3);
    s.grain = 1.0;
    const auto sparse = hissOnly (s, rate, 0.3);

    const double denseCrest = peakOf (dense) / rmsOf (dense);
    const double sparseCrest = peakOf (sparse) / rmsOf (sparse);
    const double levelDb = 20.0 * std::log10 (rmsOf (sparse) / rmsOf (dense));

    std::printf ("        [grain] crest factor %.1f dense -> %.1f at 300 events a second; level %.1f dB\n",
                 denseCrest, sparseCrest, levelDb);

    CHECK (sparseCrest > denseCrest * 3.0);
    CHECK (levelDb > -18.0 && levelDb < 3.0);
}

TEZLA_TEST (velocity_to_air_scales_the_hiss_and_nothing_else)
{
    const double rate = 96000.0;
    HatSettings s = hissRig();

    // Under the bands' rails: the SVF is exactly linear to +-1 and saturates
    // above it, and a full-level hiss through 1.4x of make-up brushes that
    // rail -- measured 0.500062 at Air 1, which is the filter being an
    // analogue filter rather than the control being wrong.
    s.air = 0.3;

    // At 0 -- the default -- a soft hit has the same hiss as a hard one.
    s.velocityAir = 0.0;
    const auto hard = hissOnly (s, rate, 0.3, false, 1.0);
    const auto softUnscaled = hissOnly (s, rate, 0.3, false, 0.5);
    CHECK (hard == softUnscaled);

    // At 1, the hiss IS the velocity: exactly half at the layer, and half
    // to within the bands' rails once heard -- the SVF is exactly linear to
    // +-1 and saturates above it, and the metal alone reaches 1.0 where its
    // six pulses coincide, so adding the hiss brushes the rail and the
    // difference of two renders is not quite the hiss alone (measured
    // 0.500009 here, 0.500062 at Air 1).
    s.velocityAir = 1.0;

    HatEngine engine;
    engine.prepare (rate);
    engine.start (s, false, 0.5, 1u, 0);
    CHECK (isExactly (engine.getAirLevel(), 0.15));
    engine.start (s, false, 1.0, 1u, 0);
    CHECK (isExactly (engine.getAirLevel(), 0.3));

    const auto soft = hissOnly (s, rate, 0.3, false, 0.5);
    const double ratio = rmsOf (soft) / rmsOf (hard);

    std::printf ("        [vel > air] hiss RMS at velocity 0.5 against 1.0: %.6f\n", ratio);

    CHECK (std::abs (ratio - 0.5) < 1.0e-3);
}

TEZLA_TEST (under_is_a_sub_locked_to_the_body_and_exact_at_zero)
{
    const double rate = 96000.0;

    KickSettings s = neutralKick();
    s.decaySeconds = 1.0;
    s.under = 1.0;
    s.underSemitones = 12.0;

    KickEngine engine;
    const auto with = renderKickEngine (engine, s, rate, 1.0);

    KickSettings off = s;
    off.under = 0.0;
    KickEngine plain;
    const auto without = renderKickEngine (plain, off, rate, 1.0);

    // The difference is the sub alone: an octave under the 50 Hz body.
    std::vector<double> sub (with.size());
    for (std::size_t n = 0; n < sub.size(); ++n)
        sub[n] = with[n] - without[n];

    const double hz = peakHzBetween (sub, rate, 15.0, 35.0);
    std::printf ("        [under] the sub's peak at %.2f Hz under a 50 Hz body, RMS %.4f\n", hz, rmsOf (sub));

    CHECK (std::abs (hz - 25.0) < 0.5);
    CHECK (rmsOf (sub) > 0.2);

    // Locked: through a drop the sub is the body's pitch times the interval's
    // ratio at every control boundary, so the two can never beat.
    s.startSemitones = 24.0;
    s.dropSeconds = 0.1;
    engine.prepare (rate);
    engine.start (s, 50.0, 1.0, 1u, 0);

    int moving = 0;

    for (int tick = 0; tick < 40; ++tick)
    {
        engine.advanceControl (Engine::kControlIntervalSamples);

        CHECK (isExactly (engine.getUnderHz(), engine.currentHz() * 0.5));

        if (engine.currentHz() > 50.5)
            ++moving;

        for (int n = 0; n < Engine::kControlIntervalSamples; ++n)
            (void) engine.process();
    }

    CHECK (moving > 10);

    // Exact at 0: no sub is placed at all.
    plain.prepare (rate);
    plain.start (off, 50.0, 1.0, 1u, 0);
    CHECK (isExactlyZero (plain.getUnderHz()));

    // Its own attack: with a 100 ms rise the sub is absent from the first
    // 20 ms and at full strength once the rise is done. (The rise delays
    // the whole envelope, so the bloomed sub is measured on its own -- the
    // hit minus the same hit with Under at 0 -- rather than against the
    // instant one, whose decay is already 100 ms further along.)
    KickSettings bloom = s;
    bloom.startSemitones = 0.0;
    bloom.underAttackSeconds = 0.1;
    KickEngine blooming;
    const auto late = renderKickEngine (blooming, bloom, rate, 1.0);

    std::vector<double> bloomedSub (late.size());
    for (std::size_t n = 0; n < bloomedSub.size(); ++n)
        bloomedSub[n] = late[n] - without[n];

    const double first = rmsBetween (bloomedSub, rate, 0.0, 0.02);
    const double risen = rmsBetween (bloomedSub, rate, 0.10, 0.12);
    const double subEarly = rmsBetween (sub, rate, 0.0, 0.02);

    std::printf ("        [under] with a 100 ms rise the sub is %.4f in the first 20 ms and %.4f at 100 ms (instant: %.4f)\n",
                 first, risen, subEarly);

    CHECK (first < subEarly * 0.15);
    CHECK (risen > subEarly * 0.6);

    // A long sub outlives a short body and the hit still retires exactly.
    KickSettings tail = neutralKick();
    tail.decaySeconds = 0.1;
    tail.under = 0.5;
    tail.underDecay = 4.0;
    KickEngine lingering;
    const auto out = renderKickEngine (lingering, tail, rate, 1.0);
    const double lastSecond = static_cast<double> (lastNonZeroSample (out)) / rate;

    std::printf ("        [under] a 0.1 s body with a x4 sub last sounds at %.3f s, active %d\n",
                 lastSecond, lingering.isActive() ? 1 : 0);

    CHECK (lastSecond > 0.3 && lastSecond < 0.6);
    CHECK (! lingering.isActive());
}

TEZLA_TEST (knock_rings_at_its_pitch_and_is_cut_exactly)
{
    const double rate = 96000.0;

    KickSettings s = neutralKick();
    s.decaySeconds = 0.02;
    s.knock = 1.0;
    s.knockHz = 350.0;
    s.knockSeconds = 0.05;

    KickEngine engine;
    const auto out = renderKickEngine (engine, s, rate, 0.5);

    const double hz = peakHzBetween (out, rate, 200.0, 600.0);
    const int last = lastNonZeroSample (out);
    const double cutAt = static_cast<double> (last + 1) / rate;

    std::printf ("        [knock] peak at %.1f Hz; cut at %.4f s against %.4f s (four T60s), active %d\n",
                 hz, cutAt, KickEngine::kKnockTailT60s * s.knockSeconds, engine.isActive() ? 1 : 0);

    CHECK (std::abs (hz - 350.0) < 4.0);
    CHECK (std::abs (cutAt - KickEngine::kKnockTailT60s * s.knockSeconds) < 0.001);
    CHECK (! engine.isActive());

    for (std::size_t n = static_cast<std::size_t> (last) + 1; n < out.size(); ++n)
        CHECK (isExactlyZero (out[n]));

    // Nothing rings at 0.
    s.knock = 0.0;
    engine.prepare (rate);
    engine.start (s, 50.0, 1.0, 1u, 0);
    CHECK (! engine.isKnockActive());

    s.knock = 0.4;
    engine.start (s, 50.0, 1.0, 1u, 0);
    CHECK (engine.isKnockActive());
}

TEZLA_TEST (thump_is_a_low_mode_under_the_shell_that_retires_exactly)
{
    const double rate = 96000.0;

    SnareSettings s = neutralSnare();
    s.body = 0.0;
    s.tone = 0.0;
    s.thump = 1.0;
    s.thumpHz = 100.0;
    s.thumpDecaySeconds = 0.2;

    SnareEngine engine;
    engine.prepare (rate);
    engine.start (s, 200.0, 1.0, 1u, 0);
    CHECK (engine.isThumpSounding());

    const auto out = renderSnareEngine (engine, s, rate, 1.0);

    const double hz = peakHzBetween (out, rate, 60.0, 140.0);
    const double start = rmsBetween (out, rate, 0.0, 0.02);
    const double atT60 = rmsBetween (out, rate, 0.19, 0.21);
    const double fallDb = 20.0 * std::log10 (atT60 / start);

    std::printf ("        [thump] peak at %.2f Hz; %.1f dB down at its 0.2 s T60; sounding at 1 s: %d\n",
                 hz, fallDb, engine.isThumpSounding() ? 1 : 0);

    CHECK (std::abs (hz - 100.0) < 1.0);
    CHECK (fallDb < -50.0 && fallDb > -70.0);
    CHECK (! engine.isThumpSounding());

    // Exact at 0: the mode is never placed.
    s.thump = 0.0;
    engine.prepare (rate);
    engine.start (s, 200.0, 1.0, 1u, 0);
    CHECK (! engine.isThumpSounding());
}

TEZLA_TEST (ring_scales_the_upper_modes_decay_and_leaves_the_fundamental)
{
    CHECK (isExactly (SnareEngine::ringFactorFor (0.0), 1.0));
    CHECK (std::abs (SnareEngine::ringFactorFor (1.0) - SnareEngine::kRingRange) < 1.0e-12);
    CHECK (std::abs (SnareEngine::ringFactorFor (-1.0) - 1.0 / SnareEngine::kRingRange) < 1.0e-12);

    const double rate = 96000.0;
    SnareSettings s = neutralSnare();
    s.decaySeconds = 0.4;

    SnareEngine engine;
    engine.prepare (rate);

    for (double ring : { 0.0, 1.0, -1.0, 0.5 })
    {
        s.ring = ring;
        engine.start (s, 200.0, 1.0, 1u, 0);

        const double factor = SnareEngine::ringFactorFor (ring);

        CHECK (isExactly (engine.getModeT60 (0), 0.4));
        CHECK (std::abs (engine.getModeT60 (1) - 0.4 * SnareEngine::kModeDecays[1] * factor) < 1.0e-12);
        CHECK (std::abs (engine.getModeT60 (2) - 0.4 * SnareEngine::kModeDecays[2] * factor) < 1.0e-12);

        if (isExactlyZero (ring))
        {
            CHECK (isExactly (engine.getModeT60 (1), 0.4 * SnareEngine::kModeDecays[1]));
            CHECK (isExactly (engine.getModeT60 (2), 0.4 * SnareEngine::kModeDecays[2]));
        }
    }

    // Heard: with Ring at +1 the upper pair is still there at 0.3 s where at
    // -1 it has gone, the fundamental the same either way.
    s.tone = 1.0;
    s.ring = 1.0;
    SnareEngine ringing;
    const auto rung = renderSnareEngine (ringing, s, rate, 0.6);
    s.ring = -1.0;
    SnareEngine dead;
    const auto thud = renderSnareEngine (dead, s, rate, 0.6);

    // The upper modes sit at 320 and 440 Hz: their share of the energy at 0.3 s.
    const auto upperShare = [&] (const std::vector<double>& x)
    {
        std::vector<double> window (x.begin() + static_cast<long> (0.3 * rate),
                                    x.begin() + static_cast<long> (0.4 * rate));
        std::vector<double> padded (1u << 16, 0.0);
        std::copy (window.begin(), window.end(), padded.begin());
        const auto spectrum = fftOfReal (padded);
        const double binWidth = rate / static_cast<double> (padded.size());

        double low = 0.0, upper = 0.0;
        for (std::size_t bin = static_cast<std::size_t> (150.0 / binWidth); bin < static_cast<std::size_t> (250.0 / binWidth); ++bin)
            low += std::norm (spectrum[bin]);
        for (std::size_t bin = static_cast<std::size_t> (280.0 / binWidth); bin < static_cast<std::size_t> (500.0 / binWidth); ++bin)
            upper += std::norm (spectrum[bin]);
        return upper / std::max (low, 1.0e-300);
    };

    const double rungShare = upperShare (rung);
    const double thudShare = upperShare (thud);

    std::printf ("        [ring] upper modes against the fundamental at 0.3 s: %.4f at +1, %.6f at -1\n", rungShare, thudShare);

    CHECK (rungShare > thudShare * 100.0);
}

TEZLA_TEST (pan_is_a_balance_whose_centre_is_the_dual_mono_render)
{
    CHECK (isExactly (Engine::balanceLeft (0.0), 1.0));
    CHECK (isExactly (Engine::balanceRight (0.0), 1.0));
    CHECK (isExactlyZero (Engine::balanceRight (-1.0)));
    CHECK (isExactlyZero (Engine::balanceLeft (1.0)));
    CHECK (isExactly (Engine::balanceLeft (-1.0), 1.0));
    CHECK (isExactly (Engine::balanceRight (0.5), 1.0));
    CHECK (isExactly (Engine::balanceLeft (0.5), 0.5));

    const double rate = 48000.0;
    const int samples = 24000;
    const std::vector<Hit> hits { { 4800, 36, 1.0 } };   // after the smoothers have settled

    EngineParameters p;
    p.kick1 = everythingOn();

    const auto centre = renderBoth (p, rate, samples, hits);

    // Centre: both channels carry the pad at unity -- the old dual mono, and
    // the left channel is what the mono `render` always returned.
    CHECK (centre.left == centre.right);
    CHECK (centre.left == render (p, rate, samples, hits));
    CHECK (rmsOf (centre.left) > 0.01);

    // Hard left: the right channel is exactly nothing, the left untouched.
    p.pan[static_cast<int> (PadIndex::kick1)] = -1.0;
    const auto left = renderBoth (p, rate, samples, hits);
    CHECK (left.left == centre.left);

    for (const double sample : left.right)
        CHECK (isExactlyZero (sample));

    // Half right: the right channel untouched, the left exactly half --
    // a multiply by 0.5 is exact, so this is bit for bit too.
    p.pan[static_cast<int> (PadIndex::kick1)] = 0.5;
    const auto half = renderBoth (p, rate, samples, hits);
    CHECK (half.right == centre.right);

    bool halved = true;
    for (std::size_t n = 0; n < half.left.size(); ++n)
        halved = halved && isExactly (half.left[n], centre.left[n] * 0.5);
    CHECK (halved);

    // The pan is smoothed per sample, so the block size cannot reach it.
    p.pan[static_cast<int> (PadIndex::kick1)] = 0.3;
    const auto small = renderBoth (p, rate, samples, hits, 64);
    const auto large = renderBoth (p, rate, samples, hits, 512);
    CHECK (small.left == large.left && small.right == large.right);

    std::printf ("        [pan] centre dual mono; hard left leaves the right channel exact zeros; 64 vs 512 blocks identical\n");
}

// ---------------------------------------------------------------------------
// I4.5 -- the rig's fifth round: the pads render mid and side, the sources
// spread across the field, a room, a clap layer under the snare, a wash on
// the plate, and the rattle's own decay, tone and tension
// ---------------------------------------------------------------------------

namespace
{
struct MidSide
{
    std::vector<double> mid, side;
};

/// One hit of any engine, stepped on the control grid, with its side
/// alongside. `start` is the engine's own start call.
template <typename EngineType, typename Start>
MidSide renderMidSide (EngineType& engine, double rate, double seconds, Start&& start)
{
    engine.prepare (rate);
    start (engine);

    const int total = static_cast<int> (seconds * rate);
    MidSide out;
    out.mid.resize (static_cast<std::size_t> (total));
    out.side.resize (static_cast<std::size_t> (total));

    for (int n = 0; n < total; ++n)
    {
        if (n % Engine::kControlIntervalSamples == 0)
            engine.advanceControl (Engine::kControlIntervalSamples);

        out.mid[static_cast<std::size_t> (n)] = engine.process (out.side[static_cast<std::size_t> (n)]);
    }

    return out;
}

bool allExactlyZero (const std::vector<double>& x)
{
    return std::all_of (x.begin(), x.end(), [] (double v) { return isExactlyZero (v); });
}

/// A copy of `x` between two instants, so the spectral helpers -- which look
/// at the START of what they are given -- can look at a tail.
std::vector<double> slice (const std::vector<double>& x, double rate, double from, double to)
{
    const auto first = std::min (x.size(), static_cast<std::size_t> (from * rate));
    const auto last = std::min (x.size(), static_cast<std::size_t> (to * rate));
    return std::vector<double> (x.begin() + static_cast<std::ptrdiff_t> (first),
                                x.begin() + static_cast<std::ptrdiff_t> (last));
}

std::vector<double> difference (const std::vector<double>& a, const std::vector<double>& b)
{
    std::vector<double> d (a.size());

    for (std::size_t n = 0; n < a.size(); ++n)
        d[n] = a[n] - b[n];

    return d;
}

std::vector<double> sum (const std::vector<double>& a, const std::vector<double>& b, double signOfB)
{
    std::vector<double> d (a.size());

    for (std::size_t n = 0; n < a.size(); ++n)
        d[n] = a[n] + signOfB * b[n];

    return d;
}

/// The side of a stereo render, (L - R) / 2, and its mid, (L + R) / 2.
std::vector<double> sideOf (const StereoRender& r)
{
    std::vector<double> s (r.left.size());

    for (std::size_t n = 0; n < s.size(); ++n)
        s[n] = 0.5 * (r.left[n] - r.right[n]);

    return s;
}

std::vector<double> midOf (const StereoRender& r)
{
    std::vector<double> m (r.left.size());

    for (std::size_t n = 0; n < m.size(); ++n)
        m[n] = 0.5 * (r.left[n] + r.right[n]);

    return m;
}

/// A snare that is wires and nothing else, so the wires can be measured
/// whole: no shell, no crack, a long burst.
SnareSettings wiresOnlySnare()
{
    SnareSettings s = neutralSnare();
    s.body = 0.0;
    s.wires = 0.6;
    s.snappyHz = 3000.0;
    s.wiresHoldSeconds = 0.1;
    s.wiresDecaySeconds = 0.2;
    return s;
}

/// A snare whose rattle can be heard on its own: a long shell, a short stick
/// burst, and Rattle at 1, so that from 100 ms on the wires are the shell's
/// throw and nothing else. `rattleOnly` renders it with and without Rattle
/// and returns the difference -- the shell's path is the same in both.
SnareSettings rattleRig()
{
    SnareSettings s = neutralSnare();
    s.body = 1.0;
    s.decaySeconds = 1.0;
    s.tone = 0.6;
    s.wires = 0.6;
    s.wiresHoldSeconds = 0.0;
    s.wiresDecaySeconds = 0.05;
    s.rattle = 1.0;
    return s;
}

std::vector<double> rattleOnly (const SnareSettings& s, double rate, double seconds)
{
    SnareSettings quiet = s;
    quiet.rattle = 0.0;

    SnareEngine with, without;
    return difference (renderSnareEngine (with, s, rate, seconds, 180.0),
                       renderSnareEngine (without, quiet, rate, seconds, 180.0));
}
} // namespace

TEZLA_TEST (every_engine_writes_an_exact_zero_side_when_nothing_is_spread)
{
    // The side is the new thing in the path, and a pad that has nothing
    // spread must not have one: exactly 0.0 at every sample, and the mid it
    // returns is the mono render bit for bit -- the round's golden render in
    // plugins/Ictus/PLAN.md is the same claim over the whole kit.
    const double rate = 96000.0;

    {
        HatSettings s = bareHat();
        s.plate = 0.5;
        s.air = 0.5;
        s.grit = 0.3;
        s.drive = 0.4;
        HatEngine mono, stereo;
        const auto reference = renderHatEngine (mono, s, rate, 0.4);
        const auto ms = renderMidSide (stereo, rate, 0.4, [&] (HatEngine& e) { e.start (s, false, 1.0, 99u, 0); });
        CHECK (ms.mid == reference);
        CHECK (allExactlyZero (ms.side));
        CHECK (! stereo.isSideOn());
    }

    {
        SnareSettings s = snareEverythingOn();
        SnareEngine mono, stereo;
        const auto reference = renderSnareEngine (mono, s, rate, 0.4, 200.0);
        const auto ms = renderMidSide (stereo, rate, 0.4, [&] (SnareEngine& e) { e.start (s, 200.0, 1.0, 99u, 0); });
        CHECK (ms.mid == reference);
        CHECK (allExactlyZero (ms.side));
        CHECK (! stereo.isWiresSideOn());
    }

    {
        ClapSettings s;
        ClapEngine mono, stereo;
        const auto reference = renderClapEngine (mono, s, rate, 0.4);
        const auto ms = renderMidSide (stereo, rate, 0.4, [&] (ClapEngine& e) { e.start (s, 1.0, 99u, 0); });
        CHECK (ms.mid == reference);
        CHECK (allExactlyZero (ms.side));
        CHECK (! stereo.isSideOn());
    }

    {
        KickSettings s = everythingOn();
        KickEngine mono, stereo;
        const auto reference = renderKickEngine (mono, s, rate, 0.4);
        const auto ms = renderMidSide (stereo, rate, 0.4, [&] (KickEngine& e) { e.start (s, 50.0, 1.0, 5u, 0); });
        CHECK (ms.mid == reference);
        CHECK (allExactlyZero (ms.side));
    }
}

TEZLA_TEST (metal_spread_leaves_the_mid_untouched_and_air_spread_holds_the_channel_level)
{
    const double rate = 96000.0;

    // METAL: the six pulses and the plate's modes weighted across the field.
    // A linear spread -- the mid is the mono render bit for bit, so a mono
    // fold of the pair is exactly the hat that was there before.
    {
        HatSettings s = bareHat();
        s.plate = 0.6;
        HatEngine mono;
        const auto reference = renderHatEngine (mono, s, rate, 0.4);

        s.metalStereo = 1.0;
        HatEngine spread;
        const auto ms = renderMidSide (spread, rate, 0.4, [&] (HatEngine& e) { e.start (s, false, 1.0, 99u, 0); });

        const double sideRms = rmsOf (ms.side);
        const double midRms = rmsOf (ms.mid);
        std::printf ("        [metal spread] mid RMS %.5f (mono %.5f), side RMS %.5f\n", midRms, rmsOf (reference), sideRms);

        CHECK (ms.mid == reference);
        CHECK (sideRms > 0.1 * midRms);
    }

    // AIR: a second, independent hiss on the side, the mid scaled by
    // 1 / sqrt (1 + s^2) so a channel's hiss holds its level whatever the
    // spread; the mono fold of a full spread is 3 dB down, and the tooltip
    // says so. Heard through the same subtraction as the R1 hiss tests: the
    // hat with Air minus the hat without, which with Drive and Grit off is
    // the hiss and nothing else.
    {
        HatSettings s = hissRig();
        s.air = 0.3;

        HatSettings quiet = s;
        quiet.air = 0.0;

        HatEngine plain, none;
        const auto hissMono = difference (renderHatEngine (plain, s, rate, 0.3, false, 1.0, 21u),
                                          renderHatEngine (none, quiet, rate, 0.3, false, 1.0, 21u));

        s.airStereo = 1.0;
        HatEngine spread, noneSpread;
        const auto ms = renderMidSide (spread, rate, 0.3, [&] (HatEngine& e) { e.start (s, false, 1.0, 21u, 0); });
        quiet.airStereo = 1.0;
        const auto silentMid = renderHatEngine (noneSpread, quiet, rate, 0.3, false, 1.0, 21u);

        const auto hissMid = difference (ms.mid, silentMid);
        const auto hissLeft = sum (hissMid, ms.side, 1.0);
        const auto hissRight = sum (hissMid, ms.side, -1.0);

        const double mono = rmsOf (slice (hissMono, rate, 0.02, 0.3));
        const double mid = rmsOf (slice (hissMid, rate, 0.02, 0.3));
        const double left = rmsOf (slice (hissLeft, rate, 0.02, 0.3));
        const double right = rmsOf (slice (hissRight, rate, 0.02, 0.3));

        std::printf ("        [air spread] hiss RMS: mono %.5f, mid %.5f (x%.3f), left %.5f, right %.5f\n",
                     mono, mid, mid / mono, left, right);

        CHECK (spread.isSideOn());
        CHECK (isExactly (spread.getAirMidLevel(), 0.3 / std::sqrt (2.0)));
        CHECK (std::abs (mid / mono - 1.0 / std::sqrt (2.0)) < 0.08);
        CHECK (std::abs (left / mono - 1.0) < 0.12);
        CHECK (std::abs (right / mono - 1.0) < 0.12);
    }
}

TEZLA_TEST (wash_feeds_the_plate_for_half_the_decay_and_is_over_exactly)
{
    // The plate struck by six pulses rings 64 pure lines; a real pair is fed
    // and re-fed for as long as it moves. Wash drives every mode with noise
    // that falls away over half the pad's decay, weighted so that at 1 it
    // puts about as much energy into the plate as the strike did, and is
    // then over exactly -- a noisier strike and a shimmer in the tail.
    const double rate = 96000.0;

    const auto energyOf = [] (const std::vector<double>& x)
    {
        double e = 0.0;
        for (double v : x)
            e += v * v;
        return e;
    };

    for (double decay : { 0.1, 0.5 })
    {
        HatSettings s = bareHat();
        s.plate = 1.0;
        s.wash = 0.0;
        s.decayClosedSeconds = decay;

        HatEngine dry;
        dry.prepare (rate);
        dry.start (s, false, 1.0, 99u, 0);
        CHECK (! dry.isWashing());
        const auto without = renderHatEngine (dry, s, rate, 1.0);

        s.wash = 1.0;
        HatEngine wet;
        wet.prepare (rate);
        wet.start (s, false, 1.0, 99u, 0);
        CHECK (wet.isWashing());

        // Still feeding at 0.3 of the decay, over by 0.7 of it.
        std::vector<double> with (static_cast<std::size_t> (rate));
        bool washingAtThird = false;
        bool washingAtTwoThirds = true;

        for (std::size_t n = 0; n < with.size(); ++n)
        {
            if (n % static_cast<std::size_t> (Engine::kControlIntervalSamples) == 0)
                wet.advanceControl (Engine::kControlIntervalSamples);

            with[n] = wet.process();

            if (n == static_cast<std::size_t> (0.3 * decay * rate))
                washingAtThird = wet.isWashing();

            if (n == static_cast<std::size_t> (0.7 * decay * rate))
                washingAtTwoThirds = wet.isWashing();
        }

        const double ratio = energyOf (with) / energyOf (without);

        std::printf ("        [wash] %.1f s pad: the hit's energy x%.3f with Wash at 1 (2.0 by design); feeding at 0.3 of the decay: %s, at 0.7: %s\n",
                     decay, ratio, washingAtThird ? "yes" : "no", washingAtTwoThirds ? "yes" : "no");

        CHECK (ratio > 1.6 && ratio < 2.3);
        CHECK (washingAtThird);
        CHECK (! washingAtTwoThirds);
        CHECK (! wet.isWashing());
    }
}

TEZLA_TEST (wire_spread_is_a_second_stream_on_the_side_that_holds_the_channel_level)
{
    const double rate = 96000.0;

    SnareSettings s = wiresOnlySnare();

    SnareEngine plain;
    const auto ms0 = renderMidSide (plain, rate, 0.3, [&] (SnareEngine& e) { e.start (s, 200.0, 1.0, 99u, 0); });
    CHECK (allExactlyZero (ms0.side));
    CHECK (! plain.isWiresSideOn());

    s.wiresStereo = 1.0;
    SnareEngine spread;
    const auto ms1 = renderMidSide (spread, rate, 0.3, [&] (SnareEngine& e) { e.start (s, 200.0, 1.0, 99u, 0); });
    CHECK (spread.isWiresSideOn());

    const double mono = rmsOf (slice (ms0.mid, rate, 0.01, 0.1));
    const double mid = rmsOf (slice (ms1.mid, rate, 0.01, 0.1));
    const double left = rmsOf (slice (sum (ms1.mid, ms1.side, 1.0), rate, 0.01, 0.1));
    const double right = rmsOf (slice (sum (ms1.mid, ms1.side, -1.0), rate, 0.01, 0.1));

    std::printf ("        [wire spread] wires RMS: mono %.5f, mid %.5f (x%.3f), left %.5f, right %.5f\n",
                 mono, mid, mid / mono, left, right);

    CHECK (std::abs (mid / mono - 1.0 / std::sqrt (2.0)) < 0.08);
    CHECK (std::abs (left / mono - 1.0) < 0.12);
    CHECK (std::abs (right / mono - 1.0) < 0.12);
}

TEZLA_TEST (wires_tilt_and_bed_colour_the_wires)
{
    const double rate = 96000.0;

    SnareSettings s = wiresOnlySnare();

    const auto centroidAt = [&] (double tilt, double bed)
    {
        SnareSettings t = s;
        t.wiresTilt = tilt;
        t.bed = bed;
        SnareEngine e;
        return centroidHz (renderSnareEngine (e, t, rate, 0.15, 200.0), rate, 0.15);
    };

    const double dark = centroidAt (-1.0, 0.0);
    const double flat = centroidAt (0.0, 0.0);
    const double bright = centroidAt (1.0, 0.0);

    std::printf ("        [wires tilt] centroid %.0f Hz dark, %.0f flat, %.0f bright\n", dark, flat, bright);

    CHECK (dark < flat * 0.9);
    CHECK (bright > flat * 1.08);

    // The bed: six resonances at the published-style ratios above the
    // corner. The lowest, 0.71 x 3 kHz = 2130 Hz, is where the strongest
    // peak below the corner sits with Bed up -- and it is not there without.
    SnareSettings b = s;
    b.bed = 1.0;
    SnareEngine bedded, plain;
    const auto withBed = renderSnareEngine (bedded, b, rate, 0.3, 200.0);
    const auto without = renderSnareEngine (plain, s, rate, 0.3, 200.0);

    const double peak = peakHzBetween (withBed, rate, 1900.0, 2400.0);
    std::printf ("        [bed] strongest peak between 1.9 and 2.4 kHz: %.0f Hz (expected 2130)\n", peak);
    CHECK (std::abs (peak - 2130.0) < 70.0);
    CHECK (withBed != without);
}

TEZLA_TEST (head_moves_the_upper_modes_from_the_snares_ratios_to_a_toms)
{
    // Fletcher & Rossing Table 18.7 (read first-hand): a tom's second and
    // third modes sit at 2.16 and 3.14 times the fundamental, against the
    // snare set's 1.6 and 2.2. Head glides the ratios between the two,
    // geometrically; 0 is the snare set exactly.
    const double rate = 48000.0;

    SnareSettings s = neutralSnare();
    SnareEngine e;
    e.prepare (rate);

    s.head = 0.0;
    e.start (s, 200.0, 1.0, 1u, 0);
    CHECK (isExactly (e.getModeRatio (1), SnareEngine::kModeRatios[1]));
    CHECK (isExactly (e.getModeRatio (2), SnareEngine::kModeRatios[2]));

    s.head = 1.0;
    e.start (s, 200.0, 1.0, 1u, 0);
    std::printf ("        [head] ratios at 1: %.4f and %.4f (tom 2.16 and 3.14)\n", e.getModeRatio (1), e.getModeRatio (2));
    CHECK_NEAR (e.getModeRatio (1), 2.16, 1.0e-9);
    CHECK_NEAR (e.getModeRatio (2), 3.14, 1.0e-9);

    s.head = 0.5;
    e.start (s, 200.0, 1.0, 1u, 0);
    CHECK_NEAR (e.getModeRatio (1), std::sqrt (1.6 * 2.16), 1.0e-9);
}

TEZLA_TEST (rattle_decay_ends_the_shells_throw_while_the_shell_rings_on)
{
    // The shell's drive on the wires used to last exactly as long as the
    // shell -- and Ring made the shell last. Rattle decay gives the throw a
    // fall of its own; 0 is the follower as it always was.
    const double rate = 96000.0;

    SnareSettings s = rattleRig();
    s.rattleDecaySeconds = 0.0;

    SnareEngine shell;
    shell.prepare (rate);
    shell.start (s, 180.0, 1.0, 99u, 0);
    const auto forever = rattleOnly (s, rate, 0.5);

    SnareEngine e;
    e.prepare (rate);
    e.start (s, 180.0, 1.0, 99u, 0);

    for (int n = 0; n < static_cast<int> (0.3 * rate); ++n)
    {
        if (n % Engine::kControlIntervalSamples == 0)
            e.advanceControl (Engine::kControlIntervalSamples);
        (void) e.process();
    }

    CHECK (isExactly (e.getRattleEnvelope(), 1.0));
    CHECK (e.isRattling());
    CHECK (e.isShellSounding());

    s.rattleDecaySeconds = 0.05;
    const auto falling = rattleOnly (s, rate, 0.5);

    SnareEngine f;
    f.prepare (rate);
    f.start (s, 180.0, 1.0, 99u, 0);

    for (int n = 0; n < static_cast<int> (0.3 * rate); ++n)
    {
        if (n % Engine::kControlIntervalSamples == 0)
            f.advanceControl (Engine::kControlIntervalSamples);
        (void) f.process();
    }

    CHECK (! f.isRattling());
    CHECK (f.isShellSounding());

    // The first 2 ms, where a 50 ms fall to -60 dB has taken 2.4 dB: the
    // throw starts at the level it always started at.
    const double earlyForever = rmsBetween (forever, rate, 0.0, 0.002);
    const double lateForever = rmsBetween (forever, rate, 0.2, 0.3);
    const double earlyFalling = rmsBetween (falling, rate, 0.0, 0.002);
    const double lateFalling = rmsBetween (falling, rate, 0.2, 0.3);

    std::printf ("        [rattle decay] shell's throw RMS 0-2 ms %.5f, 200-300 ms %.6f with the shell's decay; "
                 "%.5f and %.9f with a 50 ms decay of its own\n",
                 earlyForever, lateForever, earlyFalling, lateFalling);

    CHECK (lateForever > 0.02 * earlyForever);
    CHECK (isExactlyZero (lateFalling));
    CHECK (earlyFalling > 0.6 * earlyForever && earlyFalling <= earlyForever);
}

TEZLA_TEST (rattle_tone_gives_the_shells_throw_a_corner_of_its_own)
{
    const double rate = 96000.0;

    SnareSettings s = rattleRig();

    const auto centroidAt = [&] (double octaves)
    {
        SnareSettings t = s;
        t.rattleToneOctaves = octaves;
        return centroidHz (slice (rattleOnly (t, rate, 0.4), rate, 0.1, 0.4), rate, 0.3);
    };

    const double dark = centroidAt (-2.0);
    const double flat = centroidAt (0.0);
    const double bright = centroidAt (2.0);

    std::printf ("        [rattle tone] the shell's throw: centroid %.0f Hz two octaves down, %.0f at the snap's corner, %.0f two up\n",
                 dark, flat, bright);

    CHECK (dark < flat * 0.6);
    CHECK (bright > flat * 1.25);

    SnareEngine e;
    e.prepare (rate);
    e.start (s, 180.0, 1.0, 99u, 0);
    CHECK (! e.isRattleToneOn());

    s.rattleToneOctaves = -1.0;
    e.start (s, 180.0, 1.0, 99u, 0);
    CHECK (e.isRattleToneOn());
}

TEZLA_TEST (rattle_tension_lifts_the_snares_only_above_the_heads_critical_amplitude)
{
    // Fletcher & Rossing 18.13: the snares leave the head only above a
    // certain head amplitude, which rises with their tension; a soft blow at
    // high tension does not rattle at all (Fig. 18.10). Tension 0 is the
    // smooth follower and never evaluates a lift.
    const double rate = 96000.0;

    SnareSettings s = rattleRig();
    s.decaySeconds = 0.25;
    s.velocityWires = 0.4;

    const auto liftEnds = [&] (double tension, double velocity, double& earlyPeak)
    {
        SnareSettings t = s;
        t.rattleTension = tension;
        SnareEngine e;
        e.prepare (rate);
        e.start (t, 180.0, velocity, 99u, 0);

        int last = -1;
        earlyPeak = 0.0;

        for (int n = 0; n < static_cast<int> (0.4 * rate); ++n)
        {
            if (n % Engine::kControlIntervalSamples == 0)
                e.advanceControl (Engine::kControlIntervalSamples);

            (void) e.process();

            if (! isExactlyZero (e.getLiftLevel()))
                last = n;

            if (n < static_cast<int> (0.01 * rate))
                earlyPeak = std::max (earlyPeak, e.getLiftLevel());
        }

        return last / rate;
    };

    double peak0 = 0.0, peakHard = 0.0, peakSoft = 0.0, peakHalf = 0.0;
    const double none = liftEnds (0.0, 1.0, peak0);
    const double hard = liftEnds (1.0, 1.0, peakHard);
    const double soft = liftEnds (1.0, 0.4, peakSoft);
    const double half = liftEnds (0.5, 1.0, peakHalf);

    std::printf ("        [tension] lift ends at %.4f s (tension 1, hard), %.4f s (tension 1, soft), %.4f s (tension 0.5); "
                 "peak lift %.3f hard, %.3f soft; tension 0 never lifts (%.0f)\n",
                 hard, soft, half, peakHard, peakSoft, none);

    CHECK (none < 0.0);
    CHECK (isExactlyZero (peak0));
    CHECK (peakHard > 0.1);
    CHECK (hard > 0.01 && hard < 0.06);
    CHECK (soft < hard);
    CHECK (half > hard);
}

TEZLA_TEST (burst_spread_places_the_claps_bursts_across_the_field)
{
    const double rate = 96000.0;

    ClapSettings s;
    ClapEngine plain;
    const auto ms0 = renderMidSide (plain, rate, 0.3, [&] (ClapEngine& e) { e.start (s, 1.0, 99u, 0); });
    CHECK (allExactlyZero (ms0.side));

    s.stereo = 1.0;
    ClapEngine spread;
    spread.prepare (rate);
    spread.start (s, 1.0, 99u, 0);
    CHECK (spread.isSideOn());

    const auto ms1 = renderMidSide (spread, rate, 0.3, [&] (ClapEngine& e) { e.start (s, 1.0, 99u, 0); });

    const double mid = rmsOf (ms1.mid);
    const double side = rmsOf (ms1.side);
    std::printf ("        [burst spread] clap mid RMS %.5f, side RMS %.5f\n", mid, side);

    CHECK (side > 0.1 * mid);
}

TEZLA_TEST (drop_curve_bends_the_kicks_landing_and_is_the_exponential_at_zero)
{
    // Curve -1 lands on a straight line in cents; at half the stated time
    // half the depth remains, where the exponential has 8 % left.
    const double rate = 96000.0;

    KickSettings s = neutralKick();
    s.startSemitones = 12.0;
    s.dropSeconds = 0.1;
    s.velocityDrop = 0.0;

    const auto ratioAtHalfTime = [&] (double curve)
    {
        KickSettings k = s;
        k.dropCurve = curve;
        KickEngine e;
        e.prepare (rate);
        e.start (k, 50.0, 1.0, 5u, 0);

        for (int n = 0; n < static_cast<int> (0.05 * rate); ++n)
        {
            if (n % Engine::kControlIntervalSamples == 0)
                e.advanceControl (Engine::kControlIntervalSamples);
            (void) e.process();
        }

        return e.currentHz() / 50.0;
    };

    const double exponential = ratioAtHalfTime (0.0);
    const double line = ratioAtHalfTime (-1.0);
    const double snap = ratioAtHalfTime (1.0);

    std::printf ("        [drop curve] pitch ratio at half the drop: %.4f exponential, %.4f line, %.4f snap\n",
                 exponential, line, snap);

    CHECK (std::abs (line - std::exp2 (6.0 / 12.0)) < 0.03);
    CHECK (std::abs (exponential - std::exp2 (12.0 * std::exp (-2.5) / 12.0)) < 0.02);
    CHECK (snap > line);

    KickEngine e;
    e.prepare (rate);
    e.start (s, 50.0, 1.0, 5u, 0);
    CHECK (isExactlyZero (e.getDropCurve()));
}

TEZLA_TEST (width_scales_a_pads_side_and_folds_it_to_mono_at_zero)
{
    const double rate = 48000.0;
    const int samples = 24000;

    EngineParameters p;
    p.hat = bareHat();
    p.hat.air = 0.4;
    p.hat.airStereo = 1.0;
    p.hat.metalStereo = 1.0;
    p.hat.plate = 0.5;

    const std::vector<Hit> hits { { 100, 42, 1.0 } };
    constexpr int hat = static_cast<int> (PadIndex::hatClosed);

    p.width[hat] = 1.0;
    const auto unity = renderBoth (p, rate, samples, hits);
    p.width[hat] = 0.0;
    const auto folded = renderBoth (p, rate, samples, hits);
    p.width[hat] = 2.0;
    const auto doubled = renderBoth (p, rate, samples, hits);

    const double sideUnity = rmsOf (sideOf (unity));
    const double sideDouble = rmsOf (sideOf (doubled));
    const double midUnity = rmsOf (midOf (unity));
    const double midDouble = rmsOf (midOf (doubled));

    std::printf ("        [width] side RMS %.5f at 100 %%, %.5f at 200 %% (x%.4f); mid %.5f and %.5f\n",
                 sideUnity, sideDouble, sideDouble / sideUnity, midUnity, midDouble);

    CHECK (sideUnity > 0.0);
    CHECK (folded.left == folded.right);
    CHECK (std::abs (sideDouble / sideUnity - 2.0) < 0.01);
    CHECK (std::abs (midDouble / midUnity - 1.0) < 1.0e-9);
}

TEZLA_TEST (mono_below_keeps_the_low_end_of_a_spread_pad_in_the_centre)
{
    // A room on the kick puts the kick's sub on the side. Mono below
    // high-passes the side, so the low band folds to mono whatever is spread
    // above it -- read by the suite's own StereoAnalyser, whose low band
    // ends at 120 Hz.
    const double rate = 48000.0;
    const int samples = 24000;

    EngineParameters p;
    p.kick1 = neutralKick();
    p.kick1.decaySeconds = 0.4;
    p.room[static_cast<int> (RoomIndex::kick1)].level = 1.0;
    p.room[static_cast<int> (RoomIndex::kick1)].seconds = 0.2;
    p.room[static_cast<int> (RoomIndex::kick1)].toneHz = 20000.0;

    const std::vector<Hit> hits { { 100, 36, 1.0 } };
    constexpr int kick = static_cast<int> (PadIndex::kick1);

    const auto lowCorrelation = [&] (double monoBelowHz)
    {
        p.monoBelowHz[kick] = monoBelowHz;
        const auto r = renderBoth (p, rate, samples, hits);

        StereoAnalyser analyser;
        analyser.prepare (rate, 0.4);
        const double* channels[2] = { r.left.data(), r.right.data() };
        analyser.process (channels, 2, samples);
        return analyser.getBandCorrelation (StereoAnalyser::low);
    };

    const double open = lowCorrelation (0.0);
    const double centred = lowCorrelation (150.0);

    std::printf ("        [mono below] low-band correlation %.3f with the side open, %.3f with Mono below at 150 Hz\n",
                 open, centred);

    CHECK (centred > open);
    CHECK (centred > 0.9);
}

TEZLA_TEST (a_room_adds_its_pads_reflections_and_is_not_run_at_zero)
{
    const double rate = 48000.0;
    const int samples = 24000;

    EngineParameters p;
    p.kick1 = neutralKick();
    p.kick1.decaySeconds = 0.05;
    p.kick1.tailMix = 0.0;

    const std::vector<Hit> hits { { 0, 36, 1.0 } };

    // At 0 the room is not run at all: 100 ms in, with the kick still
    // sounding, its line has never been fed.
    auto dryEngine = heapEngine();
    dryEngine->prepare (rate, 512);
    (void) render (p, rate, 4800, hits, 256, {}, dryEngine.get());
    CHECK (! dryEngine->room (RoomIndex::kick1).isActive());
    CHECK (isExactlyZero (dryEngine->getRoomLevelNow (RoomIndex::kick1)));
    const auto dry = render (p, rate, samples, hits);

    auto& room = p.room[static_cast<int> (RoomIndex::kick1)];
    room.level = 1.0;
    room.seconds = 0.2;
    room.toneHz = 4000.0;

    auto wetEngine = heapEngine();
    wetEngine->prepare (rate, 512);
    const auto wet = render (p, rate, 4800, hits, 256, {}, wetEngine.get());
    CHECK (wetEngine->room (RoomIndex::kick1).isActive());
    CHECK (isExactly (wetEngine->room (RoomIndex::kick1).getLengthSeconds(), 0.2));
    CHECK (isExactly (wetEngine->room (RoomIndex::kick1).getToneHz(), 4000.0));

    const auto wetFull = render (p, rate, samples, hits);

    const double tailDry = rmsBetween (dry, rate, 0.15, 0.25);
    const double tailWet = rmsBetween (wetFull, rate, 0.15, 0.25);
    const double strikeDry = rmsBetween (dry, rate, 0.0, 0.02);
    const double strikeWet = rmsBetween (wetFull, rate, 0.0, 0.02);

    std::printf ("        [room] kick RMS 150-250 ms %.6f dry, %.5f with a 200 ms room; the strike %.4f dry, %.4f wet\n",
                 tailDry, tailWet, strikeDry, strikeWet);

    CHECK (tailWet > tailDry * 10.0);
    CHECK (std::abs (strikeWet / strikeDry - 1.0) < 0.5);
    (void) wet;
}

TEZLA_TEST (the_clap_layer_starts_under_snare_one_after_its_offset_at_every_block_size)
{
    const double rate = 48000.0;

    EngineParameters p;
    p.snare1 = snareEverythingOn();
    p.snareClap = 0.8;
    p.snareClapOffsetSeconds = 0.01;

    const std::vector<Hit> hits { { 0, 38, 1.0 } };

    // At 48 k with Auto (x4) the offset is 480 host samples: pending at 300,
    // sounding at 600, and counted as a hit of its own.
    auto engine = heapEngine();
    engine->prepare (rate, 512);
    (void) render (p, rate, 300, hits, 64, {}, engine.get());
    CHECK (engine->isSnareClapPending());
    CHECK (! engine->snareClapLayer().isActive());
    CHECK (engine->activeHitCount() == 1);

    (void) render (p, rate, 300, {}, 64, {}, engine.get());
    CHECK (! engine->isSnareClapPending());
    CHECK (engine->snareClapLayer().isActive());
    CHECK (engine->activeHitCount() == 2);

    const auto small = renderBoth (p, rate, 24000, hits, 64);
    const auto large = renderBoth (p, rate, 24000, hits, 512);
    const auto odd = renderBoth (p, rate, 24000, hits, 97);

    CHECK (small.left == large.left && small.left == odd.left);
    CHECK (small.right == large.right && small.right == odd.right);

    // The layer is there: the snare with it is not the snare without.
    EngineParameters bare = p;
    bare.snareClap = 0.0;
    const auto alone = renderBoth (bare, rate, 24000, hits, 256);
    CHECK (alone.left != small.left);

    std::printf ("        [clap layer] snare RMS %.4f alone, %.4f with the clap under it 10 ms behind\n",
                 rmsOf (alone.left), rmsOf (small.left));

    // The offset is exact to the sample, not to the control grid: at 192 kHz
    // with oversampling off the internal rate is the host's, and an offset of
    // 1363 samples -- not a multiple of the 32-sample grid -- starts the layer
    // on sample 1363, because the render loop cuts a chunk there for it.
    {
        EngineParameters q = p;
        q.oversampling = OversamplingMode::Off;
        q.snareClapOffsetSeconds = 1363.0 / 192000.0;

        EngineParameters without = q;
        without.snareClap = 0.0;

        const auto layered = render (q, 192000.0, 4000, hits, 64);
        const auto plain = render (without, 192000.0, 4000, hits, 64);

        int first = -1;

        for (std::size_t n = 0; n < layered.size() && first < 0; ++n)
            if (! isExactly (layered[n], plain[n]))
                first = static_cast<int> (n);

        std::printf ("        [clap layer] first sample the layer touches: %d (asked for 1363)\n", first);
        CHECK (first == 1363);
    }
}

TEZLA_TEST (the_kit_with_everything_spread_is_the_same_at_every_block_size)
{
    // The round's block rule: the side path, the rooms, the clap layer's
    // countdown and the rattle's own envelopes all count in samples on the
    // control grid, so 64-, 97- and 512-sample blocks produce the same bits.
    constexpr double rate = 48000.0;
    constexpr int samples = 24000;

    EngineParameters p;
    p.kick1 = everythingOn();
    p.kick1.dropCurve = -0.5;
    p.kick2 = everythingOn();
    p.kick2.dropCurve = 0.7;
    p.snare1 = snareEverythingOn();
    p.snare1.wiresStereo = 0.8;
    p.snare1.wiresTilt = 0.5;
    p.snare1.bed = 0.6;
    p.snare1.head = 0.5;
    p.snare1.rattle = 0.7;
    p.snare1.rattleDecaySeconds = 0.08;
    p.snare1.rattleToneOctaves = -1.0;
    p.snare1.rattleTension = 0.6;
    p.snare2 = p.snare1;
    p.perc = tomSettings();
    p.perc.rattle = 0.5;
    p.perc.wires = 0.3;
    p.perc.rattleTension = 1.0;
    p.hat = HatSettings {};
    p.hat.plate = 0.7;
    p.hat.wash = 0.8;
    p.hat.air = 0.4;
    p.hat.airStereo = 0.7;
    p.hat.metalStereo = 0.9;
    p.clap = ClapSettings {};
    p.clap.stereo = 0.8;
    p.snareClap = 0.6;
    p.snareClapOffsetSeconds = 0.007;

    for (int pad = 0; pad < kPadCount; ++pad)
    {
        p.width[pad] = 0.5 + 0.2 * pad;
        p.monoBelowHz[pad] = 100.0 + 50.0 * pad;
        p.pan[pad] = -0.6 + 0.15 * pad;
    }

    for (int room = 0; room < kRoomCount; ++room)
    {
        p.room[room].level = 0.4 + 0.1 * room;
        p.room[room].seconds = 0.05 + 0.03 * room;
        p.room[room].toneHz = 3000.0 + 1000.0 * room;
    }

    const std::vector<Hit> hits { { 0, 36, 0.9 }, { 1000, 38, 1.0 }, { 2500, 35, 0.7 },
                                  { 3100, 37, 0.8 }, { 4000, 36, 1.0 }, { 4700, 38, 0.6 },
                                  { 1500, 46, 0.8 }, { 2200, 42, 1.0 }, { 3600, 42, 0.5 },
                                  { 5200, 39, 0.9 }, { 6000, 40, 0.8 } };

    const auto small = renderBoth (p, rate, samples, hits, 64);
    const auto large = renderBoth (p, rate, samples, hits, 512);
    const auto odd = renderBoth (p, rate, samples, hits, 97);

    double worst = 0.0;

    for (std::size_t i = 0; i < small.left.size(); ++i)
    {
        worst = std::max (worst, std::abs (small.left[i] - large.left[i]));
        worst = std::max (worst, std::abs (small.left[i] - odd.left[i]));
        worst = std::max (worst, std::abs (small.right[i] - large.right[i]));
        worst = std::max (worst, std::abs (small.right[i] - odd.right[i]));
    }

    std::printf ("        [block rule] everything spread: worst difference across 64 / 97 / 512 = %g; side RMS %.5f\n",
                 worst, rmsOf (sideOf (small)));

    CHECK (worst == 0.0);
    CHECK (rmsOf (sideOf (small)) > 0.0);
}
