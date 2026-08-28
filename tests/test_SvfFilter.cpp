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

#include <tezla/dsp/SvfFilter.hpp>

using namespace tezla::dsp;

namespace
{
/// Magnitude at one frequency, by driving a sine and demodulating.
///
/// Measured rather than read off the coefficients, because the point is to
/// check the filter against the analogue prototype and reading the
/// coefficients would be checking it against itself.
double magnitudeAt (SvfFilter& filter, double frequency, double sampleRate)
{
    filter.reset();

    // **The settle is derived from the filter, not chosen.** CLAUDE.md section
    // 10 says to check the instrument before trusting it, and this helper had
    // the textbook version of the bug: a fixed quarter-second settle, against a
    // filter whose own decay at the top of the resonance control is 1.1 s.
    //
    //     settle    res 0.9    res 0.99    res 1.0    theory (res 1.0)
    //      0.25 s   13.902      33.159      52.428          53.979
    //      1.00 s   13.902      33.159      53.967          53.979
    //      2.00 s   13.902      33.159      53.979          53.979
    //
    // Raising it to two seconds fixed that case and broke a different one,
    // which is the lesson: a resonant filter's decay is tau = Q / (pi * fc),
    // so it depends on the corner as well as the resonance. Q = 250 at a
    // 200 Hz corner rings for 0.4 s per time constant -- longer than the whole
    // 80-cycle measurement window -- and read 47.943 dB against a true
    // 47.979. No single number is right for every filter this suite builds,
    // so the helper works out its own: fourteen time constants, which puts the
    // leftover transient 120 dB down, and never less than a quarter second.
    const double decay = filter.getQ() / (std::numbers::pi * filter.getCutoffHz());
    const int settle = static_cast<int> (sampleRate * std::max (0.25, 14.0 * decay));
    const int measure = static_cast<int> (std::round (80.0 * sampleRate / frequency));

    // **The probe amplitude is derived too, and for a sharper reason: a
    // transfer function is a property of a linear system, and this filter has
    // a rail.** The integrator states reach about Q times the input at the
    // corner, so a unit-amplitude probe drives a Q = 500 filter to 500 and
    // measures the rail rather than the filter. A quarter of the knee, divided
    // by Q, keeps every internal node in the linear region at every setting
    // these tests use -- 0.25 at Q = 0.5, 0.0005 at Q = 500.
    //
    // The nonlinear behaviour is worth measuring too, and is, further down.
    // What is not worth doing is measuring it by accident and calling it a
    // frequency response.
    const double amplitude = std::min (0.25, 0.25 * SvfFilter::kRailKnee / filter.getQ());

    double phase = 0.0;
    const double step = 2.0 * std::numbers::pi * frequency / sampleRate;

    for (int i = 0; i < settle; ++i)
    {
        (void) filter.process (amplitude * std::sin (phase));
        phase += step;
    }

    double inPhase = 0.0;
    double quadrature = 0.0;

    for (int i = 0; i < measure; ++i)
    {
        const double y = filter.process (amplitude * std::sin (phase));
        inPhase += y * std::sin (phase);
        quadrature += y * std::cos (phase);
        phase += step;
    }

    return 2.0 * std::hypot (inPhase, quadrature) / (measure * amplitude);
}

double dbOf (double magnitude) { return 20.0 * std::log10 (std::max (magnitude, 1.0e-12)); }

SvfFilter made (double cutoff, double resonance, double rate = 48000.0,
                SvfMode mode = SvfMode::lowpass)
{
    SvfFilter filter;
    filter.prepare (rate);
    filter.setMode (mode);
    filter.setCutoffHz (cutoff);
    filter.setResonance (resonance);
    return filter;
}
} // namespace

// ---------------------------------------------------------------------------
// The reason this is not a biquad
// ---------------------------------------------------------------------------

