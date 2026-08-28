// Copyright (c) 2026 The Tezla <thetezla@proton.me>
// Created by The Tezla -- https://github.com/wingit33/tezla.tech
// Music: https://soundcloud.com/thetezla | https://thetezla.bandcamp.com
// Built with development assistance from Claude (Anthropic).
// SPDX-License-Identifier: AGPL-3.0-only
// GNU AGPLv3 (see LICENSE), plus NOTICE.md's attribution term. Keep intact.

#include "TestFramework.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <vector>

#include <tezla/dsp/Comb.hpp>

using namespace tezla::dsp;

namespace
{
/// Magnitude response at one frequency, by driving a sine and demodulating the
/// left channel.
double magnitudeAt (Comb& comb, double frequency, double sampleRate)
{
    comb.reset();

    const int settle = static_cast<int> (sampleRate * 0.5);
    const int measure = static_cast<int> (std::round (200.0 * sampleRate / frequency));

    double phase = 0.0;
    const double step = 2.0 * std::numbers::pi * frequency / sampleRate;

    for (int i = 0; i < settle; ++i)
    {
        double left = std::sin (phase);
        double right = left;
        comb.process (left, right);
        phase += step;
    }

    double inPhase = 0.0;
    double quadrature = 0.0;

    for (int i = 0; i < measure; ++i)
    {
        double left = std::sin (phase);
        double right = left;
        comb.process (left, right);

        inPhase += left * std::sin (phase);
        quadrature += left * std::cos (phase);
        phase += step;
    }

    return 2.0 * std::hypot (inPhase, quadrature) / measure;
}

double dbOf (double magnitude) { return 20.0 * std::log10 (std::max (magnitude, 1.0e-12)); }

/// |H| of the comb, from the algebra rather than from the code.
///
///     v[n] = x[n] + g * v[n-D]
///     y[n] = x[n] + s * m * v[n-D]
///
/// so   H = (1 - g z^-D + s*m*z^-D) / (1 - g z^-D),   z^-D = exp(-j w D)
///
/// This is what makes the measurements a check of the implementation rather
/// than a check of a handful of inequalities that a wrong delay also satisfies.
double theoreticalMagnitude (double frequency, double sampleRate, double delaySamples,
                             double mix, double feedback, bool inverted)
{
    const double omega = 2.0 * std::numbers::pi * frequency * delaySamples / sampleRate;

    const double cosine = std::cos (omega);
    const double sine = -std::sin (omega);

    const double wet = inverted ? -mix : mix;

    const double numeratorReal = 1.0 + (wet - feedback) * cosine;
    const double numeratorImaginary = (wet - feedback) * sine;

    const double denominatorReal = 1.0 - feedback * cosine;
    const double denominatorImaginary = -feedback * sine;

    return std::hypot (numeratorReal, numeratorImaginary)
             / std::hypot (denominatorReal, denominatorImaginary);
}

Comb made (double rate = 48000.0, double delaySeconds = 0.002, double mix = 1.0,
           double feedback = 0.0)
{
    Comb comb;
    comb.prepare (rate);
    comb.setDelaySeconds (delaySeconds);
    comb.setMix (mix);
    comb.setFeedback (feedback);
    return comb;
}
} // namespace

// ---------------------------------------------------------------------------
// The delay line underneath it
// ---------------------------------------------------------------------------

TEZLA_TEST (an_integer_delay_returns_the_stored_sample_bit_for_bit)
{
    // The Lagrange weights at a fraction of zero are exactly (0, 1, 0, 0), so
    // an integer delay is a read rather than a weighted sum that lands nearby.
    // Everything the comb claims about a bit-exact neutral rests on this.
    CHECK (FractionalDelay::lagrange (9.9, 0.3141592653589793, -7.7, 4.4, 0.0)
             == 0.3141592653589793);

    // Exercised in the order a feedback loop uses: read first, then write. So
    // `read(D)` before writing sample n is sample n-D.
    FractionalDelay line;
    line.prepare (64);

    std::vector<double> written;

    for (int i = 0; i < 512; ++i)
    {
        for (const int delay : { 2, 7, 33, 60 })
            if (static_cast<int> (written.size()) >= delay)
                CHECK (line.read (static_cast<double> (delay))
                         == written[written.size() - static_cast<std::size_t> (delay)]);

        const double x = std::sin (i * 0.37) + 0.3 * std::sin (i * 1.1);

        line.write (x);
        written.push_back (x);
    }
}

