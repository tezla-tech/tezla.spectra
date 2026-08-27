#include "TestFramework.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

#include <tezla/dsp/StepSequencer.hpp>

using namespace tezla::dsp;

namespace
{
StepSequencer made (double rate = 48000.0, int length = 4, double glide = 0.0)
{
    StepSequencer sequencer;
    sequencer.prepare (rate);
    sequencer.setLength (length);
    sequencer.setGlide (glide);

    // A pattern with a distinct value per step, so which step is playing is
    // readable from the output alone.
    for (int i = 0; i < StepSequencer::kMaxSteps; ++i)
        sequencer.setStep (i, -1.0 + 2.0 * i / (StepSequencer::kMaxSteps - 1.0));

    return sequencer;
}
} // namespace

TEZLA_TEST (the_pattern_plays_in_order_and_repeats_at_its_length)
{
    auto sequencer = made (48000.0, 4);
    sequencer.setRateHz (1.0);

    // A whole step per call, so each call lands exactly on a boundary.
    for (int pass = 0; pass < 3; ++pass)
        for (int step = 0; step < 4; ++step)
        {
            CHECK (sequencer.getStepIndex() == step);
            CHECK_NEAR (sequencer.getValue(), sequencer.getStep (step), 1.0e-12);

            (void) sequencer.advance (48000);
        }
}

TEZLA_TEST (a_hard_pattern_holds_each_value_for_the_whole_step)
{
    // Glide at zero means steps, not ramps: within a step the output must not
    // move at all.
    auto sequencer = made (48000.0, 4, 0.0);
    sequencer.setRateHz (1.0);

    for (int step = 0; step < 4; ++step)
    {
        const double expected = sequencer.getStep (step);

        for (int i = 0; i < 64; ++i)
        {
            CHECK (sequencer.getValue() == expected);
            (void) sequencer.advance (48000 / 64);
        }
    }
}

TEZLA_TEST (the_glide_slides_out_of_a_step_rather_than_into_it)
{
    // The distinction the header argues for: at every glide setting the output
    // is exactly the step's own value at the instant the step begins. Gliding
    // *into* a value would put the pattern you hear half a step behind the one
    // you drew.
    //
    // Break-checked by interpolating from the previous step instead, which
    // leaves this reading 0.333 at the start of a step whose value is 1.0.
    for (const double glide : { 0.0, 0.25, 0.5, 0.75, 1.0 })
    {
        auto sequencer = made (48000.0, 4, glide);
        sequencer.setRateHz (1.0);

        for (int step = 0; step < 4; ++step)
        {
            CHECK_NEAR (sequencer.getValue(), sequencer.getStep (step), 1.0e-12);
            (void) sequencer.advance (48000);
        }
    }

    // And with the glide up, it does eventually move -- otherwise the above
    // would pass on an implementation with no glide at all.
    auto glided = made (48000.0, 4, 1.0);
    glided.setRateHz (1.0);

    (void) glided.advance (24000);

    const double halfway = glided.getValue();

    // smoothstep(0.5) is exactly 0.5, so half way through a fully glided step
    // the output is exactly half way between the two values. Stronger than
    // "somewhere in between", and it costs nothing to say.
    CHECK_NEAR (halfway, 0.5 * (glided.getStep (0) + glided.getStep (1)), 1.0e-12);
    CHECK (halfway > glided.getStep (0));
    CHECK (halfway < glided.getStep (1));
}

TEZLA_TEST (the_glide_fraction_is_how_much_of_the_step_it_takes)
{
    // Glide 0.25 holds for three quarters of the step and slides across the
    // last quarter. Measured at the 60% mark, which is inside the hold for
    // glide 0.25 and inside the slide for glide 0.75.
    auto quick = made (48000.0, 4, 0.25);
    auto slow = made (48000.0, 4, 0.75);

    quick.setRateHz (1.0);
    slow.setRateHz (1.0);

    (void) quick.advance (static_cast<int> (48000 * 0.6));
    (void) slow.advance (static_cast<int> (48000 * 0.6));

    CHECK_NEAR (quick.getValue(), quick.getStep (0), 1.0e-12);
    CHECK (slow.getValue() > slow.getStep (0) + 0.05);
}