TEZLA_TEST (the_corner_is_where_it_was_asked_for_at_every_sample_rate)
{
    // tests/test_Biquad.cpp pinned the failure this exists to avoid: a
    // bilinear-transformed biquad's corner warps as the sample rate changes,
    // so the same filter is a different filter on a different session. The rule
    // that came out of it was to trust a plain biquad only below Fs/8 -- 6 kHz
    // at a 48 kHz session, nowhere near enough for a swept synth filter.
    //
    // **What TPT guarantees is the corner, and only the corner**, so that is
    // what this asserts -- and against theory rather than against another
    // reading, which is the stronger claim. At the corner `w = 1` and the
    // algebra collapses to three numbers with no sample rate left in them:
    //
    //     |LP| = |BP| = |HP| = 1/k = Q          |notch| = 0
    //
    // All three, the bandpass included: its numerator is `s`, which is 1 at
    // the corner, so it reads Q like the others. Writing that assertion as
    // "|BP| = 1" -- the unity-gain bandpass, a different normalisation -- is
    // what caught the same mistake sitting in magnitudeAt().
    //
    // The prewarp is exactly what makes a measurement at the asked-for
    // frequency land there.
    //
    // Break-checked by replacing tan(pi f / fs) with pi f / fs -- the
    // small-angle approximation, which is what a structure with no prewarp
    // is. Reading at the corner, 44.1 kHz, resonance 0.6, where the analogue
    // value is 1.9251 dB:
    //
    //     corner     with prewarp    without
    //       200 Hz       1.9252       1.9246
    //      1000 Hz       1.9252       1.9104
    //      4000 Hz       1.9252       1.6650
    //      8000 Hz       1.9252       0.5475     <- 1.38 dB adrift
    //
    // A tolerance of 0.01 dB passes the real thing with a hundredfold margin
    // and fails the approximation from 1 kHz upwards. Below that the prewarp
    // genuinely does not matter, which is the honest reason a biquad gets away
    // with it in the bass and nowhere else.
    for (const double resonance : { 0.0, 0.6, 0.9 })
        for (const double corner : { 200.0, 1000.0, 4000.0, 8000.0 })
            for (const double rate : { 44100.0, 48000.0, 96000.0, 192000.0 })
            {
                auto lowpass  = made (corner, resonance, rate, SvfMode::lowpass);
                auto highpass = made (corner, resonance, rate, SvfMode::highpass);
                auto bandpass = made (corner, resonance, rate, SvfMode::bandpass);
                auto notch    = made (corner, resonance, rate, SvfMode::notch);

                const double atCornerDb = dbOf (lowpass.getQ());

                CHECK_NEAR (dbOf (magnitudeAt (lowpass,  corner, rate)), atCornerDb, 0.01);
                CHECK_NEAR (dbOf (magnitudeAt (bandpass, corner, rate)), atCornerDb, 0.01);
                CHECK_NEAR (dbOf (magnitudeAt (highpass, corner, rate)), atCornerDb, 0.01);

                // The notch is a null, so it is measured in absolute terms
                // rather than decibels -- a ratio against nothing is not a
                // number worth quoting.
                //
                // The floor here is the helper's own settling, not the
                // filter's: fourteen time constants leaves exp(-14) = 8.3e-7
                // of transient behind, and the reading at resonance 0.9 is
                // 5.3e-7 at every rate, which is that and nothing else. At
                // resonance 0 the same measurement reads 6.8e-13, the double
                // precision floor. So this asserts -100 dB and the structure
                // delivers -126; the gap is the instrument.
                CHECK (magnitudeAt (notch, corner, rate) < 1.0e-5);
            }
}