TEZLA_TEST (the_delay_line_matches_the_lagrange_kernel_it_claims_to_use)
{
    // Checked against the kernel written out separately rather than against the
    // line's own arithmetic -- otherwise this is a test of nothing.
    FractionalDelay line;
    line.prepare (64);

    std::vector<double> written;

    for (int i = 0; i < 300; ++i)
    {
        const double x = std::sin (i * 0.21) * 0.7;

        line.write (x);
        written.push_back (x);
    }

    for (const double delay : { 4.25, 9.5, 17.75, 40.1 })
    {
        const double whole = std::floor (delay);
        const double fraction = delay - whole;

        // y0 is one sample less delayed, y1 at the integer delay, y2 and y3 one
        // and two more -- counted back from the *next* write, which is where
        // the line reads from.
        const std::size_t index = written.size() - static_cast<std::size_t> (whole);

        const double y0 = written[index + 1];
        const double y1 = written[index];
        const double y2 = written[index - 1];
        const double y3 = written[index - 2];

        CHECK_NEAR (line.read (delay),
                    FractionalDelay::lagrange (y0, y1, y2, y3, fraction), 1.0e-15);
    }
}

TEZLA_TEST (a_delay_beyond_the_line_is_clamped_rather_than_read_out_of_bounds)
{
    // Both ends, and a negative -- a modulation destination can be driven
    // anywhere and the audio thread has no business finding out where the heap
    // ends.
    FractionalDelay line;
    line.prepare (32);

    for (int i = 0; i < 200; ++i)
        line.write (std::sin (i * 0.3));

    for (const double delay : { -1000.0, -1.0, 0.0, 0.5, 1.0, 2.0, 31.0, 40.0, 1.0e9 })
    {
        const double y = line.read (delay);

        CHECK (std::isfinite (y));
        CHECK (std::abs (y) <= 1.0);
    }
}

// ---------------------------------------------------------------------------
// Where the notches land
// ---------------------------------------------------------------------------

TEZLA_TEST (the_comb_matches_its_own_transfer_function)
{
    // The measurement that catches an off-by-one, and did: reading the delay
    // line from the most recent *written* sample rather than from the next
    // write slot makes the true loop delay D+1. A 6.02 dB peak then measures
    // 5.946 dB at the first harmonic and 5.333 dB at the third -- a phase error
    // of one sample scaled by frequency, which looks like nothing at all until
    // it is compared against the algebra.
    constexpr double rate = 48000.0;

    for (const double delaySeconds : { 0.0005, 0.002, 0.008 })
        for (const double feedback : { 0.0, 0.5, -0.5 })
            for (const bool inverted : { false, true })
            {
                auto comb = made (rate, delaySeconds, 1.0, feedback);
                comb.setWetInverted (inverted);

                const double delaySamples = comb.currentDelaySamples();
                const double loopGain = comb.getFeedback();

                for (const double frequency : { 137.0, 611.0, 1489.0, 3671.0, 7919.0 })
                {
                    const double measured = magnitudeAt (comb, frequency, rate);
                    const double expected = theoreticalMagnitude (frequency, rate, delaySamples,
                                                                  1.0, loopGain, inverted);

                    CHECK_NEAR (dbOf (measured), dbOf (expected), 0.02);
                }
            }
}

TEZLA_TEST (the_notches_are_where_one_over_twice_the_delay_says)
{
    // The readable form of the same claim, and the one the control surface is
    // described by. With the wet added in phase the first null is at 1/(2D),
    // they repeat every 1/D, and the peaks sit half way between at +6.02 dB.
    constexpr double rate = 48000.0;

    for (const double delaySeconds : { 0.0005, 0.002, 0.008 })
    {
        auto comb = made (rate, delaySeconds, 1.0, 0.0);

        const double spacing = 1.0 / delaySeconds;

        CHECK_NEAR (comb.firstNotchHz(), 0.5 * spacing, 0.5);

        for (int k = 0; k < 3; ++k)
        {
            const double notch = (2.0 * k + 1.0) * 0.5 * spacing;
            const double peak = (k + 1.0) * spacing;

            if (peak > rate * 0.4)
                break;

            CHECK (dbOf (magnitudeAt (comb, notch, rate)) < -60.0);
            CHECK_NEAR (dbOf (magnitudeAt (comb, peak, rate)), 6.021, 0.02);
        }
    }
}

