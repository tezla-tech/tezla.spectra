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

#include <tezla/dsp/ToneStack.hpp>

using namespace tezla::dsp;

namespace
{
constexpr double kRate = 192000.0;

double responseDb (ToneStack& stack, double bass, double middle, double treble, double frequency)
{
    stack.setControls (bass, middle, treble);
    stack.reset();

    const int settle = static_cast<int> (kRate * 0.3);
    const int measure = static_cast<int> (std::round (60.0 * kRate / frequency));

    double phase = 0.0;
    const double step = 2.0 * std::numbers::pi * frequency / kRate;

    for (int i = 0; i < settle; ++i)
    {
        (void) stack.process (std::sin (phase));
        phase += step;
    }

    double inPhase = 0.0;
    double quadrature = 0.0;

    for (int i = 0; i < measure; ++i)
    {
        const double y = stack.process (std::sin (phase));
        inPhase += y * std::sin (phase);
        quadrature += y * std::cos (phase);
        phase += step;
    }

    return 20.0 * std::log10 (std::max (2.0 * std::hypot (inPhase, quadrature) / measure, 1.0e-9));
}

ToneStack made (ToneStackVoicing voicing = ToneStackVoicing::american)
{
    ToneStack stack;
    stack.setVoicing (voicing);
    stack.prepare (kRate);
    return stack;
}
} // namespace

TEZLA_TEST (tone_stack_dips_the_midrange_with_everything_at_noon)
{
    // The signature of the circuit, and the thing nobody dials in: with all
    // three controls centred there is an eleven-decibel hole around 640 Hz.
    // A bank of three flat-at-noon filters has no way to produce this, which is
    // why amp simulations built that way never sound quite right.
    //
    // Measured: -2.8 at 40 Hz, -14.1 at 640, -5.1 at 10 kHz.
    auto stack = made();

    const double low = responseDb (stack, 0.5, 0.5, 0.5, 40.0);
    const double dip = responseDb (stack, 0.5, 0.5, 0.5, 640.0);
    const double high = responseDb (stack, 0.5, 0.5, 0.5, 10240.0);

    CHECK (dip < low - 8.0);
    CHECK (dip < high - 8.0);
    CHECK_NEAR (dip, -14.1, 1.5);
}

TEZLA_TEST (tone_stack_mid_control_scoops)
{
    // Turning the middle down deepens the hole rather than merely trimming a
    // band. Measured: -25.0 dB at 640 Hz with the mid at zero, against -10.5
    // with it at maximum.
    auto stack = made();

    const double scooped = responseDb (stack, 0.7, 0.0, 0.7, 640.0);
    const double filled = responseDb (stack, 0.5, 1.0, 0.5, 640.0);

    CHECK (scooped < -20.0);
    CHECK (filled > -12.0);
    CHECK (filled - scooped > 12.0);
}

TEZLA_TEST (tone_stack_controls_have_the_range_they_should)
{
    auto stack = made();

    // Treble: 13 dB at 10 kHz.
    const double trebleRange = responseDb (stack, 0.5, 0.5, 1.0, 10240.0)
                             - responseDb (stack, 0.5, 0.5, 0.0, 10240.0);
    CHECK (trebleRange > 10.0);

    // Bass: 8.5 dB at 80 Hz.
    const double bassRange = responseDb (stack, 1.0, 0.5, 0.5, 80.0)
                           - responseDb (stack, 0.0, 0.5, 0.5, 80.0);
    CHECK (bassRange > 6.0);

    // Mid: 14 dB at 640 Hz.
    const double midRange = responseDb (stack, 0.5, 1.0, 0.5, 640.0)
                          - responseDb (stack, 0.5, 0.0, 0.5, 640.0);
    CHECK (midRange > 10.0);
}

TEZLA_TEST (tone_stack_controls_interact_the_way_a_passive_network_does)
{
    // The property that separates a tone stack from an equaliser, and the
    // reason this is a solved circuit rather than three filters.
    //
    // The treble control is supposed to affect treble. It also moves the
    // midrange, because turning it re-taps a divider the other two controls are
    // sitting in: 640 Hz reads -12.3 dB with treble at zero and -14.2 with it
    // at maximum.
    auto stack = made();

    const double midWithTrebleDown = responseDb (stack, 0.5, 0.5, 0.0, 640.0);
    const double midWithTrebleUp = responseDb (stack, 0.5, 0.5, 1.0, 640.0);

    CHECK (std::abs (midWithTrebleUp - midWithTrebleDown) > 1.0);

    // And the bass control reaches well past the bass: moving it changes 320 Hz
    // by several decibels.
    const double lowMidBassDown = responseDb (stack, 0.0, 0.5, 0.5, 320.0);
    const double lowMidBassUp = responseDb (stack, 1.0, 0.5, 0.5, 320.0);

    CHECK (std::abs (lowMidBassUp - lowMidBassDown) > 3.0);
}

