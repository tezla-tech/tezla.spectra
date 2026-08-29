// Copyright (c) 2026 The Tezla <thetezla@proton.me>
// Created by The Tezla -- https://github.com/wingit33/tezla.tech
// Music: https://soundcloud.com/thetezla | https://thetezla.bandcamp.com
// Built with development assistance from Claude (Anthropic).
// SPDX-License-Identifier: AGPL-3.0-only
// GNU AGPLv3 (see LICENSE), plus NOTICE.md's attribution term. Keep intact.

#include "TestFramework.hpp"

#include <cmath>
#include <cstdio>
#include <vector>

#include "Exciters.hpp"

using namespace tezla::malleus;

namespace
{
/// Spectral centroid of a set of per-mode weights: sum(f w^2) / sum(w^2).
[[nodiscard]] double centroidHz (const double* weights, const double* frequencies,
                                 int count)
{
    double power = 0.0;
    double weighted = 0.0;

    for (int mode = 0; mode < count; ++mode)
    {
        const double p = weights[mode] * weights[mode];
        power += p;
        weighted += frequencies[mode] * p;
    }

    return power > 0.0 ? weighted / power : 0.0;
}

struct RollEvent
{
    int sample;
    double velocity;
};

[[nodiscard]] std::vector<RollEvent> runRoll (RollClock& clock, int samples)
{
    std::vector<RollEvent> events;

    for (int n = 0; n < samples; ++n)
    {
        const double v = clock.next();

        if (v > 0.0)
            events.push_back ({ n, v });
    }

    return events;
}
} // namespace

TEZLA_TEST (the_mallet_pulse_spectrum_matches_the_closed_form)
{
    // The Hann force pulse's transform, |sinc(u) / (1 - u^2)| with u = f w,
    // is textbook Fourier arithmetic -- every value below is hand-computed
    // from that closed form, so the implementation cannot drift from the
    // mathematics without going red here.
    CHECK (hannPulseSpectrum (0.0) == 1.0);          // DC: a pulse integrates
    CHECK (hannPulseSpectrum (1.0) == 0.5);          // the removable point

    // Either side of the removable point the formula itself must approach
    // the same 1/2 -- the guard patches a hole, not a discontinuity.
    CHECK_NEAR (hannPulseSpectrum (1.000000002), 0.5, 1.0e-4);
    CHECK_NEAR (hannPulseSpectrum (0.999999998), 0.5, 1.0e-4);

    // sinc(1/2) = 2/pi; / (1 - 1/4) gives (8 / 3 pi).
    CHECK_NEAR (hannPulseSpectrum (0.5), 8.0 / (3.0 * std::numbers::pi), 1.0e-12);

    // sin(1.5 pi) = -1: |(-1 / 1.5 pi) / (1 - 2.25)| = 1 / (1.875 pi).
    CHECK_NEAR (hannPulseSpectrum (1.5), 1.0 / (1.875 * std::numbers::pi), 1.0e-12);

    // The first true null is at u = 2, then one per integer.
    CHECK (hannPulseSpectrum (2.0) < 1.0e-9);
    CHECK (hannPulseSpectrum (3.0) < 1.0e-9);

    // Lobe peaks: 1 / (u (u^2 - 1) pi) at the half-integers.
    CHECK_NEAR (hannPulseSpectrum (2.5), 1.0 / (2.5 * 5.25 * std::numbers::pi), 1.0e-12);
    CHECK_NEAR (hannPulseSpectrum (3.5), 1.0 / (3.5 * 11.25 * std::numbers::pi), 1.0e-12);

    // The u^-3 envelope is what makes the strike band-limited by
    // construction: by u = 4.5 the lobes are below 0.4% of DC.
    CHECK (hannPulseSpectrum (3.5) < hannPulseSpectrum (2.5));
    CHECK (hannPulseSpectrum (4.5) < hannPulseSpectrum (3.5));
    CHECK (hannPulseSpectrum (4.5) < 0.004);

    // Even in u -- a spectrum magnitude has no sign of frequency.
    CHECK (hannPulseSpectrum (-0.5) == hannPulseSpectrum (0.5));
}

