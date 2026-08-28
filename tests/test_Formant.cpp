// Copyright (c) 2026 The Tezla <thetezla@proton.me>
// Created by The Tezla -- https://github.com/wingit33/tezla.tech
// Music: https://soundcloud.com/thetezla | https://thetezla.bandcamp.com
// Built with development assistance from Claude (Anthropic).
// SPDX-License-Identifier: AGPL-3.0-only
// GNU AGPLv3 (see LICENSE), plus NOTICE.md's attribution term. Keep intact.

#include "TestFramework.hpp"

#include <algorithm>
#include <algorithm>
#include <cmath>
#include <numbers>
#include <vector>

#include <tezla/dsp/Formant.hpp>

using namespace tezla::dsp;

namespace
{
double magnitudeAt (Formant& formant, double frequency, double sampleRate)
{
    formant.reset();

    const int settle = static_cast<int> (sampleRate * 0.25);
    const int measure = static_cast<int> (std::round (200.0 * sampleRate / frequency));

    double phase = 0.0;
    const double step = 2.0 * std::numbers::pi * frequency / sampleRate;

    for (int i = 0; i < settle; ++i)
    {
        double left = std::sin (phase);
        double right = left;
        formant.process (left, right);
        phase += step;
    }

    double inPhase = 0.0;
    double quadrature = 0.0;

    for (int i = 0; i < measure; ++i)
    {
        double left = std::sin (phase);
        double right = left;
        formant.process (left, right);

        inPhase += left * std::sin (phase);
        quadrature += left * std::cos (phase);
        phase += step;
    }

    return 2.0 * std::hypot (inPhase, quadrature) / measure;
}

double dbOf (double magnitude) { return 20.0 * std::log10 (std::max (magnitude, 1.0e-12)); }

Formant made (double rate = 48000.0, double morph = 0.0, double sharpness = 0.5,
              double mix = 1.0)
{
    Formant formant;
    formant.prepare (rate);
    formant.setMorph (morph);
    formant.setSharpness (sharpness);
    formant.setMix (mix);
    return formant;
}
} // namespace

TEZLA_TEST (each_pure_vowel_lands_on_its_published_formants)
{
    // The table is measured data taken whole rather than derived -- CLAUDE.md
    // section 9 -- so what is checkable here is that the filter is placed where
    // the table says, not that the table is right. Five vowels, five morph
    // positions, three formants each.
    auto formant = made();

    for (int vowel = 0; vowel < Formant::kVowels; ++vowel)
    {
        formant.setMorph (vowel / (Formant::kVowels - 1.0));

        for (int index = 0; index < Formant::kFormants; ++index)
            CHECK_NEAR (formant.formantHz (index),
                        Formant::kFrequencies[static_cast<std::size_t> (vowel)]
                                             [static_cast<std::size_t> (index)],
                        1.0e-9);
    }
}

TEZLA_TEST (the_morph_blends_the_formants_geometrically)
{
    // Half way between "ee" at 270 Hz and "eh" at 530 Hz is 378 Hz, not 400 --
    // a formant is a frequency and the ear hears the ratio. On a sweep the
    // difference is the whole character of the movement: arithmetic blending
    // spends too long at the top of the range and arrives late.
    auto formant = made();

    // Quarter of the way along a five-vowel list is half way from vowel 1 to
    // vowel 2.
    formant.setMorph (0.5 / (Formant::kVowels - 1.0));

    CHECK_NEAR (formant.formantHz (0), std::sqrt (270.0 * 530.0), 1.0e-9);
    CHECK_NEAR (formant.formantHz (0), 378.29, 0.01);

    CHECK_NEAR (formant.formantHz (1), std::sqrt (2290.0 * 1840.0), 1.0e-9);

    // And an arithmetic blend would have put it at 400.
    CHECK (std::abs (formant.formantHz (0) - 400.0) > 20.0);

    // Monotonic all the way along, for the formant that moves furthest.
    double previous = 0.0;

    for (double morph = 0.0; morph <= 0.5000001; morph += 0.02)
    {
        formant.setMorph (morph);

        CHECK (formant.formantHz (0) > previous);
        previous = formant.formantHz (0);
    }
}

