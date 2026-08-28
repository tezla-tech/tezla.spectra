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

#include <tezla/dsp/PowerAmp.hpp>
#include <tezla/measure/Harmonics.hpp>
#include <tezla/measure/Signals.hpp>

using namespace tezla::dsp;
namespace measure = tezla::measure;

namespace
{
constexpr double kRate = 192000.0;

PowerAmp made (const PowerAmpParameters& parameters = {})
{
    PowerAmp amp;
    amp.prepare (kRate);
    amp.setParameters (parameters);
    amp.reset();
    return amp;
}

/// Amplitude of the fundamental after the amp has settled.
double toneLevel (PowerAmp& amp, double amplitude, double frequency, double seconds = 0.5)
{
    const int total = static_cast<int> (kRate * seconds);
    const int from = total / 2;

    double inPhase = 0.0;
    double quadrature = 0.0;
    int counted = 0;

    for (int i = 0; i < total; ++i)
    {
        const double angle = 2.0 * std::numbers::pi * frequency * i / kRate;
        const double y = amp.process (amplitude * std::sin (angle));

        if (i >= from)
        {
            inPhase += y * std::sin (angle);
            quadrature += y * std::cos (angle);
            ++counted;
        }
    }

    return 2.0 * std::hypot (inPhase, quadrature) / counted;
}

/// Total harmonic distortion of a sustained tone.
double thdOf (PowerAmp& amp, double amplitude, double frequency)
{
    constexpr std::size_t fftSize = 1 << 15;

    const double binExact = measure::binExactFrequency (frequency, kRate, fftSize);

    std::vector<double> y (2 * fftSize);

    for (std::size_t i = 0; i < y.size(); ++i)
        y[i] = amp.process (amplitude * std::sin (2.0 * std::numbers::pi * binExact
                                                  * static_cast<double> (i) / kRate));

    // The second half only: the first carries the settling of the sag and the
    // flux, and a DFT treats its block as circular.
    const std::vector<double> settled (y.begin() + static_cast<long> (fftSize), y.end());

    return measure::analyseHarmonics (settled, kRate, binExact).thdDb;
}
} // namespace

// ---------------------------------------------------------------------------
// Crossover
// ---------------------------------------------------------------------------

TEZLA_TEST (crossover_dips_the_gain_at_the_handover_by_exactly_its_depth)
{
    // f'(0) = 1 - depth, by construction. Class A is depth zero and must be a
    // straight wire.
    for (const double depth : { 0.0, 0.2, 0.5, 0.9 })
    {
        const Crossover crossover { depth, 0.05 };
        constexpr double h = 1.0e-7;
        const double slope = (crossover.evaluate (h) - crossover.evaluate (-h)) / (2.0 * h);

        CHECK_NEAR (slope, 1.0 - depth, 1.0e-5);
    }

    const Crossover classA { 0.0, 0.05 };

    for (const double x : { -2.0, -0.1, 0.0, 0.1, 2.0 })
        CHECK (classA.evaluate (x) == x);
}

TEZLA_TEST (crossover_becomes_an_offset_once_one_side_has_taken_over)
{
    // Well away from the handover the tanh has saturated, so what is left is
    // the signal minus a constant -- which is what crossover distortion looks
    // like on a scope.
    const Crossover crossover { 0.4, 0.05 };
    const double offset = 0.4 * 0.05;

    CHECK_NEAR (crossover.evaluate (1.0), 1.0 - offset, 1.0e-9);
    CHECK_NEAR (crossover.evaluate (-1.0), -1.0 + offset, 1.0e-9);

    // And it is odd, so a push-pull pair makes no even harmonics from it.
    for (const double x : { 0.02, 0.2, 1.5 })
        CHECK_NEAR (crossover.evaluate (-x), -crossover.evaluate (x), 1.0e-12);
}

TEZLA_TEST (crossover_antiderivative_is_the_integral)
{
    const Crossover crossover { 0.35, 0.04 };

    constexpr double from = -1.5;
    constexpr double to = 1.5;
    constexpr int steps = 400000;
    constexpr double h = (to - from) / steps;

    double integral = 0.5 * (crossover.evaluate (from) + crossover.evaluate (to));

    for (int i = 1; i < steps; ++i)
        integral += crossover.evaluate (from + i * h);

    integral *= h;

    CHECK_NEAR (integral, crossover.antiderivative (to) - crossover.antiderivative (from), 1.0e-6);
}