TEZLA_TEST (a_fully_glided_pattern_has_no_corner_in_it)
{
    // Smoothstep rather than a line, because a modulation source with a corner
    // in it puts that corner into whatever it is modulating. The test is on the
    // second difference: a linear ramp has a jump in slope at every boundary, a
    // smoothstep does not.
    constexpr double rate = 48000.0;
    constexpr int samplesPerStep = 4800;

    auto sequencer = made (rate, 4, 1.0);
    sequencer.setRateHz (rate / samplesPerStep);

    std::vector<double> values;
    values.reserve (samplesPerStep * 4 + 2);

    for (int i = 0; i < samplesPerStep * 4 + 2; ++i)
        values.push_back (sequencer.advance (1));

    double biggestSecondDifference = 0.0;
    double biggestFirstDifference = 0.0;

    for (std::size_t i = 2; i < values.size(); ++i)
    {
        const double first = values[i] - values[i - 1];
        const double second = first - (values[i - 1] - values[i - 2]);

        biggestFirstDifference = std::max (biggestFirstDifference, std::abs (first));
        biggestSecondDifference = std::max (biggestSecondDifference, std::abs (second));
    }

    // The slope itself is small, and its rate of change is smaller still by
    // about the same factor -- which is what "no corner" means numerically.
    CHECK (biggestFirstDifference < 0.001);
    CHECK (biggestSecondDifference < 1.0e-6);
}

TEZLA_TEST (the_transport_position_is_assigned_rather_than_accumulated)
{
    // The Lfo.hpp argument, and the reason a bounce matches what was heard:
    // there is no running total, so the same bar is the same position however
    // the transport arrived there.
    auto played = made (48000.0, 4, 0.5);
    auto jumped = made (48000.0, 4, 0.5);

    // One walks the timeline; the other is dropped straight onto the same
    // point. The landing is deliberately **part way through a step and inside
    // the glide** -- 32.4 beats is position 129.6, which is step 1 at 60% with
    // the slide already under way.
    //
    // Landing on a step boundary instead is what made the first version of
    // this test pass with the position accumulated rather than assigned: two
    // positions 496 steps apart both wrapped into the hold region of step 0
    // and produced the identical value. Comparing an output that is constant
    // across most of its input is not comparing much.
    for (int beat = 0; beat < 32; ++beat)
        (void) played.setPhaseFromPpq (beat * 0.25, 4.0);

    const double walked = played.setPhaseFromPpq (32.4, 4.0);
    const double dropped = jumped.setPhaseFromPpq (32.4, 4.0);

    CHECK (walked == dropped);
    CHECK (played.getStepIndex() == jumped.getStepIndex());
    CHECK (played.getStepFraction() == jumped.getStepFraction());

    // And the value is genuinely mid-glide, so the comparison above had
    // something to distinguish.
    CHECK_NEAR (played.getStepFraction(), 0.6, 1.0e-9);
    CHECK (walked > played.getStep (1));
    CHECK (walked < played.getStep (2));

    // Bar 1 and bar 9 are the same point in a four-step pattern of sixteenths
    // -- but **not bit-identically**, and the reason is worth stating rather
    // than hiding behind a tolerance. `ppq * stepsPerBeat` is a large number by
    // the end of a long arrangement, and a double has fewer bits left over for
    // the fraction the further out it gets. Measured, the same point in the
    // bar against bar 1:
    //
    //     bar        step fraction            error in the output
    //         1   0.60000000000000009                    0
    //         9   0.59999999999999432             1.44e-15
    //        65   0.59999999999990905             2.33e-14
    //      1025   0.59999999999854481             3.73e-13
    //     16385   0.59999999997671694             5.96e-12
    //
    // Six picounits after nine hours of music at 120 bpm, and bounded -- it
    // grows with the *magnitude* of the position, not with how long the plugin
    // has been running. That is the whole difference from an accumulator,
    // whose error grows without bound and never comes back.
    const double barOne = played.setPhaseFromPpq (0.4, 4.0);
    const double barNine = played.setPhaseFromPpq (32.4, 4.0);
    const double barSixteenThousand = played.setPhaseFromPpq (0.4 + 16384.0 * 4.0, 4.0);

    CHECK_NEAR (barNine, barOne, 1.0e-14);
    CHECK_NEAR (barSixteenThousand, barOne, 1.0e-11);
}