TEZLA_TEST (the_spectral_centroid_rises_with_hardness)
{
    // The whole point of the Hardness control: felt (long contact) rolls the
    // spectrum off low, brass (short contact) lets every mode through. On a
    // 32-mode harmonic object at 100 Hz, struck at 0.29:
    //
    //   hardness 0.2   centroid  154.6 Hz   (contact 3.61 ms)
    //   hardness 0.5   centroid  429.7 Hz   (contact 1.10 ms)
    //   hardness 0.9   centroid 1437.6 Hz   (contact 0.22 ms)
    //
    // measured from this very code and pinned, so the hardness -> contact ->
    // rolloff chain cannot silently change shape.
    constexpr int kModes = 32;
    double frequencies[kModes];
    double soft[kModes];
    double mid[kModes];
    double hard[kModes];

    for (int mode = 0; mode < kModes; ++mode)
        frequencies[mode] = 100.0 * (mode + 1);

    malletWeights (soft, frequencies, kModes, 0.29, 0.2, 1.0);
    malletWeights (mid, frequencies, kModes, 0.29, 0.5, 1.0);
    malletWeights (hard, frequencies, kModes, 0.29, 0.9, 1.0);

    const double softCentroid = centroidHz (soft, frequencies, kModes);
    const double midCentroid = centroidHz (mid, frequencies, kModes);
    const double hardCentroid = centroidHz (hard, frequencies, kModes);

    std::printf ("        [mallet] centroid soft %.1f Hz, mid %.1f Hz, hard %.1f Hz\n",
                 softCentroid, midCentroid, hardCentroid);

    CHECK (softCentroid < midCentroid);
    CHECK (midCentroid < hardCentroid);

    CHECK_NEAR (softCentroid, 154.6, 1.0);
    CHECK_NEAR (midCentroid, 429.7, 1.0);
    CHECK_NEAR (hardCentroid, 1437.6, 1.0);

    // And velocity is a pure scale: half velocity, exactly half the weight
    // (multiplying by a power of two commutes with rounding).
    double halfVelocity[kModes];
    malletWeights (halfVelocity, frequencies, kModes, 0.29, 0.5, 0.5);

    for (int mode = 0; mode < kModes; ++mode)
        CHECK (halfVelocity[mode] == 0.5 * mid[mode]);
}

TEZLA_TEST (a_midpoint_strike_silences_the_even_modes)
{
    // sin(k pi / 2) = 0 for even k: strike the middle and every even mode
    // sits on a node. The plan asks for 40 dB of suppression; the closed
    // form delivers ~300 dB (the weights land at ~1e-16 of the odd modes,
    // the rounding of pi itself), asserted here at 1e-12.
    constexpr int kModes = 16;
    double frequencies[kModes];
    double weights[kModes];

    for (int mode = 0; mode < kModes; ++mode)
        frequencies[mode] = 55.0 * (mode + 1);

    malletWeights (weights, frequencies, kModes, 0.5, 1.0, 1.0);

    for (int mode = 0; mode < kModes; ++mode)
    {
        const int k = mode + 1;

        if (k % 2 == 0)
            CHECK (std::abs (weights[mode]) < 1.0e-12);
        else
            CHECK (std::abs (weights[mode]) > 0.5);   // hard mallet, low u
    }

    // A third-point strike silences every third mode the same way (p = 1/3
    // is not exactly representable, so the node is dust rather than zero).
    malletWeights (weights, frequencies, kModes, 1.0 / 3.0, 1.0, 1.0);

    CHECK (std::abs (weights[2]) < 1.0e-9);    // k = 3
    CHECK (std::abs (weights[5]) < 1.0e-9);    // k = 6
    CHECK (std::abs (weights[0]) > 0.5);       // k = 1 rings

    // The pluck is combed by the same law.
    pluckWeights (weights, kModes, 0.5, 1.0);

    for (int mode = 0; mode < kModes; ++mode)
        if ((mode + 1) % 2 == 0)
            CHECK (std::abs (weights[mode]) < 1.0e-12);
}

TEZLA_TEST (a_pluck_is_darker_than_any_mallet)
{
    // The pluck's 1/k^2 displacement series against the same 32-mode object
    // and strike point as the hardness test:
    //
    //   pluck centroid 111.3 Hz -- below even the softest mallet's 154.6 Hz
    //
    // which is the audible difference between the two exciters, pinned.
    constexpr int kModes = 32;
    double frequencies[kModes];
    double pluck[kModes];
    double soft[kModes];

    for (int mode = 0; mode < kModes; ++mode)
        frequencies[mode] = 100.0 * (mode + 1);

    pluckWeights (pluck, kModes, 0.29, 1.0);
    malletWeights (soft, frequencies, kModes, 0.29, 0.2, 1.0);

    const double pluckCentroid = centroidHz (pluck, frequencies, kModes);
    const double softCentroid = centroidHz (soft, frequencies, kModes);

    std::printf ("        [pluck] centroid %.1f Hz vs softest mallet %.1f Hz\n",
                 pluckCentroid, softCentroid);

    CHECK (pluckCentroid < softCentroid);
    CHECK_NEAR (pluckCentroid, 111.3, 1.0);
}

