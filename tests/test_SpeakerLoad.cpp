#include "TestFramework.hpp"

#include <algorithm>
#include <cmath>
#include <array>
#include <complex>
#include <numbers>

#include <tezla/dsp/SpeakerLoad.hpp>

using namespace tezla::dsp;

namespace
{
constexpr double kRate = 192000.0;

SpeakerLoad made (const SpeakerLoadParameters& parameters = {})
{
    SpeakerLoad load;
    load.prepare (kRate);
    load.setParameters (parameters);
    load.reset();
    return load;
}

/// Magnitude response at one frequency, by driving a sine and demodulating.
double magnitudeAt (SpeakerLoad& load, double frequency, double sampleRate = kRate)
{
    load.reset();

    const int settle = static_cast<int> (sampleRate * 0.3);
    const int measure = static_cast<int> (std::round (60.0 * sampleRate / frequency));

    double phase = 0.0;
    const double step = 2.0 * std::numbers::pi * frequency / sampleRate;

    for (int i = 0; i < settle; ++i)
    {
        (void) load.process (std::sin (phase));
        phase += step;
    }

    double inPhase = 0.0;
    double quadrature = 0.0;

    for (int i = 0; i < measure; ++i)
    {
        const double y = load.process (std::sin (phase));
        inPhase += y * std::sin (phase);
        quadrature += y * std::cos (phase);
        phase += step;
    }

    return 2.0 * std::hypot (inPhase, quadrature) / measure;
}

double dbOf (double magnitude) { return 20.0 * std::log10 (std::max (magnitude, 1.0e-12)); }
} // namespace

// ---------------------------------------------------------------------------
// The impedance curve, against what a driver's specification says it must be
// ---------------------------------------------------------------------------

TEZLA_TEST (speaker_impedance_peaks_at_resonance_by_the_ratio_of_the_two_qs)
{
    // The height of a driver's impedance peak is not a free parameter. The
    // motional branch is a parallel RLC whose resistance is Res = Re*Qms/Qes,
    // and at resonance the reactances cancel and leave exactly that in series
    // with Re. So the peak is Re*(1 + Qms/Qes) and nothing else -- 6.5*(1+8/0.6)
    // = 93.2 ohms for the defaults here.
    const DriverParameters driver;

    const double expected = driver.reOhms * (1.0 + driver.qms / driver.qes);

    CHECK_NEAR (std::abs (SpeakerLoad::impedance (driver, driver.resonanceHz)), expected, 0.5);

    // And it really is a peak: an octave either side is far below it.
    CHECK (std::abs (SpeakerLoad::impedance (driver, driver.resonanceHz * 0.5)) < expected * 0.3);
    CHECK (std::abs (SpeakerLoad::impedance (driver, driver.resonanceHz * 2.0)) < expected * 0.3);

    // At resonance the impedance is purely resistive, which is the definition of
    // resonance rather than a consequence of it.
    CHECK (std::abs (std::arg (SpeakerLoad::impedance (driver, driver.resonanceHz))) < 0.02);
}

TEZLA_TEST (speaker_impedance_bottoms_out_a_shade_above_the_coil_resistance)
{
    // Between the resonance and the inductive rise there is a minimum, and it
    // sits just above Re -- which is why a "8 ohm" driver measures 6.5 at DC
    // and never actually presents 8 anywhere useful.
    const DriverParameters driver;

    double lowest = 1.0e9;
    double lowestAt = 0.0;

    for (double f = 100.0; f < 2000.0; f *= 1.01)
    {
        const double z = std::abs (SpeakerLoad::impedance (driver, f));

        if (z < lowest)
        {
            lowest = z;
            lowestAt = f;
        }
    }

    CHECK (lowest > driver.reOhms);
    CHECK (lowest < driver.reOhms * 1.1);
    CHECK (lowestAt > 200.0);
    CHECK (lowestAt < 800.0);
}

TEZLA_TEST (speaker_voice_coil_rises_like_a_lossy_semi_inductance_not_an_inductor)
{
    // Eddy currents in the pole piece make a real voice coil's impedance climb
    // as about f^0.6 with a phase near 55 degrees, not f^1 at 90 -- Leach,
    // JAES 2002. A plain inductor overstates the top of the guitar band by
    // several decibels, and this is the second thing amp sims get wrong after
    // ignoring the load entirely.
    const DriverParameters driver;

    const double at400 = std::abs (SpeakerLoad::impedance (driver, 400.0));
    const double at5k = std::abs (SpeakerLoad::impedance (driver, 5000.0));

    const double slope = std::log (at5k / at400) / std::log (5000.0 / 400.0);

    CHECK (slope > 0.45);
    CHECK (slope < 0.75);

    // The phase says the same thing more directly, and cannot be faked by a
    // magnitude that happens to fit.
    const double phase = std::arg (SpeakerLoad::impedance (driver, 5000.0)) * 180.0
                       / std::numbers::pi;

    CHECK (phase > 40.0);
    CHECK (phase < 70.0);
}

// ---------------------------------------------------------------------------
// The netlist, against the algebra it is meant to embody
// ---------------------------------------------------------------------------

TEZLA_TEST (speaker_load_solver_matches_the_circuit_equations)
{
    // The solver runs a netlist; impedance() evaluates the transfer function.
    // They are two independent statements of the same circuit, and if the
    // netlist is wired wrongly they disagree.
    SpeakerLoadParameters parameters;
    parameters.dampingFactor = 1.0;

    auto load = made (parameters);

    for (const double frequency : { 41.0, 75.0, 110.0, 220.0, 440.0, 1000.0, 3000.0, 6000.0 })
    {
        const double measured = dbOf (magnitudeAt (load, frequency));
        const double expected = dbOf (std::abs (SpeakerLoad::response (parameters, frequency)));

        CHECK_NEAR (measured, expected, 0.1);
    }
}

