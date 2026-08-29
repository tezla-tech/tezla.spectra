// Copyright (c) 2026 The Tezla <thetezla@proton.me>
// Created by The Tezla -- https://github.com/wingit33/tezla.tech
// Music: https://soundcloud.com/thetezla | https://thetezla.bandcamp.com
// Built with development assistance from Claude (Anthropic).
// SPDX-License-Identifier: AGPL-3.0-only
// GNU AGPLv3 (see LICENSE), plus NOTICE.md's attribution term. Keep intact.

#pragma once

// One playable object: the modal bank with its exciter, tension drop and
// vactrol gate.  Signal path per sample:
//
//   [roll clock -> re-strike]        (sample-accurate retrigger)
//   noise burst + bow force  ->  ModalResonator  ->  DC blocker  ->  LPG
//
// The DC blocker exists for the bow: its kinetic force statically deflects
// the object (measured ~0.7 at full pressure, see Bow.hpp), and gating a
// standing offset would thump. It sits OUTSIDE the bow's loop -- the
// deflection is the loop's operating point -- and before the LPG.
//
// Mode frequencies are built once per note: material morph ratio, then
// Overtone Lock against the voice's own fundamental on the loaded scale,
// then the drop multiplier on top -- the lock is computed on the LANDED
// pitch, so the whole locked object glides as one thing. Modes that would
// land above 0.45 fs get zero gain and zero weight instead of being folded
// onto the Nyquist clamp: band-limited by construction, which is what the
// engine's inharmonicity gate measures.
//
// A voice is ACTIVE while its key is held or its gate still conducts; a
// closed gate is bit-exact silence (LowpassGate's contract), so inactive
// means contributes-nothing, and the manager's retirement check is a real
// measurement, not a hope (the Sonitus zombie-voice lesson).

#include <cstdint>

#include <tezla/dsp/DcBlocker.hpp>
#include <tezla/dsp/LowpassGate.hpp>
#include <tezla/dsp/ModalResonator.hpp>
#include <tezla/dsp/ModeShapes.hpp>
#include <tezla/dsp/Tuning.hpp>

#include "Bow.hpp"
#include "Exciters.hpp"
#include "TensionDrop.hpp"

namespace tezla::malleus {

enum class Exciter
{
    Mallet = 0,
    Pluck,
    Roll,
    Bow,

    count
};

/// Everything a note needs to know about the object and how it is hit.
/// Plain data, copied per note-on so a ringing voice keeps the settings it
/// was struck with.
struct VoiceSettings
{
    double material { 1.0 };            ///< 0..4: String..Bell morph
    double stretch { 0.0 };             ///< -0.5..2 inharmonicity power
    double lockAmount { 0.0 };          ///< 0..1 Overtone Lock
    int partials { 32 };                ///< 8..64
    double decaySeconds { 2.0 };        ///< prime T60
    double tilt { 0.5 };                ///< 0..1: upper partials die faster
    double position { 0.29 };           ///< strike point comb
    Exciter exciter { Exciter::Mallet };
    double hardness { 0.5 };
    double noiseAmount { 0.0 };         ///< scrape mixed into the strike
    double dropSemitones { 0.0 };       ///< signed per-hit tension drop
    double dropSeconds { 0.08 };
    double bowPressure { 0.35 };
    double bowSpeed { 0.5 };
    double rollStartSeconds { 0.09 };
    double rollRatio { 0.72 };
    double rollMinimumSeconds { 0.028 };
    double rollHumanise { 0.35 };
};

class MalleusVoice
{
public:
    void prepare (double sampleRate) noexcept
    {
        sampleRate_ = sampleRate > 0.0 ? sampleRate : 44100.0;
        bank_.prepare (sampleRate_);
        drop_.prepare (sampleRate_);
        gate_.prepare (sampleRate_);
        noise_.prepare (sampleRate_);
        roll_.prepare (sampleRate_);
        bow_.prepare (sampleRate_);
        dcBlocker_.prepare (sampleRate_, 10.0);
        held_ = false;
        note_ = -1;
        age_ = 0;
    }

    void noteOn (int note, double fundamentalHz, double velocity,
                 std::uint64_t seed, const VoiceSettings& settings,
                 const dsp::Scale& lockScale, long long age) noexcept
    {
        settings_ = settings;
        note_ = note;
        fundamental_ = fundamentalHz;
        velocity_ = velocity < 0.0 ? 0.0 : velocity > 1.0 ? 1.0 : velocity;
        age_ = age;
        held_ = true;

        // A fresh strike, even on a stolen voice: the old ring does not
        // belong to the new object.
        bank_.reset();
        dcBlocker_.reset();
        gate_.reset();

        noise_.setSeed (seed);
        roll_.setSeed (seed ^ 0x517CC1B727220A95ULL);

        drop_.reset();
        drop_.trigger (settings_.dropSemitones, settings_.dropSeconds);

        rebuildModes (lockScale);
        applyDrop();

        switch (settings_.exciter)
        {
            case Exciter::Mallet:
                strike (1.0);
                break;

            case Exciter::Pluck:
                pluck();
                break;

            case Exciter::Roll:
                roll_.trigger (settings_.rollStartSeconds, settings_.rollRatio,
                               settings_.rollMinimumSeconds, settings_.rollHumanise);
                strike (1.0);
                break;

            case Exciter::Bow:
                bow_.reset();
                bow_.resetClampExcess();
                bow_.setPressure (settings_.bowPressure * velocity_);
                bow_.setSpeed (settings_.bowSpeed);
                gate_.setHold (velocity_);
                break;

            case Exciter::count:
                break;
        }
    }

    void noteOff() noexcept
    {
        held_ = false;
        gate_.setHold (0.0);
        roll_.stop();
    }