TEZLA_TEST (inverting_the_wet_moves_the_pattern_by_half_a_spacing)
{
    // Not reachable by turning the mix knob: it swaps the notches and the
    // peaks. That is why it is a switch.
    constexpr double rate = 48000.0;
    constexpr double delaySeconds = 0.002;

    const double spacing = 1.0 / delaySeconds;
    const double notch = 0.5 * spacing;
    const double peak = spacing;

    auto plain = made (rate, delaySeconds, 1.0, 0.0);

    auto inverted = made (rate, delaySeconds, 1.0, 0.0);
    inverted.setWetInverted (true);

    CHECK (dbOf (magnitudeAt (plain, notch, rate)) < -60.0);
    CHECK_NEAR (dbOf (magnitudeAt (plain, peak, rate)), 6.021, 0.02);

    // Exactly the other way round.
    CHECK_NEAR (dbOf (magnitudeAt (inverted, notch, rate)), 6.021, 0.02);
    CHECK (dbOf (magnitudeAt (inverted, peak, rate)) < -60.0);

    CHECK_NEAR (inverted.firstNotchHz(), spacing, 0.5);
}

TEZLA_TEST (feedback_sharpens_the_peaks_and_its_sign_moves_them)
{
    // Feedback and wet-invert are not redundant: one changes how sharp the
    // pattern is, the other changes where it sits, and the four combinations
    // are all different.
    constexpr double rate = 48000.0;
    constexpr double delaySeconds = 0.002;

    const double spacing = 1.0 / delaySeconds;

    double previous = 0.0;

    for (const double feedback : { 0.0, 0.4, 0.72, 1.0 })
    {
        auto comb = made (rate, delaySeconds, 1.0, feedback);

        const double peak = dbOf (magnitudeAt (comb, spacing, rate));

        CHECK (peak > previous);
        previous = peak;
    }

    // 0.72 is the setting the brief actually used. Scaled by kMaximumFeedback
    // that is a loop gain of 0.684, and at a resonance the wet and the dry are
    // in phase with the loop summed: (1 - g + m)/(1 - g) = 4.165, or 12.39 dB.
    // Quoted from the algebra rather than from a guess -- the first version of
    // this test added the feedforward's 6.02 dB to the loop's gain and expected
    // 16.03, which is not how the two compose.
    auto atSeventyTwo = made (rate, delaySeconds, 1.0, 0.72);

    const double loopGain = atSeventyTwo.getFeedback();

    CHECK_NEAR (loopGain, 0.684, 1.0e-12);
    CHECK_NEAR (dbOf (magnitudeAt (atSeventyTwo, spacing, rate)),
                dbOf ((1.0 - loopGain + 1.0) / (1.0 - loopGain)), 0.02);
    CHECK_NEAR (dbOf (magnitudeAt (atSeventyTwo, spacing, rate)), 12.39, 0.05);

    // Negative feedback moves the resonances to where the notches were. The two
    // differences are **not** the same size, which is worth stating because
    // assuming they were is what the first version of this test did:
    //
    //     at k/D        (2 - g)/(1 - g)    g = +0.684:  12.39 dB
    //                                      g = -0.684:   4.05 dB   -> 8.34 apart
    //     at (2k+1)/2D  g/(1 + g)          g = +0.684:  -7.82 dB
    //                                      g = -0.684:   6.71 dB   -> 14.53 apart
    //
    // The dry path is in the numerator either way, so it reinforces one case
    // and partly fills in the other. Both figures come from the transfer
    // function here rather than from a round number that happened to pass.
    auto positive = made (rate, delaySeconds, 1.0, 0.72);
    auto negative = made (rate, delaySeconds, 1.0, -0.72);

    const double samples = positive.currentDelaySamples();

    const double atHalfSpacing = dbOf (magnitudeAt (negative, 0.5 * spacing, rate))
                                   - dbOf (magnitudeAt (positive, 0.5 * spacing, rate));

    CHECK_NEAR (atHalfSpacing,
                dbOf (theoreticalMagnitude (0.5 * spacing, rate, samples, 1.0, -loopGain, false))
                  - dbOf (theoreticalMagnitude (0.5 * spacing, rate, samples, 1.0, loopGain, false)),
                0.05);
    CHECK_NEAR (atHalfSpacing, 14.53, 0.1);

    const double atFullSpacing = dbOf (magnitudeAt (positive, spacing, rate))
                                   - dbOf (magnitudeAt (negative, spacing, rate));

    CHECK_NEAR (atFullSpacing,
                dbOf (theoreticalMagnitude (spacing, rate, samples, 1.0, loopGain, false))
                  - dbOf (theoreticalMagnitude (spacing, rate, samples, 1.0, -loopGain, false)),
                0.05);
    CHECK_NEAR (atFullSpacing, 8.34, 0.1);
}

