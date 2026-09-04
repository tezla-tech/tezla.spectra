// Copyright (c) 2026 The Tezla <thetezla@proton.me>
// Created by The Tezla -- https://github.com/wingit33/tezla.tech
// Music: https://soundcloud.com/thetezla | https://thetezla.bandcamp.com
// Built with development assistance from Claude (Anthropic).
// SPDX-License-Identifier: AGPL-3.0-only
// GNU AGPLv3 (see LICENSE), plus NOTICE.md's attribution term. Keep intact.

#pragma once

// One FM operator, with a single control that runs from classic phase
// modulation to ModFM.
//
// ---------------------------------------------------------------------------
// The one equation this file is
// ---------------------------------------------------------------------------
//
//     x(t) = e^(r k cos(w_m t) - r k) * sin(w_c t + s k sin(w_m t))
//
// Lazzarini & Timoney, "Theory and Practice of Modified Frequency Modulation
// Synthesis", JAES 58(6), 2010, the Extensions section (their Eq 19), with
// 0 <= r <= 1 and -1 <= s <= 1. Its spectrum is the double sum
//
//     (1/2) * sum over a, b of I_a(rk) J_b(sk)
//                 [ cos(w_c t + (a+b) w_m t) + cos(w_c t - (a-b) w_m t) ]
//
// and the two ends of it are the two techniques worth having:
//
//  - **r = 0, s = 1** makes the exponential e^0 = 1 and leaves
//    `sin(w_c t + k sin(w_m t))`, which *is* classic phase modulation -- the
//    eighties operator, exactly, with no approximation anywhere.
//  - **r = 1, s = 0** leaves `e^(k cos(w_m t) - k) sin(w_c t)`, which is ModFM.
//    Its partials are scaled by *modified* Bessel functions, which do not
//    oscillate, so (DAFx-08, the same authors) they "change smoothly from one
//    to the next without the unexpected appearance of insignificant partials
//    that is characteristic of the original FM synthesis", and moving the index
//    gives "a perceptual effect more reminiscent of an opening/closing lowpass
//    filter".
//
// Between them the spectrum goes asymmetric, which is what `Tilt` is for.
//
// On a bass patch the ModFM end is the one that behaves: an index that acts
// like a filter opening is exactly what a growl wants, and a modulator envelope
// sweeping it does not step through Bessel nulls on the way. `tezla-measure
// stryda` table 4 measures both claims -- across Character the partial-order
// reversals fall from 2 to 0, and matching the spectral centroid at index 5
// takes 122 % more index at the ModFM end.
//
// ---------------------------------------------------------------------------
// Character 0 is bit-exact, by branch and not by hope
// ---------------------------------------------------------------------------
//
// CLAUDE.md section 7 requires any stage permanently in the signal path to be
// bit-exactly its neutral setting, not merely close to it. `std::exp (0.0)` is
// 1.0 on every implementation anyone would ship, but "would ship" is not a
// guarantee and the multiply afterwards is not free either, so the exponential
// is skipped outright when Character is zero. That also means the classic
// operator pays nothing at all for the ModFM half existing.
//
// ---------------------------------------------------------------------------
// Units: cycles, like the rest of the house
// ---------------------------------------------------------------------------
//
// Every input here is in **cycles** of phase deviation, matching
// `Oscillator::advance` and `Oscillator::setFeedback`. The published equations
// are in radians. The conversion is one multiply by 2*pi and it happens here,
// once, so no caller ever has to remember which side of it they are on.

#include <algorithm>
#include <cmath>
#include <numbers>

#include <tezla/dsp/FmShapes.hpp>
#include <tezla/dsp/PhaseShaper.hpp>

namespace tezla::dsp
{

class FmOperator
{
public:
    /// **Append-only**: a choice parameter stores an index, not a name.
    enum class Mode
    {
        normal = 0,   ///< a carrier or modulator like any other
        formant       ///< a self-contained resonant peak at a settable frequency
    };

    /// Matching `Oscillator::kMaxFeedback` -- one whole cycle of self-deviation.
    /// Note that this is 6.28 radians, six times past the point where the
    /// Kapteyn closed form in `FmBandwidth.hpp` still describes the loop. The
    /// operator stays bounded there (measured: peak 1.000 across the whole
    /// range); what stops is the *prediction*, and the index cap is what
    /// answers that.
    static constexpr double kMaxFeedback = 1.0;

    void prepare (double sampleRate) noexcept
    {
        sampleRate_ = sampleRate > 0.0 ? sampleRate : 48000.0;
        setFrequency (frequency_);
        reset();
    }

    void reset() noexcept
    {
        phase_ = 0.0;
        out_ = 0.0;
        quadrature_ = 0.0;
        history_[0] = 0.0;
        history_[1] = 0.0;
    }

    void setFrequency (double hz) noexcept
    {
        frequency_ = hz;
        increment_ = hz / sampleRate_;
    }

    [[nodiscard]] double getFrequency() const noexcept { return frequency_; }