TEZLA_TEST (a_negative_transport_position_does_not_index_off_the_front)
{
    // A count-in, or a loop that starts before bar 1. fmod keeps the sign of
    // its left operand, so the naive wrap indexes backwards out of the array.
    auto sequencer = made (48000.0, 4, 0.5);

    for (double ppq = -8.0; ppq <= 8.0; ppq += 0.03125)
    {
        const double value = sequencer.setPhaseFromPpq (ppq, 4.0);

        CHECK (std::isfinite (value));
        CHECK (value >= -1.0);
        CHECK (value <= 1.0);
        CHECK (sequencer.getStepIndex() >= 0);
        CHECK (sequencer.getStepIndex() < sequencer.getLength());
    }

    // -1 beat at four steps per beat is four steps back, which is a whole
    // pattern, so it must land on step 0 exactly as ppq 0 does.
    CHECK (sequencer.setPhaseFromPpq (-1.0, 4.0) == sequencer.setPhaseFromPpq (0.0, 4.0));
}

TEZLA_TEST (the_length_control_shortens_the_pattern)
{
    for (int length = 1; length <= StepSequencer::kMaxSteps; ++length)
    {
        auto sequencer = made (48000.0, length);
        sequencer.setRateHz (1.0);

        for (int i = 0; i < length * 2; ++i)
        {
            CHECK (sequencer.getStepIndex() == i % length);
            (void) sequencer.advance (48000);
        }
    }
}

TEZLA_TEST (the_output_is_block_size_independent)
{
    // CLAUDE.md section 7: the output must not depend on how the host cut the
    // callback up. There is no accumulator here beyond the position itself, so
    // this is asserting that and nothing else -- but it is the assertion that
    // would catch a smoother being added later without a sample-counted timer.
    constexpr double rate = 48000.0;

    auto small = made (rate, 5, 0.6);
    auto large = made (rate, 5, 0.6);

    small.setRateHz (7.3);
    large.setRateHz (7.3);

    double worst = 0.0;

    for (int block = 0; block < 100; ++block)
    {
        for (int i = 0; i < 8; ++i)
            (void) small.advance (64);

        const double a = large.advance (512);
        const double b = small.getValue();

        worst = std::max (worst, std::abs (a - b));
    }

    CHECK (worst < 1.0e-9);
}

TEZLA_TEST (a_rate_of_zero_holds_and_does_not_creep)
{
    auto sequencer = made (48000.0, 4, 0.5);
    sequencer.setRateHz (0.0);

    (void) sequencer.advance (1024);

    const double held = sequencer.getValue();

    for (int i = 0; i < 1000; ++i)
        CHECK (sequencer.advance (1024) == held);
}

TEZLA_TEST (every_setting_stays_inside_minus_one_to_one)
{
    auto sequencer = made (48000.0, 16, 1.0);
    sequencer.setRateHz (200.0);

    // Values are clamped on the way in, so the extremes are reachable and the
    // interpolation must not overshoot them. Smoothstep does not, but a
    // Catmull-Rom or a cubic through the points would, and this is the test
    // that would say so.
    for (int i = 0; i < StepSequencer::kMaxSteps; ++i)
        sequencer.setStep (i, i % 2 == 0 ? -5.0 : 5.0);

    for (int i = 0; i < 200000; ++i)
    {
        const double value = sequencer.advance (1);

        CHECK (std::isfinite (value));
        CHECK (value >= -1.0);
        CHECK (value <= 1.0);
    }
}

TEZLA_TEST (out_of_range_step_indices_are_refused_rather_than_written)
{
    auto sequencer = made();

    sequencer.setStep (-1, 0.5);
    sequencer.setStep (StepSequencer::kMaxSteps, 0.5);
    sequencer.setStep (10000, 0.5);

    CHECK (sequencer.getStep (-1) == 0.0);
    CHECK (sequencer.getStep (StepSequencer::kMaxSteps) == 0.0);

    // And the pattern is untouched.
    for (int i = 0; i < StepSequencer::kMaxSteps; ++i)
        CHECK_NEAR (sequencer.getStep (i), -1.0 + 2.0 * i / (StepSequencer::kMaxSteps - 1.0), 1.0e-12);
}