// ---------------------------------------------------------------------------
// Key tracking
// ---------------------------------------------------------------------------

TEZLA_TEST (key_tracking_puts_the_peaks_on_the_notes_own_harmonics)
{
    // The reason this is inside the instrument rather than after it: a comb
    // locked to the played note comes out tuned instead of clangy.
    constexpr double rate = 48000.0;

    for (const double note : { 41.2, 55.0, 110.0, 220.0 })
    {
        auto comb = made (rate, 0.002, 1.0, 0.6);
        comb.setNoteHz (note);
        comb.setKeyTrack (1.0);

        // Fully tracked, the delay is one period of the note.
        CHECK_NEAR (comb.currentDelaySamples(), rate / note, 1.0e-9);

        // So the peaks are the harmonic series, and the notches sit between
        // them at the half-harmonics. The size of the gap comes from the
        // transfer function -- at a loop gain of 0.57 it is 19.24 dB, and the
        // first version of this test asked for 20 and failed for being right.
        const double loopGain = comb.getFeedback();
        const double samples = comb.currentDelaySamples();

        const double gap = dbOf (theoreticalMagnitude (note, rate, samples, 1.0, loopGain, false))
                             - dbOf (theoreticalMagnitude (0.5 * note, rate, samples, 1.0, loopGain, false));

        CHECK_NEAR (gap, 19.24, 0.05);

        for (const int harmonic : { 1, 2, 3, 5 })
        {
            const double at = note * harmonic;

            if (at > rate * 0.4)
                break;

            CHECK_NEAR (dbOf (magnitudeAt (comb, at, rate))
                          - dbOf (magnitudeAt (comb, at - 0.5 * note, rate)), gap, 0.1);
        }
    }
}

TEZLA_TEST (the_key_track_blend_is_geometric)
{
    // Half way between a 2 ms delay and an 8 ms one is 4 ms, not 5 ms, because
    // a delay is a frequency in disguise and the ear hears the ratio.
    constexpr double rate = 48000.0;

    auto comb = made (rate, 0.002, 1.0, 0.0);
    comb.setNoteHz (125.0);   // 8 ms

    comb.setKeyTrack (0.0);
    CHECK_NEAR (comb.currentDelaySamples(), 0.002 * rate, 1.0e-9);

    comb.setKeyTrack (1.0);
    CHECK_NEAR (comb.currentDelaySamples(), 0.008 * rate, 1.0e-9);

    comb.setKeyTrack (0.5);
    CHECK_NEAR (comb.currentDelaySamples(), 0.004 * rate, 1.0e-9);

    // A quarter of the way is a quarter of the way in octaves: 2 ms * 4^0.25.
    comb.setKeyTrack (0.25);
    CHECK_NEAR (comb.currentDelaySamples(), 0.002 * rate * std::sqrt (2.0), 1.0e-9);
}

TEZLA_TEST (key_tracking_with_no_note_leaves_the_delay_alone)
{
    // A caller with nothing playing passes 0, and must get the manual setting
    // rather than a division by it.
    auto comb = made (48000.0, 0.003, 1.0, 0.0);

    comb.setKeyTrack (1.0);
    comb.setNoteHz (0.0);

    CHECK_NEAR (comb.currentDelaySamples(), 0.003 * 48000.0, 1.0e-9);
    CHECK (std::isfinite (comb.currentDelaySamples()));
}

// ---------------------------------------------------------------------------
// The things it must never do
// ---------------------------------------------------------------------------

