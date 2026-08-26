#include "TestFramework.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <set>
#include <vector>

#include <tezla/dsp/Lfo.hpp>

using namespace tezla::dsp;

namespace
{
constexpr double kRate = 48000.0;

Lfo made (Lfo::Wave wave, double rateHz = 1.0)
{
    Lfo lfo;
    lfo.prepare (kRate);
    lfo.setWave (wave);
    lfo.setRateHz (rateHz);
    return lfo;
}
} // namespace

TEZLA_TEST (lfo_waveforms_have_the_shape_they_claim)
{
    // Values at the quarter points, which is where a wrong sign or a half-cycle
    // offset shows up immediately.
    CHECK_NEAR (Lfo::shapeAt (Lfo::Wave::sine, 0.00),  0.0, 1.0e-12);
    CHECK_NEAR (Lfo::shapeAt (Lfo::Wave::sine, 0.25),  1.0, 1.0e-12);
    CHECK_NEAR (Lfo::shapeAt (Lfo::Wave::sine, 0.75), -1.0, 1.0e-12);

    CHECK_NEAR (Lfo::shapeAt (Lfo::Wave::triangle, 0.00), -1.0, 1.0e-12);
    CHECK_NEAR (Lfo::shapeAt (Lfo::Wave::triangle, 0.25),  0.0, 1.0e-12);
    CHECK_NEAR (Lfo::shapeAt (Lfo::Wave::triangle, 0.50),  1.0, 1.0e-12);

    CHECK_NEAR (Lfo::shapeAt (Lfo::Wave::sawUp, 0.00), -1.0, 1.0e-12);
    CHECK_NEAR (Lfo::shapeAt (Lfo::Wave::sawUp, 0.50),  0.0, 1.0e-12);

    CHECK_NEAR (Lfo::shapeAt (Lfo::Wave::sawDown, 0.00),  1.0, 1.0e-12);
    CHECK_NEAR (Lfo::shapeAt (Lfo::Wave::sawDown, 0.50),  0.0, 1.0e-12);

    CHECK (Lfo::shapeAt (Lfo::Wave::square, 0.25) ==  1.0);
    CHECK (Lfo::shapeAt (Lfo::Wave::square, 0.75) == -1.0);
}

TEZLA_TEST (lfo_every_waveform_stays_inside_plus_minus_one)
{
    // A modulation source that overshoots would push a destination past its own
    // range and clamp, which reads as the LFO flattening off at the top.
    for (int wave = 0; wave < Lfo::kNumWaves; ++wave)
        for (int step = 0; step <= 4000; ++step)
        {
            const double value = Lfo::shapeAt (static_cast<Lfo::Wave> (wave),
                                               static_cast<double> (step) / 1000.0);
            CHECK (value >= -1.0);
            CHECK (value <= 1.0);
        }
}

TEZLA_TEST (lfo_phase_does_not_depend_on_the_host_block_size)
{
    // The same elapsed time has to give the same phase however the host cut it
    // up, or the plugin sounds different at a 64-sample buffer than at 1024 --
    // and the difference only appears on someone else's machine.
    for (const auto wave : { Lfo::Wave::sine, Lfo::Wave::triangle, Lfo::Wave::sawUp })
    {
        auto coarse = made (wave, 3.7);
        auto fine   = made (wave, 3.7);

        double coarseValue = 0.0;
        double fineValue = 0.0;

        for (int block = 0; block < 40; ++block)
        {
            coarseValue = coarse.advance (512);

            for (int chunk = 0; chunk < 16; ++chunk)
                fineValue = fine.advance (32);
        }

        CHECK_NEAR (coarseValue, fineValue, 1.0e-9);
    }
}