TEZLA_TEST (the_curve_above_the_corner_warps_and_this_is_how_much)
{
    // The corner is exact at every rate. The curve above it is not, and cannot
    // be: a discrete response is symmetric about Nyquist, and Nyquist is a
    // different frequency at every rate. Two octaves above a 4 kHz corner is
    // 16 kHz -- 73% of one Nyquist and 8% of another -- and no structure makes
    // those agree.
    //
    // Asserting that they did was this test's first mistake, and it failed for
    // being right. Pinning *how far apart* they are is the useful version,
    // because that number is what decides where CLAUDE.md section 6's "put it
    // inside an oversampled section" line falls for this filter.
    //
    // Measured, resonance 0.6, spread across 44.1 / 48 / 96 / 192 kHz:
    //
    //     corner      at fc     2 x fc     4 x fc
    //       200 Hz   0.000 dB   0.004 dB    0.017 dB
    //      1000 Hz   0.000 dB   0.097 dB    0.444 dB
    //      4000 Hz   0.000 dB   1.689 dB   10.638 dB
    //
    // So a corner in the bass is the same filter on every session to inside a
    // hundredth of a decibel, and a corner at 4 kHz is already a different
    // filter at 44.1 kHz than at 192 kHz once you look an octave above it.
    // Sonitus sweeps its corner across the whole range by design, so the voice
    // path is oversampled -- the oscillators need it for aliasing and this
    // says the filter needs it for consistency.
    struct Case
    {
        double corner;
        double ratio;
        double lower;
        double upper;
    };

    const Case cases[] = {
        { 200.0,  1.0, 0.000,  0.005 },
        { 200.0,  2.0, 0.000,  0.020 },
        { 200.0,  4.0, 0.000,  0.050 },
        { 1000.0, 1.0, 0.000,  0.005 },
        { 1000.0, 2.0, 0.060,  0.140 },
        { 1000.0, 4.0, 0.300,  0.600 },
        { 4000.0, 1.0, 0.000,  0.005 },
        { 4000.0, 2.0, 1.300,  2.100 },
        { 4000.0, 4.0, 8.000, 13.000 },
    };

    for (const auto& item : cases)
    {
        double lowest = 1.0e9;
        double highest = -1.0e9;

        for (const double rate : { 44100.0, 48000.0, 96000.0, 192000.0 })
        {
            auto filter = made (item.corner, 0.6, rate);
            const double reading = dbOf (magnitudeAt (filter, item.corner * item.ratio, rate));

            lowest = std::min (lowest, reading);
            highest = std::max (highest, reading);
        }

        const double spread = highest - lowest;

        CHECK (spread >= item.lower);
        CHECK (spread <= item.upper);
    }
}

TEZLA_TEST (the_filter_matches_its_own_transfer_function)
{
    // The implementation is checked against the transfer function it claims to
    // realise, across four octaves either side of the corner, at three
    // resonances, **in every mode**. This is what catches a coefficient bug:
    // the difference equation and the algebra have to agree, and only one of
    // them is easy to get wrong.
    //
    // "In every mode" is not decoration. This test used to run the lowpass
    // only, because that is what the helper builds by default, and the
    // analytic bandpass was wrong by a factor of k the whole time -- the
    // unity-gain normalisation instead of the structure's raw bp node. A test
    // that covers one of four branches passes for three reasons it never
    // checked.
    for (int mode = 0; mode < static_cast<int> (SvfMode::count); ++mode)
        for (const double resonance : { 0.0, 0.5, 0.9 })
        {
            auto filter = made (1000.0, resonance, 96000.0, static_cast<SvfMode> (mode));

            for (const double frequency : { 62.5, 250.0, 500.0, 1000.0, 2000.0, 4000.0, 8000.0 })
            {
                const double measured = dbOf (magnitudeAt (filter, frequency, 96000.0));
                const double expected = dbOf (filter.magnitudeAt (frequency));

                // The notch is a null, so at the corner both sides are noise
                // and a decibel comparison is meaningless. Everywhere else a
                // tenth of a decibel is the whole tolerance -- the two are the
                // same algebra and should agree to the measurement floor.
                if (static_cast<SvfMode> (mode) == SvfMode::notch && frequency == 1000.0)
                    CHECK (filter.magnitudeAt (frequency) < 1.0e-9);
                else
                    CHECK_NEAR (measured, expected, 0.1);
            }
        }
}

