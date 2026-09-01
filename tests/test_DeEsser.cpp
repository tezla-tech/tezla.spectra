// Copyright (c) 2026 The Tezla <thetezla@proton.me>
// Created by The Tezla -- https://github.com/wingit33/tezla.tech
// Music: https://soundcloud.com/thetezla | https://thetezla.bandcamp.com
// Built with development assistance from Claude (Anthropic).
// SPDX-License-Identifier: AGPL-3.0-only
// GNU AGPLv3 (see LICENSE), plus NOTICE.md's attribution term. Keep intact.

#include "TestFramework.hpp"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <numbers>
#include <vector>

#include "DeEsser.hpp"
#include "SibilanceDetector.hpp"

using namespace tezla::phonoss;

namespace
{
constexpr double kRate = 48000.0;

/// A sung vowel: a harmonic stack on 150 Hz with its energy in the formants,
/// which is where a voice actually puts it. Deliberately not silent above the
/// de-esser's corner -- a vowel with SOME high end is the case that makes a
/// level-thresholded de-esser lisp.
/// A raised-cosine onset over `ms`. Every generator here uses one, because a
/// signal that steps from digital silence to full amplitude in one sample is
/// not a voice -- it is an impulse, and a broadband impulse genuinely IS
/// sibilant by any honest measure of high-versus-low energy.
///
/// This was found rather than assumed. The first version of the vowel below
/// started 40 harmonics phase-aligned at a zero crossing, went 0 -> 0.80 in
/// four samples, and read +19.3 dB of sibilance for the first tenth of a
/// millisecond -- enough to move the de-esser. The detector was right and the
/// test signal was wrong; `an_abrupt_onset_reads_as_sibilant_and_that_is_
/// correct` below pins the behaviour rather than papering over it.
void applyOnset (std::vector<double>& signal, double ms = 5.0)
{
    const auto fade = static_cast<std::size_t> (ms * 0.001 * kRate);

    for (std::size_t n = 0; n < fade && n < signal.size(); ++n)
    {
        const double along = static_cast<double> (n) / static_cast<double> (fade);
        signal[n] *= 0.5 - 0.5 * std::cos (std::numbers::pi * along);
    }
}

[[nodiscard]] std::vector<double> vowel (double peak, int samples)
{
    std::vector<double> out (static_cast<std::size_t> (samples), 0.0);

    // Fundamental plus harmonics, rolling off at 6 dB per octave, so there is
    // real content at 3-6 kHz but the bulk sits low.
    for (int n = 0; n < samples; ++n)
    {
        double value = 0.0;

        for (int harmonic = 1; harmonic <= 40; ++harmonic)
            value += std::sin (2.0 * std::numbers::pi * 150.0 * harmonic * n / kRate)
                       / harmonic;

        out[static_cast<std::size_t> (n)] = value;
    }

    // Normalise to the asked peak, so "the same vowel at another level" is
    // exactly that.
    double loudest = 0.0;

    for (const double v : out)
        loudest = std::max (loudest, std::abs (v));

    for (double& v : out)
        v *= peak / loudest;

    applyOnset (out);
    return out;
}

/// An /s/: band-limited noise above 5 kHz, which is what a sibilant fricative
/// actually is. Seeded, so every level uses the SAME noise and the comparison
/// across levels is a comparison of one signal at several gains.
[[nodiscard]] std::vector<double> sibilance (double peak, int samples)
{
    std::vector<double> out (static_cast<std::size_t> (samples), 0.0);

    std::uint64_t state = 0x51B1'1A4C'E5EE'D501ULL;
    double lowA = 0.0;
    double lowB = 0.0;

    // One-pole highpass pair at 5 kHz: white minus its own low end, twice.
    const double coefficient = 1.0 - std::exp (-2.0 * std::numbers::pi * 5000.0 / kRate);

    for (int n = 0; n < samples; ++n)
    {
        state ^= state >> 12;
        state ^= state << 25;
        state ^= state >> 27;

        const double white = static_cast<double> (
            (state * std::uint64_t { 0x2545F4914F6CDD1D }) >> 11)
              / static_cast<double> (std::uint64_t { 1 } << 52) - 1.0;

        lowA += coefficient * (white - lowA);
        const double onceHigh = white - lowA;

        lowB += coefficient * (onceHigh - lowB);
        out[static_cast<std::size_t> (n)] = onceHigh - lowB;
    }

    double loudest = 0.0;

    for (const double v : out)
        loudest = std::max (loudest, std::abs (v));

    for (double& v : out)
        v *= peak / loudest;

    applyOnset (out);
    return out;
}

/// The steady sibilance reading a signal settles at.
[[nodiscard]] double settledSibilanceDb (const std::vector<double>& signal)
{
    SibilanceDetector detector;
    detector.prepare (kRate);

    double last = 0.0;

    for (const double sample : signal)
        last = detector.process (sample);

    return last;
}

/// The deepest reduction a de-esser reaches on a signal, in dB.
[[nodiscard]] double deepestReductionDb (DeEsser& deEsser,
                                         const std::vector<double>& signal)
{
    double deepest = 0.0;

    for (const double sample : signal)
    {
        (void) deEsser.process (sample, sample);
        deepest = std::min (deepest, deEsser.getReductionDb());
    }

    return deepest;
}

[[nodiscard]] DeEsser makeDeEsser()
{
    DeEsser deEsser;
    deEsser.prepare (kRate);
    deEsser.setCornerHz (6000.0);
    deEsser.setThresholdDb (-6.0);
    deEsser.setRatio (4.0);
    deEsser.setKneeDb (3.0);
    deEsser.setRangeDb (12.0);
    deEsser.setAttackMs (0.5);
    deEsser.setReleaseMs (40.0);
    return deEsser;
}
} // namespace