// ---------------------------------------------------------------------------
// The transformer, which is the reason this file exists
// ---------------------------------------------------------------------------

namespace
{
/// The core on its own: valves exactly linear, no sag, no handover, no loop.
///
/// `knee` 0.7 puts the clipper's threshold at 0.3, and SoftClipExcess is
/// *exactly* zero below it -- so at an amplitude of 0.25 the valve stage is
/// bit-for-bit the identity and anything measured here came from the core.
PowerAmpParameters coreOnly()
{
    PowerAmpParameters parameters;
    parameters.feedback = 0.0;
    parameters.sagDepth = 0.0;
    parameters.crossoverDepth = 0.0;
    parameters.ceiling = 1.0;
    parameters.knee = 0.7;
    parameters.coreFrequencyHz = 320.0;
    return parameters;
}

constexpr double kCoreAmplitude = 0.25;

/// Peak flux once the integrator has settled, in units of the core's capacity.
double fluxOf (PowerAmp& amp, double amplitude, double frequency)
{
    const int total = static_cast<int> (kRate * 0.5);
    double peak = 0.0;

    for (int i = 0; i < total; ++i)
    {
        (void) amp.process (amplitude * std::sin (2.0 * std::numbers::pi * frequency * i / kRate));

        if (i > total / 2)
            peak = std::max (peak, std::abs (amp.getFlux()));
    }

    return peak;
}
} // namespace

TEZLA_TEST (power_amp_core_flux_halves_with_every_octave_up)
{
    // The finding this whole stage is built around, stated as the law it is.
    //
    // Flux is the integral of the voltage, so for a sine of amplitude A at
    // frequency f the peak flux is A/(2*pi*f) -- inversely proportional to
    // pitch, and nothing in the code tests the frequency to get that.
    //
    // Measured well clear of the primary's own 32 Hz corner, where the flux is
    // a pure integral, the ratio is the octave itself to three figures.
    auto parameters = coreOnly();
    parameters.coreSaturation = 0.0;    // the law, before the core bends it

    const auto flux = [&parameters] (double frequency)
    {
        auto amp = made (parameters);
        return fluxOf (amp, kCoreAmplitude, frequency);
    };

    const double at320 = flux (320.0);
    const double at640 = flux (640.0);
    const double at1280 = flux (1280.0);

    CHECK_NEAR (at320 / at640, 2.0, 0.02);
    CHECK_NEAR (at640 / at1280, 2.0, 0.02);

    // Lower down the primary's highpass has taken a little off the voltage
    // before it reaches the winding, so the octave reads 1.87 rather than 2.
    // That is the filter, not the integrator.
    CHECK (flux (80.0) / flux (160.0) > 1.8);
    CHECK (flux (80.0) / flux (160.0) < 2.0);
}

TEZLA_TEST (power_amp_saturates_the_core_on_flux_so_low_notes_distort_first)
{
    // And the distortion follows the flux: identical voltage, dirty low and
    // clean high. Measured on this rig at 192 kHz, valves bit-exact:
    //
    //     40 Hz   flux 1.269   thd  -20.1 dB
    //     80 Hz   flux 0.902   thd  -32.0 dB
    //    160 Hz   flux 0.487   thd  -47.8 dB
    //    320 Hz   flux 0.248   thd  -66.0 dB
    //    640 Hz   flux 0.125   thd  -83.8 dB
    //   1280 Hz   flux 0.062   thd -102.2 dB
    //
    // The flux falls 6 dB per octave, as an integral must; the distortion it
    // produces falls about **18 dB per octave**, because the permeability term
    // goes as the square of the flux and the corner it moves is itself further
    // from the note each time.
    auto parameters = coreOnly();

    const auto thd = [&parameters] (double frequency)
    {
        auto amp = made (parameters);
        return thdOf (amp, kCoreAmplitude, frequency);
    };

    const double at80 = thd (80.0);
    const double at160 = thd (160.0);
    const double at320 = thd (320.0);
    const double at640 = thd (640.0);
    const double at1280 = thd (1280.0);

    // Every octave up buys at least 15 dB, and the low note is filthy against
    // a top note that is beyond clean.
    CHECK (at80 > at160 + 15.0);
    CHECK (at160 > at320 + 15.0);
    CHECK (at320 > at640 + 15.0);
    CHECK (at640 > at1280 + 15.0);

    CHECK (at80 > -40.0);
    CHECK (at1280 < -95.0);
}

