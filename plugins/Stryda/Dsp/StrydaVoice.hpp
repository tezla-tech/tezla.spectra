// Copyright (c) 2026 The Tezla <thetezla@proton.me>
// Created by The Tezla -- https://github.com/wingit33/tezla.tech
// Music: https://soundcloud.com/thetezla | https://thetezla.bandcamp.com
// Built with development assistance from Claude (Anthropic).
// SPDX-License-Identifier: AGPL-3.0-only
// GNU AGPLv3 (see LICENSE), plus NOTICE.md's attribution term. Keep intact.

#pragma once

// One voice: six operators, six envelopes, one note.

#include <algorithm>
#include <array>
#include <cmath>

#include <tezla/dsp/Adsr.hpp>
#include <tezla/dsp/FmBandwidth.hpp>
#include <tezla/dsp/SmallRandom.hpp>

#include "OperatorMatrix.hpp"

namespace tezla::stryda
{

/// Everything the JUCE layer pushes down, once per control chunk. Plain data:
/// the voice reads it, nothing here allocates or locks.
struct OperatorParameters
{
    double ratio { 1.0 };          ///< multiple of the note's frequency
    double fineCents { 0.0 };
    double fixedHz { 0.0 };        ///< when > 0, replaces ratio x note entirely
    double character { 0.0 };      ///< 0 = classic PM, 1 = ModFM
    double level { 0.0 };          ///< contribution to the output mix
    double pan { 0.0 };
    double feedback { 0.0 };       ///< cycles of self-modulation

    double attack { 0.001 };
    double hold { 0.0 };
    double decay { 0.5 };
    double sustain { 1.0 };
    double release { 0.2 };

    // ---- F4 -----------------------------------------------------------------

    double fold { 0.0 };            ///< phase distortion, 0 = identity
    int mode { 0 };                 ///< 0 normal, 1 formant
    double formantHz { 800.0 };
    double formantDepth { 1.0 };    ///< the ModFM index k, in cycles

    /// Key scaling, in the shape the DX7 documents: a break point, and a signed
    /// depth on each side of it. Positive means louder or brighter that way.
    double keyBreak { 60.0 };       ///< MIDI note
    double keyLeft { 0.0 };         ///< -1 .. 1, per octave below the break
    double keyRight { 0.0 };        ///< -1 .. 1, per octave above it

    double velLevel { 0.0 };        ///< how much velocity moves the output level
    double velIndex { 0.0 };        ///< how much it moves the modulation this operator sends
};

struct VoiceParameters
{
    std::array<OperatorParameters, OperatorMatrix::kNumOperators> operators {};

    /// The matrix, in cycles: `indices[to][from]`.
    std::array<std::array<double, OperatorMatrix::kNumOperators>,
               OperatorMatrix::kNumOperators> indices {};

    std::array<double, OperatorMatrix::kNumOperators> noiseIndices {};

    double masterLevel { 1.0 };

    /// Off / Soft / Hard, as an amount: 0 leaves every index alone.
    double indexCap { 0.0 };

    /// What fraction of the internal Nyquist the cap aims below.
    double capCeiling { 0.9 };
};

class StrydaVoice
{
public:
    static constexpr int kNumOperators = OperatorMatrix::kNumOperators;

    void prepare (double sampleRate) noexcept
    {
        sampleRate_ = sampleRate > 0.0 ? sampleRate : 48000.0;
        matrix_.prepare (sampleRate_);

        for (auto& envelope : envelopes_)
            envelope.prepare (sampleRate_);

        reset();
    }

    void reset() noexcept
    {
        matrix_.reset();

        for (auto& envelope : envelopes_)
            envelope.reset();

        gains_.fill (0.0);
        active_ = false;
        note_ = -1;
        age_ = 0;
    }

    [[nodiscard]] bool isActive() const noexcept { return active_; }
    [[nodiscard]] int getNote() const noexcept { return note_; }
    [[nodiscard]] std::uint64_t getAge() const noexcept { return age_; }
    [[nodiscard]] bool isReleasing() const noexcept { return active_ && ! held_; }