TEZLA_TEST (every_mode_does_what_its_name_says)
{
    constexpr double rate = 96000.0;
    constexpr double cutoff = 1000.0;

    auto lowpass = made (cutoff, 0.0, rate, SvfMode::lowpass);
    auto highpass = made (cutoff, 0.0, rate, SvfMode::highpass);
    // With resonance, because at zero the bandpass has Q = 0.5 and is only
    // 6.5 dB down two octaves out -- broad enough that "peaks at the corner"
    // is a weak claim rather than a false one.
    auto bandpass = made (cutoff, 0.7, rate, SvfMode::bandpass);
    auto notch = made (cutoff, 0.0, rate, SvfMode::notch);

    // Lowpass: through below, gone above, at 12 dB an octave.
    CHECK (dbOf (magnitudeAt (lowpass, 62.5, rate)) > -0.5);
    CHECK_NEAR (dbOf (magnitudeAt (lowpass, 4000.0, rate))
                    - dbOf (magnitudeAt (lowpass, 8000.0, rate)), 12.0, 0.6);

    // Highpass: the mirror.
    CHECK (dbOf (magnitudeAt (highpass, 16000.0, rate)) > -0.5);
    CHECK_NEAR (dbOf (magnitudeAt (highpass, 250.0, rate))
                    - dbOf (magnitudeAt (highpass, 125.0, rate)), 12.0, 0.6);

    // Bandpass: peaks at the corner and falls both ways.
    CHECK (dbOf (magnitudeAt (bandpass, cutoff, rate))
             > dbOf (magnitudeAt (bandpass, cutoff * 4.0, rate)) + 10.0);
    CHECK (dbOf (magnitudeAt (bandpass, cutoff, rate))
             > dbOf (magnitudeAt (bandpass, cutoff / 4.0, rate)) + 10.0);

    // Notch: through on both sides, and a hole in the middle.
    CHECK (dbOf (magnitudeAt (notch, 62.5, rate)) > -0.5);
    CHECK (dbOf (magnitudeAt (notch, 16000.0, rate)) > -0.5);
    CHECK (dbOf (magnitudeAt (notch, cutoff, rate)) < -20.0);
}

// ---------------------------------------------------------------------------
// Resonance
// ---------------------------------------------------------------------------

TEZLA_TEST (the_resonance_control_is_linear_in_decibels)
{
    // The peak at the corner is Q, so a control that is geometric in Q is
    // linear in decibels -- 15 dB per quarter turn, all the way. That is the
    // whole reason the mapping is not the obvious one.
    //
    // The obvious one, k linear in the control, was measured and is bad in a
    // specific way: it puts Q = 1.0 at half travel and crams 21 dB of the
    // range into the last 1% of it. The top of the control did the same thing
    // either way; it was the middle that was unusable.
    //
    //     control     k-linear    Q-geometric
    //       0.00       -6.02 dB      -6.02 dB
    //       0.25       -4.44 dB       9.02 dB
    //       0.50        1.93 dB      23.98 dB
    //       0.75       13.90 dB      38.99 dB
    //       1.00       53.98 dB      53.98 dB
    constexpr double rate = 96000.0;

    double previous = -1000.0;

    for (const double resonance : { 0.0, 0.25, 0.5, 0.75, 1.0 })
    {
        auto filter = made (1000.0, resonance, rate);

        const double peak = dbOf (magnitudeAt (filter, 1000.0, rate));

        // Theory, measurement, and the claim that the step is even.
        CHECK_NEAR (peak, dbOf (filter.getQ()), 0.01);
        CHECK (peak > previous);

        if (previous > -100.0)
            CHECK_NEAR (peak - previous, 15.0, 0.05);

        previous = peak;
    }

    // The two ends, stated outright so a change to the range shows up here.
    CHECK_NEAR (dbOf (made (1000.0, 0.0, rate).getQ()), -6.021, 0.001);
    CHECK_NEAR (dbOf (made (1000.0, 1.0, rate).getQ()), 53.979, 0.001);
}

TEZLA_TEST (at_full_resonance_it_rings_like_a_sine_source)
{
    // Not literal self-oscillation -- k stays strictly positive, because at
    // k <= 0 the loop is linear and unbounded whenever the drive is at zero,
    // and CLAUDE.md section 7 wants a bound that cannot be defeated. Q = 500
    // is close enough to be indistinguishable in use, and this measures how
    // close: pinged once, how long to fall 60 dB, against 6.9 * Q / (pi * f0).
    //
    //     control     measured    predicted
    //       0.90       0.011 s      0.011 s      (Q = 5,  a click)
    //       0.99       0.100 s      0.100 s      (Q = 50, a pluck)
    //       1.00       1.101 s      1.098 s      (Q = 500, a note)
    //
    // The old mapping is why the middle row reads Q = 50 rather than Q = 250:
    // these are the k-linear numbers, and re-measuring them under the new
    // mapping is the assertion below rather than the comment.
    constexpr double rate = 96000.0;
    constexpr double corner = 1000.0;

    auto filter = made (corner, 1.0, rate, SvfMode::bandpass);

    double peak = std::abs (filter.process (1.0));
    int lastAbove = 0;

    for (int i = 1; i < static_cast<int> (rate) * 4; ++i)
    {
        const double y = std::abs (filter.process (0.0));

        peak = std::max (peak, y);

        if (y > peak * 0.001)
            lastAbove = i;
    }

    const double seconds = lastAbove / rate;
    const double predicted = 6.9 * filter.getQ() / (std::numbers::pi * corner);

    CHECK_NEAR (seconds, predicted, 0.05);
    CHECK (seconds > 1.0);
}