TEZLA_TEST (power_amp_a_filling_core_sheds_its_own_flux)
{
    // The model is self-limiting rather than merely clamped, and that follows
    // from integrating the voltage across the primary instead of the source's.
    //
    // As the core fills, its corner rises; the low end that was filling it is
    // what the rising corner removes; so the flux stops climbing on its own.
    // At 40 Hz the effect is worth 12%: 1.269 against the 1.449 the same
    // voltage would have put in a linear core.
    auto saturating = coreOnly();
    auto linear = coreOnly();
    linear.coreSaturation = 0.0;

    auto a = made (saturating);
    auto b = made (linear);

    const double withCore = fluxOf (a, kCoreAmplitude, 40.0);
    const double withLinear = fluxOf (b, kCoreAmplitude, 40.0);

    CHECK (withLinear > 1.0);              // the core is genuinely being filled
    CHECK (withCore < withLinear * 0.95);  // and it pushes back
}

TEZLA_TEST (power_amp_core_saturation_can_be_switched_off_and_then_it_is_linear)
{
    // The literature's transformer is linear -- Cohen and Helie's is explicitly
    // "a simple linear model". Setting coreSaturation to zero must give exactly
    // that, so the addition can be measured against its own absence.
    //
    // Not "quieter than the saturating one": *linear*, down at the arithmetic's
    // own floor, at a flux that would otherwise produce 20 dB of THD.
    auto parameters = coreOnly();
    parameters.coreSaturation = 0.0;

    for (const double frequency : { 40.0, 80.0, 320.0, 1280.0 })
    {
        auto amp = made (parameters);
        CHECK (thdOf (amp, kCoreAmplitude, frequency) < -200.0);
    }
}

TEZLA_TEST (power_amp_transformer_rolls_off_at_both_ends)
{
    PowerAmpParameters parameters;
    parameters.feedback = 0.0;
    parameters.sagDepth = 0.0;
    parameters.crossoverDepth = 0.0;
    parameters.ceiling = 100.0;
    parameters.coreSaturation = 0.0;
    parameters.transformerLowHz = 32.0;
    parameters.transformerHighHz = 9000.0;

    auto amp = made (parameters);

    const double atCorner = toneLevel (amp, 0.05, 32.0);
    const double atMid = toneLevel (amp, 0.05, 800.0);
    const double atTop = toneLevel (amp, 0.05, 9000.0);

    // First-order corners: -3 dB at each, measured against the passband.
    CHECK_NEAR (20.0 * std::log10 (atCorner / atMid), -3.01, 0.7);
    CHECK_NEAR (20.0 * std::log10 (atTop / atMid), -3.01, 0.7);
}

// ---------------------------------------------------------------------------
// Sag and feedback
// ---------------------------------------------------------------------------

TEZLA_TEST (power_amp_rail_sags_under_load_and_climbs_back)
{
    PowerAmpParameters parameters;
    parameters.sagDepth = 0.35;
    parameters.sagMs = 45.0;
    auto amp = made (parameters);

    CHECK (amp.getSag() == 0.0);

    for (int i = 0; i < static_cast<int> (kRate * 0.4); ++i)
        (void) amp.process (2.0 * std::sin (2.0 * std::numbers::pi * 110.0 * i / kRate));

    const double loaded = amp.getSag();
    CHECK (loaded > 0.4);

    for (int i = 0; i < static_cast<int> (kRate * 0.5); ++i)
        (void) amp.process (0.0);

    CHECK (amp.getSag() < 0.02);
}

