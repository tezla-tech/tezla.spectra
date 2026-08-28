// Copyright (c) 2026 The Tezla <thetezla@proton.me>
// Created by The Tezla -- https://github.com/wingit33/tezla.tech
// Music: https://soundcloud.com/thetezla | https://thetezla.bandcamp.com
// Built with development assistance from Claude (Anthropic).
// SPDX-License-Identifier: AGPL-3.0-only
// GNU AGPLv3 (see LICENSE), plus NOTICE.md's attribution term. Keep intact.

#pragma once

// What the amplifier actually delivers, once a loudspeaker is connected to it.
//
// ---------------------------------------------------------------------------
// The part almost every amp simulation leaves out
// ---------------------------------------------------------------------------
//
// A loudspeaker is not an 8 ohm resistor. It is 8 ohms at one frequency and
// nothing like it anywhere else: a moving-coil driver has a mechanical
// resonance around 75 Hz where its electrical impedance rises to eighty or a
// hundred ohms, a minimum a little above its DC resistance around 400 Hz, and
// a steady climb above that as the voice coil's inductance takes over.
//
// A power amplifier with a low output impedance -- any solid-state one -- holds
// its output voltage regardless, and none of that curve is audible. A valve
// amplifier does not. Its output impedance is a large fraction of the load, so
// the speaker and the amplifier form a voltage divider that the speaker's own
// impedance curve shapes. Measured here, against the same amplifier at
// 1 kHz, with the driver model below:
//
//     damping factor      0.5      1.0      3.0     20.0
//        41 Hz          +2.19    +1.69    +0.83    +0.15
//        75 Hz          +7.67    +4.81    +1.92    +0.31
//       400 Hz          -1.63    -1.35    -0.77    -0.16
//      3000 Hz          +4.48    +3.17    +1.40    +0.24
//      8000 Hz          +6.68    +4.35    +1.79    +0.29
//
// Nine decibels of tone shaping, with a bass lift at resonance, a scoop through
// the low mids and a rising presence -- and the only thing that changes between
// the columns is how stiffly the amplifier holds its voltage. That is a large
// part of what "amp and cab interaction" means, and it is why a valve amp into
// a resistive load box sounds wrong in a way no cabinet impulse response fixes:
// the impulse response is measured at the speaker terminals, and this is what
// decides what arrives there.
//
// ---------------------------------------------------------------------------
// The model
// ---------------------------------------------------------------------------
//
// The standard electrical equivalent circuit of a moving-coil driver, which is
// textbook Thiele-Small and has been since the early seventies:
//
//   amp --Rout-- terminals --Re-- L1 --+-- R2 --+-- Res --+-- ground
//                                      |        |         |
//                                      +-- L2 --+  Lces --+
//                                                         |
//                                                  Cmes --+
//
//   Re            voice coil DC resistance
//   L1, L2, R2    voice coil inductance, as a two-element semi-inductance
//   Res, Lces,    the mechanical resonance, reflected into the electrical
//   Cmes          domain: mass becomes capacitance, compliance becomes
//                 inductance, and mechanical loss becomes resistance
//
// The motional branch's element values come from the four parameters a driver
// is actually specified by -- fs, Qms, Qes and Re:
//
//     Res  = Re * Qms/Qes         the height of the impedance peak
//     Lces = Res / (ws * Qms)
//     Cmes = 1 / (ws^2 * Lces)
//
// **The voice coil is not a plain inductor**, and modelling it as one is the
// second thing sims get wrong. Eddy currents in the pole piece make its
// impedance rise as roughly f^0.6 rather than f^1, with a phase near 55 degrees
// instead of 90 -- Leach's "lossy semi-inductance" (JAES 2002). A single
// inductor overstates the top by several decibels across the guitar band. The
// two-element L1 + (R2 || L2) approximation used here measures a slope of
// **0.542** between 400 Hz and 5 kHz against the fractional model's 0.6, and a
// phase of +55 degrees where a plain inductor would give +90.
//
// The resulting impedance, with the defaults below:
//
//        20 Hz   7.35 ohms      400 Hz   6.62 ohms      2 kHz   12.78 ohms
//        75 Hz  93.17 ohms        1 kHz  8.25 ohms      5 kHz   26.01 ohms
//       100 Hz  20.07 ohms                             10 kHz   40.02 ohms
//
// which is a 12 inch guitar speaker to the eye: a tall peak at resonance, a
// minimum a shade above Re, and a climb to four or five times nominal at the
// top of the band.
//
// ---------------------------------------------------------------------------
// Where this belongs in the chain, and why it must be oversampled
// ---------------------------------------------------------------------------
//
// Straight after the power amplifier and before the cabinet. It converts the
// amplifier's open-circuit voltage into the voltage that actually appears at
// the speaker terminals; the cabinet then converts that into what a microphone
// in front of it hears.
//
// It is linear, so it adds nothing and cannot alias. It runs oversampled inside
// Anvil because the valves either side of it have to, not because it needs to
// -- and that is worth stating precisely, because the reflex from CLAUDE.md
// section 6 is that a network with an 8 kHz corner must be badly warped at a
// 48 kHz host rate, where 8 kHz is 34% of Nyquist.
//
// Measured, against the exact transfer function, it is not:
//
//        Hz      44.1k      48k      96k     192k     exact
//        41     -6.859   -6.859   -6.859   -6.859    -6.859
//        75     -1.377   -1.377   -1.377   -1.377    -1.377
//       220     -9.868   -9.868   -9.868   -9.868    -9.868
//      1000     -9.039   -9.040   -9.044   -9.044    -9.045
//      8000     -2.236   -2.250   -2.336   -2.357    -2.364
//     16000     -1.015   -1.121   -1.554   -1.629    -1.653
//
// Identical to three decimals below 220 Hz at every rate, 0.13 dB out at 8 kHz
// at 48 kHz, and only at 16 kHz does it reach 0.64 dB. The reason is that
// warping distorts the frequency axis, and this network has no sharp feature
// up there to distort: the semi-inductance is a gentle shelf, so moving the
// axis under it barely moves the curve. A resonant filter at the same corner
// would be far worse -- which is what the biquad table in
// docs/DSP-REFERENCES.md measured.
//
// The first draft of this comment claimed 1.9 dB at 8 kHz, which was a guess
// written down as though it were a measurement. It was out by a factor of
// fifteen, and the test that asserted it is what found that.

