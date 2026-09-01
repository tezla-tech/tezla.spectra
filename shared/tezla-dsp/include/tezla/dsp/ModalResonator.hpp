// Copyright (c) 2026 The Tezla <thetezla@proton.me>
// Created by The Tezla -- https://github.com/wingit33/tezla.tech
// Music: https://soundcloud.com/thetezla | https://thetezla.bandcamp.com
// Built with development assistance from Claude (Anthropic).
// SPDX-License-Identifier: AGPL-3.0-only
// GNU AGPLv3 (see LICENSE), plus NOTICE.md's attribution term. Keep intact.

#pragma once

// A bank of resonant modes -- the core of modal synthesis: an object IS its
// modes, each a frequency, a decay time and an amplitude.
//
// Each mode is the complex one-pole ("phasor") resonator, the standard modal
// formulation:
//
//     s[n] = a * s[n-1] + x[n]        a = r * e^{i omega},  s complex
//
// advanced as two real multiplies-and-adds, with the output taken from the
// imaginary part so an impulse rings as g * r^n * sin(n omega) -- starting
// from zero, the way a struck mode does. The form is chosen over the
// equivalent two-pole biquad for two properties that matter here and that the
// biquad does not have:
//
//   * Retuning is AMPLITUDE-CONTINUOUS. The state s is a rotating phasor
//     whose magnitude is the ring's envelope; changing the pole angle spins
//     it at a new rate without touching its length. That is what lets a
//     tension Drop glide every mode of a ringing drum without a click --
//     the same state-preserving-retune contract as DcBlocker::retune, held
//     by construction rather than by correction.
//
//   * Energy is free. |s|^2 per mode, summed with the mode gains, is the
//     ring's audible energy -- the number that decides when a voice has
//     genuinely finished. Voices must measurably die (the zombie-voice
//     lesson: silence-based tests pass while retired-but-running voices pin
//     the CPU), and this readout is what the assertion bites on.
//
// Everything is fixed-size and arithmetic-only: prepare() allocates nothing,
// so the whole bank is safe to reconfigure from the audio thread.

#include <cmath>
#include <numbers>

#include "Exact.hpp"

namespace tezla::dsp {

class ModalResonator
{
public:
    static constexpr int kMaxModes = 64;

    /// Shortest decay accepted. Below ~1 ms a "mode" is a click generator
    /// and r collapses toward zero anyway.
    static constexpr double kMinT60Seconds = 0.001;

    /// The most coupling Bloom may ask for. Chosen from the sweep in
    /// `tests/test_ModalResonator.cpp` rather than from a round number: it is
    /// where a bounded injection into a decaying bank still cannot sustain
    /// itself at any pitch, decay or mode count the instrument reaches.
    static constexpr double kMaxBloom = 1.0;

    /// What counts as a large displacement, in output units.
    ///
    /// Chosen by measurement on a **voice**, not on a bare bank, and that is
    /// the part that matters: a unit test's mode bank and a Malleus note reach
    /// completely different amplitudes for the same coupling response, so a
    /// constant calibrated on the first is wrong for the second. The first
    /// attempt at this number was calibrated on a 32-mode bar and put the
    /// window at velocities 0.1 to 0.3 -- the control was *off* at a hard hit
    /// and reversed at full velocity, which is backwards.
    ///
    /// Late high-band share of a struck plate against the velocity it was
    /// struck at, bloom 1 against bloom 0:
    ///
    ///     velocity   voice peak   drive 3   drive 4   drive 6   bloom 0
    ///       0.10       0.0002      0.0006    0.0007    0.0009    0.0005
    ///       0.25       0.0026      0.0085    0.0145    0.0355    0.0036
    ///       0.40       0.0072      0.0520    0.0976    0.1209    0.0087
    ///       0.55       0.0138      0.1505    0.1909    0.9068    0.0124
    ///       0.70       0.0224      0.2315    0.1860    0.9779    0.0147
    ///       0.85       0.0330      0.1623    0.9773    0.9853    0.0160
    ///       1.00       0.0458      0.9077    0.9874    0.9900    0.0168
    ///
    /// Four, because it climbs across the whole velocity range without a dip
    /// and without saturating early. Six is fully on by half velocity, which
    /// throws away the dynamic; three dips at 0.85.
    ///
    /// The **useful window is about 10 dB wide** and that is a real limitation
    /// of a coupling bounded by `q / (1 + |q|)`: past it the injection swamps
    /// the state instead of perturbing it and the control reverses. Widening
    /// it means changing the saturation, which is a redesign of the bound
    /// rather than a constant, so the window is placed rather than widened.
    /// `tezla-measure malleus` prints the velocity sweep.
    static constexpr double kBloomDrive = 4.0;

