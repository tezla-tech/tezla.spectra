// Copyright (c) 2026 The Tezla <thetezla@proton.me>
// Created by The Tezla -- https://github.com/wingit33/tezla.tech
// Music: https://soundcloud.com/thetezla | https://thetezla.bandcamp.com
// Built with development assistance from Claude (Anthropic).
// SPDX-License-Identifier: AGPL-3.0-only
// GNU AGPLv3 (see LICENSE), plus NOTICE.md's attribution term. Keep intact.

#include "TestFramework.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <numbers>
#include <vector>

#include <tezla/dsp/Decibels.hpp>
#include <tezla/dsp/LimiterCore.hpp>

using namespace tezla::dsp;

namespace
{
constexpr double kRate = 48000.0;

struct Random
{
    std::uint64_t state { 0x243f6a8885a308d3ULL };

    double next() noexcept
    {
        state ^= state << 13;
        state ^= state >> 7;
        state ^= state << 17;
        return static_cast<double> (state >> 11) * (1.0 / 9007199254740992.0);
    }
};

/// Everything that has ever caught a limiter out, in one signal: sustained
/// tones over the ceiling, isolated single-sample spikes, bursts that stop
/// dead, DC, silence, and content near Nyquist where the inter-sample peaks
/// live.
std::vector<double> nastySignal (int length, double scale, std::uint64_t seed)
{
    Random random;
    random.state = seed;

    std::vector<double> x (static_cast<std::size_t> (length));

    for (int i = 0; i < length; ++i)
    {
        const double t = i / kRate;
        double v = 0.0;

        const int section = (i / 900) % 6;

        switch (section)
        {
            case 0: v = std::sin (2.0 * std::numbers::pi * 90.0 * t); break;
            case 1: v = std::sin (2.0 * std::numbers::pi * 21000.0 * t); break;
            case 2: v = 2.0 * random.next() - 1.0; break;
            case 3: v = (i % 311 == 0) ? 1.0 : 0.0; break;      // isolated spikes
            case 4: v = 1.0; break;                              // DC
            case 5: v = 0.0; break;                              // and silence
        }

        x[static_cast<std::size_t> (i)] = v * scale;
    }

    return x;
}

/// What one run of the limiter is worth checking for.
struct RunResult
{
    /// How far the delivered output ever went above the ceiling, in linear
    /// units. Always <= 0 in practice, because of the clamp -- which is
    /// exactly why it is not the interesting number.
    double overshoot { 0.0 };

    /// How much the final clamp had to remove. This is the one that tests the
    /// chain: see LimiterCore::getClampExcess().
    double clampExcess { 0.0 };
};

/// Runs a configured limiter over a signal in mixed block sizes.
RunResult runLimiter (LimiterCore& limiter, double ceilingDb, const std::vector<double>& left,
                      const std::vector<double>& right)
{
    std::vector<double> a = left;
    std::vector<double> b = right;

    RunResult result;

    // Deliberately not one call: a limiter that only held the ceiling when
    // handed the whole signal at once would be useless in a host.
    int offset = 0;
    const int total = static_cast<int> (a.size());

    for (const int block : { 64, 17, 512, 1, 333 })
    {
        while (offset < total)
        {
            const int span = std::min (block, total - offset);
            double* chunk[2] { a.data() + offset, b.data() + offset };
            limiter.process (chunk, 2, span);
            offset += span;

            result.clampExcess = std::max (result.clampExcess, limiter.getClampExcess());

            if (offset >= total)
                break;
        }
    }

    const double ceiling = dbToGain (ceilingDb);

    for (const double v : a) result.overshoot = std::max (result.overshoot, std::abs (v) - ceiling);
    for (const double v : b) result.overshoot = std::max (result.overshoot, std::abs (v) - ceiling);

    return result;
}
} // namespace