TEZLA_TEST (power_amp_sag_costs_headroom_so_a_loud_passage_squashes_the_next)
{
    // What sag is *for*. A quiet note after a loud one has less rail to swing
    // into, so it comes out smaller than the same note played cold.
    PowerAmpParameters parameters;
    parameters.sagDepth = 0.5;
    parameters.sagMs = 60.0;
    parameters.feedback = 0.0;
    parameters.ceiling = 0.5;

    auto cold = made (parameters);
    const double fresh = toneLevel (cold, 0.45, 220.0, 0.3);

    auto tired = made (parameters);

    for (int i = 0; i < static_cast<int> (kRate * 0.3); ++i)
        (void) tired.process (4.0 * std::sin (2.0 * std::numbers::pi * 90.0 * i / kRate));

    const double after = toneLevel (tired, 0.45, 220.0, 0.05);

    CHECK (after < fresh * 0.95);
}

TEZLA_TEST (power_amp_feedback_cleans_up_and_then_lets_go)
{
    // The property that needs no code of its own. A loop trades gain for
    // linearity, and the trade is only available while there is gain to trade:
    // as the valves clip their incremental gain collapses and the loop's
    // authority goes with it.
    //
    // So feedback must help a lot when the stage is loafing, and much less when
    // it is being hammered.
    PowerAmpParameters open;
    open.feedback = 0.0;
    open.sagDepth = 0.0;
    open.coreSaturation = 0.0;
    open.crossoverDepth = 0.3;

    PowerAmpParameters closed = open;
    closed.feedback = 0.7;

    auto quietOpen = made (open);
    auto quietClosed = made (closed);
    const double quietGain = thdOf (quietOpen, 0.08, 400.0) - thdOf (quietClosed, 0.08, 400.0);

    auto loudOpen = made (open);
    auto loudClosed = made (closed);
    const double loudGain = thdOf (loudOpen, 6.0, 400.0) - thdOf (loudClosed, 6.0, 400.0);

    // Cleans up meaningfully when there is headroom.
    CHECK (quietGain > 4.0);

    // And much less when there is not.
    CHECK (loudGain < quietGain - 3.0);
}

// ---------------------------------------------------------------------------
// The rules everything here has to obey
// ---------------------------------------------------------------------------

TEZLA_TEST (power_amp_is_silent_in_silence)
{
    auto amp = made();

    for (int i = 0; i < 8192; ++i)
        CHECK (amp.process (0.0) == 0.0);
}

TEZLA_TEST (power_amp_never_runs_away)
{
    // Two feedback paths -- the loop and the flux integrator -- swept rather
    // than sampled. CLAUDE.md section 7.
    double worst = 0.0;

    for (const double feedback : { 0.0, 0.5, 0.95 })
        for (const double sag : { 0.0, 0.5, 1.0 })
            for (const double core : { 0.0, 1.0, 4.0 })
                for (const double drive : { 1.0, 50.0, 1000.0 })
                {
                    PowerAmpParameters parameters;
                    parameters.feedback = feedback;
                    parameters.sagDepth = sag;
                    parameters.coreSaturation = core;
                    parameters.coreFrequencyHz = 400.0;
                    parameters.drive = drive;
                    parameters.sagMs = 1.0;

                    auto amp = made (parameters);

                    for (int i = 0; i < 6000; ++i)
                    {
                        const double y = amp.process (std::sin (2.0 * std::numbers::pi * 60.0 * i / kRate));
                        worst = std::max (worst, std::abs (y));
                        CHECK (std::isfinite (y));
                    }

                    CHECK (std::abs (amp.getFlux()) <= PowerAmp::kFluxLimit + 1.0e-9);
                }

    // The valves bound the swing whatever the loops do.
    CHECK (worst < 10.0);
}

// ---------------------------------------------------------------------------
// Presence and resonance, which are feedback controls rather than tone controls
// ---------------------------------------------------------------------------