    /// Arithmetic only -- no allocation, safe anywhere. Re-preparing keeps
    /// each mode's frequency/decay/gain request and rebuilds the poles for
    /// the new rate (the coefficients must never embed a stale rate,
    /// CLAUDE.md section 6); state is cleared, as prepare always does.
    void prepare (double sampleRate) noexcept
    {
        sampleRate_ = sampleRate > 0.0 ? sampleRate : 44100.0;

        for (int index = 0; index < kMaxModes; ++index)
            rebuildPole (index);

        reset();
    }

    void reset() noexcept
    {
        for (int index = 0; index < kMaxModes; ++index)
        {
            stateRe_[index] = 0.0;
            stateIm_[index] = 0.0;
        }

        previousOutput_ = 0.0;
        stateScale_ = 0.0;
    }

    /// How many of the modes sound. Clamped to [1, kMaxModes]. Modes beyond
    /// the count keep their settings and their (decaying) state but are
    /// neither advanced nor summed -- change the count at note boundaries,
    /// not mid-ring, or the truncated modes vanish as a step.
    void setModeCount (int count) noexcept
    {
        modeCount_ = count < 1 ? 1 : count > kMaxModes ? kMaxModes : count;
    }

    [[nodiscard]] int getModeCount() const noexcept { return modeCount_; }

    /// One mode's identity: frequency, T60 decay, output gain.
    ///
    /// STATE-PRESERVING by construction -- a ringing mode glides to the new
    /// frequency with its envelope intact, which is what a per-hit tension
    /// drop leans on. The no-op guard skips the transcendentals when nothing
    /// moved, so pushing every mode every control tick is affordable.
    void setMode (int index, double frequencyHz, double t60Seconds,
                  double gain) noexcept
    {
        if (index < 0 || index >= kMaxModes)
            return;

        const double frequency = clampFrequency (frequencyHz);
        const double t60 = t60Seconds < kMinT60Seconds ? kMinT60Seconds : t60Seconds;

        gain_[index] = gain;

        if (isExactly (frequency, frequencyHz_[index])
            && isExactly (t60, t60_[index]))
            return;

        frequencyHz_[index] = frequency;
        t60_[index] = t60;
        rebuildPole (index);
    }

    /// Coupling weight for the continuous input path (a bow, a scrape). A
    /// plain multiplier on x per mode -- no state to clear, no guard needed.
    void setInputWeight (int index, double weight) noexcept
    {
        if (index >= 0 && index < kMaxModes)
            weight_[index] = weight;
    }

    /// Strikes the mode: an impulse of this amount into its input, exactly
    /// as one sample of x = amount would arrive, so the mode rings as
    /// gain * r^n * sin(n omega) * amount from the next process() call.
    void excite (int index, double amount) noexcept
    {
        if (index >= 0 && index < kMaxModes)
            stateRe_[index] += amount;
    }

