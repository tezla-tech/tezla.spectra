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

        return sum;
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

    [[nodiscard]] double getSampleRate() const noexcept { return sampleRate_; }

private:
    [[nodiscard]] double clampFrequency (double hz) const noexcept
    {
        const double top = sampleRate_ * 0.49;
        return hz < 1.0 ? 1.0 : hz > top ? top : hz;
    }

    void rebuildPole (int index) noexcept
    {
        // r from T60: amplitude falls 60 dB over t60 seconds, so per sample
        // r = 10^(-3 / (t60 * fs)).
        const double r = std::pow (10.0, -3.0 / (t60_[index] * sampleRate_));
        const double omega = 2.0 * std::numbers::pi
                               * clampFrequency (frequencyHz_[index]) / sampleRate_;

        poleRe_[index] = r * std::cos (omega);
        poleIm_[index] = r * std::sin (omega);
    }

    double sampleRate_ { 44100.0 };
    int modeCount_ { 1 };

    double frequencyHz_[kMaxModes] {};
    double t60_[kMaxModes] { };
    double gain_[kMaxModes] {};
    double weight_[kMaxModes] {};

    double poleRe_[kMaxModes] {};
    double poleIm_[kMaxModes] {};
    double stateRe_[kMaxModes] {};
    double stateIm_[kMaxModes] {};
};

} // namespace tezla::dsp