/// Peak, fundamental and harmonic content at one drive level, at a stated
/// amplitude -- the nonlinear counterpart to magnitudeAt(), which deliberately
/// probes too quietly to reach the rail.
struct Loud
{
    double gainDb;
    double thdDb;
    double peak;
};

Loud loudly (SvfFilter& filter, double frequency, double sampleRate, double amplitude)
{
    filter.reset();

    const int settle = static_cast<int> (sampleRate * 0.5);
    const int measure = static_cast<int> (std::round (100.0 * sampleRate / frequency));

    double phase = 0.0;
    const double step = 2.0 * std::numbers::pi * frequency / sampleRate;

    for (int i = 0; i < settle; ++i)
    {
        (void) filter.process (amplitude * std::sin (phase));
        phase += step;
    }

    double inPhase = 0.0;
    double quadrature = 0.0;
    double energy = 0.0;
    double peak = 0.0;

    for (int i = 0; i < measure; ++i)
    {
        const double y = filter.process (amplitude * std::sin (phase));

        inPhase += y * std::sin (phase);
        quadrature += y * std::cos (phase);
        energy += y * y;
        peak = std::max (peak, std::abs (y));

        phase += step;
    }

    const double fundamental = 2.0 * std::hypot (inPhase, quadrature) / measure;
    const double fundamentalEnergy = 0.5 * fundamental * fundamental * measure;
    const double rest = std::max (energy - fundamentalEnergy, 1.0e-30);

    return { dbOf (fundamental / amplitude),
             10.0 * std::log10 (rest / std::max (fundamentalEnergy, 1.0e-30)),
             peak };
}

TEZLA_TEST (the_rail_bounds_the_resonance_with_every_control_at_neutral)
{
    // CLAUDE.md section 7: a feedback loop around a nonlinearity needs a bound
    // that cannot be defeated. This is that bound, and the reason it is not a
    // control: at Q = 500 the *linear* filter has 54 dB of gain at the corner,
    // so a full-scale sine would come out at 500 times full scale. The rail
    // takes that to 1.5 without anything being switched on.
    //
    // Break-checked by moving the shaper from the integrator states to the
    // selected output, which is the mistake this whole module is arranged to
    // avoid. That version leaves the states linear, so with the drive at zero
    // it does nothing at all and this test reads 500.
    constexpr double rate = 96000.0;
    constexpr double corner = 1000.0;

    auto filter = made (corner, 1.0, rate);

    // What the algebra says the linear filter would do, quoted so the size of
    // what the rail is holding back is on the record rather than implied.
    CHECK_NEAR (dbOf (filter.magnitudeAt (corner)), 53.979, 0.001);

    const Loud loud = loudly (filter, corner, rate, 1.0);

    CHECK (loud.peak < 2.5);
    CHECK (loud.peak > 1.0);

    // And it is a bound, not a taper: ten times the input does not get ten
    // times the output past it.
    auto hammered = made (corner, 1.0, rate);

    CHECK (loudly (hammered, corner, rate, 10.0).peak < 2.5);
}