    /// **Bloom -- the modes talk to each other.** Zero is off and is exact.
    ///
    /// Without this every mode here is independent: struck, it rings down,
    /// and nothing it does reaches any other mode. Real objects are not like
    /// that. A tam-tam hit hard *builds after the strike* -- a shimmer that
    /// was not there at contact climbs out of the low modes over the next
    /// second. That is energy migrating upward through the bank, and it is
    /// why a gong sample never sounds like a gong played quietly.
    ///
    /// -----------------------------------------------------------------
    /// The physics, and the one honest departure from it
    /// -----------------------------------------------------------------
    ///
    /// When a plate's displacement approaches its thickness the restoring
    /// force stops being linear, and the leading correction is **quadratic**
    /// (the von Karman term). A quadratic couples mode *triads*: it generates
    /// f1 + f2 and f1 - f2 from every pair, which for a bank dominated by its
    /// low modes means a cascade upward. The rate goes as amplitude squared,
    /// which is exactly the "only when you hit it hard" behaviour.
    ///
    /// The departure: a true x^2 also has a **DC term** -- the static
    /// deflection of a plate pushed off centre. A mode bank has no
    /// zero-frequency mode to hold it, so injecting it would leave every
    /// resonator sitting at a constant offset and the output with real DC on
    /// it. So the coupling is `x * |x|` instead: still quadratic in magnitude,
    /// still amplitude-squared in rate, still generating sum and difference
    /// content -- and odd, so its mean is zero. What is lost is the static
    /// deflection, which is inaudible; what is kept is the cascade, which is
    /// the whole point.
    ///
    /// -----------------------------------------------------------------
    /// Why it cannot run away
    /// -----------------------------------------------------------------
    ///
    /// CLAUDE.md section 7: a feedback loop around a nonlinearity needs a
    /// bound that cannot be defeated. Two, here, and neither is optional:
    ///
    ///  1. The coupling passes through `q / (1 + |q|)`, whose magnitude is
    ///     **below 1 for every finite input** -- including one already
    ///     diverging. It is not a threshold that a big enough signal steps
    ///     over; there is no input for which it exceeds unity.
    ///  2. Each mode receives that term scaled by its **own bandwidth**,
    ///     `(1 - r)`, and normalised by the total coupling weight.
    ///
    /// The second is not decoration, and the first draft did not have it. A
    /// bounded term is not a bounded *result*: a resonator driven by a
    /// constant of amplitude A settles at A / (1 - r), which for a two-second
    /// decay at 48 kHz is about **28000 times** the drive. Sixty-four of them
    /// summing made it worse again. The sweep found it immediately -- worst
    /// sample 72.9 and the ring's energy *rising* by 4.48x over its second
    /// half -- which is precisely why section 7 asks for a swept test rather
    /// than a plausible argument.
    ///
    /// Scaling by (1 - r) turns the coupling from a **drive** into a **target
    /// amplitude**: a mode that rings for eight seconds is no longer driven
    /// eight times harder than one that rings for one, merely for being
    /// longer. Dividing by the summed weight keeps the total added amplitude
    /// near the coupling term itself however many modes are receiving it.
    ///
    /// The loop is broken by one sample -- the coupling reads the *previous*
    /// output -- which is what makes it computable rather than algebraic.
    void setBloom (double amount) noexcept
    {
        bloom_ = amount < 0.0 ? 0.0 : amount > kMaxBloom ? kMaxBloom : amount;
    }

    [[nodiscard]] double getBloom() const noexcept { return bloom_; }

    /// **Damp -- a hand on the object.** Zero is off, and off is exact.
    ///
    /// Percussion is *played* with damping: a palm on a drum head, fingers on
    /// a cymbal's edge, the heel of a hand stopping a gong. Without it the only
    /// way to shorten a note is to have set Decay shorter before playing it,
    /// which is not the same instrument.
    ///
    /// **The loss is proportional to frequency**, and that is the whole design
    /// rather than a curve chosen to sound nice. Soft tissue is a
    /// constant-loss-factor absorber: the energy it takes per cycle is roughly
    /// fixed, so the loss per *second* rises with the frequency and a mode at
    /// 4 kHz dies sixteen times faster than one at 250 Hz. That is why a
    /// damped cymbal goes **dull before it goes quiet**. A flat multiplier on
    /// every decay would be a volume pedal, and would sound like one.
    ///
    /// In numbers: at full damp a mode at `kDampReferenceHz` loses 60 dB in
    /// `kDampT60AtReference` on top of whatever its own decay was doing, and
    /// the added loss scales linearly with frequency from there. Decays
    /// combine as rates, not as times -- 1/T = 1/T_own + 1/T_added -- so a
    /// mode already dying faster than the damping is barely touched, which is
    /// correct: a hand cannot make a click shorter.
    ///
    /// State-preserving and no-op guarded, like `setMode`: a ringing object
    /// changes how fast it is dying without any discontinuity in what it is
    /// doing, so this can be pushed from an aftertouch or a pedal every
    /// control chunk.
    void setDamp (double amount) noexcept
    {
        const double wanted = amount < 0.0 ? 0.0 : amount > 1.0 ? 1.0 : amount;

        if (isExactly (wanted, damp_))
            return;

        damp_ = wanted;

        // Every mode's radius depends on it, and rebuilding a pole touches no
        // state -- so a bank ringing through a damping sweep glides rather
        // than steps.
        for (int index = 0; index < kMaxModes; ++index)
            rebuildPole (index);
    }