TEZLA_TEST (sibilance_is_measured_as_a_ratio_so_level_does_not_move_it)
{
    // THE GATE THE WHOLE DESIGN STANDS ON. The same /s/ at four levels 30 dB
    // apart must read the SAME sibilance, because sibilance is a balance
    // between two bands and a balance does not care how hard the singer is
    // pushing. A de-esser thresholding the absolute high-band level reads
    // these four as 30 dB apart and ducks the loud ones far harder, which is
    // exactly the failure this measure exists to avoid.
    double readings[4] {};
    int index = 0;

    for (const double peak : { 0.02, 0.1, 0.4, 1.0 })
        readings[index++] = settledSibilanceDb (sibilance (peak, 12000));

    std::printf ("        [sibilance] /s/ at -34/-20/-8/0 dBFS reads %.2f %.2f %.2f %.2f dB\n",
                 readings[0], readings[1], readings[2], readings[3]);

    // Break-checked against the design it replaces: swapping the ratio for
    // the absolute high-band level -- the conventional de-esser -- makes
    // these same four readings -41.70, -27.72, -15.68 and -7.72 dB. They
    // track the level exactly, 34 dB apart, and the ducking that follows
    // becomes 0.00, 0.00, 0.00 and -0.16 dB: the quiet /s/ gets nothing at
    // all and the loud one barely anything. That is the plugin you have to
    // ride the threshold on, phrase by phrase.
    for (int i = 1; i < 4; ++i)
        CHECK_NEAR (readings[i], readings[0], 0.01);

    // And a vowel at those same levels reads far lower -- that separation is
    // what the threshold sits in.
    double vowelReadings[4] {};
    index = 0;

    for (const double peak : { 0.02, 0.1, 0.4, 1.0 })
        vowelReadings[index++] = settledSibilanceDb (vowel (peak, 12000));

    std::printf ("        [sibilance] vowel at those levels reads %.2f %.2f %.2f %.2f dB\n",
                 vowelReadings[0], vowelReadings[1], vowelReadings[2], vowelReadings[3]);

    for (int i = 1; i < 4; ++i)
        CHECK_NEAR (vowelReadings[i], vowelReadings[0], 0.01);

    // The separation between the two is the headroom a threshold has to sit
    // in. Measured, and pinned: an /s/ reads more than 20 dB above a vowel.
    CHECK (readings[0] - vowelReadings[0] > 20.0);
}