TEZLA_TEST (limiter_never_exceeds_the_ceiling_anywhere_in_its_parameter_space)
{
    // The whole product, swept rather than sampled. CLAUDE.md section 7 asks
    // for exactly this of anything whose safety is a claim: "a bound that
    // cannot be defeated, and a test that sweeps the whole parameter space
    // rather than sampling it".
    //
    // The signal is 5400 samples of everything that has ever caught a limiter
    // out, at four times the ceiling, run through the limiter in blocks of 64,
    // 17, 512, 1 and 333 so a block-size assumption cannot hide in it.
    const auto left  = nastySignal (5400, 4.0, 0x243f6a8885a308d3ULL);
    const auto right = nastySignal (5400, 3.1, 0x13198a2e03707344ULL);

    double worst = 0.0;
    double worstClampExcess = 0.0;
    int combinations = 0;

    for (const double ceilingDb : { -0.3, -6.0, 3.0 })
    for (const double kneeDb : { 0.0, 6.0, 24.0 })
    for (const double attackMs : { 0.0, 0.05, 1.0, 20.0 })
    for (const double holdMs : { 0.0, 40.0 })
    for (const double releaseMs : { 1.0, 200.0, 2000.0 })
    for (const bool autoRelease : { false, true })
    for (const double link : { 0.0, 1.0 })
    {
        LimiterCore limiter;
        limiter.prepare (kRate, 2);
        limiter.setCeilingDb (ceilingDb);
        limiter.setKneeDb (kneeDb);
        limiter.setAttackMs (attackMs);
        limiter.setHoldMs (holdMs);
        limiter.setReleaseMs (releaseMs);
        limiter.setAutoRelease (autoRelease);
        limiter.setStereoLink (link);
        limiter.setTruePeakFactor (1);
        limiter.reset();

        const auto result = runLimiter (limiter, ceilingDb, left, right);

        worst = std::max (worst, result.overshoot);
        worstClampExcess = std::max (worstClampExcess, result.clampExcess);
        ++combinations;
    }

    // Not "small". None: the clamp at the end of the chain makes it exact.
    CHECK (worst <= 0.0);

    // And this is the assertion that has any teeth, because the line above has
    // none on its own. The clamp holds the ceiling whatever the chain feeding
    // it does, so a ceiling check alone passes even when the guarantee is
    // broken -- measured here by halving the minimum window against the
    // smoother's support, which left the clamp removing 1.02 of full scale
    // while every peak reading stayed exactly on the ceiling.
    //
    // 6.1e-15 is what a correct chain leaves for it: the accumulated rounding
    // in the smoother's running sums, at -285 dBFS. The bound is two orders
    // above that and eleven below anything audible.
    CHECK (worstClampExcess < 1.0e-12);

    // 3 ceilings x 3 knees x 4 attacks x 2 holds x 3 releases x 2 auto x 2 link.
    // Asserted so a loop accidentally collapsing to one case would show up as a
    // failure rather than as a suspiciously fast pass.
    CHECK (combinations == 864);
}

TEZLA_TEST (limiter_holds_the_ceiling_with_the_true_peak_detector_in_front)
{
    // The detector adds its own latency, and the audio delay has to grow to
    // match or the gain arrives after the peak it was computed for. Cheaper to
    // sweep than to reason about.
    const auto left  = nastySignal (4000, 4.0, 0x9e3779b97f4a7c15ULL);
    const auto right = nastySignal (4000, 2.5, 0xbf58476d1ce4e5b9ULL);

    double worst = 0.0;

    for (const int factor : { 1, 4, 8, 16 })
    for (const double attackMs : { 0.0, 0.2, 5.0 })
    for (const double ceilingDb : { -1.0, 0.0 })
    {
        LimiterCore limiter;
        limiter.prepare (kRate, 2);
        limiter.setCeilingDb (ceilingDb);
        limiter.setKneeDb (3.0);
        limiter.setAttackMs (attackMs);
        limiter.setReleaseMs (100.0);
        limiter.setTruePeakFactor (factor);
        limiter.reset();

        worst = std::max (worst, runLimiter (limiter, ceilingDb, left, right).overshoot);
    }

    CHECK (worst <= 0.0);
}

TEZLA_TEST (limiter_is_bit_exact_below_the_ceiling)
{
    // A limiter that is not doing anything must not be doing anything. Not
    // "inaudibly little" -- the same bits, delayed by the latency it reports.
    LimiterCore limiter;
    limiter.prepare (kRate, 2);
    limiter.setCeilingDb (0.0);
    limiter.setAttackMs (3.0);
    limiter.setHoldMs (20.0);
    limiter.setReleaseMs (150.0);
    limiter.setTruePeakFactor (16);
    limiter.reset();

    const int latency = limiter.getLatencySamples();
    const auto source = nastySignal (6000, 0.1, 0x2545f4914f6cdd1dULL);

    std::vector<double> a = source;
    std::vector<double> b = source;
    double* pointers[2] { a.data(), b.data() };
    limiter.process (pointers, 2, static_cast<int> (a.size()));

    bool identical = true;

    for (int i = latency; i < static_cast<int> (a.size()); ++i)
        if (a[static_cast<std::size_t> (i)] != source[static_cast<std::size_t> (i - latency)])
            identical = false;

    CHECK (identical);
}

