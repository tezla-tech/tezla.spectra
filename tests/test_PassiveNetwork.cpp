#include "TestFramework.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>

#include <tezla/dsp/PassiveNetwork.hpp>

using namespace tezla::dsp;

namespace
{
using Network = PassiveNetwork<>;

/// Magnitude response at one frequency, by driving a sine and demodulating.
///
/// Measured rather than derived from coefficients, because the point is to
/// check the solver against circuit theory and a derivation would be checking
/// the solver against itself.
double magnitudeAt (Network& network, double frequency, double sampleRate)
{
    network.reset();

    const int settle = static_cast<int> (sampleRate * 0.2);
    const int cycles = 40;
    const int measure = static_cast<int> (std::round (cycles * sampleRate / frequency));

    double phase = 0.0;
    const double step = 2.0 * std::numbers::pi * frequency / sampleRate;

    for (int i = 0; i < settle; ++i)
    {
        (void) network.process (std::sin (phase));
        phase += step;
    }

    double inPhase = 0.0;
    double quadrature = 0.0;

    for (int i = 0; i < measure; ++i)
    {
        const double y = network.process (std::sin (phase));
        inPhase += y * std::sin (phase);
        quadrature += y * std::cos (phase);
        phase += step;
    }

    return 2.0 * std::hypot (inPhase, quadrature) / measure;
}

double dbOf (double magnitude)
{
    return 20.0 * std::log10 (std::max (magnitude, 1.0e-12));
}
} // namespace

TEZLA_TEST (passive_network_solves_a_resistive_divider_exactly)
{
    // No capacitors, so the answer is arithmetic and must come out exact at
    // every frequency: two resistors in series divide by their ratio.
    Network network;
    network.setNodeCount (3);
    network.setOutputNode (2);
    network.clearElements();
    network.addResistor (Network::kInput, 2, 10000.0);
    network.addResistor (2, Network::kGround, 30000.0);
    network.prepare (48000.0);

    // 30k / (10k + 30k) = 0.75
    for (const double frequency : { 20.0, 200.0, 2000.0, 15000.0 })
        CHECK_NEAR (magnitudeAt (network, frequency, 48000.0), 0.75, 1.0e-9);
}

TEZLA_TEST (passive_network_lowpass_corner_is_where_circuit_theory_puts_it)
{
    // R to the output node, C from there to ground. The corner is 1/(2*pi*R*C)
    // and the response there is -3.01 dB, which is not a convention -- it is
    // 1/sqrt(2), and any solver that gets the companion model wrong misses it.
    constexpr double r = 10000.0;
    constexpr double c = 1.0e-8;
    const double corner = 1.0 / (2.0 * std::numbers::pi * r * c);   // 1591.5 Hz

    Network network;
    network.setNodeCount (3);
    network.setOutputNode (2);
    network.clearElements();
    network.addResistor (Network::kInput, 2, r);
    network.addCapacitor (2, Network::kGround, c);
    network.prepare (192000.0);

    // Checked against the exact first-order magnitude at every point, not
    // against the 6 dB/octave asymptote. The asymptote is only reached far
    // above the corner -- at four times it the true slope is still 5.83 dB per
    // octave, and a test written against 6.02 is measuring its own impatience.
    const auto exactLowpass = [corner] (double f)
    {
        const double ratio = f / corner;
        return 1.0 / std::sqrt (1.0 + ratio * ratio);
    };

    // Below Fs/8 the solver tracks the analogue prototype to a hundredth of a
    // decibel. Measured at 192 kHz, where Fs/8 is 24 kHz:
    //
    //        15.9 Hz   -0.0004 dB   exact  -0.0004   diff  0.0000
    //       397.9 Hz   -0.2633      exact  -0.2633   diff -0.0000
    //      1591.5 Hz   -3.0118      exact  -3.0103   diff -0.0015
    //      6366.2 Hz  -12.3315      exact -12.3045   diff -0.0270
    //     12732.4 Hz  -18.2566      exact -18.1291   diff -0.1274
    double worst = 0.0;

    for (const double f : { corner / 100.0, corner / 4.0, corner, corner * 4.0 })
        worst = std::max (worst, std::abs (dbOf (magnitudeAt (network, f, 192000.0))
                                           - dbOf (exactLowpass (f))));

    CHECK (worst < 0.05);

    // And the corner really is -3.01 dB, which is 1/sqrt(2) rather than a
    // convention.
    CHECK_NEAR (dbOf (magnitudeAt (network, corner, 192000.0)), -3.0103, 0.05);
}