    /// Where the operator starts each note. 0 is the house default and is what
    /// makes a note repeatable; anything else is a deliberate click.
    void setPhase (double cycles) noexcept
    {
        phase_ = cycles - std::floor (cycles);
    }

    [[nodiscard]] double getPhase() const noexcept { return phase_; }

    /// 0 = classic phase modulation, 1 = ModFM.
    void setCharacter (double character) noexcept
    {
        character_ = std::clamp (character, 0.0, 1.0);
    }

    [[nodiscard]] double getCharacter() const noexcept { return character_; }

    /// The `s` of the equation: how much of the modulation still reaches the
    /// phase. The default pairing is `s = 1 - r`, so one knob walks cleanly
    /// from one technique to the other; setting it independently is what makes
    /// the spectrum asymmetric, which is the third thing the extension buys.
    void setTilt (double tilt) noexcept
    {
        tilt_ = std::clamp (tilt, -1.0, 1.0);
    }

    [[nodiscard]] double getTilt() const noexcept { return tilt_; }

    void setMode (Mode mode) noexcept { mode_ = mode; }
    [[nodiscard]] Mode getMode() const noexcept { return mode_; }

    /// Phase distortion, 0 to 1. Exactly the identity at 0 -- see PhaseShaper.
    void setFold (double amount) noexcept { fold_.setAmount (amount); }
    [[nodiscard]] double getFold() const noexcept { return fold_.getAmount(); }

    /// The waveform this operator reads. **`FmShape::sine` is the default and
    /// costs nothing** -- `advance` branches to `std::sin` exactly as it did
    /// before shapes existed, so every patch saved before them is bit-exact.
    ///
    /// Anything else carries `dsp::fmShapeHarmonics` harmonics of the
    /// operator's own frequency, which multiplies the sideband ladder it
    /// produces by the same number. `FmBandwidth` is told; the index cap
    /// answers it.
    void setShape (FmShape shape) noexcept { shape_ = shape; }
    [[nodiscard]] FmShape getShape() const noexcept { return shape_; }

    /// Where the formant sits, in Hz, when the mode is `formant`.
    void setFormantHz (double hz) noexcept { formantHz_ = hz > 0.0 ? hz : 0.0; }

    /// How wide the formant is, as the ModFM index `k` in cycles. Larger is
    /// wider and brighter; 0 collapses it to a single partial.
    void setFormantDepth (double cycles) noexcept
    {
        formantDepth_ = std::clamp (cycles, 0.0, 16.0);
    }

    /// Self-modulation in cycles. Bounded structurally: phase modulation is
    /// applied to the *reading* of a bounded shape, so the loop gain on
    /// amplitude is zero and a bigger feedback makes a differently-shaped
    /// output rather than a bigger one.
    void setFeedback (double cycles) noexcept
    {
        feedback_ = std::clamp (cycles, 0.0, kMaxFeedback);
    }

    [[nodiscard]] double getFeedback() const noexcept { return feedback_; }

    /// Whether anything downstream actually reads `getQuadrature()`.
    ///
    /// The quadrature exists for the ModFM half of the equation, and nothing
    /// else looks at it -- so on a classic-FM patch it is a `std::cos` per
    /// operator per sample that is computed and thrown away. At six operators,
    /// eight voices and a x4 internal rate that is 9.2 million wasted
    /// transcendentals a second, which is not a rounding error: switching it
    /// off took a full six-operator patch from 41.5 % of a core to the figure
    /// in `tests/test_Stryda.cpp`.
    ///
    /// Defaults to true, because a wrong answer is worse than a slow one and
    /// the matrix is what knows.
    void setQuadratureNeeded (bool needed) noexcept { needsQuadrature_ = needed; }