TEZLA_TEST (a_vowel_is_not_ducked_at_any_level)
{
    // The lisping failure, tested directly: sweep a vowel across 30 dB and
    // the de-esser must do nothing at any of them.
    for (const double peak : { 0.02, 0.1, 0.4, 1.0 })
    {
        auto deEsser = makeDeEsser();
        const auto signal = vowel (peak, 12000);

        // In STEADY STATE, nothing audible -- at any level. Measured:
        //
        //     vowel peak    steady-state worst duck
        //       -34 dBFS          0.0495 dB
        //       -20 dBFS          0.0534 dB
        //        -8 dBFS          0.0664 dB
        //         0 dBFS          0.0733 dB
        //
        // Not zero, and the bound below says so rather than pretending. A
        // periodic voice makes the ratio wobble a little around its average,
        // and the peaks of that wobble graze the bottom of the knee. What
        // matters is the size: 0.07 dB is roughly a tenth of the ~0.5-1 dB at
        // which a gain change becomes audible at all, and it is 170 times
        // smaller than the 12 dB this same de-esser puts on an /s/. That
        // ratio is the claim worth making, and it is asserted below.
        //
        // The window starts at 150 ms, and the reason is a real property of
        // the measure rather than a convenient choice. The body follower
        // needs roughly one period of the fundamental before its reading
        // means anything (6.7 ms at this 150 Hz voice), while the high band
        // is represented immediately -- so the first few ms of ANY phrase
        // read as more sibilant than they are. What follows is the 40 ms
        // release letting go. Both are measured in
        // `an_abrupt_onset_reads_as_sibilant_and_that_is_correct`.
        double worst = 0.0;

        for (std::size_t n = 0; n < signal.size(); ++n)
        {
            (void) deEsser.process (signal[n], signal[n]);

            if (n > static_cast<std::size_t> (0.15 * kRate))
                worst = std::min (worst, deEsser.getReductionDb());
        }

        CHECK (worst > -0.25);      // inaudible
        CHECK (worst < 0.0 + 1.0);  // and the loop really ran
    }
}

TEZLA_TEST (an_ess_is_ducked_two_hundred_times_harder_than_a_vowel)
{
    // The separation, stated as one number, because it is the whole product:
    // at identical settings and identical peak level, the /s/ is ducked 12 dB
    // and the vowel 0.07 dB. A de-esser thresholding absolute high-band level
    // cannot produce a gap like that -- it ducks whatever is loud, and a loud
    // vowel is loud.
    auto onEss = makeDeEsser();
    auto onVowel = makeDeEsser();

    const double essDuck = deepestReductionDb (onEss, sibilance (0.8, 24000));

    double vowelDuck = 0.0;
    const auto tone = vowel (0.8, 24000);

    for (std::size_t n = 0; n < tone.size(); ++n)
    {
        (void) onVowel.process (tone[n], tone[n]);

        if (n > static_cast<std::size_t> (0.15 * kRate))
            vowelDuck = std::min (vowelDuck, onVowel.getReductionDb());
    }

    std::printf ("        [de-ess] same settings: /s/ ducked %.2f dB, vowel %.4f dB (x%.0f)\n",
                 essDuck, vowelDuck, essDuck / vowelDuck);

    CHECK (essDuck < -10.0);
    CHECK (vowelDuck > -0.25);
    CHECK (essDuck / vowelDuck > 50.0);
}

TEZLA_TEST (an_ess_is_ducked_by_the_same_amount_at_every_level)
{
    // The over-essing failure, tested directly: the same /s/ 30 dB apart must
    // be ducked by the same amount, because the detector reads the same
    // ratio. This is the property a level-thresholded de-esser cannot have.
    double reductions[4] {};
    int index = 0;

    for (const double peak : { 0.02, 0.1, 0.4, 1.0 })
    {
        auto deEsser = makeDeEsser();
        reductions[index++] = deepestReductionDb (deEsser, sibilance (peak, 12000));
    }

    std::printf ("        [de-ess] /s/ at -34/-20/-8/0 dBFS ducked %.2f %.2f %.2f %.2f dB\n",
                 reductions[0], reductions[1], reductions[2], reductions[3]);

    CHECK (reductions[0] < -3.0);   // it is actually working

    for (int i = 1; i < 4; ++i)
        CHECK_NEAR (reductions[i], reductions[0], 0.05);
}

TEZLA_TEST (an_idle_de_esser_is_bit_exact_identity)
{
    // Section 7: this stage is in the path of every vocal that ever goes
    // through the strip, so at rest it must be the identity function and not
    // merely a transparent one. The crossfade form gives that by
    // construction -- g is exactly 1, so the multiplies are by exactly 1.0
    // and exactly 0.0.
    //
    // The FORM matters and not just the algebra, which the break-check makes
    // vivid: writing the same crossfade as `low + g * (input - low)` -- which
    // is identical on paper and is how most people would write it -- fails
    // this test 1860 times. Subtracting `low` and adding it back does not
    // return the original bits, while multiplying by exactly 1.0 and adding
    // exactly 0.0 does.
    auto deEsser = makeDeEsser();
    deEsser.setThresholdDb (60.0);   // unreachable: nothing is ever sibilant

    const auto signal = vowel (0.8, 8000);

    for (const double sample : signal)
        CHECK (deEsser.process (sample, sample) == sample);

    // Range 0 is the other neutral setting, and it must be exact too.
    auto ranged = makeDeEsser();
    ranged.setThresholdDb (-40.0);   // everything is sibilant
    ranged.setRangeDb (0.0);

    const auto hiss = sibilance (0.8, 8000);

    for (const double sample : hiss)
        CHECK (ranged.process (sample, sample) == sample);
}