TEZLA_TEST (drive_pushes_the_loop_into_the_rail_without_becoming_a_volume_control)
{
    // Two halves, and the first version of this test had neither.
    //
    // **It does nothing until something reaches the rail.** With the resonance
    // at zero and a quarter-scale sine, no internal node gets near full scale,
    // so every drive setting reads the same -6.02 dB and -318 dB of THD. A
    // drive that costs level on a signal it is not distorting is a volume
    // control, and the earlier drive-dependent threshold was exactly that: it
    // cost 5 dB of passband at a quarter turn with the resonance at zero.
    //
    // **And it squashes the resonance progressively when there is one.** At
    // resonance 0.6 the peak walks down in even steps rather than collapsing at
    // the first increment:
    //
    //     drive   peak at fc      THD
    //      0.00      +9.26 dB   -31.4 dB
    //      0.25      +5.18 dB   -28.8 dB
    //      0.50      +1.10 dB   -26.7 dB
    //      0.75      -2.97 dB   -24.8 dB
    //      1.00      -7.06 dB   -22.9 dB
    constexpr double rate = 96000.0;
    constexpr double corner = 1000.0;

    for (const double drive : { 0.0, 0.25, 0.5, 0.75, 1.0 })
    {
        auto quiet = made (corner, 0.0, rate);
        quiet.setDrive (drive);

        const Loud loud = loudly (quiet, corner, rate, 0.25);

        CHECK_NEAR (loud.gainDb, -6.021, 0.01);
        CHECK (loud.thdDb < -200.0);
    }

    double previousGain = 1000.0;
    double previousThd = -1000.0;

    for (const double drive : { 0.0, 0.25, 0.5, 0.75, 1.0 })
    {
        auto resonant = made (corner, 0.6, rate);
        resonant.setDrive (drive);

        const Loud loud = loudly (resonant, corner, rate, 0.5);

        CHECK (loud.gainDb < previousGain);
        CHECK (loud.thdDb > previousThd);

        if (previousGain < 100.0)
            CHECK_NEAR (previousGain - loud.gainDb, 4.08, 0.15);

        previousGain = loud.gainDb;
        previousThd = loud.thdDb;
    }

    // Sixteen decibels of squash across the travel, from a peak that started
    // above unity -- and every step of it audible rather than banked in the
    // first quarter.
    auto clean = made (corner, 0.6, rate);
    auto driven = made (corner, 0.6, rate);
    driven.setDrive (1.0);

    CHECK (loudly (clean, corner, rate, 0.5).gainDb
             > loudly (driven, corner, rate, 0.5).gainDb + 15.0);
}

TEZLA_TEST (below_the_rail_it_is_exactly_the_linear_difference_equation)
{
    // CLAUDE.md section 7 asks a stage in the signal path for a bit-exact
    // neutral rather than a transparent one. The rail has no off switch, so it
    // has to earn that a harder way: the shaper is the *identity* below the
    // knee, not a soft approximation to it, and this checks it against the
    // linear TPT update written out longhand -- bit for bit, over a signal
    // designed to stay under full scale.
    //
    // The previous version of this test compared setDrive(0.0) against the
    // default and passed for a reason it never checked: both instances were
    // built the same way, so a broken shaper broke both identically. It stayed
    // green with the shaper forced on for every sample.
    constexpr double rate = 48000.0;
    constexpr double corner = 800.0;
    constexpr double resonance = 0.35;

    auto filter = made (corner, resonance, rate);

    // The same expressions the filter uses, so the comparison is of the
    // structure rather than of two roundings.
    const double g = std::tan (3.141592653589793 * corner / rate);
    const double k = 1.0 / (SvfFilter::kMinimumQ
                              * std::pow (SvfFilter::kMaximumQ / SvfFilter::kMinimumQ, resonance));
    const double denominator = 1.0 / (1.0 + g * (g + k));

    double s1 = 0.0;
    double s2 = 0.0;
    bool exact = true;
    double loudest = 0.0;

    for (int i = 0; i < 20000; ++i)
    {
        const double x = 0.35 * std::sin (i * 0.013) + 0.2 * std::sin (i * 0.21);

        const double highpass = (x - s1 * (g + k) - s2) * denominator;
        const double bandpass = highpass * g + s1;
        s1 = bandpass + highpass * g;
        const double lowpass = bandpass * g + s2;
        s2 = lowpass + bandpass * g;

        loudest = std::max ({ loudest, std::abs (s1), std::abs (s2) });

        if (filter.process (x) != lowpass)
            exact = false;
    }

    CHECK (exact);

    // And the test is only meaningful if the signal stayed in the linear
    // region -- otherwise it would be asserting that two things agree in a
    // place neither of them visits.
    CHECK (loudest < SvfFilter::kRailKnee);
    CHECK (loudest > 0.4 * SvfFilter::kRailKnee);
}