TEZLA_TEST (a_mix_of_zero_is_bit_exactly_transparent)
{
    // CLAUDE.md section 7: a stage permanently in the signal path needs a
    // bit-exact bypass at its neutral setting, not merely a transparent one.
    // Every other control at an extreme, so the bypass cannot be relying on
    // them being neutral too.
    //
    // Break-checking this found that it holds **without** the early-return in
    // processChannel: `input + 0.0 * delayed` is already `input` bit for bit,
    // so that branch is a fast path rather than the mechanism. The property is
    // real and worth asserting; the branch is an optimisation and the comment
    // there now says so.
    for (const double feedback : { -1.0, 0.0, 1.0 })
        for (const double damping : { 0.0, 1.0 })
            for (const bool inverted : { false, true })
                for (const double keyTrack : { 0.0, 1.0 })
                {
                    auto comb = made (48000.0, 0.004, 0.0, feedback);
                    comb.setDamping (damping);
                    comb.setWetInverted (inverted);
                    comb.setKeyTrack (keyTrack);
                    comb.setNoteHz (55.0);
                    comb.setSpread (1.0);

                    bool exact = true;

                    for (int i = 0; i < 8192; ++i)
                    {
                        const double x = 0.7 * std::sin (i * 0.019) + 0.2 * std::sin (i * 0.31);

                        double left = x;
                        double right = -x;

                        comb.process (left, right);

                        if (left != x || right != -x)
                            exact = false;
                    }

                    CHECK (exact);
                }
}

TEZLA_TEST (silence_in_silence_out)
{
    // The feedback path must not be able to start itself.
    for (const double feedback : { -1.0, 1.0 })
    {
        auto comb = made (48000.0, 0.004, 1.0, feedback);
        comb.setDamping (0.5);
        comb.setSpread (0.7);

        bool silent = true;

        for (int i = 0; i < 48000; ++i)
        {
            double left = 0.0;
            double right = 0.0;

            comb.process (left, right);

            if (left != 0.0 || right != 0.0)
                silent = false;
        }

        CHECK (silent);
    }
}

TEZLA_TEST (the_feedback_loop_cannot_be_made_to_run_away)
{
    // Every combination, hammered, including the delay swept while the loop is
    // ringing -- which is the case a static sweep misses, because a shrinking
    // delay line reads the same energy round more often than the feedback
    // coefficient alone predicts.
    double worst = 0.0;

    for (const double feedback : { -1.0, -0.72, 0.72, 1.0 })
        for (const double damping : { 0.0, 0.5, 1.0 })
            for (const bool inverted : { false, true })
            {
                auto comb = made (48000.0, 0.004, 1.0, feedback);
                comb.setDamping (damping);
                comb.setWetInverted (inverted);
                comb.setSpread (1.0);

                for (int i = 0; i < 200000; ++i)
                {
                    // Full scale, and the delay swept across its whole range
                    // while the loop rings.
                    const double sweep = 0.5 + 0.5 * std::sin (i * 0.0007);

                    comb.setDelaySeconds (Comb::kMinimumSeconds
                                            + sweep * (Comb::kMaximumSeconds - Comb::kMinimumSeconds));

                    double left = (i / 32) % 2 == 0 ? 1.0 : -1.0;
                    double right = -left;

                    comb.process (left, right);

                    CHECK (std::isfinite (left));
                    CHECK (std::isfinite (right));

                    worst = std::max ({ worst, std::abs (left), std::abs (right) });
                }
            }

    // 1/(1 - 0.95) is 20 on the wet path plus the dry, so 21 is the algebraic
    // worst case for a signal sitting exactly on a resonance. Measured: 15.6.
    CHECK (worst < 21.0);
}