    void noteOn (int note, double frequency, double velocity) noexcept
    {
        note_ = note;
        frequency_ = frequency;
        velocity_ = std::clamp (velocity, 0.0, 1.0);
        active_ = true;
        held_ = true;
        age_ = 0;

        matrix_.reset();

        for (int op = 0; op < kNumOperators; ++op)
        {
            matrix_.setStartPhase (op, 0.0);
            envelopes_[static_cast<std::size_t> (op)].noteOn();
        }
    }

    void noteOff() noexcept
    {
        held_ = false;

        for (auto& envelope : envelopes_)
            envelope.noteOff();
    }

    void kill() noexcept { reset(); }

    /// The key-scaling gain for one operator at one note.
    ///
    /// **The two directions are separate controls and that is the point.** A
    /// real instrument does not simply get brighter or duller uniformly; it
    /// does one thing below its centre and another above it. The DX7 article
    /// read for this puts it plainly: applied to a carrier you hear a volume
    /// change, applied to a modulator you hear a timbral one -- which is why
    /// this gain is used in both places below, on the level and on the indices
    /// the operator sends, rather than only on one.
    ///
    /// A depth of 0 gives `pow (2, 0)` = **exactly 1.0**, so a flat curve is
    /// bit-exactly no curve.
    ///
    /// Simplified from the four-curve original: the DX7 offers linear and
    /// exponential shapes independently on each side, and this offers the
    /// exponential one with a signed depth. That is the shape that does the
    /// musical work -- little change for an octave or so, then increasingly
    /// drastic -- and the linear variants are a roadmap item rather than a
    /// silent omission.
    [[nodiscard]] static double keyScaleGain (const OperatorParameters& settings,
                                              double note) noexcept
    {
        const double octaves = (note - settings.keyBreak) / 12.0;
        const double depth = octaves >= 0.0 ? settings.keyRight : settings.keyLeft;

        // A flat curve must be bit-exactly no curve, so the guard compares
        // against zero rather than a tolerance. -Wfloat-equal forbids the
        // direct form; this is the same test written as two inequalities.
        if (! (depth < 0.0) && ! (depth > 0.0))
            return 1.0;

        return std::pow (2.0, depth * std::abs (octaves));
    }

    /// The Malleus velocity form, `x * ((1 - a) + a * v)`: at amount 0 the
    /// control is exactly 1 and velocity does nothing at all.
    [[nodiscard]] static double velocityGain (double amount, double velocity) noexcept
    {
        return amount <= 0.0 ? 1.0 : (1.0 - amount) + amount * velocity;
    }

    /// Push the current settings. Called once per control chunk, never per
    /// sample, and every setter it reaches is guarded against a no-op.
    void applyParameters (const VoiceParameters& parameters, double internalRate) noexcept
    {
        parameters_ = parameters;

        for (int op = 0; op < kNumOperators; ++op)
        {
            const auto& settings = parameters.operators[static_cast<std::size_t> (op)];
            const auto slot = static_cast<std::size_t> (op);

            const double hz = settings.fixedHz > 0.0
                                ? settings.fixedHz
                                : frequency_ * settings.ratio
                                    * std::pow (2.0, settings.fineCents / 1200.0);

            const double keyGain = keyScaleGain (settings, static_cast<double> (note_));

            matrix_.setFrequency (op, hz);
            matrix_.setCharacter (op, settings.character);
            matrix_.setFeedback (op, settings.feedback);
            matrix_.setFold (op, settings.fold);
            matrix_.setMode (op, settings.mode == 1 ? dsp::FmOperator::Mode::formant
                                                    : dsp::FmOperator::Mode::normal);
            matrix_.setFormant (op, settings.formantHz, settings.formantDepth);
            matrix_.setPan (op, settings.pan);

            // Key scaling and velocity reach the output level and the outgoing
            // modulation independently, because on a carrier they are a volume
            // and on a modulator they are a timbre.
            matrix_.setOutputLevel (op, settings.level * keyGain
                                          * velocityGain (settings.velLevel, velocity_));

            auto& envelope = envelopes_[slot];
            envelope.setAttackSeconds (settings.attack);
            envelope.setHoldSeconds (settings.hold);
            envelope.setDecaySeconds (settings.decay);
            envelope.setSustain (settings.sustain);
            envelope.setReleaseSeconds (settings.release);

            matrix_.setNoiseIndex (op, parameters.noiseIndices[slot]);

            for (int from = 0; from < kNumOperators; ++from)
            {
                const auto& source = parameters.operators[static_cast<std::size_t> (from)];
                const double sourceGain = keyScaleGain (source, static_cast<double> (note_))
                                            * velocityGain (source.velIndex, velocity_);

                matrix_.setIndex (op, from,
                                  parameters.indices[slot][static_cast<std::size_t> (from)]
                                    * sourceGain);
            }
        }

        matrix_.setIndexScale (capScaleFor (parameters, internalRate));
        matrix_.refreshQuadratureNeeds();
    }