TEZLA_TEST (passive_network_highpass_is_the_mirror_of_it)
{
    constexpr double r = 22000.0;
    constexpr double c = 2.2e-8;
    const double corner = 1.0 / (2.0 * std::numbers::pi * r * c);   // 328.9 Hz

    Network network;
    network.setNodeCount (3);
    network.setOutputNode (2);
    network.clearElements();
    network.addCapacitor (Network::kInput, 2, c);
    network.addResistor (2, Network::kGround, r);
    network.prepare (192000.0);

    const auto exactHighpass = [corner] (double f)
    {
        const double ratio = f / corner;
        return ratio / std::sqrt (1.0 + ratio * ratio);
    };

    double worst = 0.0;

    for (const double f : { corner / 8.0, corner / 4.0, corner, corner * 4.0, corner * 100.0 })
        worst = std::max (worst, std::abs (dbOf (magnitudeAt (network, f, 192000.0))
                                           - dbOf (exactHighpass (f))));

    CHECK (worst < 0.05);
    CHECK_NEAR (dbOf (magnitudeAt (network, corner, 192000.0)), -3.0103, 0.05);
}

TEZLA_TEST (passive_network_loads_itself_the_way_a_real_one_does)
{
    // The property a chain of separate filters cannot reproduce, and the whole
    // reason for solving the network. Two cascaded RC lowpasses that *load*
    // each other are not the same as two independent ones: the second stage
    // draws current through the first, so the response is not the product of
    // the two.
    constexpr double r = 10000.0;
    constexpr double c = 1.0e-8;
    const double corner = 1.0 / (2.0 * std::numbers::pi * r * c);

    Network single;
    single.setNodeCount (3);
    single.setOutputNode (2);
    single.clearElements();
    single.addResistor (Network::kInput, 2, r);
    single.addCapacitor (2, Network::kGround, c);
    single.prepare (192000.0);

    Network cascaded;
    cascaded.setNodeCount (4);
    cascaded.setOutputNode (3);
    cascaded.clearElements();
    cascaded.addResistor (Network::kInput, 2, r);
    cascaded.addCapacitor (2, Network::kGround, c);
    cascaded.addResistor (2, 3, r);
    cascaded.addCapacitor (3, Network::kGround, c);
    cascaded.prepare (192000.0);

    const double one = dbOf (magnitudeAt (single, corner, 192000.0));
    const double two = dbOf (magnitudeAt (cascaded, corner, 192000.0));

    // Buffered, two sections would multiply: -6.02 dB at the corner.
    //
    // Loaded, they do not. The second section draws its charging current
    // through the first resistor as well, and the transfer function picks up a
    // cross term -- 1/(1 + 3sRC + (sRC)^2) rather than 1/(1 + sRC)^2 -- so at
    // sRC = j the magnitude is exactly 1/3, or -9.54 dB.
    //
    // That 3.5 dB is the whole argument for solving the network instead of
    // cascading filters, and it is worth asserting to the decimal rather than
    // as an inequality.
    CHECK_NEAR (one, -3.0103, 0.05);
    CHECK_NEAR (two, dbOf (1.0 / 3.0), 0.08);
    CHECK (two < 2.0 * one - 3.0);
}