#include <algorithm>
#include <cmath>
#include <complex>

#include "PassiveNetwork.hpp"

namespace tezla::dsp {

/// A moving-coil driver, in the parameters a datasheet quotes.
struct DriverParameters
{
    /// Nominal impedance, which is what a damping factor is quoted against.
    double nominalOhms { 8.0 };

    /// Voice coil DC resistance. Always below nominal -- about 6.5 for an
    /// 8 ohm driver, 13 for a 16 ohm one.
    double reOhms { 6.5 };

    /// Free-air resonance. A vintage-voiced 12 inch guitar driver sits around
    /// 75 Hz; a modern one is nearer 100; a bass driver is far lower.
    double resonanceHz { 75.0 };

    /// Mechanical Q at resonance. Guitar drivers are lightly damped -- doped
    /// cloth surrounds and no attempt at flatness -- so this is high, and it is
    /// what makes the impedance peak tall.
    double qms { 8.0 };

    /// Electrical Q at resonance. Together with Qms it sets both the height of
    /// the impedance peak and the damping of the acoustic low end.
    double qes { 0.6 };

    /// Voice coil inductance at low frequency, henries. Sets how far the top
    /// end climbs: bigger coil, more presence lift into a valve amplifier and
    /// less treble out of the driver itself.
    double voiceCoilHenries { 0.85e-3 };

    /// Where the eddy-current losses take over and the rise flattens from f^1
    /// towards f^0.6. Above this the coil looks more resistive than inductive.
    double voiceCoilCornerHz { 8100.0 };

    /// Total mechanical Q. Sets how damped the acoustic low end is, and is the
    /// number a driver is usually chosen by.
    [[nodiscard]] double qts() const noexcept
    {
        return (qms * qes) / std::max (qms + qes, 1.0e-9);
    }

    [[nodiscard]] bool operator== (const DriverParameters&) const = default;
};

/// The amplifier's grip on the speaker, and the voltage that results.
struct SpeakerLoadParameters
{
    DriverParameters driver;

    /// Nominal load divided by the amplifier's output impedance.
    ///
    /// Below 1 is a valve amplifier with no global feedback, where the output
    /// impedance exceeds the speaker's; 2 to 4 is one with a modest loop; 20
    /// and up is solid state, and flat to within a third of a decibel.
    double dampingFactor { 1.0 };

    /// Bit-exact passthrough. Not "damping factor at its maximum" -- a network
    /// that is nearly transparent is not transparent, and CLAUDE.md section 7
    /// asks for the difference.
    bool bypassed { false };

    [[nodiscard]] bool operator== (const SpeakerLoadParameters&) const = default;
};

class SpeakerLoad
{
public:
    /// How the voice coil's inductance splits between the plain series part and
    /// the lossy part. Fitted so the pair tracks f^0.6 across the guitar band;
    /// holding the ratio fixed is what lets one control scale the whole rise
    /// without changing its shape.
    static constexpr double kSeriesInductanceFraction = 0.353;

    /// Damping factors below this are refused. At 0.05 the amplifier's output
    /// impedance is twenty times the load and the "amplifier" is a current
    /// source, which is a real thing to build and not a guitar amplifier.
    static constexpr double kMinimumDampingFactor = 0.05;

    void prepare (double sampleRate)
    {
        sampleRate_ = sampleRate > 0.0 ? sampleRate : 48000.0;
        build();
        network_.prepare (sampleRate_);
    }

    void reset() noexcept { network_.reset(); }

