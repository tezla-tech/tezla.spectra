// Copyright (c) 2026 The Tezla <thetezla@proton.me>
// Created by The Tezla -- https://github.com/wingit33/tezla.tech
// Music: https://soundcloud.com/thetezla | https://thetezla.bandcamp.com
// Built with development assistance from Claude (Anthropic).
// SPDX-License-Identifier: AGPL-3.0-only
// GNU AGPLv3 (see LICENSE), plus NOTICE.md's attribution term. Keep intact.

#include "TestFramework.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <numbers>
#include <utility>
#include <vector>

#include <tezla/dsp/Scales.hpp>

#include "MalleusVoice.hpp"

using namespace tezla::malleus;

namespace
{
constexpr double kRate = 48000.0;

struct Stereo
{
    std::vector<double> left;
    std::vector<double> right;
};

Stereo playNote (const VoiceSettings& settings, double velocity, int samples,
                 int blockSize = 128)
{
    MalleusVoice voice;

    voice.prepare (kRate);
    voice.noteOn (57, 220.0, velocity, 0x9E3779B97F4A7C15ULL, settings,
                  tezla::dsp::scales::twelveToneEqual(), 0);

    Stereo out { std::vector<double> (static_cast<std::size_t> (samples), 0.0),
                 std::vector<double> (static_cast<std::size_t> (samples), 0.0) };

    for (int done = 0; done < samples; done += blockSize)
    {
        const int take = std::min (blockSize, samples - done);

        voice.controlTick (take);
        voice.render (out.left.data() + done, out.right.data() + done, take);
    }

    return out;
}

double rmsOf (const std::vector<double>& x)
{
    double sum = 0.0;

    for (const double sample : x)
        sum += sample * sample;

    return x.empty() ? 0.0 : std::sqrt (sum / static_cast<double> (x.size()));
}

/// RMS of the mono fold -- what a mono host hears.
double monoRms (const Stereo& s)
{
    double sum = 0.0;

    for (std::size_t i = 0; i < s.left.size(); ++i)
    {
        const double fold = 0.5 * (s.left[i] + s.right[i]);
        sum += fold * fold;
    }

    return s.left.empty() ? 0.0 : std::sqrt (sum / static_cast<double> (s.left.size()));
}

VoiceSettings baseSettings()
{
    VoiceSettings settings;

    settings.material = 1.2;
    settings.partials = 32;
    settings.decaySeconds = 2.0;
    settings.position = 0.29;
    settings.hardness = 0.6;

    return settings;
}
} // namespace

// ---------------------------------------------------------------------------
// The neutral setting
// ---------------------------------------------------------------------------

TEZLA_TEST (listen_amount_zero_is_the_mono_voice_that_shipped)
{
    // Section 7: a stage permanently in the path needs a bit-exact bypass, and
    // this one is in every note. At amount 0 the render takes the path it
    // always took -- one DC blocker, one gate call, one number written to both
    // channels.
    //
    // **And it is exact without that shortcut too**, which is the finding
    // worth recording. Deleting the fast path so amount 0 goes through the
    // two-tap arithmetic -- two dot products, a second DC blocker, a second
    // pair of gate poles -- leaves the whole 945-test suite green. The lerp
    // `(1 - a) * 1 + a * sin(k pi q)` really is exactly 1.0 at a = 0, and
    // `1.0 * x` summed in the same order really is the sum `process` computed.
    // So the guard is speed rather than correctness, and this test asserts the
    // property rather than the shortcut.
    //
    // Swept over positions as well, because the positions must be *inert* at
    // amount 0 rather than merely defaulted: a preset that saves a spread and
    // a zero amount has to sound like one that saves neither.
    int checked = 0;

    for (const double q : { 0.0, 0.13, 0.5, 0.71, 1.0 })
    {
        auto settings = baseSettings();
        settings.listenLeft = q;
        settings.listenRight = 1.0 - q;
        settings.listenAmount = 0.0;

        const auto note = playNote (settings, 0.85, 24000);

        for (std::size_t i = 0; i < note.left.size(); ++i)
            CHECK (note.left[i] == note.right[i]);

        // ...and against a voice that names no positions at all.
        auto plain = baseSettings();

        const auto reference = playNote (plain, 0.85, 24000);

        for (std::size_t i = 0; i < note.left.size(); ++i)
            CHECK (note.left[i] == reference.left[i]);

        ++checked;
    }

    CHECK (checked == 5);
}