TEZLA_TEST (the_roll_follows_its_ratio_then_settles_on_the_floor)
{
    // Bouncing-ball arithmetic at 48 kHz, no humanise: drop at 0.1 s with
    // ratio 0.7 and a 20 ms floor. The gaps must be exactly the scheduled
    // sample counts -- 4800, then x0.7 each bounce, then the floor at
    // exactly 960 samples, forever. The pinned gaps are the truncation of
    // the double schedule (3359 rather than 3360 because 0.1 * 0.7 in IEEE
    // doubles lands a hair BELOW 0.07): each sits within one sample of the
    // ideal, deterministically, on every conformant platform.
    RollClock clock;
    clock.prepare (48000.0);
    clock.setSeed (0x1234);
    clock.trigger (0.1, 0.7, 0.02, 0.0);

    const auto events = runRoll (clock, 25000);

    CHECK (events.size() >= 10);

    // First bounce lands exactly one interval after the trigger.
    CHECK (events[0].sample == 4800 - 1);   // sample index of the Nth call

    std::vector<int> gaps;

    for (std::size_t n = 1; n < events.size(); ++n)
        gaps.push_back (events[n].sample - events[n - 1].sample);

    std::printf ("        [roll] gaps %d %d %d %d %d ...\n",
                 gaps[0], gaps[1], gaps[2], gaps[3], gaps[4]);

    const int expected[] { 3359, 2351, 1646, 1152 };

    for (int n = 0; n < 4; ++n)
    {
        CHECK (gaps[static_cast<std::size_t> (n)] == expected[n]);

        // And each gap is the last times the ratio, within the one sample
        // the truncation can move it.
        const double previous = n == 0 ? 4800.0 : static_cast<double> (expected[n - 1]);
        CHECK_NEAR (static_cast<double> (expected[n]) / previous, 0.7, 0.001);
    }

    // Settled: every remaining gap is the floor, to the sample.
    for (std::size_t n = 4; n < gaps.size(); ++n)
        CHECK (gaps[n] == 960);

    // No humanise, no jitter: every velocity is exactly 1.
    for (const auto& event : events)
        CHECK (event.velocity == 1.0);

    // stop() is immediate and final.
    clock.stop();
    CHECK (! clock.isRunning());

    for (int n = 0; n < 2000; ++n)
        CHECK (clock.next() == 0.0);

    // A decelerating roll (ratio > 1) grows instead of shrinking.
    clock.trigger (0.02, 1.5, 0.01, 0.0);
    const auto slowing = runRoll (clock, 8000);

    CHECK (slowing.size() >= 3);
    CHECK (slowing[1].sample - slowing[0].sample == 1440);   // 960 * 1.5
    CHECK (slowing[2].sample - slowing[1].sample == 2160);   // * 1.5 again
}