TEZLA_TEST (the_response_has_a_peak_at_each_formant_and_at_the_stated_height)
{
    // Two claims, and the second is the one with teeth.
    //
    // **A local maximum**, checked over a narrow window rather than against a
    // valley an octave down. The first version of this asked for 3 dB of drop
    // 1.6 times below each formant and failed at "ah" and "oh" -- where F1 and
    // F2 are 360 and 270 Hz apart and the space between them is *supposed* to
    // be filled. Back vowels having merged formants is the data being right,
    // not the filter being wrong.
    //
    // **And at a height the table explains**, which is what the division by Q
    // in updateCoefficients() buys. Without it the peaks would sit 20*log10(Q)
    // high -- 17.7 dB out at "ee" F2 -- and the sharpness control would be a
    // tone control and a fader at the same time.
    //
    // The height is checked here against the table *loosely*, and the exact
    // claim is the next test's. Measured, every vowel, wet only, sharpness 0.8:
    //
    //           F1                  F2                   F3
    //     ee  -4.000 (-4)     -23.898 (-24)      -27.746 (-28)
    //     eh  -2.000 (-2)     -16.931 (-17)      -23.670 (-24)
    //     ah  -0.986 (-1)      -4.919  (-5)      -26.488 (-28)
    //     oh   0.016  (0)      -6.766  (-7)      -30.183 (-34)
    //     oo  -3.000 (-3)     -18.557 (-19)      -36.322 (-43)
    //
    // **F1 and F2 land on the table; F3 reads high, by as much as 6.7 dB, and
    // that is the filter being right.** This measures the *summed* response of
    // three resonators, and the table describes each one's own peak. When a
    // formant is thirty decibels below its neighbours, what you measure at its
    // centre is mostly the skirts of the other two. Peterson & Barney's [u] has
    // its third formant 43 dB down and its second 19 -- so at 2240 Hz the F2
    // skirt is simply louder than F3's own peak.
    //
    // This is the reading that would have made a naive test fail after the
    // paper was read, and reaching for a wider tolerance would have hidden it.
    // The claim about the coefficients is checked directly instead.
    constexpr double rate = 48000.0;

    for (int vowel = 0; vowel < Formant::kVowels; ++vowel)
    {
        auto formant = made (rate, vowel / (Formant::kVowels - 1.0), 0.8, 1.0);

        for (int index = 0; index < Formant::kFormants; ++index)
        {
            const double centre = formant.formantHz (index);

            const double atPeak = dbOf (magnitudeAt (formant, centre, rate));
            const double below = dbOf (magnitudeAt (formant, centre / 1.06, rate));
            const double above = dbOf (magnitudeAt (formant, centre * 1.06, rate));

            const double stated = Formant::kAmplitudesDb[static_cast<std::size_t> (vowel)]
                                                        [static_cast<std::size_t> (index)];

            // A local maximum, **except where the vowel's own data merges two
            // formants**. In [ɔ] F1 and F2 are 570 and 840 Hz apart with F1
            // seven decibels the louder, so the response is still climbing
            // towards F1 six percent below F2's centre. Back vowels having
            // merged formants is the data being right, not the filter being
            // wrong -- the same note as above, and the per-vowel amplitudes
            // made it sharper.
            const bool merged = std::abs (formant.formantHz (1) / formant.formantHz (0)) < 1.6;

            if (! merged || index != 1)
            {
                // Still a bump, always.
                CHECK (atPeak > below);
                CHECK (atPeak > above);

                // A *prominent* bump only where the formant is not buried
                // under its neighbours. [ɔ] and [u] put their third formant 34
                // and 43 dB below their first, and at that depth the summed
                // response barely dips either side of it -- measured, the peak
                // clears its neighbours by 1.44 dB at [ɔ] and 0.80 at [u],
                // against 2 dB or better everywhere else. That is the paper's
                // own data showing through, and loosening the margin for every
                // cell to accommodate it would have thrown the test away.
                const double loudest = *std::max_element (
                    std::begin (Formant::kAmplitudesDb[static_cast<std::size_t> (vowel)]),
                    std::end (Formant::kAmplitudesDb[static_cast<std::size_t> (vowel)]));

                if (stated > loudest - 25.0)
                {
                    CHECK (atPeak > below + 2.0);
                    CHECK (atPeak > above + 2.0);
                }
            }

            // Never below what the table asks for: skirts can only add.
            CHECK (atPeak > stated - 0.25);

            // And never far above it. Seven decibels of headroom is what the
            // [u] third formant needs and nothing needs more.
            CHECK (atPeak < stated + 7.0);
        }
    }
}