    [[nodiscard]] double getDamp() const noexcept { return damp_; }

    /// Where the damping law is anchored: at full damp a mode here loses 60 dB
    /// in `kDampT60AtReference` seconds on top of its own decay.
    static constexpr double kDampReferenceHz = 1000.0;
    static constexpr double kDampT60AtReference = 0.10;

    /// Advances every active mode one sample with no input and returns the
    /// bank's output.
    [[nodiscard]] double process() noexcept
    {
        return process (0.0);
    }

    /// Advances with a continuous input, injected into each mode through its
    /// input weight.
    [[nodiscard]] double process (double input) noexcept
    {
        // The coupling term, from the PREVIOUS sample's output. One sample of
        // delay is what makes the loop computable rather than algebraic; see
        // setBloom() for the physics and for why it cannot run away.
        //
        // Guarded rather than always computed, so bloom at zero is the
        // original bank by inspection and not by an argument about what
        // adding 0.0 does -- and so it costs nothing when it is off.
        if (isExactlyZero (bloom_))
        {
            double sum = 0.0;

            for (int index = 0; index < modeCount_; ++index)
            {
                const double re = stateRe_[index];
                const double im = stateIm_[index];

                stateRe_[index] = poleRe_[index] * re - poleIm_[index] * im
                                    + input * weight_[index];
                stateIm_[index] = poleIm_[index] * re + poleRe_[index] * im;

                sum += gain_[index] * stateIm_[index];
            }

            previousOutput_ = sum;

            return sum;
        }

        const double coupling = couplingTerm();

        double sum = 0.0;
        double uncoupledEnergy = 0.0;
        double coupledEnergy = 0.0;

        for (int index = 0; index < modeCount_; ++index)
        {
            const double re = stateRe_[index];
            const double im = stateIm_[index];

            // What this mode would have been with no coupling at all: the
            // rotation, the decay, and the external input.
            const double uncoupledRe = poleRe_[index] * re - poleIm_[index] * im
                                         + input * weight_[index];
            const double nextIm = poleIm_[index] * re + poleRe_[index] * im;

            const double coupled = uncoupledRe + coupling * couplingWeight_[index];

            stateRe_[index] = coupled;
            stateIm_[index] = nextIm;

            uncoupledEnergy += uncoupledRe * uncoupledRe + nextIm * nextIm;

            coupledEnergy += coupled * coupled + nextIm * nextIm;

            sum += gain_[index] * nextIm;
        }

        // **The coupling redistributes energy; it does not create any.** The
        // whole bank is scaled back to the energy it would have had without
        // the cascade, so what the high modes gained the low ones lost. This
        // is the von Karman term's actual character -- it is conservative,
        // and the losses live in the modal damping, which is already here.
        //
        // It is also the bound that cannot be defeated, and unlike the first
        // two attempts it is not a constant anyone has to choose: the bank's
        // energy after coupling can never exceed the energy the linear bank
        // would have had, at any amount, on any object, so there is nothing
        // for a large enough signal to overwhelm.
        // The typical per-mode amplitude, for the next sample's coupling
        // reference. One sample stale, like the output the coupling reads.
        stateScale_ = std::sqrt (uncoupledEnergy / static_cast<double> (modeCount_));

        if (coupledEnergy > uncoupledEnergy && uncoupledEnergy > 0.0)
        {
            const double scale = std::sqrt (uncoupledEnergy / coupledEnergy);

            for (int index = 0; index < modeCount_; ++index)
            {
                stateRe_[index] *= scale;
                stateIm_[index] *= scale;
            }

            sum *= scale;
        }

        previousOutput_ = sum;

        return sum;
    }