TEZLA_TEST (limiter_reports_the_latency_it_actually_has)
{
    // Reported latency is what a host uses to line the track back up. If it is
    // wrong by a sample the plugin is a delay, and a plugin that lies about its
    // delay is worse than one that has none.
    for (const double attackMs : { 0.0, 0.5, 5.0, 20.0 })
    for (const int factor : { 1, 4, 16 })
    {
        LimiterCore limiter;
        limiter.prepare (kRate, 1);
        limiter.setCeilingDb (0.0);
        limiter.setAttackMs (attackMs);
        limiter.setTruePeakFactor (factor);
        limiter.reset();

        const int reported = limiter.getLatencySamples();

        std::vector<double> x (4096, 0.0);
        x[100] = 0.25;                               // small, so nothing limits

        double* pointers[1] { x.data() };
        limiter.process (pointers, 1, static_cast<int> (x.size()));

        int found = -1;

        for (int i = 0; i < static_cast<int> (x.size()); ++i)
            if (std::abs (x[static_cast<std::size_t> (i)]) > 1.0e-12)
            {
                found = i;
                break;
            }

        CHECK (found == 100 + reported);
    }
}

TEZLA_TEST (limiter_with_no_lookahead_has_no_latency)
{
    // Attack at zero is the zero-latency mode, and it has to actually be zero
    // -- with a sample-peak detector, which is the only way it can be.
    LimiterCore limiter;
    limiter.prepare (kRate, 2);
    limiter.setAttackMs (0.0);
    limiter.setTruePeakFactor (1);

    CHECK (limiter.getLatencySamples() == 0);

    // And the true-peak detector is what costs the rest, so it is worth knowing
    // it is the detector rather than the limiter.
    limiter.setTruePeakFactor (4);
    CHECK (limiter.getLatencySamples() == 6);
}

TEZLA_TEST (limiter_release_and_auto_release_hold_the_gain_down)
{
    // A single loud burst, then quiet. How fast the gain comes back is the
    // release; auto-release should come back more slowly after sustained work
    // and no more slowly after a blip, which is the whole point of it.
    const auto recoveryAt = [] (double releaseMs, bool automatic, int burstLength)
    {
        LimiterCore limiter;
        limiter.prepare (kRate, 1);
        limiter.setCeilingDb (-6.0);
        limiter.setAttackMs (1.0);
        limiter.setReleaseMs (releaseMs);
        limiter.setAutoRelease (automatic);
        limiter.setTruePeakFactor (1);
        limiter.reset();

        std::vector<double> x (24000, 0.0);

        for (int i = 2000; i < 2000 + burstLength; ++i)
            x[static_cast<std::size_t> (i)] = 0.9 * std::sin (2.0 * std::numbers::pi * 200.0 * i / kRate);

        // A quiet tone afterwards, so the gain is observable rather than
        // multiplying silence.
        for (int i = 2000 + burstLength; i < 24000; ++i)
            x[static_cast<std::size_t> (i)] = 0.02 * std::sin (2.0 * std::numbers::pi * 200.0 * i / kRate);

        double* pointers[1] { x.data() };
        limiter.process (pointers, 1, static_cast<int> (x.size()));

        // How long after the burst until the quiet tone is back to full size.
        for (int i = 2000 + burstLength + 200; i < 24000 - 400; ++i)
        {
            double peak = 0.0;

            for (int k = 0; k < 400; ++k)
                peak = std::max (peak, std::abs (x[static_cast<std::size_t> (i + k)]));

            if (peak > 0.019)
                return i - (2000 + burstLength);
        }

        return 100000;
    };

    const int fast = recoveryAt (5.0, false, 4000);
    const int slow = recoveryAt (500.0, false, 4000);

    CHECK (fast < slow);

    // Program dependence: after a long burst it should hang on longer than the
    // fixed release does, and after a short one it should not.
    const int fixedLong  = recoveryAt (50.0, false, 6000);
    const int autoLong   = recoveryAt (50.0, true,  6000);
    const int autoShort  = recoveryAt (50.0, true,  40);

    CHECK (autoLong > fixedLong);
    CHECK (autoShort < autoLong);
}

TEZLA_TEST (limiter_stereo_link_keeps_the_centre_image_still)
{
    // Linked, both channels get the same gain, so a mono signal stays mono. Off
    // and they move independently, which is wider and which shifts a centred
    // image when only one side is loud. Both are legitimate; the default must
    // be the one that does not move the image.
    const auto sideEnergy = [] (double link)
    {
        LimiterCore limiter;
        limiter.prepare (kRate, 2);
        limiter.setCeilingDb (-6.0);
        limiter.setAttackMs (1.0);
        limiter.setReleaseMs (50.0);
        limiter.setStereoLink (link);
        limiter.setTruePeakFactor (1);
        limiter.reset();

        std::vector<double> a (12000), b (12000);

        for (int i = 0; i < 12000; ++i)
        {
            const double t = i / kRate;
            const double centre = 0.8 * std::sin (2.0 * std::numbers::pi * 220.0 * t);

            // Identical either side -- and a loud thump on the left only,
            // which is what pulls the image over when the channels are free.
            a[static_cast<std::size_t> (i)] = centre + (i > 4000 && i < 4400 ? 1.5 : 0.0);
            b[static_cast<std::size_t> (i)] = centre;
        }

        double* pointers[2] { a.data(), b.data() };
        limiter.process (pointers, 2, 12000);

        double energy = 0.0;

        // After the thump has gone, anything left between the channels is the
        // limiter having moved one and not the other.
        for (int i = 6000; i < 12000; ++i)
        {
            const double side = a[static_cast<std::size_t> (i)] - b[static_cast<std::size_t> (i)];
            energy += side * side;
        }

        return energy;
    };

    CHECK (sideEnergy (1.0) < 1.0e-20);
    CHECK (sideEnergy (0.0) > 1.0e-6);
}