TEZLA_TEST (the_interpolator_does_not_colour_a_swept_delay)
{
    // **On the delay line alone**, and that matters: the first version of this
    // ran the whole comb and swept the delay across a sample, which moves the
    // comb's own notches by 1% and swamps the thing being measured. A test
    // whose signal is dominated by something other than its subject is not a
    // measurement.
    //
    // So: hold a tone, read it out at a fixed fractional delay, and look at how
    // much the gain moves as the fraction walks across a whole sample. Against
    // a linear interpolator written out here, at 48 kHz:
    //
    //        freq      linear    Lagrange-3
    //       100 Hz    0.000 dB    0.000 dB
    //         1 kHz   0.018 dB    0.000 dB
    //         4 kHz   0.302 dB    0.016 dB
    //         8 kHz   1.248 dB    0.225 dB
    //        16 kHz   6.021 dB    3.255 dB
    //
    // Nineteen times better where a flanger lives, and only twice as good at
    // 16 kHz -- a four-point interpolator cannot hold a tone flat near Nyquist
    // and neither of these is the answer up there. Sonitus oversamples the
    // mangle path, so 16 kHz at the host rate is 4 kHz internally at x4.
    constexpr double rate = 48000.0;

    struct Reading
    {
        double frequency;
        double linearSpread;
        double lagrangeSpread;
    };

    const auto sweep = [] (double frequency, bool linear)
    {
        double lowest = 1.0e9;
        double highest = -1.0e9;

        for (int step = 0; step < 20; ++step)
        {
            const double delay = 20.0 + step / 20.0;

            FractionalDelay line;
            line.prepare (256);

            double phase = 0.0;
            const double increment = 2.0 * std::numbers::pi * frequency / rate;

            const auto readOut = [&] () -> double
            {
                if (! linear)
                    return line.read (delay);

                // Linear interpolation between the same two taps, built from
                // the Lagrange kernel with the outer weights zeroed -- so the
                // comparison is of the kernel and nothing else.
                const double whole = std::floor (delay);
                const double fraction = delay - whole;

                return line.read (whole) * (1.0 - fraction) + line.read (whole + 1.0) * fraction;
            };

            for (int i = 0; i < 4096; ++i)
            {
                (void) readOut();
                line.write (std::sin (phase));
                phase += increment;
            }

            double inPhase = 0.0;
            double quadrature = 0.0;
            const int measure = 8192;

            for (int i = 0; i < measure; ++i)
            {
                const double y = readOut();

                line.write (std::sin (phase));

                inPhase += y * std::sin (phase - delay * increment);
                quadrature += y * std::cos (phase - delay * increment);
                phase += increment;
            }

            const double reading = 20.0 * std::log10 (
                std::max (2.0 * std::hypot (inPhase, quadrature) / measure, 1.0e-12));

            lowest = std::min (lowest, reading);
            highest = std::max (highest, reading);
        }

        return highest - lowest;
    };

    const Reading expected[] = {
        { 1000.0, 0.018, 0.000 },
        { 4000.0, 0.302, 0.016 },
        { 8000.0, 1.248, 0.225 },
    };

    for (const auto& item : expected)
    {
        CHECK_NEAR (sweep (item.frequency, true), item.linearSpread, 0.01);
        CHECK_NEAR (sweep (item.frequency, false), item.lagrangeSpread, 0.01);

        // And the point of the whole thing.
        CHECK (sweep (item.frequency, false) <= sweep (item.frequency, true));
    }
}

TEZLA_TEST (the_spread_offsets_the_two_channels_and_zero_leaves_them_identical)
{
    constexpr double rate = 48000.0;

    // At zero spread the two channels are the same filter, so identical input
    // gives bit-identical output.
    {
        auto comb = made (rate, 0.003, 1.0, 0.6);
        comb.setSpread (0.0);

        bool identical = true;

        for (int i = 0; i < 8192; ++i)
        {
            double left = std::sin (i * 0.05);
            double right = left;

            comb.process (left, right);

            if (left != right)
                identical = false;
        }

        CHECK (identical);
    }

    // And with spread up they are genuinely different -- which is what makes
    // it wide rather than merely louder.
    {
        auto comb = made (rate, 0.003, 1.0, 0.6);
        comb.setSpread (1.0);

        double biggest = 0.0;

        for (int i = 0; i < 8192; ++i)
        {
            double left = std::sin (i * 0.05);
            double right = left;

            comb.process (left, right);

            biggest = std::max (biggest, std::abs (left - right));
        }

        CHECK (biggest > 0.2);
    }
}