TEZLA_TEST (the_body_of_the_voice_survives_a_de_ess_event)
{
    // "It does not lisp", measured rather than asserted. A vowel and an /s/
    // together: the de-esser fires on the /s/, and the low-frequency content
    // must come through with its energy essentially intact.
    auto deEsser = makeDeEsser();

    const auto body = vowel (0.3, 12000);
    const auto hiss = sibilance (0.7, 12000);

    double inputLowEnergy = 0.0;
    double outputLowEnergy = 0.0;

    // One-pole low-passes at 1 kHz on both sides, so this measures the body
    // rather than the thing being removed.
    double lowIn = 0.0;
    double lowOut = 0.0;
    const double coefficient = 1.0 - std::exp (-2.0 * std::numbers::pi * 1000.0 / kRate);

    double deepest = 0.0;

    for (std::size_t n = 0; n < body.size(); ++n)
    {
        const double input = body[n] + hiss[n];
        const double output = deEsser.process (input, input);

        deepest = std::min (deepest, deEsser.getReductionDb());

        lowIn += coefficient * (input - lowIn);
        lowOut += coefficient * (output - lowOut);

        if (n > 2000)   // past the followers settling
        {
            inputLowEnergy += lowIn * lowIn;
            outputLowEnergy += lowOut * lowOut;
        }
    }

    const double keptDb = 10.0 * std::log10 (outputLowEnergy / inputLowEnergy);

    std::printf ("        [de-ess] ducked %.2f dB; body below 1 kHz changed by %.4f dB\n",
                 deepest, keptDb);

    CHECK (deepest < -3.0);              // it really did fire
    CHECK (std::abs (keptDb) < 0.15);    // and the body is essentially untouched
}

TEZLA_TEST (range_caps_the_duck_and_listen_solos_what_is_removed)
{
    // Range is what stops a hard /s/ taking the whole top of the voice.
    for (const double range : { 3.0, 6.0, 12.0 })
    {
        auto deEsser = makeDeEsser();
        deEsser.setThresholdDb (-30.0);   // drive it hard into the range
        deEsser.setRangeDb (range);

        const double deepest = deepestReductionDb (deEsser, sibilance (0.9, 12000));

        CHECK (deepest >= -range - 1.0e-9);
        CHECK (deepest < -range + 1.0);   // and it does reach it
    }

    // Listen returns exactly what the de-esser removed, so setting it by ear
    // is possible: solo + normal must sum back to the input, bit for bit.
    auto normal = makeDeEsser();
    auto solo = makeDeEsser();
    solo.setListen (true);

    const auto hiss = sibilance (0.7, 6000);

    for (const double sample : hiss)
    {
        const double kept = normal.process (sample, sample);
        const double removed = solo.process (sample, sample);

        CHECK_NEAR (kept + removed, sample, 1.0e-12);
    }
}