TEZLA_TEST (the_amplitude_table_is_applied_per_vowel)
{
    // The exact claim, read off the coefficients rather than out of the summed
    // response: each resonator's own peak gain is what Peterson & Barney's
    // Table II says it should be, for that vowel.
    //
    // **This was one constant set of three for every vowel before the paper was
    // read**, which gave every vowel the same spectral balance -- and the
    // balance is most of what tells one vowel from another. An "ee" wants its
    // second formant 24 dB below its first; the old constant put it 7 dB below,
    // seventeen decibels too loud.
    constexpr double rate = 48000.0;

    for (int vowel = 0; vowel < Formant::kVowels; ++vowel)
    {
        auto formant = made (rate, vowel / (Formant::kVowels - 1.0), 0.8, 1.0);

        for (int index = 0; index < Formant::kFormants; ++index)
            CHECK_NEAR (formant.formantAmplitudeDb (index),
                        Formant::kAmplitudesDb[static_cast<std::size_t> (vowel)]
                                              [static_cast<std::size_t> (index)],
                        1.0e-9);
    }

    // The spread is the point: thirty decibels between the quietest formant in
    // the table and the loudest, where a single constant set has none.
    double lowest = 0.0;
    double highest = -100.0;

    for (int vowel = 0; vowel < Formant::kVowels; ++vowel)
        for (int index = 0; index < Formant::kFormants; ++index)
        {
            const double db = Formant::kAmplitudesDb[static_cast<std::size_t> (vowel)]
                                                    [static_cast<std::size_t> (index)];

            lowest = std::min (lowest, db);
            highest = std::max (highest, db);
        }

    CHECK_NEAR (highest, 0.0, 1.0e-12);      // [ɔ] F1, the table's reference
    CHECK_NEAR (lowest, -43.0, 1.0e-12);     // [u] F3
}

TEZLA_TEST (the_amplitudes_blend_in_decibels_across_the_morph)
{
    // The amplitudes interpolate the same way the frequencies do -- geometric
    // in linear gain, which is linear in decibels -- because the ear hears
    // ratios. Blending the linear gains instead would put the midpoint between
    // a 0 dB and a -24 dB formant at -6 dB rather than -12.
    constexpr double rate = 48000.0;

    // Half way from "ee" to "eh": F2 goes from -24 to -17, so the midpoint is
    // -20.5 dB and not the -19.3 that a linear-gain blend would give.
    auto formant = made (rate, 0.5 / (Formant::kVowels - 1.0), 0.8, 1.0);

    CHECK_NEAR (formant.formantAmplitudeDb (1), -20.5, 1.0e-9);

    const double linearBlend = 20.0 * std::log10 (
        0.5 * (std::pow (10.0, -24.0 / 20.0) + std::pow (10.0, -17.0 / 20.0)));

    // Measured: the dB blend gives -20.5 and a linear-gain blend would give
    // -19.81, so they differ by 0.69 dB. Small, but it is the difference
    // between a morph that passes through the vowels and one that bulges.
    CHECK (std::abs (formant.formantAmplitudeDb (1) - linearBlend) > 0.5);
}