TEZLA_TEST (damping_takes_the_top_off_the_feedback_and_zero_is_bit_exact)
{
    // A one-pole inside a high-Q loop lowers *every* peak, and more at the top.
    // That is what a damping control is, and the first version of this test
    // asserted the fundamental would move less than 3 dB -- which is not true
    // and not desirable: at a loop gain of 0.855 even a 6% loss of gain at
    // 500 Hz takes several decibels off the peak.
    //
    // Measured, feedback 0.9, a 2 ms delay, the peak at each harmonic of
    // 500 Hz:
    //
    //     damping        1x        2x        4x        8x       16x
    //        0.00    17.949    17.949    17.949    17.949    17.949
    //        0.40    16.971    15.066    11.964     9.248     7.933
    //        0.80    11.136     8.301     6.948     6.514     6.397
    //        1.00     8.226     6.816     6.359     6.236     6.204
    //
    // Two things are worth pinning. The tilt: at full damping the top has
    // fallen to 6.20 dB, which is the feedforward comb's own +6.02 with almost
    // nothing left of the loop, while the fundamental still has 8.23. And the
    // undamped row is flat to the last digit, because the loop has no filter
    // in it at all rather than one set to unity.
    constexpr double rate = 48000.0;
    constexpr double delaySeconds = 0.002;

    const double spacing = 1.0 / delaySeconds;

    auto open = made (rate, delaySeconds, 1.0, 0.9);

    const double undampedLow = dbOf (magnitudeAt (open, spacing, rate));
    const double undampedHigh = dbOf (magnitudeAt (open, spacing * 16.0, rate));

    CHECK_NEAR (undampedLow, 17.949, 0.02);
    CHECK_NEAR (undampedHigh, 17.949, 0.02);

    for (const double damping : { 0.4, 0.8, 1.0 })
    {
        auto damped = made (rate, delaySeconds, 1.0, 0.9);
        damped.setDamping (damping);

        const double low = dbOf (magnitudeAt (damped, spacing, rate));
        const double high = dbOf (magnitudeAt (damped, spacing * 16.0, rate));

        // Tilted, always: the top loses more than the bottom.
        CHECK (undampedHigh - high > undampedLow - low + 1.0);

        // And never below the feedforward path's own +6.02, which the loop
        // cannot take away.
        CHECK (high > 6.0);
    }

    // The two ends of the table, so a change to the corner range shows here.
    auto fully = made (rate, delaySeconds, 1.0, 0.9);
    fully.setDamping (1.0);

    CHECK_NEAR (dbOf (magnitudeAt (fully, spacing, rate)), 8.226, 0.05);
    CHECK_NEAR (dbOf (magnitudeAt (fully, spacing * 16.0, rate)), 6.204, 0.05);

    // And at zero the one-pole is skipped rather than run with a coefficient
    // of one, so an undamped comb is bit-exactly an undamped comb.
    auto reference = made (rate, delaySeconds, 1.0, 0.9);
    auto explicitZero = made (rate, delaySeconds, 1.0, 0.9);
    explicitZero.setDamping (0.0);

    bool exact = true;

    for (int i = 0; i < 8192; ++i)
    {
        double la = std::sin (i * 0.05);
        double ra = la;
        double lb = la;
        double rb = la;

        reference.process (la, ra);
        explicitZero.process (lb, rb);

        if (la != lb || ra != rb)
            exact = false;
    }

    CHECK (exact);
}