TEZLA_TEST (tone_stack_costs_level_even_at_its_most_generous)
{
    // A passive network cannot make gain. Everything it does is subtraction,
    // and the insertion loss is real and has to be made up somewhere -- which
    // is why the gain staging of an amplifier changes when the tone controls
    // move.
    auto stack = made();

    double loudest = -1000.0;

    for (const double b : { 0.0, 0.5, 1.0 })
        for (const double m : { 0.0, 0.5, 1.0 })
            for (const double t : { 0.0, 0.5, 1.0 })
                for (const double f : { 40.0, 320.0, 2560.0, 10240.0 })
                    loudest = std::max (loudest, responseDb (stack, b, m, t, f));

    // Never above unity, anywhere, at any setting.
    CHECK (loudest < 0.05);

    // And at noon the midband really is down about eleven decibels.
    CHECK (responseDb (stack, 0.5, 0.5, 0.5, 640.0) < -10.0);
}

TEZLA_TEST (tone_stack_voicings_differ_in_character_not_merely_in_level)
{
    // Same topology, different values -- which is all the difference between
    // one manufacturer's stack and another's ever was. But the difference that
    // matters is the *shape*, not the level: a smaller slope resistor makes the
    // whole thing louder without changing what it does, so comparing absolute
    // decibels at one frequency measures the wrong thing.
    //
    // Response at noon, measured:
    //
    //                80Hz    320    640   2560   5120  10240
    //   american    -3.84 -10.92 -14.07  -7.96  -5.88  -5.15
    //   british     -2.54  -7.68  -9.98  -5.55  -4.59  -4.30
    //   modern      -4.19 -13.69 -13.72  -6.54  -5.68  -5.45
    //
    // So: how deep is the hole, relative to the ends of the band?
    const auto scoopDepth = [] (ToneStackVoicing voicing)
    {
        auto stack = made (voicing);
        const double low = responseDb (stack, 0.5, 0.5, 0.5, 80.0);
        const double dip = responseDb (stack, 0.5, 0.5, 0.5, 640.0);
        const double high = responseDb (stack, 0.5, 0.5, 0.5, 10240.0);

        return 0.5 * (low + high) - dip;
    };

    // Measured 9.57, 6.56 and 8.90.
    const double american = scoopDepth (ToneStackVoicing::american);
    const double british = scoopDepth (ToneStackVoicing::british);
    const double modern = scoopDepth (ToneStackVoicing::modern);

    CHECK (american > 8.0);
    CHECK (modern > 8.0);

    // The British stack is the one with its midrange left in.
    CHECK (british < 7.5);
    CHECK (british < american - 2.0);
    CHECK (british < modern - 1.5);
}

TEZLA_TEST (tone_stack_modern_voicing_holds_a_tighter_low_end)
{
    // Its bigger bass capacitor shunts more away before it reaches the
    // distortion, which is the point of a tight stack: 320 Hz sits 9.5 dB below
    // 80 Hz on the modern set, against 7.1 on the american one.
    const auto tilt = [] (ToneStackVoicing voicing)
    {
        auto stack = made (voicing);
        return responseDb (stack, 0.5, 0.5, 0.5, 80.0) - responseDb (stack, 0.5, 0.5, 0.5, 320.0);
    };

    CHECK (tilt (ToneStackVoicing::modern) > tilt (ToneStackVoicing::american));
    CHECK (tilt (ToneStackVoicing::modern) > tilt (ToneStackVoicing::british));
}