TEZLA_TEST (the_sharpness_control_is_not_a_volume_control)
{
    // The bandpass node reads Q at its own corner, so without the division by Q
    // the sharpness control would raise the peak by 20*log10(Q) as it narrowed
    // -- a sharpness knob that is mostly a gain knob. With it, the peak height
    // stays put and only the width moves.
    //
    // Measured at "ah", F1 = 730 Hz, wet only:
    //
    //     sharpness       Q    peak dB    down at x1.09
    //          0.00    2.28     -0.099            0.891
    //          0.25    4.56     -0.730            2.942
    //          0.50    9.12     -0.929            6.525
    //          0.75   18.25     -0.982           11.571
    //          1.00   36.50     -0.996           17.303
    //
    // Sixteen times the Q, nineteen times the skirt, and the peak **converging
    // on the -1.0 dB that Peterson & Barney give [ɑ]'s first formant**. The
    // 0.9 dB it travels is the neighbours filling in underneath: at the widest
    // setting F2 and F3 are broad enough to lift the peak almost to 0, and as
    // they narrow they stop reaching. That is overlap, not the resonance's own
    // gain -- which is the whole claim.
    //
    // These numbers moved when the amplitude table became per-vowel: [ɑ] F1 was
    // 0.0 dB under one constant set and is -1.0 in the paper.
    constexpr double rate = 48000.0;
    constexpr double centre = 730.0;

    double previousQ = 0.0;
    double previousSkirt = 0.0;

    for (const double sharpness : { 0.0, 0.25, 0.5, 0.75, 1.0 })
    {
        auto formant = made (rate, 0.0, sharpness, 1.0);

        formant.setMorph (2.0 / (Formant::kVowels - 1.0));   // "ah"

        CHECK_NEAR (formant.formantHz (0), centre, 1.0e-9);

        const double peak = dbOf (magnitudeAt (formant, centre, rate));
        const double skirt = peak - dbOf (magnitudeAt (formant, centre * 1.09, rate));

        // The peak barely moves, and settles on what the table asks for.
        CHECK (peak > -1.05);
        CHECK (peak < 0.0);

        // ...while the Q and the skirt climb together, every step.
        CHECK (formant.formantQ (0) > previousQ);
        CHECK (skirt > previousSkirt);

        previousQ = formant.formantQ (0);
        previousSkirt = skirt;
    }

    // The two ends, so a change to kNarrowest or kWidest shows up here.
    CHECK_NEAR (previousQ, 36.50, 0.05);
    CHECK_NEAR (previousSkirt, 17.30, 0.15);
}

TEZLA_TEST (the_formants_are_where_they_were_asked_for_at_every_sample_rate)
{
    // The prewarp again. A vowel filter whose formants drift between a 48 kHz
    // session and a 96 kHz one is a different instrument on each.
    for (int vowel = 0; vowel < Formant::kVowels; ++vowel)
        for (int index = 0; index < Formant::kFormants; ++index)
        {
            std::vector<double> readings;

            for (const double rate : { 44100.0, 48000.0, 96000.0, 192000.0 })
            {
                auto formant = made (rate, vowel / (Formant::kVowels - 1.0), 0.9, 1.0);

                const double centre = formant.formantHz (index);

                CHECK_NEAR (centre,
                            Formant::kFrequencies[static_cast<std::size_t> (vowel)]
                                                 [static_cast<std::size_t> (index)],
                            1.0e-9);

                readings.push_back (dbOf (magnitudeAt (formant, centre, rate)));
            }

            for (const double reading : readings)
                CHECK_NEAR (reading, readings.back(), 0.15);
        }
}

TEZLA_TEST (a_mix_of_zero_is_bit_exactly_transparent)
{
    for (const double morph : { 0.0, 0.5, 1.0 })
        for (const double sharpness : { 0.0, 1.0 })
        {
            auto formant = made (48000.0, morph, sharpness, 0.0);

            bool exact = true;

            for (int i = 0; i < 8192; ++i)
            {
                const double x = 0.7 * std::sin (i * 0.019) + 0.2 * std::sin (i * 0.31);

                double left = x;
                double right = -x;

                formant.process (left, right);

                if (left != x || right != -x)
                    exact = false;
            }

            CHECK (exact);
        }
}

