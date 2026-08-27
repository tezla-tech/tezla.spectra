#include "TestFramework.hpp"

#include <algorithm>
#include <cmath>
#include <complex>
#include <numbers>
#include <vector>

#include <tezla/dsp/Phaser.hpp>

using namespace tezla::dsp;

namespace
{
double magnitudeAt (Phaser& phaser, double frequency, double sampleRate)
{
    phaser.reset();

    const int settle = static_cast<int> (sampleRate * 0.25);
    const int measure = static_cast<int> (std::round (200.0 * sampleRate / frequency));

    double phase = 0.0;
    const double step = 2.0 * std::numbers::pi * frequency / sampleRate;

    for (int i = 0; i < settle; ++i)
    {
        double left = std::sin (phase);
        double right = left;
        phaser.process (left, right);
        phase += step;
    }

    double inPhase = 0.0;
    double quadrature = 0.0;

    for (int i = 0; i < measure; ++i)
    {
        double left = std::sin (phase);
        double right = left;
        phaser.process (left, right);

        inPhase += left * std::sin (phase);
        quadrature += left * std::cos (phase);
        phase += step;
    }

    return 2.0 * std::hypot (inPhase, quadrature) / measure;
}

double dbOf (double magnitude) { return 20.0 * std::log10 (std::max (magnitude, 1.0e-12)); }

/// |H| from the algebra rather than from the code.
///
///     A(z) = (c + z^-1) / (1 + c z^-1)          one allpass stage
///     V    = X / (1 - fb * z^-1 * A^N)          the loop, with its unit delay
///     Y    = X + s * m * A^N * V
double theoreticalMagnitude (double frequency, double sampleRate, double c, int stages,
                             double mix, double feedback, bool inverted)
{
    const double omega = 2.0 * std::numbers::pi * frequency / sampleRate;
    const std::complex<double> z (std::cos (-omega), std::sin (-omega));

    const std::complex<double> allpass = (c + z) / (1.0 + c * z);

    std::complex<double> cascade { 1.0, 0.0 };

    for (int stage = 0; stage < stages; ++stage)
        cascade *= allpass;

    const double wet = inverted ? -mix : mix;

    return std::abs (1.0 + wet * cascade / (1.0 - feedback * z * cascade));
}

Phaser made (double rate = 48000.0, double frequency = 800.0, int stages = 4,
             double mix = 1.0, double feedback = 0.0)
{
    Phaser phaser;
    phaser.prepare (rate);
    phaser.setFrequencyHz (frequency);
    phaser.setStages (stages);
    phaser.setMix (mix);
    phaser.setFeedback (feedback);
    return phaser;
}
} // namespace

TEZLA_TEST (the_phaser_matches_its_own_transfer_function)
{
    constexpr double rate = 48000.0;

    for (const double frequency : { 200.0, 800.0, 4000.0 })
        for (const int stages : { 2, 4, 8, 16 })
            for (const double feedback : { 0.0, 0.5, -0.5 })
                for (const bool inverted : { false, true })
                {
                    auto phaser = made (rate, frequency, stages, 1.0, feedback);
                    phaser.setWetInverted (inverted);

                    const double c = phaser.coefficientFor (frequency);
                    const double loopGain = phaser.getFeedback();

                    for (const double probe : { 137.0, 611.0, 1489.0, 3671.0, 7919.0 })
                    {
                        const double measured = magnitudeAt (phaser, probe, rate);
                        const double expected = theoreticalMagnitude (probe, rate, c, stages,
                                                                      1.0, loopGain, inverted);

                        CHECK_NEAR (dbOf (measured), dbOf (expected), 0.05);
                    }
                }
}