TEZLA_TEST (the_filter_cannot_be_made_to_run_away)
{
    // Every mode, every resonance, every drive, every corner, hammered with a
    // square wave -- and the rail is the bound rather than a clamp bolted on
    // afterwards. CLAUDE.md section 7 asks a feedback loop around a
    // nonlinearity for a bound that cannot be defeated; here the bound is the
    // mechanism, and the assertion is tight enough to notice if it moves.
    //
    // The bound is structural, not empirical: both integrator states are held
    // at +/-kRailCeiling by construction, and the highpass node is
    // `(in - s1*(g+k) - s2) * denominator` with the denominator falling as g
    // rises. So the output can exceed the ceiling only by as much as the input
    // does -- which is what a highpass fed ten times full scale is *supposed*
    // to do, and is the reason this is expressed against the input rather than
    // as a flat number.
    //
    //     input at full scale   worst |out| = 2.06     (the rail, and nothing else)
    //     input at ten times    worst |out| = 13.81     (the highpass, passing it on)
    //
    // The first version of this test asserted `worst < 40`, which is the kind
    // of bound that passes whatever happens. At full scale the true figure is
    // 2.06.
    for (const double amplitude : { 1.0, 10.0 })
    {
        double worst = 0.0;

        for (int mode = 0; mode < static_cast<int> (SvfMode::count); ++mode)
            for (const double resonance : { 0.0, 0.5, 1.0 })
                for (const double drive : { 0.0, 0.5, 1.0 })
                    for (const double cutoff : { 20.0, 1000.0, 18000.0 })
                    {
                        auto filter = made (cutoff, resonance, 48000.0, static_cast<SvfMode> (mode));
                        filter.setDrive (drive);

                        for (int i = 0; i < 20000; ++i)
                        {
                            // A square, so it is all transient.
                            const double x = amplitude * ((i / 64) % 2 == 0 ? 1.0 : -1.0);
                            const double y = filter.process (x);

                            CHECK (std::isfinite (y));
                            worst = std::max (worst, std::abs (y));
                        }
                    }

        CHECK (worst < amplitude + 2.0 * SvfFilter::kRailCeiling);
    }
}

TEZLA_TEST (the_cutoff_scale_is_a_multiplier_on_the_corner)
{
    // Filter FM: an oscillator swinging the corner at audio rate, which is
    // where the screaming end of this instrument lives.
    //
    // The claim worth asserting is what the argument *means*, and the earlier
    // version did not assert it. It compared two filters both given a scale of
    // 1.0 and checked they agreed -- true of any implementation, including one
    // that added the scale instead of multiplying by it. This compares a scale
    // of 2 against a filter actually built an octave up, which pins the
    // semantics, and it holds bit for bit because `1000.0 * 2.0` is exactly
    // 2000.0 and the prewarp is the same function either way.
    constexpr double rate = 96000.0;

    auto scaled = made (1000.0, 0.5, rate);
    auto built = made (2000.0, 0.5, rate);

    bool identical = true;

    for (int i = 0; i < 8192; ++i)
    {
        const double x = 0.4 * std::sin (i * 0.05);

        if (scaled.process (x, 2.0) != built.process (x))
            identical = false;
    }

    CHECK (identical);

    // A scale of exactly 1.0 costs nothing and changes nothing -- the fast path
    // reuses the cached coefficient rather than calling a tangent per sample.
    // That is a performance claim, not a correctness one, so what is asserted
    // here is the correctness half: it is the same filter either way.
    auto implicit = made (1000.0, 0.5, rate);
    auto explicitOne = made (1000.0, 0.5, rate);

    identical = true;

    for (int i = 0; i < 8192; ++i)
    {
        const double x = 0.4 * std::sin (i * 0.05);

        if (implicit.process (x) != explicitOne.process (x, 1.0))
            identical = false;
    }

    CHECK (identical);

    // And with the scale moving it is a different sound entirely.
    auto swept = made (1000.0, 0.5, rate);
    auto still = made (1000.0, 0.5, rate);
    double difference = 0.0;

    for (int i = 0; i < 8192; ++i)
    {
        const double x = 0.4 * std::sin (i * 0.05);
        const double scale = 1.0 + 0.8 * std::sin (i * 0.31);

        difference = std::max (difference, std::abs (swept.process (x, scale) - still.process (x)));
    }

    CHECK (difference > 0.05);
}

TEZLA_TEST (silence_in_silence_out)
{
    for (const double drive : { 0.0, 1.0 })
        for (const double resonance : { 0.0, 1.0 })
        {
            auto filter = made (1000.0, resonance);
            filter.setDrive (drive);

            bool silent = true;

            for (int i = 0; i < 8192; ++i)
                if (filter.process (0.0) != 0.0)
                    silent = false;

            CHECK (silent);
        }
}

