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
#include <numbers>

#include <tezla/dsp/Adsr.hpp>
#include <tezla/dsp/Decibels.hpp>
#include <tezla/dsp/FmBandwidth.hpp>
#include <tezla/dsp/Oscillator.hpp>
#include <tezla/dsp/SmallRandom.hpp>
#include <tezla/dsp/SvfFilter.hpp>

#include "Modulation.hpp"
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

    /// Free / Harmonic / Scale, as a `RatioMode`. The engine resolves it before
    /// the voice ever sees `ratio`, so a voice only ever handles a plain
    /// number -- the quantiser needs the loaded scale, and the scale lives with
    /// the tuning in the engine.
    int ratioMode { 0 };

    double velLevel { 0.0 };        ///< how much velocity moves the output level
    double velIndex { 0.0 };        ///< how much it moves the modulation this operator sends
};

/// The filter, the sub lane and the unison spread. All F5.
struct VoiceExtras
{
    // ---- the per-voice filter ----------------------------------------------

    double cutoffHz { 20000.0 };
    double resonance { 0.0 };
    double morph { 0.0 };          ///< lowpass -> bandpass -> highpass
    double keyTrack { 0.0 };       ///< 0 = fixed, 1 = follows the note exactly
    double envAmount { 0.0 };      ///< octaves the filter envelope opens by
    double drive { 0.0 };
    double sing { 0.0 };

    double filterAttack { 0.002 };
    double filterDecay { 0.5 };
    double filterSustain { 1.0 };
    double filterRelease { 0.25 };

    // ---- the protected sub lane --------------------------------------------

    double subLevel { 0.0 };
    int subOctave { -1 };          ///< -2, -1 or 0 relative to the note
    int subShape { 0 };            ///< 0 sine, 1 triangle

    double subAttack { 0.002 };
    double subDecay { 2.0 };
    double subSustain { 1.0 };
    double subRelease { 0.25 };

    // ---- unison -------------------------------------------------------------

    /// **These are the global amounts, not per-copy offsets.** Each voice knows
    /// which copy of the stack it is (`setUnisonSlot`) and works out its own
    /// share every control chunk, so turning Detune up moves the copies that
    /// are already sounding instead of waiting for the next note. Sonitus had
    /// the other arrangement and it shipped a bug: the spread did not apply
    /// until the detune knob happened to move.
    int unisonCount { 1 };
    double unisonDetuneCents { 0.0 };
    double unisonSpread { 0.0 };        ///< stereo, 0 = mono stack
    double unisonIndexSpread { 0.0 };   ///< cycles added to every live index
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

    VoiceExtras extras {};

    ModulationSettings modulation {};
};

class StrydaVoice
{
public:
    static constexpr int kNumOperators = OperatorMatrix::kNumOperators;

    void prepare (double sampleRate) noexcept
    {
        sampleRate_ = sampleRate > 0.0 ? sampleRate : 48000.0;
        matrix_.prepare (sampleRate_);
        filter_.prepare (sampleRate_);
        sub_.reset();
        filterEnvelope_.prepare (sampleRate_);
        subEnvelope_.prepare (sampleRate_);

        for (auto& envelope : envelopes2_)
            envelope.prepare (sampleRate_);

        for (auto& lfo : lfos_)
            lfo.prepare (sampleRate_);

        for (auto& envelope : envelopes_)
            envelope.prepare (sampleRate_);

        reset();
    }