    /// The scale the cap would apply. Exposed so the editor can say whether it
    /// is biting, and so a test can assert it is exactly 1.0 when it is not.
    [[nodiscard]] double capScaleFor (const VoiceParameters& parameters,
                                      double internalRate) const noexcept
    {
        if (parameters.indexCap <= 0.0)
            return 1.0;

        dsp::FmBandwidth bandwidth;
        bandwidth.setOperatorCount (kNumOperators);

        for (int op = 0; op < kNumOperators; ++op)
        {
            const auto& settings = parameters.operators[static_cast<std::size_t> (op)];
            const double hz = settings.fixedHz > 0.0
                                ? settings.fixedHz
                                : frequency_ * settings.ratio;

            bandwidth.setOperatorFrequency (op, hz);
            bandwidth.setFeedback (op, settings.feedback);

            for (int from = 0; from < kNumOperators; ++from)
                bandwidth.setIndex (from, op,
                                    parameters.indices[static_cast<std::size_t> (op)]
                                                      [static_cast<std::size_t> (from)]);
        }

        const double ceiling = parameters.capCeiling * 0.5 * internalRate;
        const double full = bandwidth.indexScaleFor (ceiling);

        // A partial cap moves towards the safe scale rather than jumping to it,
        // so "Soft" is a lean rather than a ceiling the patch slams into.
        return 1.0 + parameters.indexCap * (full - 1.0);
    }

    [[nodiscard]] double getIndexScale() const noexcept { return matrix_.getIndexScale(); }

    /// One sample.
    void process (double& left, double& right) noexcept
    {
        if (! active_)
            return;

        ++age_;

        bool anyActive = false;
        for (int op = 0; op < kNumOperators; ++op)
        {
            const auto slot = static_cast<std::size_t> (op);
            auto& envelope = envelopes_[slot];

            gains_[slot] = envelope.process();

            // The Sonitus zombie lesson: an Adsr at sustain 0 parks in the
            // sustain stage with isActive() still true, so a voice that only
            // asks "is anything active" never retires and the CPU meter pins
            // seconds after the last key is up. Kill it the moment it reaches
            // a zero sustain while still held.
            if (envelope.isActive() && dsp::isExactlyZero (gains_[slot])
                && parameters_.operators[slot].sustain <= 0.0 && ! held_)
                envelope.kill();

            anyActive = anyActive || envelope.isActive();
        }

        if (! anyActive)
        {
            reset();
            return;
        }

        double voiceLeft = 0.0;
        double voiceRight = 0.0;
        matrix_.process (gains_.data(), noise_.bipolar(), voiceLeft, voiceRight);

        left += voiceLeft * parameters_.masterLevel * velocity_;
        right += voiceRight * parameters_.masterLevel * velocity_;
    }

    void setSeed (std::uint64_t seed) noexcept { noise_.seed (seed); }

private:
    double sampleRate_ { 48000.0 };
    double frequency_ { 440.0 };
    double velocity_ { 1.0 };
    int note_ { -1 };
    bool active_ { false };
    bool held_ { false };
    std::uint64_t age_ { 0 };

    OperatorMatrix matrix_;
    std::array<dsp::Adsr, kNumOperators> envelopes_ {};
    std::array<double, kNumOperators> gains_ {};
    VoiceParameters parameters_ {};
    dsp::SmallRandom noise_ {};
};

} // namespace tezla::stryda