TEZLA_TEST (silence_in_silence_out)
{
    for (const double morph : { 0.0, 1.0 })
        for (const double sharpness : { 0.0, 1.0 })
        {
            auto formant = made (48000.0, morph, sharpness, 1.0);

            bool silent = true;

            for (int i = 0; i < 48000; ++i)
            {
                double left = 0.0;
                double right = 0.0;

                formant.process (left, right);

                if (left != 0.0 || right != 0.0)
                    silent = false;
            }

            CHECK (silent);
        }
}

TEZLA_TEST (the_whole_parameter_space_stays_bounded)
{
    // Including the morph swept while the resonators ring, which is the case
    // that a static parameter sweep misses: retuning a Q of 30 every sample
    // moves energy between the state variables.
    double worst = 0.0;

    for (const double sharpness : { 0.0, 0.5, 1.0 })
        for (const double mix : { 0.0, 0.5, 1.0 })
        {
            auto formant = made (48000.0, 0.0, sharpness, mix);

            for (int i = 0; i < 200000; ++i)
            {
                formant.setMorph (0.5 + 0.5 * std::sin (i * 0.0011));

                double left = (i / 64) % 2 == 0 ? 1.0 : -1.0;
                double right = -left;

                formant.process (left, right);

                CHECK (std::isfinite (left));
                CHECK (std::isfinite (right));

                worst = std::max ({ worst, std::abs (left), std::abs (right) });
            }
        }

    CHECK (worst < 8.0);
}

TEZLA_TEST (a_low_sample_rate_clamps_the_top_formant_rather_than_folding_it)
{
    // F3 at "ee" is 3010 Hz, which is above Nyquist at 5512 Hz... it is not,
    // but it is above the 45% ceiling, and a tangent at Nyquist is infinite.
    Formant formant;
    formant.prepare (5512.5);
    formant.setMorph (0.0);
    formant.setMix (1.0);

    for (int index = 0; index < Formant::kFormants; ++index)
    {
        CHECK (std::isfinite (formant.formantHz (index)));
        CHECK (formant.formantHz (index) <= 5512.5 * 0.45 + 1.0e-9);
        CHECK (formant.formantQ (index) > 0.0);
    }

    for (int i = 0; i < 8192; ++i)
    {
        double left = std::sin (i * 0.2);
        double right = left;

        formant.process (left, right);

        CHECK (std::isfinite (left));
        CHECK (std::abs (left) < 20.0);
    }
}

TEZLA_TEST (the_vowel_list_runs_front_to_back_so_a_sweep_is_one_mouth_movement)
{
    // The order is the vowel circle, not the alphabet: F2 falls monotonically
    // from "ee" to "oo" while F1 rises and then falls. If the list were sorted
    // some other way, sweeping the morph would jump about.
    auto formant = made();

    // F2 falls by a factor of 2.6 from "ee" to "oo" -- 2290, 1840, 1090, 840,
    // 870. Every step falls **except the last, which rises 3.5%**, and that is
    // the measured data rather than a sorting mistake: "oo" is more rounded
    // than "oh" but not further back. Asserting strict monotonicity is what the
    // first version of this did, and it failed on a 30 Hz step in real data.
    double previousSecond = 1.0e9;

    for (int vowel = 0; vowel < Formant::kVowels; ++vowel)
    {
        formant.setMorph (vowel / (Formant::kVowels - 1.0));

        const double here = formant.formantHz (1);

        if (vowel < Formant::kVowels - 1)
            CHECK (here < previousSecond);
        else
            CHECK (here < previousSecond * 1.05);

        previousSecond = here;
    }

    formant.setMorph (0.0);
    const double highest = formant.formantHz (1);

    formant.setMorph (1.0);

    CHECK_NEAR (highest / formant.formantHz (1), 2.632, 0.001);

    // And F1 traces an arch: open in the middle, closed at both ends.
    formant.setMorph (0.0);
    const double atEe = formant.formantHz (0);

    formant.setMorph (2.0 / (Formant::kVowels - 1.0));
    const double atAh = formant.formantHz (0);

    formant.setMorph (1.0);
    const double atOo = formant.formantHz (0);

    CHECK (atAh > atEe);
    CHECK (atAh > atOo);
}