    void reset() noexcept
    {
        matrix_.reset();
        filter_.reset();
        sub_.reset();
        filterEnvelope_.reset();
        subEnvelope_.reset();

        for (auto& envelope : envelopes_)
            envelope.reset();

        gains_.fill (0.0);
        active_ = false;
        note_ = -1;
        age_ = 0;

        // Back to a stack of one, so a freed voice reused without a slot being
        // set does not inherit the last note's detune and pan.
        unisonCopy_ = 0;
        unisonCount_ = 1;
        unisonDetune_ = 0.0;
        unisonPan_ = 0.0;
        unisonIndex_ = 0.0;
        unisonGain_ = 1.0;
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
        filter_.reset();
        sub_.reset();

        filterEnvelope_.noteOn();
        subEnvelope_.noteOn();

        for (auto& envelope : envelopes2_)
            envelope.noteOn();

        for (std::size_t i = 0; i < lfos_.size(); ++i)
            if (parameters_.modulation.lfos[i].retrigger)
                lfos_[i].reset();

        // The sources have just been reset, so the values the first
        // `applyParameters` reads are the ones at t = 0. The engine's chunk
        // grid advances them from there.
        envelopeValues_ = {};
        lfoValues_ = {};
        modulatorsStarted_ = false;

        for (int op = 0; op < kNumOperators; ++op)
        {
            matrix_.setStartPhase (op, 0.0);
            envelopes_[static_cast<std::size_t> (op)].noteOn();
        }
    }

    /// Which copy of the unison stack this voice is, and how many there are.
    ///
    /// Set once at note-on. Everything derived from it -- the detune, the pan
    /// and the index offset -- is recomputed per chunk from the current global
    /// amounts, so this is an identity rather than a set of values.
    void setUnisonSlot (int copy, int count) noexcept
    {
        unisonCount_ = std::max (1, count);
        unisonCopy_ = std::clamp (copy, 0, unisonCount_ - 1);
    }

    /// True for the one copy that carries the sub lane.
    [[nodiscard]] bool carriesSub() const noexcept { return unisonCopy_ == 0; }