    /// Control-rate work, called every kControlIntervalSamples by the
    /// engine: the tension glide retunes the whole bank state-preservingly.
    void controlTick (int samples) noexcept
    {
        if (! isActive())
            return;

        if (drop_.isActive())
        {
            drop_.advance (samples);
            applyDrop();
        }
    }

    /// Renders and ADDS `count` samples into `out`.
    void render (double* out, int count) noexcept
    {
        if (! isActive())
            return;

        const bool bowing = held_ && settings_.exciter == Exciter::Bow;
        const bool rolling = held_ && settings_.exciter == Exciter::Roll;

        for (int n = 0; n < count; ++n)
        {
            if (rolling)
            {
                const double restrike = roll_.next();

                if (restrike > 0.0)
                    strike (restrike);
            }

            double input = noise_.next();

            if (bowing)
                input += bow_.force (bank_.contactVelocity());

            const double rung = bank_.process (input);
            out[n] += gate_.process (dcBlocker_.process (rung));
        }
    }

    [[nodiscard]] bool isActive() const noexcept
    {
        return held_ || gate_.conductance() > 0.0;
    }

    [[nodiscard]] bool isHeld() const noexcept { return held_; }
    [[nodiscard]] int getNote() const noexcept { return note_; }
    [[nodiscard]] long long getAge() const noexcept { return age_; }
    [[nodiscard]] double bankEnergy() const noexcept { return bank_.energy(); }

    [[nodiscard]] double modeFrequency (int mode) const noexcept
    {
        return bank_.getModeFrequency (mode);
    }

    [[nodiscard]] double modeGain (int mode) const noexcept
    {
        return mode >= 0 && mode < dsp::ModalResonator::kMaxModes
                 ? gain_[mode] : 0.0;
    }

    [[nodiscard]] int getPartialCount() const noexcept
    {
        return settings_.partials;
    }

private:
    /// Material morph -> Overtone Lock (rooted on the landed fundamental)
    /// -> per-mode T60 from decay and tilt. Super-Nyquist modes are muted
    /// outright, never folded onto the frequency clamp.
    void rebuildModes (const dsp::Scale& lockScale) noexcept
    {
        const int partials = settings_.partials < 1 ? 1
                           : settings_.partials > dsp::ModalResonator::kMaxModes
                               ? dsp::ModalResonator::kMaxModes
                               : settings_.partials;

        bank_.setModeCount (partials);

        const double ceiling = 0.45 * sampleRate_;

        for (int mode = 0; mode < partials; ++mode)
        {
            const double ratio = dsp::ModeShapes::ratioAt (settings_.material,
                                                           mode, settings_.stretch);
            double frequency = fundamental_ * ratio;

            if (settings_.lockAmount > 0.0)
                frequency = dsp::ModeShapes::lockToScale (frequency, fundamental_,
                                                          lockScale,
                                                          settings_.lockAmount);

            const bool audible = frequency < ceiling;

            base_[mode] = frequency;
            t60_[mode] = settings_.decaySeconds
                       * std::pow (ratio, -2.0 * settings_.tilt);
            gain_[mode] = audible ? 1.0 / partials : 0.0;

            bank_.setInputWeight (mode,
                audible ? positionWeight (mode + 1, settings_.position)
                            / partials
                        : 0.0);
        }
    }

    void applyDrop() noexcept
    {
        const double multiplier = drop_.multiplier();
        const int partials = bank_.getModeCount();

        for (int mode = 0; mode < partials; ++mode)
            bank_.setMode (mode, base_[mode] * multiplier, t60_[mode], gain_[mode]);
    }

    /// One mallet contact at the CURRENT mode frequencies (mid-drop, a
    /// re-strike excites the glided object, as a real hand would).
    void strike (double amount) noexcept
    {
        double frequencies[dsp::ModalResonator::kMaxModes];
        double amounts[dsp::ModalResonator::kMaxModes];
        const int partials = bank_.getModeCount();

        for (int mode = 0; mode < partials; ++mode)
            frequencies[mode] = bank_.getModeFrequency (mode);

        malletWeights (amounts, frequencies, partials, settings_.position,
                       settings_.hardness, velocity_ * amount);

        for (int mode = 0; mode < partials; ++mode)
            if (gain_[mode] > 0.0)
                bank_.excite (mode, amounts[mode]);

        if (settings_.noiseAmount > 0.0)
            noise_.trigger (settings_.hardness,
                            settings_.noiseAmount * velocity_ * amount);

        gate_.ping (velocity_ * amount);
    }

    void pluck() noexcept
    {
        double amounts[dsp::ModalResonator::kMaxModes];
        const int partials = bank_.getModeCount();

        pluckWeights (amounts, partials, settings_.position, velocity_);

        for (int mode = 0; mode < partials; ++mode)
            if (gain_[mode] > 0.0)
                bank_.excite (mode, amounts[mode]);

        if (settings_.noiseAmount > 0.0)
            noise_.trigger (settings_.hardness, settings_.noiseAmount * velocity_);

        gate_.ping (velocity_);
    }

    double sampleRate_ { 44100.0 };

    dsp::ModalResonator bank_;
    TensionDrop drop_;
    dsp::LowpassGate gate_;
    NoiseBurst noise_;
    RollClock roll_;
    Bow bow_;
    dsp::DcBlocker<double> dcBlocker_;

    VoiceSettings settings_;
    double base_[dsp::ModalResonator::kMaxModes] {};
    double t60_[dsp::ModalResonator::kMaxModes] {};
    double gain_[dsp::ModalResonator::kMaxModes] {};

    int note_ { -1 };
    double fundamental_ { 220.0 };
    double velocity_ { 0.0 };
    long long age_ { 0 };
    bool held_ { false };
};

} // namespace tezla::malleus