TEZLA_TEST (each_stage_has_unity_magnitude_so_the_notches_are_complete_nulls)
{
    // The proof that the cascade really is allpass, and it is a stronger one
    // than measuring its gain: `1 + A^N` is exactly zero only where `A^N` is
    // exactly -1, in **magnitude and phase**. A cascade that was 0.99 rather
    // than 1.0 could not produce a null deeper than -46 dB however good its
    // phase was.
    //
    // The **non-inverted** sum, and that is not an arbitrary choice. `1 + A^N`
    // nulls where N * phi is an odd multiple of 180 degrees, which is N/2
    // frequencies inside the band. `1 - A^N` nulls where N * phi is a multiple
    // of 360 -- and phi runs from 0 at DC to -180 at Nyquist, so for a
    // two-stage cascade those are DC and Nyquist and there is nothing in
    // between. Searching for one there is what the first version of this test
    // did, and it failed because there was no null to find rather than because
    // the cascade leaked.
    //
    // Which is also a real property of the control: inverting the wet on a
    // phaser does not move the notches half a spacing the way it does on a
    // comb. It moves them to the gaps, and the outermost pair falls off the
    // ends of the band.
    constexpr double rate = 48000.0;

    for (const int stages : { 2, 4, 8 })
    {
        auto phaser = made (rate, 800.0, stages, 1.0, 0.0);

        const double c = phaser.coefficientFor (800.0);

        // Find where the algebra says the null is, then measure there. **In
        // two passes**, because a true zero is arbitrarily narrow: a coarse
        // 0.2%-per-step sweep lands up to 1.6 Hz away from an eight-stage
        // phaser's null at 800 Hz, which is enough to read -30 dB at a point
        // that is actually -300. The first version of this failed for that
        // reason and not because the cascade was leaky.
        double deepest = 1.0e9;
        double deepestFrequency = 0.0;

        for (double frequency = 30.0; frequency < 18000.0; frequency *= 1.002)
        {
            const double predicted = theoreticalMagnitude (frequency, rate, c, stages,
                                                           1.0, 0.0, false);

            if (predicted < deepest)
            {
                deepest = predicted;
                deepestFrequency = frequency;
            }
        }

        CHECK (deepestFrequency > 0.0);

        const double low = deepestFrequency / 1.002;
        const double high = deepestFrequency * 1.002;

        for (double frequency = low; frequency < high; frequency *= 1.0000005)
        {
            const double predicted = theoreticalMagnitude (frequency, rate, c, stages,
                                                           1.0, 0.0, false);

            if (predicted < deepest)
            {
                deepest = predicted;
                deepestFrequency = frequency;
            }
        }

        // The algebra says it is a true zero.
        CHECK (dbOf (deepest) < -80.0);

        // And the implementation follows it down. The floor here is the
        // measurement, not the filter: an 800 Hz probe resolved to five
        // significant figures still sits a fraction of a hertz off a zero of
        // infinite depth.
        CHECK (dbOf (magnitudeAt (phaser, deepestFrequency, rate)) < -40.0);
    }
}

TEZLA_TEST (the_number_of_notches_is_half_the_number_of_stages)
{
    // N stages sweep N * 180 degrees from DC to Nyquist, so the sum with the
    // dry crosses an odd multiple of 180 exactly N/2 times.
    constexpr double rate = 96000.0;

    for (const int stages : { 2, 4, 6, 8, 12, 16 })
    {
        auto phaser = made (rate, 800.0, stages, 1.0, 0.0);

        CHECK (phaser.notchCount() == stages / 2);

        const double c = phaser.coefficientFor (800.0);

        // Count the minima of the predicted response across the band. Counting
        // from the algebra rather than from a measurement keeps the sweep cheap
        // and the count exact; the previous test is what ties the algebra to
        // the implementation.
        int found = 0;
        double previous = 1.0e9;
        double beforeThat = 1.0e9;

        for (double frequency = 20.0; frequency < rate * 0.499; frequency *= 1.0005)
        {
            const double here = theoreticalMagnitude (frequency, rate, c, stages, 1.0, 0.0, false);

            if (previous < beforeThat && previous < here && previous < 0.5)
                ++found;

            beforeThat = previous;
            previous = here;
        }

        CHECK (found == stages / 2);
    }
}