    void noteOff() noexcept
    {
        held_ = false;

        filterEnvelope_.noteOff();
        subEnvelope_.noteOff();

        for (auto& envelope : envelopes2_)
            envelope.noteOff();

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
    /// `advanceModulators` is false when this is an extra refresh inside a
    /// control chunk -- the ratio sequencer's step edge (F6) -- because the
    /// modulators are on the chunk grid and running them twice for one chunk
    /// would make an LFO's rate depend on the sequencer's division.
    void applyParameters (const VoiceParameters& parameters,
                          bool advanceModulators = true) noexcept
    {
        parameters_ = parameters;

        // **The whole layer is skipped when no slot is doing anything**, so a
        // patch that uses none of it is bit-identical to a build without it --
        // the destinations are not even read, let alone written with a zero.
        if (parameters.modulation.anyActive())
            applyModulation (advanceModulators);

        for (int op = 0; op < kNumOperators; ++op)
        {
            const auto& settings = parameters_.operators[static_cast<std::size_t> (op)];
            const auto slot = static_cast<std::size_t> (op);

            // The unison copy's detune rides on the operator's own fine tune,
            // so a detuned copy is the same patch a few cents away rather than
            // a differently-voiced one. A fixed-Hz operator is deliberately
            // exempt: a formant does not detune with the note.
            const double cents = settings.fineCents + unisonDetune_;

            const double hz = settings.fixedHz > 0.0
                                ? settings.fixedHz
                                : frequency_ * settings.ratio * std::pow (2.0, cents / 1200.0);

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

            matrix_.setNoiseIndex (op, parameters_.noiseIndices[slot]);

            for (int from = 0; from < kNumOperators; ++from)
            {
                const auto& source = parameters_.operators[static_cast<std::size_t> (from)];
                const double sourceGain = keyScaleGain (source, static_cast<double> (note_))
                                            * velocityGain (source.velIndex, velocity_);

                const double cell = parameters_.indices[slot][static_cast<std::size_t> (from)];

                // **Index spread, and it is the thickest thing here.** Detuning
                // a unison stack in pitch gives you the same timbre several
                // times; offsetting each copy's modulation index gives you
                // several *different* timbres beating against each other, which
                // is what a reese actually is. Only cells that are already
                // doing something are offset -- a connection at zero stays at
                // zero, so the spread cannot switch on a path the patch never
                // asked for.
                const double spread = dsp::isExactlyZero (cell) ? 0.0 : unisonIndex_;

                matrix_.setIndex (op, from, std::max (0.0, (cell + spread) * sourceGain));
            }
        }

        matrix_.refreshQuadratureNeeds();

        applyExtras (parameters_.extras);
    }

    /// Resolve the index cap and install its scale.
    ///
    /// ---------------------------------------------------------------------------
    /// **Kept out of `applyParameters`, and that is not tidiness**
    /// ---------------------------------------------------------------------------
    ///
    /// Everything else in `applyParameters` is arithmetic on numbers already to
    /// hand and costs tens of nanoseconds. This one resolves a bandwidth
    /// prediction by bisection -- `dsp::FmBandwidth::indexScaleFor`, about
    /// **4.6 us** even with both order searches tabled, and seconds before they
    /// were. Running it per voice per 32-sample control chunk is 16 voices at
    /// 6000 chunks a second: 44 % of a core spent deciding how loud the
    /// modulators are allowed to be, on top of the synthesis itself.
    ///
    /// So the engine calls this on its own sample-counted sub-grid instead, and
    /// on note-on so a new voice is never briefly uncapped. The grid is
    /// counted in control chunks, which are themselves anchored to the stream
    /// rather than to the host's block, so the update instants -- and therefore
    /// the output -- stay independent of the buffer size (CLAUDE.md section 7).
    ///
    /// It reads the voice's **modulated** copy, which `applyParameters` has
    /// always just written -- both call sites push parameters first. A slot
    /// aimed at `matrixDepth` or an operator's feedback moves exactly the
    /// numbers the bandwidth prediction is made of, so capping the patch's
    /// values instead would leave the cap protecting a spectrum nobody is
    /// listening to.
    void refreshIndexCap (double internalRate) noexcept
    {
        matrix_.setIndexScale (capScaleFor (parameters_, internalRate));
    }

    /// Run the sources forward and add every live slot into `parameters_`.
    ///
    /// Written against the modulated COPY rather than the patch, so a slot can
    /// stack on another slot's result and the patch itself is never touched.
    void applyModulation (bool advance) noexcept
    {
        const auto& settings = parameters_.modulation;

        for (std::size_t i = 0; i < envelopes2_.size(); ++i)
        {
            auto& envelope = envelopes2_[i];
            const auto& shape = settings.envelopes[i];

            envelope.setPointCount (shape.pointCount);
            envelope.setSustainIndex (shape.sustain);
            envelope.setLoopStart (shape.loopStart);
            envelope.setLoop (shape.loop);

            for (int p = 0; p < dsp::MultiEnvelope::kMaxPoints; ++p)
            {
                const auto& point = shape.points[static_cast<std::size_t> (p)];
                envelope.setPoint (p, point.seconds, point.level, point.tension);
            }
        }

        for (std::size_t i = 0; i < lfos_.size(); ++i)
        {
            const auto& shape = settings.lfos[i];

            lfos_[i].setWave (static_cast<dsp::Lfo::Wave> (shape.wave));
            lfos_[i].setRateHz (shape.rateHz);
            lfos_[i].setSmooth (shape.smooth);
            lfos_[i].setPhaseOffset (shape.phaseOffset);
        }

        // The first refresh after a note-on reads the sources where the
        // note-on left them rather than a chunk into their life: `noteOn` has
        // just reset them, and advancing before the first read would start
        // every modulation envelope 32 internal samples ahead of the amp
        // envelope beside it. After that the chunk grid drives them.
        if (advance && modulatorsStarted_)
        {
            // One envelope sample per internal sample, which is what a
            // breakpoint envelope in seconds means; the LFO takes the whole
            // chunk at once. Only the READ is on the chunk grid -- the timing
            // is exact, and a modulation envelope that ran at a 32nd of the
            // rate would drift against the amp envelope beside it.
            for (std::size_t i = 0; i < envelopes2_.size(); ++i)
            {
                double value = envelopeValues_[i];

                for (int n = 0; n < kModulationChunk; ++n)
                    value = envelopes2_[i].process();

                envelopeValues_[i] = value;
            }

            for (std::size_t i = 0; i < lfos_.size(); ++i)
                lfoValues_[i] = lfos_[i].advance (kModulationChunk);
        }
        else if (advance)
        {
            // Read where the reset left them, then hand the grid the baton.
            for (std::size_t i = 0; i < lfos_.size(); ++i)
                lfoValues_[i] = lfos_[i].advance (0);

            modulatorsStarted_ = true;
        }

        for (const auto& slot : settings.slots)
        {
            if (slot.source == source::none || slot.destination == dest::off
                || dsp::isExactlyZero (slot.amount))
                continue;

            addModulation (slot.destination, slot.amount * sourceValue (slot.source, settings));
        }
    }

    [[nodiscard]] double sourceValue (int which,
                                      const ModulationSettings& settings) const noexcept
    {
        switch (which)
        {
            case source::env1: return envelopeValues_[0];
            case source::env2: return envelopeValues_[1];
            case source::lfo1: return lfoValues_[0];
            case source::lfo2: return lfoValues_[1];
            case source::velocity: return velocity_;

            // 0 at the bottom of the keyboard, 1 at the top, so a positive
            // amount always means "more as you play up".
            case source::key: return static_cast<double> (note_) / 127.0;

            case source::macro1: return settings.macros[0];
            case source::macro2: return settings.macros[1];
            case source::macro3: return settings.macros[2];
            case source::macro4: return settings.macros[3];

            default: return 0.0;
        }
    }

    /// Add `value` to one destination of the modulated copy.
    ///
    /// The per-operator groups are contiguous and in operator order (asserted
    /// in `Modulation.hpp`), so the operator falls out of a subtraction rather
    /// than needing a table that could drift from the list.
    void addModulation (int destination, double value) noexcept
    {
        const auto operatorAt = [destination] (int first)
        {
            return static_cast<std::size_t> (destination - first);
        };

        if (destination >= dest::op1Ratio && destination <= dest::op6Ratio)
        {
            auto& ratio = parameters_.operators[operatorAt (dest::op1Ratio)].ratio;
            ratio = std::clamp (ratio + value, 0.03125, 64.0);
            return;
        }

        if (destination >= dest::op1Character && destination <= dest::op6Character)
        {
            auto& character = parameters_.operators[operatorAt (dest::op1Character)].character;
            character = std::clamp (character + value, 0.0, 1.0);
            return;
        }

        if (destination >= dest::op1Level && destination <= dest::op6Level)
        {
            auto& level = parameters_.operators[operatorAt (dest::op1Level)].level;
            level = std::clamp (level + value, 0.0, 1.0);
            return;
        }

        if (destination >= dest::op1Feedback && destination <= dest::op6Feedback)
        {
            auto& feedback = parameters_.operators[operatorAt (dest::op1Feedback)].feedback;
            feedback = std::clamp (feedback + value, 0.0, dsp::FmOperator::kMaxFeedback);
            return;
        }

        if (destination >= dest::op1Fold && destination <= dest::op6Fold)
        {
            auto& fold = parameters_.operators[operatorAt (dest::op1Fold)].fold;
            fold = std::clamp (fold + value, 0.0, 1.0);
            return;
        }

        switch (destination)
        {
            case dest::matrixDepth:
            {
                // Every live cell together, as a scale rather than an offset:
                // adding a fixed amount to thirty cells would switch on paths
                // the patch never asked for, which is the same trap the unison
                // index spread avoids.
                const double scale = std::max (0.0, 1.0 + value);

                for (auto& row : parameters_.indices)
                    for (auto& cell : row)
                        cell *= scale;

                break;
            }

            case dest::filterCutoff:
                // In octaves, because that is how a filter is heard.
                parameters_.extras.cutoffHz = std::clamp (
                    parameters_.extras.cutoffHz * std::exp2 (value), 20.0, 20000.0);
                break;

            case dest::filterResonance:
                parameters_.extras.resonance
                    = std::clamp (parameters_.extras.resonance + value, 0.0, 1.0);
                break;

            case dest::filterMorph:
                parameters_.extras.morph
                    = std::clamp (parameters_.extras.morph + value, 0.0, 1.0);
                break;

            case dest::filterDrive:
                parameters_.extras.drive
                    = std::clamp (parameters_.extras.drive + value, 0.0, 1.0);
                break;

            case dest::filterSing:
                parameters_.extras.sing
                    = std::clamp (parameters_.extras.sing + value, 0.0, 1.0);
                break;

            case dest::subLevel:
                parameters_.extras.subLevel
                    = std::clamp (parameters_.extras.subLevel + value, 0.0, 1.0);
                break;

            default:
                break;
        }
    }

    /// The filter, the sub lane and the unison offsets.
    void applyExtras (const VoiceExtras& extras) noexcept
    {
        // Key tracking is exponential in the note, so at 1 the filter follows
        // the keyboard exactly and the timbre is constant across it -- which is
        // what "tracking" means and what a fixed cutoff is not.
        const double tracked = frequency_ / 440.0;
        const double trackScale = std::pow (tracked, extras.keyTrack);

        filterCutoff_ = extras.cutoffHz * trackScale;
        filterEnvAmount_ = extras.envAmount;

        filter_.setMorph (extras.morph);
        filter_.setResonance (extras.resonance);
        filter_.setDrive (extras.drive);
        filter_.setSing (extras.sing);

        filterEnvelope_.setAttackSeconds (extras.filterAttack);
        filterEnvelope_.setDecaySeconds (extras.filterDecay);
        filterEnvelope_.setSustain (extras.filterSustain);
        filterEnvelope_.setReleaseSeconds (extras.filterRelease);

        subLevel_ = extras.subLevel;
        sub_.setShape (extras.subShape == 1 ? dsp::OscShape::triangle : dsp::OscShape::sine);
        // `Oscillator` takes a phase increment rather than a frequency: it has
        // no sample rate of its own, deliberately, so the same object can run
        // at whatever rate the caller is at.
        sub_.setIncrement (frequency_ * std::pow (2.0, static_cast<double> (extras.subOctave))
                             / sampleRate_);

        subEnvelope_.setAttackSeconds (extras.subAttack);
        subEnvelope_.setDecaySeconds (extras.subDecay);
        subEnvelope_.setSustain (extras.subSustain);
        subEnvelope_.setReleaseSeconds (extras.subRelease);

        // The copy's own share of each global amount. `position` runs -1 to +1
        // across the stack, so a lone voice sits at 0 and is untouched by all
        // three -- which is what makes unison exactly inert at a count of one.
        const double position = unisonCount_ > 1
                                  ? 2.0 * static_cast<double> (unisonCopy_)
                                        / static_cast<double> (unisonCount_ - 1)
                                      - 1.0
                                  : 0.0;

        unisonDetune_ = extras.unisonDetuneCents * position;
        unisonPan_ = extras.unisonSpread * position;

        // **Index spread, and it is the thickest thing here.** Detuning a stack
        // gives you the same timbre several times; offsetting each copy's
        // modulation index gives you several *different* timbres beating
        // against each other, which is what a reese actually is.
        unisonIndex_ = extras.unisonIndexSpread * position;

        // 1/sqrt(n), so a thicker stack is not simply a louder one. Applied to
        // the matrix only: the sub lane below is one oscillator on one copy and
        // stays at the level it was set to however thick the stack gets.
        unisonGain_ = unisonCount_ > 1
                        ? 1.0 / std::sqrt (static_cast<double> (unisonCount_))
                        : 1.0;
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

        // **What keeps a voice alive is what can be HEARD, not what is
        // running.** The filter envelope shapes something that must itself be
        // sounding, so it never justifies a voice on its own; the sub envelope
        // only does on the one copy that carries the lane, and only while the
        // lane has a level. Counting either unconditionally is the Sonitus
        // zombie again in a new place -- a silent voice that still costs a
        // voice slot and a sample loop, and that no silence-based test can
        // see. So: assert activity, not silence (CLAUDE.md section 7).
        bool anyActive = carriesSub() && ! dsp::isExactlyZero (subLevel_)
                           && subEnvelope_.isActive();
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

        // The filter, per voice, with its own envelope opening it. Skipped
        // entirely when it is wide open and doing nothing, so a patch that does
        // not use it pays nothing and is bit-identical to one built without it.
        const double filterEnv = filterEnvelope_.process();

        if (filterCutoff_ < kFilterBypassHz || filterEnvAmount_ > 0.0
            || filter_.getResonance() > 0.0)
        {
            const double octaves = filterEnvAmount_ * filterEnv;
            filter_.setCutoffHz (filterCutoff_ * std::pow (2.0, octaves));

            voiceLeft = filter_.process (voiceLeft);
            voiceRight = filter_.process (voiceRight);
        }

        // Applied here rather than at the end, so the sub below rides at its
        // own level whatever the stack thickness is. Exactly 1.0 at a count of
        // one, so a patch that does not use unison is untouched.
        if (unisonGain_ < 1.0)
        {
            voiceLeft *= unisonGain_;
            voiceRight *= unisonGain_;
        }

        // **The sub never goes through any of that.** Not the matrix, not the
        // filter, and (from F7) not the mangle chain either. On a DnB rig that
        // is the difference between a bass that survives a club system and one
        // that collapses the moment the growl bites -- and it is why the lane
        // exists at all rather than being another operator.
        //
        // **And only one copy of the stack carries it.** Eight unison voices
        // each adding their own sub is eight sub oscillators a few cents apart,
        // which is a chorused mush exactly where the track needs one solid
        // fundamental. `carriesSub()` picks copy 0 and the rest run the
        // envelope without summing it, so the lane costs nothing on them and
        // its state stays in step for a retrigger.
        const double subEnv = subEnvelope_.process();

        if (carriesSub() && ! dsp::isExactlyZero (subLevel_))
        {
            const double value = sub_.advance() * subEnv * subLevel_;
            voiceLeft += value;
            voiceRight += value;
        }

        // Unison pan, constant power, computed per sample only because it is
        // two multiplies against a pair precomputed at note-on.
        const double gain = parameters_.masterLevel * velocity_;

        left += voiceLeft * gain * unisonLeftGain();
        right += voiceRight * gain * unisonRightGain();
    }

    void setSeed (std::uint64_t seed) noexcept { noise_.seed (seed); }

private:
    /// Above this the filter is doing nothing a listener could hear, so it is
    /// skipped -- and skipped means bit-identical, not nearly.
    static constexpr double kFilterBypassHz = 19000.0;

    [[nodiscard]] double unisonLeftGain() const noexcept
    {
        return dsp::isExactlyZero (unisonPan_)
                 ? 1.0
                 : std::cos (0.25 * std::numbers::pi * (unisonPan_ + 1.0)) * std::numbers::sqrt2;
    }

    [[nodiscard]] double unisonRightGain() const noexcept
    {
        return dsp::isExactlyZero (unisonPan_)
                 ? 1.0
                 : std::sin (0.25 * std::numbers::pi * (unisonPan_ + 1.0)) * std::numbers::sqrt2;
    }

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

    dsp::SvfFilter filter_;
    dsp::Adsr filterEnvelope_ {};
    double filterCutoff_ { 20000.0 };
    double filterEnvAmount_ { 0.0 };

    dsp::Oscillator sub_;
    dsp::Adsr subEnvelope_ {};
    double subLevel_ { 0.0 };

    int unisonCopy_ { 0 };
    int unisonCount_ { 1 };
    double unisonDetune_ { 0.0 };
    double unisonPan_ { 0.0 };
    double unisonIndex_ { 0.0 };
    double unisonGain_ { 1.0 };

    /// How many samples the modulators advance per control chunk. Mirrors
    /// `StrydaEngine::kControlChunk` rather than including the engine, which
    /// would be a circular dependency -- the test asserts they agree.
    static constexpr int kModulationChunk = 32;

    std::array<dsp::MultiEnvelope, 2> envelopes2_ {};
    std::array<dsp::Lfo, 2> lfos_ {};
    std::array<double, 2> envelopeValues_ {};
    std::array<double, 2> lfoValues_ {};
    bool modulatorsStarted_ { false };
};

} // namespace tezla::stryda