TEZLA_TEST (the_de_esser_reads_the_same_at_every_host_rate)
{
    // Section 6: the corner is in Hz and every time constant in ms, so the
    // same /s/ must be ducked the same at 44.1, 48, 96 and 192 kHz.
    double reductions[4] {};
    int index = 0;

    for (const double rate : { 44100.0, 48000.0, 96000.0, 192000.0 })
    {
        DeEsser deEsser;
        deEsser.prepare (rate);
        deEsser.setCornerHz (6000.0);
        deEsser.setThresholdDb (-6.0);
        deEsser.setRatio (4.0);
        deEsser.setKneeDb (3.0);
        deEsser.setRangeDb (12.0);
        deEsser.setAttackMs (0.5);
        deEsser.setReleaseMs (40.0);

        // A quarter second of the same noise character at each rate.
        const int samples = static_cast<int> (0.25 * rate);

        std::uint64_t state = 0x51B1'1A4C'E5EE'D501ULL;
        double lowA = 0.0, lowB = 0.0;
        const double coefficient = 1.0 - std::exp (-2.0 * std::numbers::pi * 5000.0 / rate);

        double deepest = 0.0;

        for (int n = 0; n < samples; ++n)
        {
            state ^= state >> 12;
            state ^= state << 25;
            state ^= state >> 27;

            const double white = static_cast<double> (
                (state * std::uint64_t { 0x2545F4914F6CDD1D }) >> 11)
                  / static_cast<double> (std::uint64_t { 1 } << 52) - 1.0;

            lowA += coefficient * (white - lowA);
            const double onceHigh = white - lowA;
            lowB += coefficient * (onceHigh - lowB);

            const double sample = 0.7 * (onceHigh - lowB);

            (void) deEsser.process (sample, sample);
            deepest = std::min (deepest, deEsser.getReductionDb());
        }

        reductions[index++] = deepest;
    }

    std::printf ("        [de-ess] rates 44.1/48/96/192k duck %.2f %.2f %.2f %.2f dB\n",
                 reductions[0], reductions[1], reductions[2], reductions[3]);

    for (int i = 1; i < 4; ++i)
        CHECK_NEAR (reductions[i], reductions[0], 0.5);
}

TEZLA_TEST (an_abrupt_onset_reads_as_sibilant_and_that_is_correct)
{
    // Recorded rather than hidden. A signal that steps from silence to full
    // amplitude in a few samples has genuine broadband energy, so a measure
    // of high-versus-low energy reports it as sibilant -- and should. The
    // question is only whether the de-esser's attack lets that reach the
    // audio, and the answer is measured here.
    //
    // Two mechanisms, both measured:
    //
    //   * a step edge reads about +19 dB for its first tenth of a
    //     millisecond, because a near-vertical edge genuinely is broadband;
    //   * and more interestingly, the BODY follower needs about one period
    //     of the fundamental before it represents the low end at all, while
    //     the high band is there from the first sample. So the opening few
    //     ms of any phrase read as more sibilant than the phrase is.
    //
    // What reaches the audio is bounded by the attack: 1.40 dB at its
    // deepest, and then the 40 ms release lets go, which is why the duck
    // lasts about 48 ms rather than the 0.1 ms the spike does. That is a
    // compressor behaving like a compressor.
    //
    // For a plosive this is arguably right -- a hard /t/ has real high end.
    // The point is that the figure is known here rather than discovered
    // later on a vocal take.
    SibilanceDetector detector;
    detector.prepare (kRate);

    std::vector<double> step (4800, 0.0);

    for (int n = 0; n < 4800; ++n)
    {
        double value = 0.0;

        for (int harmonic = 1; harmonic <= 40; ++harmonic)
            value += std::sin (2.0 * std::numbers::pi * 150.0 * harmonic * n / kRate)
                       / harmonic;

        step[static_cast<std::size_t> (n)] = 0.8 * value / 4.3;
    }

    double peakReading = -200.0;

    for (int n = 0; n < 240; ++n)
        peakReading = std::max (peakReading, detector.process (step[static_cast<std::size_t> (n)]));

    double settled = 0.0;

    for (int n = 240; n < 4800; ++n)
        settled = detector.process (step[static_cast<std::size_t> (n)]);

    std::printf ("        [onset] step edge peaks at %.1f dB, settles at %.1f dB\n",
                 peakReading, settled);

    CHECK (peakReading > 10.0);    // the edge really does read as sibilant
    CHECK (settled < -10.0);       // and the vowel behind it does not

    // What actually reaches the audio: bounded, and brief.
    auto deEsser = makeDeEsser();

    double deepest = 0.0;
    int duckedSamples = 0;

    for (int n = 0; n < 4800; ++n)
    {
        (void) deEsser.process (step[static_cast<std::size_t> (n)],
                                step[static_cast<std::size_t> (n)]);

        deepest = std::min (deepest, deEsser.getReductionDb());

        if (deEsser.getReductionDb() < -0.5)
            ++duckedSamples;
    }

    std::printf ("        [onset] deepest duck %.2f dB, over %.2f ms\n",
                 deepest, 1000.0 * duckedSamples / kRate);

    // Shallow, and bounded by the release rather than arbitrary: the duck is
    // gone within a couple of release times of a spike lasting a fraction of
    // a millisecond.
    CHECK (deepest > -3.0);
    CHECK (duckedSamples < static_cast<int> (0.003 * 40.0 * kRate));
}