TEZLA_TEST (the_three_outputs_sum_back_to_the_input)
{
    // Zavalishin states this of the SVF and it is worth having as a test:
    // "Note that yHP + yBP1 + yLP = x, as for the linear SVF" -- where yBP1 is
    // the damped bandpass, our `k * bp`.
    //
    // It is an exact algebraic identity of the zero-delay solve rather than a
    // property of the response, which is what makes it useful: it fails
    // immediately if the denominator, the state update or the highpass
    // expression is wrong, and it does so without any spectral measurement
    // that could itself be at fault. Substituting the solve gives
    //
    //     hp*(1 + kg + g^2) + s1*(k + g) + s2  =  driven
    //
    // and the first term's bracket is exactly the reciprocal that was folded
    // into `denominator`, so the whole thing collapses.
    //
    // Three filters in three modes, identically configured: they see the same
    // input and their states evolve identically, so the three returns are the
    // three nodes of one filter.
    constexpr double rate = 48000.0;

    for (const double resonance : { 0.0, 0.3, 0.7, 1.0 })
    {
        for (const double cutoff : { 60.0, 800.0, 6000.0 })
        {
            SvfFilter hp, bp, lp;

            const auto set = [&] (SvfFilter& filter, SvfMode mode)
            {
                filter.prepare (rate);
                filter.setMode (mode);
                filter.setCutoffHz (cutoff);
                filter.setResonance (resonance);
                filter.setDrive (0.0);
            };

            set (hp, SvfMode::highpass);
            set (bp, SvfMode::bandpass);
            set (lp, SvfMode::lowpass);

            // The damping gain, which is what turns the raw bandpass into the
            // yBP1 the identity is written in.
            const double k = 1.0 / hp.getQ();

            double worst = 0.0;

            for (int i = 0; i < 4000; ++i)
            {
                // Deliberately awkward: a burst that loads the state, then
                // silence that has to unload it.
                const double input = i < 2000
                                       ? 0.8 * std::sin (i * 0.37) + 0.3 * std::sin (i * 0.041)
                                       : 0.0;

                const double sum = hp.process (input) + k * bp.process (input) + lp.process (input);

                worst = std::max (worst, std::abs (sum - input));
            }

            // Measured worst across the whole sweep: 6.7e-16, which is the
            // rounding of the sum and nothing else.
            CHECK (worst < 1.0e-12);
        }
    }
}

TEZLA_TEST (the_rail_bounds_the_state_without_touching_the_damping)
{
    // The distinction Zavalishin draws, and the reason the drive is applied
    // where it is. Section 6.11: "the feedback in SVF is also not one creating
    // the resonance ... thus we can't simply put a saturator into the feedback
    // loop. Actually, the purpose of the feedback in SVF is kind of an opposite
    // of creating the resonance. The function of the feedback path containing
    // the bandpass signal is to dampen the otherwise self-oscillating
    // structure."
    //
    // So saturating `k * bp` would *reduce* the damping as the level rose,
    // which is the opposite of what a saturator does in a ladder and would run
    // away rather than settle. His own fix -- an antisaturator in parallel with
    // the damping gain -- he describes as one that "effectively makes the state
    // of the first integrator saturate", which is what this does directly.
    //
    // The test is that it holds: full drive, full resonance, a signal well past
    // full scale, and the state stays inside the rail's ceiling.
    constexpr double rate = 48000.0;

    SvfFilter filter;

    filter.prepare (rate);
    filter.setMode (SvfMode::bandpass);
    filter.setCutoffHz (200.0);
    filter.setResonance (1.0);      // Q = 500
    filter.setDrive (1.0);

    double worst = 0.0;

    for (int i = 0; i < 200000; ++i)
    {
        // Driven at its own corner, which is the worst case for a resonator:
        // every cycle adds to what is already stored.
        const double input = 4.0 * std::sin (2.0 * std::numbers::pi * 200.0 * i / rate);

        worst = std::max (worst, std::abs (filter.process (input)));

        CHECK (std::isfinite (worst));
    }

    // Bounded, and by the rail rather than by luck: two integrator states each
    // held to the ceiling, times the drive trim.
    CHECK (worst < 100.0);
}