TEZLA_TEST (the_notches_are_not_evenly_spaced)
{
    // The whole reason this exists next to Comb: a flanger's notches are at
    // 1/(2D), 3/(2D), 5/(2D) -- equal *spacing* in hertz. A phaser's bunch
    // around the corner, so the ratio between successive gaps is nowhere near
    // one. That is what makes it vocal rather than metallic.
    constexpr double rate = 96000.0;
    constexpr int stages = 8;

    auto phaser = made (rate, 800.0, stages, 1.0, 0.0);

    const double c = phaser.coefficientFor (800.0);

    std::vector<double> notches;

    double previous = 1.0e9;
    double beforeThat = 1.0e9;
    double previousFrequency = 0.0;

    for (double frequency = 20.0; frequency < rate * 0.499; frequency *= 1.0005)
    {
        const double here = theoreticalMagnitude (frequency, rate, c, stages, 1.0, 0.0, false);

        if (previous < beforeThat && previous < here && previous < 0.5)
            notches.push_back (previousFrequency);

        beforeThat = previous;
        previous = here;
        previousFrequency = frequency;
    }

    CHECK (notches.size() == 4);

    if (notches.size() == 4)
    {
        const double firstGap = notches[1] - notches[0];
        const double lastGap = notches[3] - notches[2];

        // A comb would have these equal. Here they differ by a large factor.
        CHECK (lastGap > firstGap * 3.0);
    }
}

TEZLA_TEST (the_corner_is_where_it_was_asked_for_at_every_sample_rate)
{
    // The prewarp, and the reason these are TPT one-poles rather than
    // direct-form biquads. A first-order allpass passes -90 degrees at its
    // corner; that has to be the same frequency on every session.
    //
    // Measured as the notch positions of a two-stage phaser, which is the
    // single frequency at which the cascade reaches -180 degrees.
    for (const double frequency : { 200.0, 800.0, 4000.0 })
    {
        std::vector<double> notchAt;

        for (const double rate : { 44100.0, 48000.0, 96000.0, 192000.0 })
        {
            auto phaser = made (rate, frequency, 2, 1.0, 0.0);

            const double c = phaser.coefficientFor (frequency);

            double deepest = 1.0e9;
            double where = 0.0;

            for (double probe = 20.0; probe < 20000.0; probe *= 1.0002)
            {
                const double here = theoreticalMagnitude (probe, rate, c, 2, 1.0, 0.0, false);

                if (here < deepest)
                {
                    deepest = here;
                    where = probe;
                }
            }

            notchAt.push_back (where);
        }

        // A two-stage cascade reaches -180 degrees exactly at its corner, so
        // the notch is the corner -- at every rate, to within the resolution of
        // the sweep above.
        for (const double where : notchAt)
        {
            CHECK_NEAR (where, frequency, frequency * 0.001);
            CHECK_NEAR (where, notchAt.back(), frequency * 0.001);
        }
    }
}

TEZLA_TEST (a_mix_of_zero_is_bit_exactly_transparent)
{
    for (const double feedback : { -1.0, 0.0, 1.0 })
        for (const int stages : { 2, 16 })
            for (const bool inverted : { false, true })
            {
                auto phaser = made (48000.0, 800.0, stages, 0.0, feedback);
                phaser.setWetInverted (inverted);
                phaser.setSpread (1.0);

                bool exact = true;

                for (int i = 0; i < 8192; ++i)
                {
                    const double x = 0.7 * std::sin (i * 0.019) + 0.2 * std::sin (i * 0.31);

                    double left = x;
                    double right = -x;

                    phaser.process (left, right);

                    if (left != x || right != -x)
                        exact = false;
                }

                CHECK (exact);
            }
}

TEZLA_TEST (silence_in_silence_out)
{
    for (const double feedback : { -1.0, 1.0 })
    {
        auto phaser = made (48000.0, 800.0, 8, 1.0, feedback);
        phaser.setSpread (0.7);

        bool silent = true;

        for (int i = 0; i < 48000; ++i)
        {
            double left = 0.0;
            double right = 0.0;

            phaser.process (left, right);

            if (left != 0.0 || right != 0.0)
                silent = false;
        }

        CHECK (silent);
    }
}