    void setParameters (const SpeakerLoadParameters& parameters) noexcept
    {
        if (parameters == parameters_)
            return;

        parameters_ = parameters;
        build();
        network_.factorise();
    }

    [[nodiscard]] const SpeakerLoadParameters& getParameters() const noexcept
    {
        return parameters_;
    }

    [[nodiscard]] double process (double x) noexcept
    {
        return parameters_.bypassed ? x : network_.process (x);
    }

    /// The driver's electrical impedance at one frequency, from the circuit
    /// equations rather than from the solver.
    ///
    /// Exists so a test can check the netlist against the algebra it is meant
    /// to embody, and so a panel can draw the curve without running audio
    /// through it.
    [[nodiscard]] static std::complex<double> impedance (const DriverParameters& driver,
                                                         double frequency) noexcept
    {
        constexpr double twoPi = 6.283185307179586;

        const std::complex<double> s { 0.0, twoPi * frequency };

        const double re = std::max (driver.reOhms, 1.0e-3);
        const double ws = twoPi * std::max (driver.resonanceHz, 1.0e-3);
        const double qms = std::max (driver.qms, 1.0e-3);
        const double qes = std::max (driver.qes, 1.0e-3);

        const double res = re * qms / qes;
        const double lces = res / (ws * qms);
        const double cmes = 1.0 / (ws * ws * lces);

        const auto motional = 1.0 / (1.0 / res + s * cmes + 1.0 / (s * lces));

        const double l1 = seriesInductance (driver);
        const double l2 = lossyInductance (driver);
        const double r2 = lossyResistance (driver);

        const auto coil = s * l1 + (s * l2 * r2) / (r2 + s * l2);

        return re + coil + motional;
    }

    /// The fraction of the amplifier's open-circuit voltage that reaches the
    /// speaker terminals, at one frequency. Z / (Z + Rout).
    [[nodiscard]] static std::complex<double> response (const SpeakerLoadParameters& parameters,
                                                        double frequency) noexcept
    {
        if (parameters.bypassed)
            return { 1.0, 0.0 };

        const auto z = impedance (parameters.driver, frequency);
        return z / (z + outputOhms (parameters));
    }

    /// The amplifier's output impedance, in ohms.
    [[nodiscard]] static double outputOhms (const SpeakerLoadParameters& parameters) noexcept
    {
        const double damping = std::max (parameters.dampingFactor, kMinimumDampingFactor);
        return std::max (parameters.driver.nominalOhms, 1.0e-3) / damping;
    }

private:
    static double seriesInductance (const DriverParameters& driver) noexcept
    {
        return std::max (driver.voiceCoilHenries, 1.0e-9) * kSeriesInductanceFraction;
    }

    static double lossyInductance (const DriverParameters& driver) noexcept
    {
        return std::max (driver.voiceCoilHenries, 1.0e-9) * (1.0 - kSeriesInductanceFraction);
    }

    static double lossyResistance (const DriverParameters& driver) noexcept
    {
        constexpr double twoPi = 6.283185307179586;
        return twoPi * std::max (driver.voiceCoilCornerHz, 1.0) * lossyInductance (driver);
    }

    /// Node 2 is the speaker terminals, which is what the cabinet is fed.
    static constexpr std::size_t kTerminals = 2;
    static constexpr std::size_t kAfterRe = 3;
    static constexpr std::size_t kAfterSeriesCoil = 4;
    static constexpr std::size_t kMotional = 5;

    void build() noexcept
    {
        constexpr double twoPi = 6.283185307179586;

        const auto& driver = parameters_.driver;

        const double re = std::max (driver.reOhms, 1.0e-3);
        const double ws = twoPi * std::max (driver.resonanceHz, 1.0e-3);
        const double qms = std::max (driver.qms, 1.0e-3);
        const double qes = std::max (driver.qes, 1.0e-3);

        const double res = re * qms / qes;
        const double lces = res / (ws * qms);
        const double cmes = 1.0 / (ws * ws * lces);

        network_.clearElements();
        network_.setNodeCount (6);
        network_.setOutputNode (kTerminals);

        network_.addResistor (PassiveNetwork<>::kInput, kTerminals, outputOhms (parameters_));
        network_.addResistor (kTerminals, kAfterRe, re);
        network_.addResistor (kAfterSeriesCoil, kMotional, lossyResistance (driver));
        network_.addResistor (kMotional, PassiveNetwork<>::kGround, res);

        network_.addInductor (kAfterRe, kAfterSeriesCoil, seriesInductance (driver));
        network_.addInductor (kAfterSeriesCoil, kMotional, lossyInductance (driver));
        network_.addInductor (kMotional, PassiveNetwork<>::kGround, lces);

        network_.addCapacitor (kMotional, PassiveNetwork<>::kGround, cmes);
    }

    double sampleRate_ { 48000.0 };

    SpeakerLoadParameters parameters_;
    PassiveNetwork<> network_;
};

} // namespace tezla::dsp