TEZLA_TEST (lfo_runs_at_the_rate_it_is_given)
{
    // One second of a 2 Hz LFO is two cycles, so it lands back where it started.
    auto lfo = made (Lfo::Wave::sine, 2.0);

    const double start = lfo.advance (0);
    double value = 0.0;

    for (int i = 0; i < 48; ++i)
        value = lfo.advance (1000);

    CHECK_NEAR (value, start, 1.0e-9);

    // And a quarter cycle is the peak, not the start. Without this the test
    // above passes for an LFO that never moves at all.
    auto quarter = made (Lfo::Wave::sine, 2.0);
    CHECK_NEAR (quarter.advance (6000), 1.0, 1.0e-9);   // 0.125 s at 2 Hz

    // Half a cycle is back through zero, going the other way.
    auto half = made (Lfo::Wave::sine, 2.0);
    CHECK_NEAR (half.advance (12000), 0.0, 1.0e-9);
}

TEZLA_TEST (lfo_sync_puts_the_same_bar_at_the_same_phase)
{
    // The reason sync derives phase from ppq instead of accumulating: bar 33
    // has to be identical to bar 1, so a loop repeats and a bounce matches what
    // was heard. An accumulator drifts and this is the test it fails.
    auto lfo = made (Lfo::Wave::triangle);

    constexpr double cyclesPerBeat = 0.25;   // one cycle per 4/4 bar

    for (const double beatInBar : { 0.0, 0.5, 1.0, 2.5, 3.75 })
    {
        const double first  = lfo.setPhaseFromPpq (beatInBar, cyclesPerBeat, 64);
        const double later  = lfo.setPhaseFromPpq (128.0 + beatInBar, cyclesPerBeat, 64);
        const double later2 = lfo.setPhaseFromPpq (4096.0 + beatInBar, cyclesPerBeat, 64);

        CHECK_NEAR (first, later, 1.0e-9);
        CHECK_NEAR (first, later2, 1.0e-9);
    }
}

TEZLA_TEST (lfo_sync_reaches_the_same_place_however_it_got_there)
{
    // Jumping the transport -- a loop wrapping, or the user dragging the
    // playhead -- must not leave the LFO somewhere else. Assigning the phase
    // rather than accumulating it is what makes this true.
    auto stepped = made (Lfo::Wave::sawUp);
    auto jumped  = made (Lfo::Wave::sawUp);

    double steppedValue = 0.0;
    for (int i = 0; i <= 400; ++i)
        steppedValue = stepped.setPhaseFromPpq (0.01 * i, 0.5, 64);

    const double jumpedValue = jumped.setPhaseFromPpq (4.0, 0.5, 64);

    CHECK_NEAR (steppedValue, jumpedValue, 1.0e-9);
}

TEZLA_TEST (lfo_random_waveforms_repeat_with_the_loop)
{
    // Sample & hold from a running generator gives a different sequence every
    // pass, which is fine on a synth and useless on a bus effect being bounced.
    // Each cycle's value is hashed from which cycle it is, so a four-bar loop
    // plays the same four values every time round.
    for (const auto wave : { Lfo::Wave::sampleHold, Lfo::Wave::smoothRandom })
    {
        auto lfo = made (wave);

        std::vector<double> firstPass, secondPass;

        for (int beat = 0; beat < 16; ++beat)
            firstPass.push_back (lfo.setPhaseFromPpq (static_cast<double> (beat), 1.0, 64));

        // Round the loop again, from a different starting point in time.
        for (int beat = 0; beat < 16; ++beat)
            secondPass.push_back (lfo.setPhaseFromPpq (static_cast<double> (beat), 1.0, 64));

        for (std::size_t i = 0; i < firstPass.size(); ++i)
            CHECK_NEAR (firstPass[i], secondPass[i], 1.0e-12);
    }
}