TEZLA_TEST (the_loop_gain_is_clamped_below_one_however_it_is_asked_for)
{
    // The loop is `y[n] = A*x[n] - b*y[n-1]`, whose pole sits at -b. Above 1 it
    // is unstable and the only thing in the way is the saturator, which
    // CLAUDE.md section 7 says is not a bound.
    //
    // Measured before the clamp existed, driving a 110 Hz sine at 0.3: neighbour
    // samples began disagreeing with their own trend at a loop gain of 2.4, and
    // at 3.0 the output reached 1e82 inside 8000 samples. So this sweeps past
    // the bound and asks for the oscillation by name -- a Nyquist component
    // makes each sample sit off the line between its neighbours, which nothing
    // in a 110 Hz sine through a saturator otherwise does.
    for (const double asked : { 0.0, 0.5, 0.9, 1.5, 4.0, 40.0 })
    {
        PowerAmpParameters parameters;
        parameters.feedback = asked;
        parameters.drive = 4.0;
        parameters.knee = 0.7;

        auto amp = made (parameters);

        std::vector<double> y;
        y.reserve (8000);

        for (int i = 0; i < 8000; ++i)
            y.push_back (amp.process (0.3 * std::sin (2.0 * std::numbers::pi * 110.0 * i / kRate)));

        int alternating = 0;

        for (std::size_t i = 4001; i + 1 < y.size(); ++i)
        {
            CHECK (std::isfinite (y[i]));

            const double curl = y[i] - 0.5 * (y[i - 1] + y[i + 1]);

            if (std::abs (curl) > 0.02 * std::max (std::abs (y[i]), 1.0e-9))
                ++alternating;
        }

        // A clean 110 Hz sine through a saturator has essentially none of this.
        CHECK (alternating < 40);

        // And the swing stays where the valves put it, rather than where the
        // loop would like to.
        for (const double s : y)
            CHECK (std::abs (s) < 4.0);
    }
}

TEZLA_TEST (presence_authority_is_the_negative_feedback_and_nothing_more)
{
    // The claim the Anvil voicings are tuned against, and the reason a user
    // could not hear either control: a shunt gives back exactly what the loop
    // was taking away, so `20*log10(1 + loopGain)` is the ceiling on both.
    //
    // Small signal on purpose. A limiting output stage is pinned whatever the
    // loop does, and measuring there reads zero for a control that is working.
    auto liftAt = [] (double feedback, double hz, bool presence)
    {
        auto rms = [&] (double amount)
        {
            PowerAmpParameters parameters;
            parameters.feedback = feedback;
            parameters.drive = 1.0;
            parameters.presence = presence ? amount : 0.0;
            parameters.resonance = presence ? 0.0 : amount;
            parameters.presenceHz = 700.0;
            parameters.resonanceHz = 180.0;

            auto amp = made (parameters);

            double sum = 0.0;
            constexpr int kSettle = 4000;
            constexpr int kWindow = 8000;

            for (int i = 0; i < kSettle + kWindow; ++i)
            {
                const double y = amp.process (0.02 * std::sin (2.0 * std::numbers::pi * hz * i / kRate));

                if (i >= kSettle)
                    sum += y * y;
            }

            return std::sqrt (sum / kWindow);
        };

        return 20.0 * std::log10 (std::max (rms (1.0), 1.0e-30)
                                    / std::max (rms (0.0), 1.0e-30));
    };

    for (const double feedback : { 0.15, 0.35, 0.70, 0.85 })
    {
        const double ceiling = 20.0 * std::log10 (1.0 + feedback);

        const double presenceLift = liftAt (feedback, 8000.0, true);
        const double resonanceLift = liftAt (feedback, 30.0, false);

        // Close to the ceiling, and never past it -- past it would mean the
        // shunt was adding treble rather than removing correction, which is the
        // thing the comment in shapedFeedback insists it is not.
        CHECK (presenceLift <= ceiling + 0.15);
        CHECK (presenceLift > ceiling - 1.2);

        CHECK (resonanceLift <= ceiling + 0.15);
        CHECK (resonanceLift > ceiling - 1.6);
    }
}