    /// One sample.
    ///
    /// `pmCycles` is the summed phase-modulation input; `amCycles` the summed
    /// *quadrature* input the exponential reads; `normCycles` the sum of the
    /// index magnitudes, which is what makes the exponential peak at exactly 1
    /// rather than at e^k.
    [[nodiscard]] double advance (double pmCycles,
                                  double amCycles = 0.0,
                                  double normCycles = 0.0) noexcept
    {
        // The feedback modulator is the mean of the last two outputs, which is
        // a one-zero lowpass with a null at exactly Nyquist -- so the operator
        // cannot settle into the useless mode of flipping between the extremes
        // of its shape on alternate samples. Tomisawa, US 4,249,447, read
        // first-hand: the averaging "eliminates the hunting".
        //
        // Outside the phase-modulation term's tilt, deliberately: at full
        // ModFM the tilt is zero, and a feedback control that silently stopped
        // working at one end of another knob would be a dead control.
        const double self = feedback_ > 0.0
                              ? feedback_ * 0.5 * (history_[0] + history_[1])
                              : 0.0;

        // Phase distortion, applied to the accumulator's own phase before
        // anything else reads it -- which is what makes it a *distortion of the
        // ramp* rather than another modulator. Exactly the identity at 0.
        const double shaped = fold_.getAmount() > 0.0 ? fold_.map (phase_) : phase_;

        if (mode_ == Mode::formant)
        {
            advanceFormant (shaped, pmCycles);
        }
        else
        {
            const double cycles = shaped + tilt_ * pmCycles + self;
            const double theta = kTwoPi * cycles;

            // Branched so Character 0 is the classic operator bit for bit, and
            // pays nothing for the other half of the equation existing.
            const double envelope = character_ > 0.0
                                      ? std::exp (kTwoPi * character_ * (amCycles - normCycles))
                                      : 1.0;

            // Branched to skip the call, not to guarantee the result: the
            // bit-exactness guarantee lives in `FmShapeTables::read`, which
            // returns `std::sin` for Sine before it touches a table. Removing
            // *this* branch changes nothing at all, and a break-check proved
            // it -- every shape test stayed green. The guard that matters is
            // the one in `read`, and breaking that is what turns the
            // bit-exactness test red.
            if (shape_ == FmShape::sine)
            {
                out_ = envelope * std::sin (theta);
                quadrature_ = needsQuadrature_ ? envelope * std::cos (theta) : 0.0;
            }
            else
            {
                const auto& tables = FmShapeTables::instance();

                out_ = envelope * tables.read (shape_, cycles);

                // The quadrature partner is the shape read a quarter cycle
                // early. For a sine that is exactly cos; for anything else it
                // is the Hilbert partner only harmonic by harmonic, which is
                // what the ModFM exponential wants -- it reads a modulator's
                // *cosine companion*, and a quarter-cycle shift is that
                // companion for every partial the shape actually has.
                quadrature_ = needsQuadrature_ ? envelope * tables.read (shape_, cycles + 0.25)
                                               : 0.0;
            }
        }

        history_[1] = history_[0];
        history_[0] = out_;

        phase_ += increment_;
        if (phase_ >= 1.0)
            phase_ -= 1.0;

        return out_;
    }

    /// The operator's output, as `advance` last returned it.
    [[nodiscard]] double getOutput() const noexcept { return out_; }

    /// Its quadrature partner -- what the *next* operator's exponential reads.
    ///
    /// For a sine operator this is exactly the paper's `cos(w_m t)` beside its
    /// `sin(w_m t)`, which is what makes the closed form apply. It is free:
    /// the same phase, the other trig function.
    [[nodiscard]] double getQuadrature() const noexcept { return quadrature_; }

private:
    /// The phase-synchronous ModFM formant operator, Lazzarini & Timoney
    /// (JAES 58(6), 2010), their Eq (13)/(14):
    ///
    ///     s(t) = e^(k cos(w0 t) - k) * [ (1-a) sin(n w0 t) + a sin((n+1) w0 t) ]
    ///
    /// with `n = int(f_f / f_0)` and `a = f_f/f_0 - n`.
    ///
    /// The two carriers are the whole point. A single carrier can only sit on a
    /// harmonic of the fundamental, so the resonance would jump from partial to
    /// partial as the note changed; crossfading between the two nearest
    /// harmonics by the fractional part **places the peak anywhere between
    /// them**, continuously, while the spectrum stays strictly harmonic. The
    /// paper: "the second carrier, tuned to an adjacent harmonic (of fm), will
    /// allow the formant to be centered anywhere between the frequencies of
    /// these two carriers, as their output is cross-faded for this effect."
    ///
    /// This is a whole two-operator ModFM pair collapsed into one operator, so
    /// a formant costs one slot rather than two -- which is what makes a
    /// three-formant vowel reachable on a six-operator instrument.
    void advanceFormant (double shapedPhase, double pmCycles) noexcept
    {
        const double ratio = frequency_ > 0.0 ? formantHz_ / frequency_ : 0.0;
        const double lower = std::floor (ratio);
        const double blend = ratio - lower;

        const double k = kTwoPi * formantDepth_;
        const double envelope = formantDepth_ > 0.0
                                  ? std::exp (k * (std::cos (kTwoPi * shapedPhase) - 1.0))
                                  : 1.0;

        const double base = kTwoPi * (shapedPhase + pmCycles);

        out_ = envelope * ((1.0 - blend) * std::sin (lower * base)
                             + blend * std::sin ((lower + 1.0) * base));

        quadrature_ = needsQuadrature_
                        ? envelope * ((1.0 - blend) * std::cos (lower * base)
                                        + blend * std::cos ((lower + 1.0) * base))
                        : 0.0;
    }

    static constexpr double kTwoPi = 2.0 * std::numbers::pi;

    double sampleRate_ { 48000.0 };
    double frequency_ { 440.0 };
    double increment_ { 440.0 / 48000.0 };
    double phase_ { 0.0 };

    double character_ { 0.0 };
    double tilt_ { 1.0 };
    FmShape shape_ { FmShape::sine };
    double feedback_ { 0.0 };

    Mode mode_ { Mode::normal };
    PhaseShaper fold_;
    double formantHz_ { 800.0 };
    double formantDepth_ { 1.0 };

    bool needsQuadrature_ { true };
    double out_ { 0.0 };
    double quadrature_ { 0.0 };
    double history_[2] { 0.0, 0.0 };
};

} // namespace tezla::dsp