TEZLA_TEST (lfo_sample_hold_actually_holds_and_actually_changes)
{
    // Two failures a single value could hide: a hold that is really a ramp, and
    // a "random" sequence that is one constant.
    auto lfo = made (Lfo::Wave::sampleHold);

    // Within one cycle the value must not move at all.
    const double atStart   = lfo.setPhaseFromPpq (0.05, 1.0, 64);
    const double atMiddle  = lfo.setPhaseFromPpq (0.50, 1.0, 64);
    const double nearEnd   = lfo.setPhaseFromPpq (0.95, 1.0, 64);

    CHECK (atStart == atMiddle);
    CHECK (atStart == nearEnd);

    // Across cycles it must, and to more than a handful of distinct levels.
    std::set<long long> distinct;
    for (int cycle = 0; cycle < 64; ++cycle)
        distinct.insert (std::llround (
            1.0e6 * lfo.setPhaseFromPpq (static_cast<double> (cycle) + 0.5, 1.0, 64)));

    CHECK (distinct.size() > 50);
}

TEZLA_TEST (lfo_smooth_rounds_the_square_and_leaves_the_sine)
{
    // Smooth exists to take the corner off a square without reaching for the
    // parameter smoothers. It has to do that, and it has to leave a waveform
    // that has no corners roughly where it was.
    const auto travel = [] (Lfo::Wave wave, double smooth)
    {
        Lfo lfo;
        lfo.prepare (kRate);
        lfo.setWave (wave);
        lfo.setRateHz (2.0);
        lfo.setSmooth (smooth);

        double previous = lfo.advance (0);
        double worstStep = 0.0;

        for (int i = 0; i < 2000; ++i)
        {
            const double value = lfo.advance (32);
            worstStep = std::max (worstStep, std::abs (value - previous));
            previous = value;
        }

        return worstStep;
    };

    CHECK (travel (Lfo::Wave::square, 0.5) < travel (Lfo::Wave::square, 0.0) * 0.5);

    // A sine at 2 Hz sampled every 32 samples moves very little per step
    // already, so smoothing should barely register on it.
    CHECK (travel (Lfo::Wave::sine, 0.5) < travel (Lfo::Wave::sine, 0.0) * 1.05);
}

TEZLA_TEST (lfo_smoothing_does_not_depend_on_the_host_block_size)
{
    // The one-pole is advanced in closed form for the whole chunk rather than
    // stepped once per call, so the same elapsed time gives the same rounding
    // whatever the host's buffer is. Stepping once per call would make a
    // 64-sample buffer smooth eight times faster than a 512-sample one.
    const auto run = [] (int chunk, int chunks)
    {
        Lfo lfo;
        lfo.prepare (kRate);
        lfo.setWave (Lfo::Wave::square);
        lfo.setRateHz (2.0);
        lfo.setSmooth (0.6);

        double value = 0.0;
        for (int i = 0; i < chunks; ++i)
            value = lfo.advance (chunk);

        return value;
    };

    // Both cover 16384 samples. The tolerance is tight on purpose: the
    // smoother steps at fixed absolute sample positions, so this is not an
    // approximation that happens to be close, it is the same arithmetic in a
    // different order.
    CHECK_NEAR (run (64, 256), run (512, 32), 1.0e-9);
}

TEZLA_TEST (lfo_phase_offset_rotates_without_moving_the_clock)
{
    auto plain   = made (Lfo::Wave::sawUp, 1.0);
    auto shifted = made (Lfo::Wave::sawUp, 1.0);
    shifted.setPhaseOffset (0.25);

    const double a = plain.advance (12000);     // a quarter of a cycle in
    const double b = shifted.advance (0);       // a quarter of a cycle by offset

    CHECK_NEAR (a, b, 1.0e-9);
}

TEZLA_TEST (lfo_at_zero_rate_stands_still)
{
    // A rate control that reaches zero has to mean "stopped", not "very slow" --
    // it is the only way to park an LFO at a phase and use it as an offset.
    auto lfo = made (Lfo::Wave::sine, 0.0);
    lfo.setPhaseOffset (0.25);

    const double first = lfo.advance (64);

    for (int i = 0; i < 100; ++i)
        CHECK (lfo.advance (512) == first);
}