// ---------------------------------------------------------------------------
// What it does to the sound
// ---------------------------------------------------------------------------

TEZLA_TEST (speaker_load_shapes_the_tone_only_when_the_amp_has_a_loose_grip)
{
    // The whole point. Measured against the same amplifier at 1 kHz:
    //
    //     damping factor      0.5      1.0      3.0     20.0
    //        75 Hz          +7.67    +4.81    +1.92    +0.31
    //       400 Hz          -1.63    -1.35    -0.77    -0.16
    //      8000 Hz          +6.68    +4.35    +1.79    +0.29
    //
    // A valve amplifier without a global feedback loop gets nine decibels of
    // tone shaping for free, and a solid-state one gets a third of a decibel.
    const auto shapeOf = [] (double damping)
    {
        SpeakerLoadParameters parameters;
        parameters.dampingFactor = damping;

        const auto at = [&parameters] (double f)
        {
            return dbOf (std::abs (SpeakerLoad::response (parameters, f)));
        };

        const double reference = at (1000.0);
        return std::array { at (75.0) - reference, at (400.0) - reference, at (8000.0) - reference };
    };

    const auto loose = shapeOf (0.5);
    const auto modest = shapeOf (3.0);
    const auto tight = shapeOf (20.0);

    // Loose: a tall bump at resonance, a scoop through the low mids, a rising
    // presence -- and the range across the band is most of ten decibels.
    CHECK (loose[0] > 6.0);
    CHECK (loose[1] < -1.0);
    CHECK (loose[2] > 5.0);
    CHECK (loose[0] - loose[1] > 8.0);

    // Tighter grips do the same thing, less.
    CHECK (modest[0] < loose[0]);
    CHECK (modest[0] > tight[0]);

    // Tight: solid state, and flat to within a third of a decibel everywhere.
    for (const double deviation : tight)
        CHECK (std::abs (deviation) < 0.4);
}

TEZLA_TEST (speaker_load_bypass_is_bit_exact)
{
    // CLAUDE.md section 7: a stage permanently in the signal path needs a
    // bit-exact bypass at its neutral setting, not merely a transparent one.
    // "Damping factor at its maximum" is not that -- it is a network that is
    // very nearly the identity, and very nearly is what changes every project
    // the day the plugin updates.
    SpeakerLoadParameters parameters;
    parameters.bypassed = true;

    auto load = made (parameters);

    bool exact = true;

    for (int i = 0; i < 4096; ++i)
    {
        const double x = std::sin (i * 0.017) * 0.9 + std::sin (i * 0.31) * 0.1;

        if (load.process (x) != x)
            exact = false;
    }

    CHECK (exact);
}

TEZLA_TEST (speaker_load_is_the_same_circuit_at_every_sample_rate)
{
    // CLAUDE.md section 6, and the answer is better than the reflex expects.
    //
    // The voice coil's corner is at 8 kHz, which is 34% of Nyquist at 48 kHz,
    // so a trapezoidal network "should" be visibly warped there. Measured
    // against the exact transfer function it is not: identical to three
    // decimals below 220 Hz at every rate from 44.1 to 192 kHz, 0.13 dB out at
    // 8 kHz at 48 kHz, and 0.64 dB out only by 16 kHz.
    //
    // Warping distorts the frequency axis, and this network has no sharp
    // feature up there to distort -- the semi-inductance is a gentle shelf. A
    // resonant filter at the same corner would be far worse.
    SpeakerLoadParameters parameters;
    parameters.dampingFactor = 0.5;

    const double rates[] = { 44100.0, 48000.0, 96000.0, 192000.0 };

    SpeakerLoad loads[4];

    for (int i = 0; i < 4; ++i)
    {
        loads[i].prepare (rates[i]);
        loads[i].setParameters (parameters);
    }

    // Where the music is, every rate agrees with the algebra to a hundredth of
    // a decibel. This is the assertion that matters.
    for (const double frequency : { 41.0, 75.0, 220.0, 1000.0 })
    {
        const double exact = dbOf (std::abs (SpeakerLoad::response (parameters, frequency)));

        for (int i = 0; i < 4; ++i)
            CHECK_NEAR (dbOf (magnitudeAt (loads[i], frequency, rates[i])), exact, 0.01);
    }

    // And the two rates Anvil's oversampled section actually runs at agree with
    // each other across the whole band.
    for (const double frequency : { 4000.0, 8000.0, 16000.0 })
        CHECK_NEAR (dbOf (magnitudeAt (loads[2], frequency, 96000.0)),
                    dbOf (magnitudeAt (loads[3], frequency, 192000.0)),
                    0.08);

    // The warping is real, and it is where the bilinear transform puts it:
    // present at 16 kHz at 44.1 kHz, absent at 192.
    const double exact16k = dbOf (std::abs (SpeakerLoad::response (parameters, 16000.0)));

    CHECK (std::abs (dbOf (magnitudeAt (loads[0], 16000.0, 44100.0)) - exact16k) > 0.4);
    CHECK (std::abs (dbOf (magnitudeAt (loads[3], 16000.0, 192000.0)) - exact16k) < 0.05);
}

TEZLA_TEST (speaker_load_is_silent_in_silence)
{
    auto load = made();

    bool silent = true;

    for (int i = 0; i < 8192; ++i)
        if (load.process (0.0) != 0.0)
            silent = false;

    CHECK (silent);
}