TEZLA_TEST (the_kernel_is_the_lagrange_formula_the_paper_defines)
{
    // Laakso, Valimaki, Karjalainen & Laine, "Splitting the Unit Delay",
    // IEEE Signal Processing Magazine 13(1), 1996 -- saved under
    // `technical references/sonitus/`.
    //
    // `Comb::lagrange` is a closed form, and a closed form is a transcription
    // until something checks it against the definition. This evaluates the
    // paper's general product formula (Eq. 42),
    //
    //     h(n) = prod over k != n of (D - k) / (n - k),   n = 0..N
    //
    // and compares. Checking the closed form against a table copied from the
    // same page would only prove the copy was faithful; the product is the
    // thing the table is derived *from*.
    //
    // **D = 1 + fraction**, and that is not a detail. Eq. 21 puts the optimal
    // delay at the "center of gravity" of the ideal impulse response -- for an
    // odd order N, `M_opt = Int(D) - (N-1)/2`, which for four taps means the
    // interpolation point belongs between the middle two. Getting this wrong is
    // the off-by-one that made the comb's loop one sample too long, and it read
    // as slightly the wrong notch frequency rather than as anything obviously
    // broken.
    const auto reference = [] (int n, double bigD)
    {
        constexpr int order = 3;

        double product = 1.0;

        for (int k = 0; k <= order; ++k)
            if (k != n)
                product *= (bigD - k) / static_cast<double> (n - k);

        return product;
    };

    double worst = 0.0;

    for (int step = 0; step <= 1000; ++step)
    {
        const double fraction = step / 1000.0;
        const double bigD = 1.0 + fraction;

        // The kernel's four weights, recovered by feeding it unit impulses.
        const double h0 = FractionalDelay::lagrange (1.0, 0.0, 0.0, 0.0, fraction);
        const double h1 = FractionalDelay::lagrange (0.0, 1.0, 0.0, 0.0, fraction);
        const double h2 = FractionalDelay::lagrange (0.0, 0.0, 1.0, 0.0, fraction);
        const double h3 = FractionalDelay::lagrange (0.0, 0.0, 0.0, 1.0, fraction);

        worst = std::max (worst, std::abs (h0 - reference (0, bigD)));
        worst = std::max (worst, std::abs (h1 - reference (1, bigD)));
        worst = std::max (worst, std::abs (h2 - reference (2, bigD)));
        worst = std::max (worst, std::abs (h3 - reference (3, bigD)));

        // The weights sum to one at every fraction, which is what makes the
        // interpolator pass DC unchanged.
        CHECK_NEAR (h0 + h1 + h2 + h3, 1.0, 1.0e-12);
    }

    // Measured worst across a thousand fractions: 4.4e-16.
    CHECK (worst < 1.0e-12);

    // The paper's own worked values, as a second anchor. At a half sample the
    // kernel is symmetric -- which is why the even-length Lagrange filters are
    // "exactly linear-phase" at d = 0.5.
    CHECK_NEAR (FractionalDelay::lagrange (1.0, 0.0, 0.0, 0.0, 0.5), -0.0625, 1.0e-12);
    CHECK_NEAR (FractionalDelay::lagrange (0.0, 1.0, 0.0, 0.0, 0.5),  0.5625, 1.0e-12);
    CHECK_NEAR (FractionalDelay::lagrange (0.0, 0.0, 1.0, 0.0, 0.5),  0.5625, 1.0e-12);
    CHECK_NEAR (FractionalDelay::lagrange (0.0, 0.0, 0.0, 1.0, 0.5), -0.0625, 1.0e-12);
}

TEZLA_TEST (the_interpolator_never_gains_which_is_why_the_comb_can_feed_back)
{
    // The property that makes Lagrange the right choice *here* specifically,
    // and the paper states it: "The maximum of the magnitude response never
    // exceeds unity when the delay is near to the half filter length. This is
    // important in applications including feedback."
    //
    // A comb is a feedback application. An interpolator with gain above unity
    // anywhere in the band would compound every pass round the loop, so a
    // feedback setting that measured stable at one delay could run away at
    // another -- and the feedback cap alone would not save it, because the cap
    // bounds the coefficient rather than the loop gain.
    //
    // Checked across the whole fractional range and the whole band.
    constexpr double rate = 48000.0;

    double worst = 0.0;

    for (int step = 0; step <= 100; ++step)
    {
        const double fraction = step / 100.0;

        for (int bin = 1; bin < 240; ++bin)
        {
            const double frequency = bin * rate / 480.0;
            const double omega = 2.0 * std::numbers::pi * frequency / rate;

            // The kernel's frequency response, taps at 0, 1, 2, 3 samples.
            double real = 0.0;
            double imaginary = 0.0;

            const double h[4] = {
                FractionalDelay::lagrange (1.0, 0.0, 0.0, 0.0, fraction),
                FractionalDelay::lagrange (0.0, 1.0, 0.0, 0.0, fraction),
                FractionalDelay::lagrange (0.0, 0.0, 1.0, 0.0, fraction),
                FractionalDelay::lagrange (0.0, 0.0, 0.0, 1.0, fraction),
            };

            for (int tap = 0; tap < 4; ++tap)
            {
                real += h[tap] * std::cos (-omega * tap);
                imaginary += h[tap] * std::sin (-omega * tap);
            }

            worst = std::max (worst, std::hypot (real, imaginary));
        }
    }

    // Measured across 101 fractions and the whole band: 1.0000000000, reached
    // at DC where the weights sum to one, and never exceeded.
    CHECK (worst < 1.0 + 1.0e-12);
}