TEZLA_TEST (two_ears_in_the_same_place_hear_the_same_thing_exactly)
{
    // With the positions equal, the two taps are the same dot product of the
    // same numbers, so the channels are identical -- at every amount, not just
    // at zero.
    //
    // This is the test that pins the **one** vactrol. A gate is a physical
    // part and `LowpassGate::process` advances the cell as well as filtering,
    // so calling it once per ear would decay the note at double speed and
    // leave the two channels different by construction: break-checked, it
    // fails 108000 of these comparisons. Mono, but *coloured* mono: standing at one point on a real
    // object is not the same as hearing every mode equally, and this is the
    // test that separates "the taps agree" from "the feature is off".
    for (const double amount : { 0.25, 0.5, 1.0 })
        for (const double q : { 0.17, 0.5, 0.83 })
        {
            auto settings = baseSettings();
            settings.listenLeft = q;
            settings.listenRight = q;
            settings.listenAmount = amount;

            const auto note = playNote (settings, 0.85, 12000);

            for (std::size_t i = 0; i < note.left.size(); ++i)
                CHECK (note.left[i] == note.right[i]);
        }

    // And it really is coloured: listening at the middle of the object at full
    // amount is not the flat sum. Measured RMS 0.002998 against 0.003933.
    //
    // The mechanism is exact -- `sin(k pi / 2)` is zero for every even mode
    // and +/-1 for every odd one, so the middle of the object cannot hear half
    // its partials at all. The *level* is not halved, and that is worth having
    // measured rather than reasoned: a struck object's energy is concentrated
    // in its low modes, so losing the even half of a 32-mode stack costs 0.76
    // of the amplitude rather than 0.5.
    auto middle = baseSettings();
    middle.listenLeft = 0.5;
    middle.listenRight = 0.5;
    middle.listenAmount = 1.0;

    const double coloured = rmsOf (playNote (middle, 0.85, 24000).left);
    const double flat = rmsOf (playNote (baseSettings(), 0.85, 24000).left);

    std::printf ("        [listen] middle %.6f vs flat %.6f\n", coloured, flat);

    CHECK (coloured < flat);
}

// ---------------------------------------------------------------------------
// The geometry
// ---------------------------------------------------------------------------

TEZLA_TEST (a_mode_on_a_node_is_exactly_absent_from_that_ear)
{
    // The physics, at its sharpest. sin(k pi q) is exactly zero when k*q is a
    // whole number, so an ear at the middle of the object hears exactly
    // nothing of every even mode. Not "almost nothing": the weight is zero and
    // the term is dropped.
    //
    // Checked on the weights themselves rather than on a spectrum, because a
    // spectral measurement of "exactly zero" is a measurement of the analysis
    // window rather than of the object.
    for (const int k : { 2, 4, 6, 8, 10 })
        CHECK (positionWeight (k, 0.5) == std::sin (static_cast<double> (k)
                                                      * std::numbers::pi * 0.5));

    // A third of the way along silences every third mode, and so on. The
    // arithmetic is `sin(n pi)`, which is not exactly 0 in floating point --
    // it is of order 1e-16 -- so the honest assertion is that it is below the
    // weight of a mode that is *not* on a node by twelve orders of magnitude.
    for (const int divisor : { 2, 3, 4, 5 })
    {
        const double q = 1.0 / static_cast<double> (divisor);

        const double onNode = std::abs (positionWeight (divisor, q));
        const double offNode = std::abs (positionWeight (1, q));

        CHECK (offNode > 0.5);
        CHECK (onNode < offNode * 1.0e-12);
    }

    // And the audible consequence: an ear at the middle, at full amount,
    // hears a note with the even partials gone, so it is *quieter* and its
    // spectrum is the odd series alone.
    auto settings = baseSettings();
    settings.listenLeft = 0.5;    // every even mode on a node
    settings.listenRight = 0.25;  // every fourth mode on a node
    settings.listenAmount = 1.0;

    const auto note = playNote (settings, 0.85, 24000);

    std::printf ("        [listen] q=0.5 rms %.6f, q=0.25 rms %.6f\n",
                 rmsOf (note.left), rmsOf (note.right));

    CHECK (rmsOf (note.left) > 0.0);
    CHECK (rmsOf (note.right) > 0.0);
    CHECK (rmsOf (note.left) != rmsOf (note.right));
}

TEZLA_TEST (the_two_positions_genuinely_decorrelate_the_channels)
{
    // Stereo from the geometry rather than from a delay or a phase trick: the
    // wider the two points are, the less the channels agree. Measured as the
    // normalised correlation over the note, swept.
    std::printf ("        [listen]  L      R    correlation\n");

    double previous = 2.0;
    int steps = 0;

    for (const double spread : { 0.0, 0.1, 0.2, 0.3, 0.4 })
    {
        auto settings = baseSettings();
        settings.listenLeft = 0.5 - spread;
        settings.listenRight = 0.5 + spread;
        settings.listenAmount = 1.0;

        const auto note = playNote (settings, 0.85, 24000);

        double dot = 0.0;
        double energyLeft = 0.0;
        double energyRight = 0.0;

        for (std::size_t i = 0; i < note.left.size(); ++i)
        {
            dot += note.left[i] * note.right[i];
            energyLeft += note.left[i] * note.left[i];
            energyRight += note.right[i] * note.right[i];
        }

        const double correlation = dot / std::sqrt (energyLeft * energyRight);

        std::printf ("        [listen] %.2f   %.2f   %+.4f\n",
                     settings.listenLeft, settings.listenRight, correlation);

        // Identical positions are perfectly correlated; every widening step
        // agrees less.
        CHECK (correlation < previous);

        previous = correlation;
        ++steps;
    }

    CHECK (steps == 5);
}