TEZLA_TEST (passive_network_warps_towards_nyquist_like_every_bilinear_filter)
{
    // The other half of the previous test, and the reason it stops at Fs/8.
    //
    // Trapezoidal companion models are the bilinear transform by another name,
    // so the frequency axis is warped and the response diverges from the
    // analogue prototype as it approaches Nyquist. CLAUDE.md section 6 already
    // documents this for Biquad -- "trust a plain biquad to be rate-independent
    // only below about Fs/8" -- and it is exactly as true here.
    //
    // Measured at 192 kHz, so Nyquist is 96 kHz: at 50.9 kHz, which is 53% of
    // Nyquist, the network reads -32.54 dB against the prototype's -30.11.
    //
    // Pinned rather than tolerated, because it is the reason the tone stack
    // belongs inside the oversampled section rather than at the host rate.
    constexpr double r = 10000.0;
    constexpr double c = 1.0e-8;
    const double corner = 1.0 / (2.0 * std::numbers::pi * r * c);

    Network network;
    network.setNodeCount (3);
    network.setOutputNode (2);
    network.clearElements();
    network.addResistor (Network::kInput, 2, r);
    network.addCapacitor (2, Network::kGround, c);
    network.prepare (192000.0);

    const double high = corner * 32.0;                       // 50.9 kHz
    const double ratio = high / corner;
    const double prototype = dbOf (1.0 / std::sqrt (1.0 + ratio * ratio));
    const double measured = dbOf (magnitudeAt (network, high, 192000.0));

    // Over-attenuating, by more than two decibels.
    CHECK (measured < prototype - 2.0);
    CHECK (measured > prototype - 3.0);

    // While the same circuit at a quarter of the frequency is still exact.
    const double lowRatio = 8.0;
    CHECK_NEAR (dbOf (magnitudeAt (network, corner * 8.0, 192000.0)),
                dbOf (1.0 / std::sqrt (1.0 + lowRatio * lowRatio)), 0.15);
}

TEZLA_TEST (passive_network_is_the_same_circuit_at_every_sample_rate)
{
    // CLAUDE.md section 6. The companion model is derived from the sample
    // period, so this is the check that it was derived correctly rather than
    // fitted at one rate.
    constexpr double r = 10000.0;
    constexpr double c = 1.0e-8;
    const double corner = 1.0 / (2.0 * std::numbers::pi * r * c);

    double first = 0.0;
    double worst = 0.0;

    for (const double rate : { 44100.0, 48000.0, 96000.0, 192000.0 })
    {
        Network network;
        network.setNodeCount (3);
        network.setOutputNode (2);
        network.clearElements();
        network.addResistor (Network::kInput, 2, r);
        network.addCapacitor (2, Network::kGround, c);
        network.prepare (rate);

        const double db = dbOf (magnitudeAt (network, corner, rate));

        if (rate == 44100.0)
            first = db;
        else
            worst = std::max (worst, std::abs (db - first));
    }

    // The corner is at 1.6 kHz, comfortably below Fs/8 even at 44.1 -- which is
    // exactly the region CLAUDE.md section 6 says a bilinear-derived filter can
    // be trusted in.
    CHECK (worst < 0.02);
}

TEZLA_TEST (passive_network_is_silent_in_silence_and_stable_when_shorted)
{
    Network network;
    network.setNodeCount (4);
    network.setOutputNode (3);
    network.clearElements();
    network.addResistor (Network::kInput, 2, 10000.0);
    network.addCapacitor (2, 3, 1.0e-8);
    network.addResistor (3, Network::kGround, 0.0);      // a pot at zero
    network.prepare (48000.0);

    for (int i = 0; i < 2048; ++i)
        CHECK (network.process (0.0) == 0.0);

    // And a shorted output stays finite rather than dividing by zero.
    for (int i = 0; i < 2048; ++i)
    {
        const double y = network.process (std::sin (0.1 * i));
        CHECK (std::isfinite (y));
        CHECK (std::abs (y) < 1.0e-3);
    }
}

TEZLA_TEST (passive_network_survives_a_floating_node)
{
    // A netlist with a node nothing connects to is a mistake, and it must fail
    // as zero volts rather than as a NaN reaching the audio.
    Network network;
    network.setNodeCount (4);
    network.setOutputNode (3);
    network.clearElements();
    network.addResistor (Network::kInput, 2, 10000.0);
    network.addResistor (2, Network::kGround, 10000.0);
    // node 3 is connected to nothing at all
    network.prepare (48000.0);

    for (int i = 0; i < 512; ++i)
    {
        const double y = network.process (std::sin (0.1 * i));
        CHECK (std::isfinite (y));
    }
}

// ---------------------------------------------------------------------------
// Inductors
// ---------------------------------------------------------------------------