TEZLA_TEST (tone_stack_is_the_same_circuit_at_every_sample_rate)
{
    // CLAUDE.md section 6, on the part of the spectrum a tone stack actually
    // works in. Everything the controls do is below 10 kHz, and at 44.1 kHz
    // that is still under Fs/4 -- the region where a trapezoidal companion
    // model tracks its prototype.
    double worst = 0.0;

    for (const double frequency : { 80.0, 320.0, 640.0, 2560.0 })
    {
        double reference = 0.0;

        for (const double rate : { 44100.0, 48000.0, 96000.0, 192000.0 })
        {
            ToneStack stack;
            stack.setVoicing (ToneStackVoicing::american);
            stack.prepare (rate);
            stack.setControls (0.5, 0.5, 0.5);
            stack.reset();

            const int settle = static_cast<int> (rate * 0.3);
            const int measure = static_cast<int> (std::round (60.0 * rate / frequency));

            double phase = 0.0;
            const double step = 2.0 * std::numbers::pi * frequency / rate;

            for (int i = 0; i < settle; ++i)
            {
                (void) stack.process (std::sin (phase));
                phase += step;
            }

            double inPhase = 0.0;
            double quadrature = 0.0;

            for (int i = 0; i < measure; ++i)
            {
                const double y = stack.process (std::sin (phase));
                inPhase += y * std::sin (phase);
                quadrature += y * std::cos (phase);
                phase += step;
            }

            const double db = 20.0 * std::log10 (std::max (2.0 * std::hypot (inPhase, quadrature) / measure, 1.0e-9));

            if (rate == 44100.0)
                reference = db;
            else
                worst = std::max (worst, std::abs (db - reference));
        }
    }

    CHECK (worst < 0.3);
}

TEZLA_TEST (tone_stack_is_silent_in_silence_and_survives_every_extreme)
{
    auto stack = made();

    stack.setControls (0.0, 0.0, 0.0);

    for (int i = 0; i < 2048; ++i)
        CHECK (stack.process (0.0) == 0.0);

    // Every corner of the control space, with a component set nobody built and
    // which the clamp brings back inside what a tone stack can be.
    ToneStackComponents wild;
    wild.trebleCap = 2.0e-9;
    wild.bassCap = 4.7e-6;
    wild.midCap = 1.0e-9;
    wild.midPot = 500.0e3;
    stack.setComponents (wild);

    CHECK (stack.getComponents().bassCap <= 470.0e-9);

    for (const double b : { 0.0, 1.0 })
        for (const double m : { 0.0, 1.0 })
            for (const double t : { 0.0, 1.0 })
            {
                stack.setControls (b, m, t);
                stack.reset();

                for (int i = 0; i < 4096; ++i)
                {
                    const double y = stack.process (std::sin (0.05 * i));
                    CHECK (std::isfinite (y));

                    // Not bounded by unity -- see the test below.
                    CHECK (std::abs (y) < 1.5);
                }
            }
}

TEZLA_TEST (tone_stack_can_overshoot_when_a_tone_starts_and_that_is_the_circuit)
{
    // Worth its own test, because the obvious assumption is wrong and it cost
    // an hour to find out.
    //
    // A passive network cannot have gain, so it is tempting to assert that its
    // output never exceeds its input. That is true of the *steady state* and
    // false of the transient: switch a tone on and a highpass leg answers with
    // a decaying term that adds to the sine, and the sum can exceed the input's
    // amplitude for a cycle or two.
    //
    // It looked exactly like trapezoidal ringing -- a pole beside Nyquist from
    // a time constant shorter than the sample period. It is not. The same
    // circuit and the same tone, from 48 kHz to 3 MHz:
    //
    //       48 kHz   1.038735          384 kHz   1.039498
    //       96 kHz   1.038790          768 kHz   1.039551
    //      192 kHz   1.039496         3072 kHz   1.039552
    //
    // Converged to five figures and independent of the sample rate, which is
    // what "in the circuit" looks like.
    ToneStackComponents wild;
    wild.trebleCap = 2.0e-9;
    wild.bassCap = 4.7e-6;
    wild.midCap = 1.0e-9;
    wild.midPot = 500.0e3;

    const auto overshootAt = [&wild] (double rate)
    {
        ToneStack stack;
        stack.setVoicing (ToneStackVoicing::american);
        stack.prepare (rate);
        stack.setComponents (wild);
        stack.setControls (0.0, 0.0, 1.0);
        stack.reset();

        const double frequency = 0.05 * 192000.0 / (2.0 * std::numbers::pi);
        double peak = 0.0;

        for (int i = 0; i < static_cast<int> (rate * 0.05); ++i)
            peak = std::max (peak, std::abs (stack.process (
                std::sin (2.0 * std::numbers::pi * frequency * i / rate))));

        return peak;
    };

    const double low = overshootAt (48000.0);
    const double high = overshootAt (768000.0);

    // Real, and the same at sixteen times the rate.
    CHECK (low > 1.02);
    CHECK (std::abs (high - low) < 0.005);

    // And it is only the transient: the steady state obeys the passive bound,
    // which is what the insertion-loss test measures everywhere else.
    ToneStack stack;
    stack.setVoicing (ToneStackVoicing::american);
    stack.prepare (192000.0);
    stack.setComponents (wild);

    CHECK (responseDb (stack, 0.0, 0.0, 1.0, 1528.0) < 0.05);
}