// ---------------------------------------------------------------------------
// The overtone-singing machine
// ---------------------------------------------------------------------------

TEZLA_TEST (the_harmonic_lock_puts_the_resonances_on_the_notes_own_partials)
{
    // Sygyt is one source and a very sharp tract resonance selecting a single
    // *harmonic of the drone*. The melody is therefore always in tune with the
    // note underneath it, because there is nowhere else for it to land.
    //
    // This is the comb's key tracking applied to the formant: the comb locks
    // its notches to the note's period, this locks the resonances to the note's
    // harmonics. Same thesis, third time constant.
    constexpr double rate = 48000.0;
    constexpr double note = 55.0;               // A1, a bass note

    auto formant = made (rate, 0.0, 0.8, 1.0);

    formant.setNoteHz (note);
    formant.setHarmonicLock (1.0);

    for (const double harmonic : { 1.0, 4.0, 8.0, 12.0, 20.0 })
    {
        formant.setHarmonic (harmonic);

        // Three consecutive partials, so the neighbours reinforce the one in
        // the middle rather than pulling against it.
        for (int index = 0; index < Formant::kFormants; ++index)
            CHECK_NEAR (formant.formantHz (index), note * (harmonic + index), 1.0e-9);
    }

    // Every one of those is a whole multiple of the fundamental, which is the
    // property that makes the overtone line in tune.
    formant.setHarmonic (7.0);

    const double ratio = formant.formantHz (0) / note;

    CHECK_NEAR (ratio, std::round (ratio), 1.0e-12);
}

TEZLA_TEST (the_lock_blends_geometrically_and_is_bit_exactly_off_at_zero)
{
    // At zero the vowel is untouched -- **bit-exactly**, not nearly. A filter
    // that is permanently in the path needs a real bypass at its neutral
    // setting, and a new control that shifted the vowel by a hertz would change
    // every existing patch. CLAUDE.md section 7.
    constexpr double rate = 48000.0;
    constexpr double note = 110.0;

    auto plain = made (rate, 0.35, 0.6, 1.0);

    auto locked = made (rate, 0.35, 0.6, 1.0);
    locked.setNoteHz (note);
    locked.setHarmonic (9.0);
    locked.setHarmonicLock (0.0);

    for (int index = 0; index < Formant::kFormants; ++index)
        CHECK (locked.formantHz (index) == plain.formantHz (index));

    // And with no note there is nothing to lock to, so full lock is still the
    // vowel -- which is what a released key leaves behind.
    auto noNote = made (rate, 0.35, 0.6, 1.0);
    noNote.setHarmonicLock (1.0);
    noNote.setHarmonic (9.0);

    for (int index = 0; index < Formant::kFormants; ++index)
        CHECK (noNote.formantHz (index) == plain.formantHz (index));

    // Halfway is the geometric mean of the vowel and the partial, not the
    // arithmetic one -- a formant is a frequency and the ear hears the ratio.
    locked.setHarmonicLock (0.5);

    const double vowel = plain.formantHz (0);
    const double partial = note * 9.0;

    CHECK_NEAR (locked.formantHz (0), std::sqrt (vowel * partial), 1.0e-9);
}