TEZLA_TEST (rolls_and_bursts_replay_exactly_per_seed)
{
    // Humanise must be a performance, not a dice roll: the same seed replays
    // the same roll to the sample and the bit, a different seed differs, and
    // humanise 0 against humanise 0.8 actually changes the timing.
    RollClock first;
    RollClock second;
    first.prepare (48000.0);
    second.prepare (48000.0);
    first.setSeed (0xFEED);
    second.setSeed (0xFEED);

    first.trigger (0.05, 0.75, 0.015, 0.8);
    second.trigger (0.05, 0.75, 0.015, 0.8);

    const auto a = runRoll (first, 20000);
    const auto b = runRoll (second, 20000);

    CHECK (a.size() >= 5);
    CHECK (a.size() == b.size());

    for (std::size_t n = 0; n < a.size() && n < b.size(); ++n)
    {
        CHECK (a[n].sample == b[n].sample);
        CHECK (a[n].velocity == b[n].velocity);
    }

    // Re-triggering the same clock replays the same performance.
    first.trigger (0.05, 0.75, 0.015, 0.8);
    const auto replay = runRoll (first, 20000);

    CHECK (replay.size() == a.size());

    for (std::size_t n = 0; n < replay.size() && n < a.size(); ++n)
        CHECK (replay[n].sample == a[n].sample);

    // A different seed is a different performance.
    second.setSeed (0xBEEF);
    second.trigger (0.05, 0.75, 0.015, 0.8);
    const auto other = runRoll (second, 20000);

    bool differs = other.size() != a.size();

    for (std::size_t n = 0; ! differs && n < a.size(); ++n)
        differs = other[n].sample != a[n].sample
               || other[n].velocity != a[n].velocity;

    CHECK (differs);

    // And humanise genuinely humanises: against the straight clock the
    // jittered gaps are not all identical.
    RollClock straight;
    straight.prepare (48000.0);
    straight.setSeed (0xFEED);
    straight.trigger (0.05, 0.75, 0.015, 0.0);

    const auto exact = runRoll (straight, 20000);

    bool timingMoved = exact.size() != a.size();

    for (std::size_t n = 0; ! timingMoved && n < exact.size() && n < a.size(); ++n)
        timingMoved = exact[n].sample != a[n].sample;

    CHECK (timingMoved);

    // The noise burst honours the same contract.
    NoiseBurst burstA;
    NoiseBurst burstB;
    burstA.prepare (48000.0);
    burstB.prepare (48000.0);
    burstA.setSeed (42);
    burstB.setSeed (42);

    burstA.trigger (0.5, 1.0);
    burstB.trigger (0.5, 1.0);

    for (int n = 0; n < 400; ++n)
        CHECK (burstA.next() == burstB.next());

    burstA.trigger (0.5, 1.0);
    burstB.setSeed (43);
    burstB.trigger (0.5, 1.0);

    bool burstDiffers = false;

    for (int n = 0; n < 400; ++n)
        if (burstA.next() != burstB.next())
            burstDiffers = true;

    CHECK (burstDiffers);
}

TEZLA_TEST (the_noise_burst_is_finite_darkened_and_exactly_silent_after)
{
    // At hardness 0.5 the burst is 4 contacts + 2 ms = 306 samples at 48 kHz.
    // It must carry energy inside that window, be Hann-shaped at both ends,
    // return bit-exact zero forever after, and darken with soft hardness:
    //
    //   brightness (sum diff^2 / sum x^2):  hard 1.678, soft 0.288  (x5.83)
    //
    // measured from the one-pole corner at 400 + 12000 h Hz and pinned.
    NoiseBurst burst;
    burst.prepare (48000.0);
    burst.setSeed (7);
    burst.trigger (0.5, 1.0);

    CHECK (burst.isActive());

    std::vector<double> samples;
    double peak = 0.0;

    while (burst.isActive() && samples.size() < 10000)
    {
        samples.push_back (burst.next());
        peak = std::max (peak, std::abs (samples.back()));
    }

    CHECK (samples.size() == 306);
    CHECK (peak > 0.01);

    // Spent means spent: exact zeros, not small ones.
    for (int n = 0; n < 500; ++n)
        CHECK (burst.next() == 0.0);

    CHECK (! burst.isActive());

    // The Hann envelope opens from exactly zero and closes to near it -- the
    // scrape neither clicks in nor out.
    CHECK (samples.front() == 0.0);
    CHECK (std::abs (samples[1]) < 0.05 * peak);
    CHECK (std::abs (samples[samples.size() - 1]) < 0.05 * peak);
    CHECK (std::abs (samples[samples.size() - 2]) < 0.05 * peak);

    // Amount is a pure scale, bit-exactly (power-of-two amounts commute
    // with the rounding).
    NoiseBurst quarter;
    quarter.prepare (48000.0);
    quarter.setSeed (7);
    quarter.trigger (0.5, 0.25);

    for (std::size_t n = 0; n < samples.size(); ++n)
        CHECK (quarter.next() == 0.25 * samples[n]);

    // Darkening: high-frequency energy fraction, hard against soft.
    const auto brightness = [] (double hardness)
    {
        NoiseBurst b;
        b.prepare (48000.0);
        b.setSeed (99);
        b.trigger (hardness, 1.0);

        double energy = 0.0;
        double diffEnergy = 0.0;
        double previous = 0.0;

        while (b.isActive())
        {
            const double x = b.next();
            energy += x * x;
            diffEnergy += (x - previous) * (x - previous);
            previous = x;
        }

        return energy > 0.0 ? diffEnergy / energy : 0.0;
    };

    const double soft = brightness (0.1);
    const double hard = brightness (0.9);

    std::printf ("        [burst] brightness soft %.3f, hard %.3f (x%.2f)\n",
                 soft, hard, hard / soft);

    CHECK (hard > 2.5 * soft);
    CHECK_NEAR (soft, 0.288, 0.02);
    CHECK_NEAR (hard, 1.678, 0.05);
}