TEZLA_TEST (power_amp_presence_removes_correction_rather_than_adding_treble)
{
    // The control on the back of an amplifier is a capacitor from the feedback
    // tap to ground. It boosts nothing. It removes the loop's correction up
    // top, so the output stage's own gain -- and its own distortion -- show
    // through. That is why it sounds nothing like a treble control.
    //
    // Two things must therefore both be true: the top gets louder, *and* the
    // top gets dirtier. A treble control does only the first.
    //
    // Measured at 90% into a loop of 0.6 at 3x drive:
    //
    //                100 Hz     5 kHz
    //     level      +0.05     +2.99 dB
    //     THD           --    +16.59 dB   (-49.5 -> -32.9)
    //
    // Three decibels of level and sixteen and a half of distortion. The
    // assertions below are floors well under those, so they catch the
    // mechanism being removed without pinning the exact figure.
    PowerAmpParameters parameters;
    parameters.feedback = 0.6;         // a loop worth removing
    parameters.crossoverDepth = 0.0;
    parameters.coreSaturation = 0.0;   // the transformer out of the way
    parameters.sagDepth = 0.0;
    parameters.drive = 3.0;            // into the valves, so there is gain to lose

    auto flat = made (parameters);

    auto lifted = parameters;
    lifted.presence = 0.9;
    auto present = made (lifted);

    const double flatLow = toneLevel (flat, 0.3, 100.0);
    const double presentLow = toneLevel (present, 0.3, 100.0);

    auto flat2 = made (parameters);
    auto present2 = made (lifted);

    const double flatHigh = toneLevel (flat2, 0.3, 5000.0);
    const double presentHigh = toneLevel (present2, 0.3, 5000.0);

    // Louder up top, and untouched down low: this is a control that lives above
    // its corner and nowhere else.
    CHECK (presentHigh > flatHigh * 1.1);
    CHECK_NEAR (presentLow / flatLow, 1.0, 0.02);

    // And dirtier up top, which is the half a treble control cannot do.
    auto flat3 = made (parameters);
    auto present3 = made (lifted);

    CHECK (thdOf (present3, 0.3, 5000.0) > thdOf (flat3, 0.3, 5000.0) + 2.0);
}

TEZLA_TEST (power_amp_resonance_is_the_same_trick_at_the_other_end)
{
    // Removing the low frequencies from the feedback lets the output stage and
    // the transformer do as they like down there -- which, with a core that
    // saturates on flux, is a great deal. This is the control that makes a low
    // note bloom.
    PowerAmpParameters parameters;
    parameters.feedback = 0.6;
    parameters.crossoverDepth = 0.0;
    parameters.sagDepth = 0.0;
    parameters.drive = 3.0;

    auto flat = made (parameters);

    auto lifted = parameters;
    lifted.resonance = 0.9;
    auto resonant = made (lifted);

    const double flatLow = toneLevel (flat, 0.3, 60.0);
    const double resonantLow = toneLevel (resonant, 0.3, 60.0);

    auto flat2 = made (parameters);
    auto resonant2 = made (lifted);

    const double flatHigh = toneLevel (flat2, 0.3, 5000.0);
    const double resonantHigh = toneLevel (resonant2, 0.3, 5000.0);

    CHECK (resonantLow > flatLow * 1.1);
    CHECK_NEAR (resonantHigh / flatHigh, 1.0, 0.02);
}

TEZLA_TEST (power_amp_both_shunts_at_zero_leave_the_loop_bit_exact)
{
    // Both are subtractions of a filtered copy of the feedback signal, so at
    // zero each must return it unchanged to the bit. This matters more here
    // than in most places: the signal is subtracted from the input, so an error
    // in it is an error in everything the amplifier does.
    PowerAmpParameters plain;
    plain.feedback = 0.5;

    auto withShunts = plain;
    withShunts.presence = 0.0;
    withShunts.resonance = 0.0;
    withShunts.presenceHz = 3000.0;      // corners moved, amounts still zero
    withShunts.resonanceHz = 40.0;

    auto a = made (plain);
    auto b = made (withShunts);

    bool exact = true;

    for (int i = 0; i < 8192; ++i)
    {
        const double x = 0.7 * std::sin (i * 0.011) + 0.3 * std::sin (i * 0.19);

        if (a.process (x) != b.process (x))
            exact = false;
    }

    CHECK (exact);
}