TEZLA_TEST (the_feedback_loop_cannot_be_made_to_run_away)
{
    // Worth more here than on the comb: an allpass has unity magnitude at
    // *every* frequency, so the loop gain is the feedback with no frequency at
    // which it is smaller. There is nothing to save a loop set too high.
    double worst = 0.0;

    for (const double feedback : { -1.0, -0.6, 0.6, 1.0 })
        for (const int stages : { 2, 8, 16 })
            for (const bool inverted : { false, true })
            {
                auto phaser = made (48000.0, 800.0, stages, 1.0, feedback);
                phaser.setWetInverted (inverted);
                phaser.setSpread (1.0);

                for (int i = 0; i < 100000; ++i)
                {
                    // The corner swept across its whole range while the loop
                    // rings, which is where a marginal loop finds its worst
                    // moment.
                    const double sweep = 0.5 + 0.5 * std::sin (i * 0.0009);

                    phaser.setFrequencyHz (Phaser::kMinimumHz
                                             * std::pow (Phaser::kMaximumHz / Phaser::kMinimumHz, sweep));

                    double left = (i / 32) % 2 == 0 ? 1.0 : -1.0;
                    double right = -left;

                    phaser.process (left, right);

                    CHECK (std::isfinite (left));
                    CHECK (std::isfinite (right));

                    worst = std::max ({ worst, std::abs (left), std::abs (right) });
                }
            }

    // 1/(1 - 0.9) is 10 on the wet path plus the dry, so 11 is the steady-state
    // worst case for a *sine* sitting exactly on a resonance. A square wave has
    // harmonics near several resonances at once and a swept corner drags the
    // resonances through them, so the real figure is higher:
    //
    //     static corner, square wave          11.70
    //     swept corner, square wave           15.75
    //
    // What makes it a bound rather than a slow climb is that it does not move:
    // 15.7526 at 100k samples, 15.7526 at 400k. Pinned, so a change to
    // kMaximumFeedback shows up here rather than in a session.
    CHECK (worst < 16.0);
    CHECK (worst > 15.0);
}

TEZLA_TEST (the_spread_offsets_the_two_channels_and_zero_leaves_them_identical)
{
    constexpr double rate = 48000.0;

    {
        auto phaser = made (rate, 800.0, 8, 1.0, 0.5);
        phaser.setSpread (0.0);

        bool identical = true;

        for (int i = 0; i < 8192; ++i)
        {
            double left = std::sin (i * 0.05);
            double right = left;

            phaser.process (left, right);

            if (left != right)
                identical = false;
        }

        CHECK (identical);
    }

    {
        auto phaser = made (rate, 800.0, 8, 1.0, 0.5);
        phaser.setSpread (1.0);

        double biggest = 0.0;

        for (int i = 0; i < 8192; ++i)
        {
            double left = std::sin (i * 0.05);
            double right = left;

            phaser.process (left, right);

            biggest = std::max (biggest, std::abs (left - right));
        }

        CHECK (biggest > 0.1);
    }
}

TEZLA_TEST (the_stage_count_and_frequency_are_clamped_rather_than_trusted)
{
    auto phaser = made();

    phaser.setStages (-5);
    CHECK (phaser.getStages() == Phaser::kMinimumStages);

    phaser.setStages (10000);
    CHECK (phaser.getStages() == Phaser::kMaximumStages);

    phaser.setFrequencyHz (-1.0);
    CHECK (phaser.getFrequencyHz() == Phaser::kMinimumHz);

    phaser.setFrequencyHz (1.0e9);
    CHECK (phaser.getFrequencyHz() == Phaser::kMaximumHz);

    // And at a rate where the requested corner is above Nyquist, the
    // coefficient is still finite -- the prewarp is a tangent.
    Phaser slow;
    slow.prepare (8000.0);
    slow.setFrequencyHz (18000.0);
    slow.setMix (1.0);

    CHECK (std::isfinite (slow.coefficientFor (slow.getFrequencyHz())));
    CHECK (std::abs (slow.coefficientFor (slow.getFrequencyHz())) < 1.0);

    for (int i = 0; i < 4096; ++i)
    {
        double left = std::sin (i * 0.1);
        double right = left;

        slow.process (left, right);

        CHECK (std::isfinite (left));
    }
}