TEZLA_TEST (the_lock_sharpens_the_resonances_far_past_a_spoken_vowel)
{
    // Selecting one partial out of a drone is a different job from shaping a
    // vowel's broad region, and it takes a bandwidth of a few hertz where a
    // spoken formant has eighty. The extra sharpness belongs to the lock rather
    // than to the sharpness control -- widening the sharpness range instead
    // would have silently re-mapped every stored sharpness value.
    constexpr double rate = 48000.0;

    auto formant = made (rate, 0.0, 1.0, 1.0);
    formant.setNoteHz (55.0);
    formant.setHarmonic (8.0);

    formant.setHarmonicLock (0.0);
    const double spoken = formant.formantQ (0);

    formant.setHarmonicLock (1.0);
    const double sung = formant.formantQ (0);

    // Measured at sharpness 1.0: **Q 13.5 unlocked and 275 locked** onto the
    // eighth partial of A1. Two things compound -- the bandwidth falls from
    // 20 Hz to 1.6, and the resonance moves up from "ee" F1 at 270 Hz to the
    // partial at 440 -- and Q is their ratio, so the factor is 20 rather than
    // the 12.5 the narrowing alone would give.
    CHECK (sung > 200.0);
    CHECK (sung > spoken * 15.0);

    // The bandwidth itself, which is the part that is about the tract rather
    // than about where the resonance happens to sit: 80 Hz nominal, times the
    // sharpness factor of 0.25, times the locked narrowing of 0.08 -- 1.6 Hz,
    // against the eighty a spoken vowel has.
    const double bandwidth = formant.formantHz (0) / formant.formantQ (0);

    CHECK_NEAR (bandwidth, 1.6, 0.01);

    // And it is still stable: a resonance this sharp is where a filter rings
    // rather than resolves, so it gets a settle-and-decay check.
    double worst = 0.0;

    for (int i = 0; i < 400000; ++i)
    {
        double left = i < 48000 ? std::sin (i * 0.05) : 0.0;
        double right = left;

        formant.process (left, right);

        worst = std::max (worst, std::abs (left));

        CHECK (std::isfinite (left));
    }

    CHECK (worst < 100.0);
}

TEZLA_TEST (the_anti_formant_cuts_a_hole_and_is_bit_exactly_out_at_zero)
{
    // A nasal is not a vowel with different peaks; it is a vowel with a zero.
    // The nasal cavity is a side branch, and a side branch cancels rather than
    // resonates -- which is why a filter with only poles cannot say "m", or the
    // ending of a chanted "AUM".
    constexpr double rate = 48000.0;
    constexpr double hole = 1500.0;

    // Bit-exact at zero depth, sample for sample, not merely flat: this sits
    // permanently in the wet path.
    //
    // The branch that skips it is a *fast path* rather than the mechanism --
    // subtracting exactly zero is already exact -- so removing the branch does
    // not fail this, and that is right. What the test guards is the arithmetic:
    // a notch scaled by depth rather than one whose depth sets a coefficient,
    // which would not return to unity.
    {
        auto without = made (rate, 0.5, 0.5, 1.0);

        auto with = made (rate, 0.5, 0.5, 1.0);
        with.setNotchHz (hole);
        with.setNotchDepth (0.0);

        for (int i = 0; i < 4000; ++i)
        {
            const double input = std::sin (i * 0.31) * 0.7 + std::sin (i * 0.017) * 0.3;

            double a = input, b = input;
            double c = input, d = input;

            without.process (a, b);
            with.process (c, d);

            CHECK (c == a);
            CHECK (d == b);
        }
    }

    // And a real hole when it is turned up.
    auto formant = made (rate, 0.5, 0.5, 1.0);
    formant.setNotchHz (hole);

    formant.setNotchDepth (0.0);
    const double open = dbOf (magnitudeAt (formant, hole, rate));

    formant.setNotchDepth (1.0);
    const double cut = dbOf (magnitudeAt (formant, hole, rate));

    // Measured: 26.6 dB of cut at the notch centre.
    CHECK (cut < open - 12.0);

    // Localised -- two octaves away it is barely touched, which is what makes
    // it a zero rather than a tone control.
    const double far = 6000.0;

    formant.setNotchDepth (0.0);
    const double farOpen = dbOf (magnitudeAt (formant, far, rate));

    formant.setNotchDepth (1.0);
    const double farCut = dbOf (magnitudeAt (formant, far, rate));

    CHECK (std::abs (farCut - farOpen) < 3.0);
}