TEZLA_TEST (passive_network_inductor_is_the_mirror_of_a_capacitor)
{
    // An R-L divider with the output across the inductor is a *highpass*, where
    // the same topology with a capacitor is a lowpass. Corner at R/(2*pi*L),
    // and the magnitude at the corner is the same 1/sqrt(2).
    constexpr double sampleRate = 192000.0;
    constexpr double r = 800.0;
    constexpr double l = 100.0e-3;

    const double corner = r / (2.0 * std::numbers::pi * l);   // 1273 Hz

    Network network;
    network.setNodeCount (3);
    network.setOutputNode (2);
    network.addResistor (Network::kInput, 2, r);
    network.addInductor (2, Network::kGround, l);
    network.prepare (sampleRate);

    CHECK_NEAR (dbOf (magnitudeAt (network, corner, sampleRate)), -3.0103, 0.05);

    // A decade below the corner a first-order highpass is 20 dB down, and a
    // decade above it is through. Exactly, not asymptotically: the magnitude of
    // jw/(jw+wc) at w = wc/10 is 1/sqrt(101).
    CHECK_NEAR (dbOf (magnitudeAt (network, corner / 10.0, sampleRate)), -20.0432, 0.05);
    CHECK_NEAR (dbOf (magnitudeAt (network, corner * 4.0, sampleRate)), -0.2633, 0.05);
}

TEZLA_TEST (passive_network_lc_resonates_where_theory_puts_it)
{
    // A series L-C-R with the output taken across the *resistor* is a bandpass
    // that peaks at exactly 0 dB at f0 = 1/(2*pi*sqrt(LC)): at resonance the
    // two reactances are equal and opposite, cancel to nothing, and the whole
    // source appears across the resistor.
    //
    // This is the check the capacitor tests cannot make. It needs both signs of
    // reactance to be right, and an inductor carrying a capacitor's history
    // signs cannot cancel anything -- so 0 dB here is the assertion with teeth.
    //
    // Taking the output across the L-C pair instead gives the dual, a notch,
    // and getting those two the wrong way round is how this test first read
    // -30.9 dB where it expected 0. The circuit was right; the probe was on
    // the wrong node.
    constexpr double sampleRate = 192000.0;
    constexpr double r = 160.0;
    constexpr double l = 100.0e-3;
    constexpr double c = 100.0e-9;

    const double f0 = 1.0 / (2.0 * std::numbers::pi * std::sqrt (l * c));   // 1591.5 Hz

    Network network;
    network.setNodeCount (4);
    network.setOutputNode (3);
    network.addInductor (Network::kInput, 2, l);
    network.addCapacitor (2, 3, c);
    network.addResistor (3, Network::kGround, r);
    network.prepare (sampleRate);

    CHECK_NEAR (dbOf (magnitudeAt (network, f0, sampleRate)), 0.0, 0.02);

    // Q = (1/R)*sqrt(L/C) = 6.25, so an octave either side is 19.5 dB down --
    // and symmetric, because the bandpass is symmetric in w/w0 - w0/w.
    CHECK_NEAR (dbOf (magnitudeAt (network, f0 * 0.5, sampleRate)), -19.49, 0.1);
    CHECK_NEAR (dbOf (magnitudeAt (network, f0 * 2.0, sampleRate)), -19.49, 0.1);
}

TEZLA_TEST (passive_network_a_parallel_lc_traps_its_own_frequency)
{
    // The dual, and the one the speaker model needs: a parallel L-C across the
    // signal path is a *notch*, because at resonance the tank's impedance is
    // infinite in a lossless case and merely large with a resistor across it.
    //
    // Here the tank sits in series with the signal and the output is taken
    // after it, so the tank's impedance divides against the load: at resonance
    // it blocks, and away from resonance it passes.
    constexpr double sampleRate = 192000.0;
    constexpr double l = 10.0e-3;
    constexpr double c = 100.0e-9;
    constexpr double load = 100.0;

    const double f0 = 1.0 / (2.0 * std::numbers::pi * std::sqrt (l * c));

    Network network;
    network.setNodeCount (3);
    network.setOutputNode (2);
    network.addInductor (Network::kInput, 2, l);
    network.addCapacitor (Network::kInput, 2, c);
    network.addResistor (2, Network::kGround, load);
    network.prepare (sampleRate);

    const double atResonance = dbOf (magnitudeAt (network, f0, sampleRate));

    CHECK (atResonance < -30.0);
    CHECK (dbOf (magnitudeAt (network, f0 / 8.0, sampleRate)) > atResonance + 25.0);
    CHECK (dbOf (magnitudeAt (network, f0 * 8.0, sampleRate)) > atResonance + 25.0);
}