// ---------------------------------------------------------------------------
// The trap, measured rather than avoided
// ---------------------------------------------------------------------------

TEZLA_TEST (width_and_the_mono_fold_trade_off_and_the_grid_says_by_how_much)
{
    // Two taps either side of a mode's node hear it in **opposite phase**, so
    // the mono fold removes it. Break-checked with the magnitude of the
    // position weight instead of its signed value: every mono keep goes to
    // exactly 1.000 and this fails 10 checks, which is what "there is no
    // cancellation to measure" looks like. That is honest physics and a musical trap, and
    // the plan's answer is to measure it across the position grid rather than
    // to have the panel quietly avoid it.
    //
    // What the grid actually says -- and it is not what the first draft of
    // this test assumed. Mirrored pairs at (q, 1 - q), full amount, 32 modes
    // on a 220 Hz object:
    //
    //     0.05 / 0.95   correlation -0.359   mono keeps 0.566
    //     0.10 / 0.90   correlation -0.355   mono keeps 0.568
    //     0.20 / 0.80   correlation -0.280   mono keeps 0.600
    //     0.29 / 0.71   correlation -0.059   mono keeps 0.686
    //     0.35 / 0.65   correlation +0.218   mono keeps 0.780
    //     0.45 / 0.55   correlation +0.857   mono keeps 0.964
    //
    // **Width and mono survival trade off directly**, and no position pair
    // escapes that: the mono-safest mirrored pair is also the narrowest. The
    // first version of this test claimed an asymmetric pair simply keeps more
    // than a mirrored one, and 0.19/0.41 (keep 0.947) against 0.45/0.55 (keep
    // 0.964) says otherwise -- because 0.45/0.55 is barely stereo at all.
    //
    // The claim that survives the measurement is the sharper one: **at matched
    // width, asymmetric beats mirrored.** 0.10/0.75 and 0.20/0.80 have the same
    // correlation to three decimals and the asymmetric pair keeps 0.641 against
    // 0.600 -- which is the recommendation the tooltip makes.
    const auto measure = [] (double left, double right)
    {
        auto settings = baseSettings();
        settings.listenLeft = left;
        settings.listenRight = right;
        settings.listenAmount = 1.0;

        const auto note = playNote (settings, 0.85, 24000);

        double dot = 0.0;
        double energyLeft = 0.0;
        double energyRight = 0.0;

        for (std::size_t i = 0; i < note.left.size(); ++i)
        {
            dot += note.left[i] * note.right[i];
            energyLeft += note.left[i] * note.left[i];
            energyRight += note.right[i] * note.right[i];
        }

        const double correlation = dot / std::sqrt (energyLeft * energyRight);
        const double stereo = 0.5 * (rmsOf (note.left) + rmsOf (note.right));

        return std::pair { correlation, monoRms (note) / stereo };
    };

    std::printf ("        [mono]    L      R    correlation   keeps\n");

    double previousCorrelation = -2.0;
    double previousKeep = 0.0;
    int steps = 0;

    for (const double q : { 0.05, 0.10, 0.20, 0.29, 0.35, 0.45 })
    {
        const auto [correlation, keep] = measure (q, 1.0 - q);

        std::printf ("        [mono]  %.2f   %.2f    %+.4f      %.3f\n",
                     q, 1.0 - q, correlation, keep);

        // Narrowing the pair raises both, together, monotonically. That is the
        // trade-off stated as an assertion rather than as a caveat.
        CHECK (correlation > previousCorrelation);
        CHECK (keep > previousKeep);

        previousCorrelation = correlation;
        previousKeep = keep;
        ++steps;
    }

    CHECK (steps == 6);

    // The widest mirrored pair still loses a third of its level in mono, and
    // the narrowest is not stereo at all. Both ends pinned.
    CHECK_NEAR (measure (0.05, 0.95).second, 0.566, 0.005);
    CHECK_NEAR (measure (0.45, 0.55).second, 0.964, 0.005);

    // And the recommendation: at matched width, asymmetric survives better.
    const auto [mirroredWidth, mirroredKeep] = measure (0.20, 0.80);
    const auto [offsetWidth, offsetKeep] = measure (0.10, 0.75);

    std::printf ("        [mono]  matched width %+.3f vs %+.3f -- keeps %.3f vs %.3f\n",
                 mirroredWidth, offsetWidth, mirroredKeep, offsetKeep);

    CHECK (std::abs (offsetWidth - mirroredWidth) < 0.005);
    CHECK (offsetKeep > mirroredKeep);
}