    /// The object's velocity at the contact point, in output units per
    /// SECOND -- the signal a bow's friction curve acts on.
    ///
    /// Read through the same input weights the excitation enters by, so the
    /// bow is collocated: it feels the object exactly where it drives it.
    /// Per mode the output is g * Im(s) and s rotates at omega, so the
    /// velocity contribution is w * (2 pi f) * Re(s) -- the quadrature
    /// component, scaled by the PHYSICAL angular frequency rather than the
    /// per-sample one, which is what makes the reading identical at 48 and
    /// 192 kHz (CLAUDE.md section 6) instead of shrinking with the rate.
    [[nodiscard]] double contactVelocity() const noexcept
    {
        double velocity = 0.0;

        for (int index = 0; index < modeCount_; ++index)
            velocity += weight_[index] * angularHz_[index] * stateRe_[index];

        return velocity;
    }

    /// The audible energy of the ring: sum over active modes of
    /// (gain * |s|)^2. Monotone-decaying after the last excitation, and the
    /// number a voice's retirement compares against its threshold.
    [[nodiscard]] double energy() const noexcept
    {
        double total = 0.0;

        for (int index = 0; index < modeCount_; ++index)
        {
            const double magnitudeSquared = stateRe_[index] * stateRe_[index]
                                          + stateIm_[index] * stateIm_[index];
            total += gain_[index] * gain_[index] * magnitudeSquared;
        }

        return total;
    }

    [[nodiscard]] double getModeFrequency (int index) const noexcept
    {
        return index >= 0 && index < kMaxModes ? frequencyHz_[index] : 0.0;
    }

    /// One mode's contribution to the last `process`, gain included -- the
    /// term that `process` summed for this index.
    ///
    /// **Why this exists rather than a second output tap inside the loop.**
    /// A second listening point on the object is the same sum with different
    /// per-mode weights, and the obvious place to put it is beside the first
    /// one in `process`. Then it costs a multiply-add per mode per sample
    /// whether or not anything is listening twice, or the loop grows a branch,
    /// or it grows into four variants once bloom is included. Reading the
    /// terms back out costs the same arithmetic only when a caller actually
    /// wants a second tap, and leaves the hot loop -- and its bit-exactness --
    /// untouched by the feature entirely.
    ///
    /// Valid after any `process` and until the next one.
    [[nodiscard]] double modeOutput (int index) const noexcept
    {
        return index >= 0 && index < kMaxModes ? gain_[index] * stateIm_[index] : 0.0;
    }

    [[nodiscard]] double getSampleRate() const noexcept { return sampleRate_; }

private:
    [[nodiscard]] double clampFrequency (double hz) const noexcept
    {
        const double top = sampleRate_ * 0.49;
        return hz < 1.0 ? 1.0 : hz > top ? top : hz;
    }

    /// The coupling term, from the previous sample. See setBloom().
    [[nodiscard]] double couplingTerm() const noexcept
    {
        // **Referenced to the amplitude the instrument actually runs at.**
        //
        // The von Karman term is a large-displacement effect and its rate goes
        // as amplitude squared, which is the feature -- a quiet hit must bloom
        // less than a loud one. But "large" has to be measured against
        // something, and the something was implicitly 1.0, where the unit test
        // drives it. A Malleus voice at full velocity peaks near **0.046**, so
        // `x * |x|` came out around 0.002 and the control did nothing at all
        // in the instrument: measured through the plugin, the late-window
        // energy shares at 110/220/440 Hz read 0.1797/0.3642/0.4481 with Bloom
        // off and 0.1917/0.3584/0.4434 with it at maximum. A knob that moves
        // the fourth decimal place is not a knob.
        //
        // `kBloomDrive` puts a full-velocity strike at the top of the curve's
        // useful range instead. It changes nothing about the bound -- the
        // quotient below is still under 1 for every finite input, and the
        // energy renormalisation still caps the whole bank -- and it keeps the
        // amplitude dependence exactly: a quarter-velocity hit is a quarter of
        // the drive and a sixteenth of the rate.
        const double x = kBloomDrive * previousOutput_;

        // Quadratic in magnitude, odd in sign: the cascade without the DC.
        const double quadratic = x * (x < 0.0 ? -x : x);

        // Bounded below 1 for every finite input -- the bound that cannot be
        // defeated. Not a clamp: a clamp is a threshold, and this has none.
        const double magnitude = quadratic < 0.0 ? -quadratic : quadratic;

        // Referenced to the bank's own typical per-mode amplitude, so Bloom
        // is "how hard a nudge" rather than an absolute force. Without this
        // the injection swamped the state instead of perturbing it: the bank
        // was being overwritten every sample, which killed the ring and made
        // the control read as a lowpass. Measured then: the late high-band
        // fraction went DOWN with bloom (0.711 against 0.780 off) and did not
        // vary with strike amplitude at all, because it was saturated.
        return bloom_ * stateScale_ * quadratic / (1.0 + magnitude);
    }