TEZLA_TEST (limiter_leaves_silence_silent)
{
    LimiterCore limiter;
    limiter.prepare (kRate, 2);
    limiter.setCeilingDb (-0.3);
    limiter.setAttackMs (5.0);
    limiter.setTruePeakFactor (16);
    limiter.reset();

    std::vector<double> a (8000, 0.0), b (8000, 0.0);
    double* pointers[2] { a.data(), b.data() };
    limiter.process (pointers, 2, 8000);

    double worst = 0.0;

    for (int i = 0; i < 8000; ++i)
        worst = std::max (worst, std::max (std::abs (a[static_cast<std::size_t> (i)]),
                                           std::abs (b[static_cast<std::size_t> (i)])));

    CHECK (worst == 0.0);
}

TEZLA_TEST (limiter_true_peak_mode_controls_the_reconstructed_peak)
{
    // The claim the whole detector exists for, measured on the finished
    // limiter rather than on the parts. A sample-peak limiter holds every
    // sample at the ceiling and still reconstructs above it; a true-peak one
    // does not.
    //
    // The measuring instrument is a 16x detector, which is legitimate here
    // rather than circular: it was checked against the Recommendation's own
    // worst-case bound in test_TruePeakDetector before being used to check
    // anything else. CLAUDE.md section 10 -- the instrument first.
    constexpr double ceilingDb = -1.0;

    const auto outputTruePeak = [] (int detectorFactor)
    {
        LimiterCore limiter;
        limiter.prepare (kRate, 1);
        limiter.setCeilingDb (ceilingDb);
        limiter.setKneeDb (0.0);
        limiter.setAttackMs (1.0);
        limiter.setReleaseMs (50.0);
        limiter.setTruePeakFactor (detectorFactor);
        limiter.reset();

        std::vector<double> x (20000);

        // Deliberately full of inter-sample peaks: several strong partials
        // close to Nyquist, which is where the reconstruction runs highest
        // between the samples.
        for (int i = 0; i < 20000; ++i)
        {
            const double t = i / kRate;
            x[static_cast<std::size_t> (i)] =
                  0.40 * std::sin (2.0 * std::numbers::pi * 11000.0 * t + 0.3)
                + 0.35 * std::sin (2.0 * std::numbers::pi * 15500.0 * t + 1.1)
                + 0.30 * std::sin (2.0 * std::numbers::pi * 19000.0 * t + 2.7)
                + 0.25 * std::sin (2.0 * std::numbers::pi *   220.0 * t);
        }

        double* pointers[1] { x.data() };
        limiter.process (pointers, 1, static_cast<int> (x.size()));

        TruePeakDetector meter;
        meter.prepare (TruePeakDetector::kMaxFactor);
        meter.setFactor (16);
        meter.reset();

        double highest = 0.0;

        for (int i = 0; i < static_cast<int> (x.size()); ++i)
        {
            const double reading = meter.process (x[static_cast<std::size_t> (i)]);

            if (i > 2000)
                highest = std::max (highest, reading);
        }

        return highest;
    };

    const double onSamplePeak = outputTruePeak (1);
    const double onTruePeak   = outputTruePeak (16);

    const double sampleOverDb = gainToDb (onSamplePeak, -200.0) - ceilingDb;
    const double trueOverDb   = gainToDb (onTruePeak, -200.0) - ceilingDb;

    // A sample-peak limiter overshoots its own ceiling by about a decibel on
    // this material -- which is the entire reason a plugin can hold "0 dBFS"
    // and still clip a converter.
    CHECK (sampleOverDb > 0.5);

    // And with the detector in front, the residue is the ratio's own limit.
    CHECK (trueOverDb < 0.05);

    // Both hold the *sample* ceiling exactly either way; that was never the
    // part in doubt.
    CHECK (sampleOverDb > trueOverDb + 0.4);
}