    /// How much of the coupling each mode receives.
    ///
    /// **Directed upward**, because that is the direction the physical
    /// cascade runs: a mode within an octave of the fundamental gets nothing,
    /// and the weight reaches full three octaves above it. Feeding the low
    /// modes back into themselves would be a resonator with positive
    /// feedback, which is a different and much less interesting instrument.
    ///
    /// Computed against mode 0's frequency, so it follows the object rather
    /// than the sample rate. Malleus pushes modes in index order, so mode 0 is
    /// current by the time mode k is rebuilt; before anything is set at all
    /// the fundamental reads its default and the weight is harmless.
    void refreshCouplingWeight (int index) noexcept
    {
        const double fundamental = frequencyHz_[0];

        if (fundamental <= 0.0)
        {
            couplingWeight_[index] = 0.0;
            return;
        }

        const double octavesUp = std::log2 (frequencyHz_[index] / fundamental);
        const double ramp = (octavesUp - 1.0) / 3.0;

        couplingWeight_[index] = ramp < 0.0 ? 0.0 : ramp > 1.0 ? 1.0 : ramp;
    }

    void rebuildPole (int index) noexcept
    {
        // r from T60: amplitude falls 60 dB over t60 seconds, so per sample
        // r = 10^(-3 / (t60 * fs)).
        double r = std::pow (10.0, -3.0 / (t60_[index] * sampleRate_));

        // The hand, if there is one. `exp(-0)` is exactly 1 and `r * 1.0` is
        // exactly `r`, so damp 0 would be bit-exact with no branch at all --
        // this one is a **fast path**, skipping a transcendental per mode per
        // rebuild, and saying so rather than claiming it is what makes the
        // neutral setting exact.
        if (! isExactlyZero (damp_))
        {
            // Loss adds as a *rate*: nepers per second, proportional to the
            // mode's frequency. 3 ln(10) nepers is 60 dB, so the reference
            // T60 converts to a rate the same way the natural decay does.
            const double referenceRate = 3.0 * std::numbers::ln10 / kDampT60AtReference;
            const double added = damp_ * referenceRate
                                   * (clampFrequency (frequencyHz_[index]) / kDampReferenceHz);

            r *= std::exp (-added / sampleRate_);
        }

        const double frequency = clampFrequency (frequencyHz_[index]);
        const double omega = 2.0 * std::numbers::pi * frequency / sampleRate_;

        poleRadius_[index] = r;
        poleRe_[index] = r * std::cos (omega);
        poleIm_[index] = r * std::sin (omega);
        angularHz_[index] = 2.0 * std::numbers::pi * frequency;

        refreshCouplingWeight (index);
    }

    double sampleRate_ { 44100.0 };
    int modeCount_ { 1 };

    double bloom_ { 0.0 };
    double damp_ { 0.0 };
    double previousOutput_ { 0.0 };
    double couplingWeight_[kMaxModes] {};
    double poleRadius_[kMaxModes] {};
    double stateScale_ { 0.0 };

    double frequencyHz_[kMaxModes] {};
    double t60_[kMaxModes] { };
    double gain_[kMaxModes] {};
    double weight_[kMaxModes] {};

    double poleRe_[kMaxModes] {};
    double poleIm_[kMaxModes] {};
    double angularHz_[kMaxModes] {};
    double stateRe_[kMaxModes] {};
    double stateIm_[kMaxModes] {};
};

} // namespace tezla::dsp
