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
#include <tezla/dsp/Exact.hpp>
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

    /// **The second exciter, and the blend between them.** A real strike is a
    /// contact *and* a scrape: a mallet with a fingernail on it, a bow started
    /// with a pluck. One choice cannot say that.
    ///
    /// `exciterBlend` is how much of slot B there is, and it is a **lerp on
    /// the excitation amounts**, `a * (1 - t) + b * t`, which is exact at both
    /// ends rather than only at zero. At 0 slot B is never touched at all and
    /// the voice is bit for bit the one that shipped; at 1 slot A is not, and
    /// the voice is exactly slot B played alone.
    ///
    /// Slot B's default is Pluck rather than Mallet so that turning the blend
    /// up does something audible immediately. Neutrality lives in the blend,
    /// not in the choice: at 0 what B is set to cannot be heard.
    Exciter exciterB { Exciter::Pluck };
    double exciterBlend { 0.0 };        ///< 0 = A alone, 1 = B alone

    double hardness { 0.5 };

    /// **How much of Hardness comes from velocity instead of the knob.**
    ///
    /// On a real drum a soft hit is felt and a hard hit is stick, because the
    /// same mallet compresses differently -- the contact gets *shorter* as it
    /// gets harder, and a short contact is a bright one. `contactSeconds`
    /// already models that; this is what connects it to the keyboard.
    ///
    /// `effective = hardness * (1 - amount) + velocity * amount`, so at 0 the
    /// knob is the whole answer (exactly -- `h * 1.0 + 0.0` is `h`) and at 1
    /// velocity is. Resolved once at note-on, so a roll's re-strikes stay the
    /// hardness the note was struck with.
    double hardnessVelocity { 0.0 };

    /// **Where you listen from.** Two output taps at two points on the
    /// object, left and right.
    ///
    /// The strike is already combed by `sin(k pi p)`: where you *hit* it
    /// decides which modes you excite, and a mode with a node under the mallet
    /// gets exactly nothing. Where you *listen* decides which modes you hear,
    /// by the same expression, and until now there was one listening point and
    /// it was implicit -- every mode weighted equally, which is not any real
    /// point on a real object but is what a plain modal sum is.
    ///
    /// So `listenAmount` is a blend between that convention and a real point:
    /// `(1 - a) * 1 + a * sin(k pi q)`. At 0 both taps are the sum that
    /// shipped, bit for bit, and the plugin is mono exactly as before. At 1
    /// both are real listening points, and the stereo image is the geometry
    /// rather than a widener.
    ///
    /// **The mono sum can cancel**, and that is physics rather than a bug: two
    /// taps either side of a mode's node hear it in opposite phase, so summing
    /// them removes it. Measured across the position grid rather than assumed;
    /// the numbers are in the README and the tooltip.
    double listenLeft { 0.5 };
    double listenRight { 0.5 };
    double listenAmount { 0.0 };

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

/// What object these settings describe: every partial's frequency in Hz,
/// written into `frequencies`, returning how many are AUDIBLE at this rate.
///
/// Material morph, then the inharmonicity stretch, then Overtone Lock
/// rooted on the fundamental. Partials that would land above 0.45 fs are
/// dropped rather than folded onto the frequency clamp, which is what keeps
/// the strike band-limited by construction -- so the count returned is the
/// count that sounds, and raising Partials past it adds nothing.
///
/// Free, and pure, because two callers need exactly the same answer: the
/// voice building a note, and the editor drawing the object before a key
/// has been touched. A second definition would let the picture and the
/// sound disagree, which is the one thing this visualiser must never do.
[[nodiscard]] inline int buildModeFrequencies (double* frequencies,
                                               const VoiceSettings& settings,
                                               double fundamentalHz,
                                               const dsp::Scale& lockScale,
                                               double sampleRate) noexcept
{
    const int partials = settings.partials < 1 ? 1
                       : settings.partials > dsp::ModalResonator::kMaxModes
                           ? dsp::ModalResonator::kMaxModes
                           : settings.partials;

    const double ceiling = 0.45 * (sampleRate > 0.0 ? sampleRate : 44100.0);

    int audible = 0;

    for (int mode = 0; mode < partials; ++mode)
    {
        const double ratio = dsp::ModeShapes::ratioAt (settings.material, mode,
                                                       settings.stretch);
        double frequency = fundamentalHz * ratio;

        if (settings.lockAmount > 0.0)
            frequency = dsp::ModeShapes::lockToScale (frequency, fundamentalHz,
                                                      lockScale, settings.lockAmount);

        frequencies[mode] = frequency;

        if (frequency < ceiling)
            ++audible;
    }

    return audible;
}

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
        dcBlockerRight_.prepare (sampleRate_, 10.0);
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
        dcBlockerRight_.reset();
        gate_.reset();

        noise_.setSeed (seed);
        roll_.setSeed (seed ^ 0x517CC1B727220A95ULL);

        drop_.reset();
        drop_.trigger (settings_.dropSemitones, settings_.dropSeconds);

        // The gate closes on the object's own timescale, so Decay stays
        // live across its whole range. Without this the vactrol's fixed
        // 1.5 s close dominated every longer setting -- a 4-second object
        // rendered a 0.33-second note -- which the close-out measurement
        // caught.
        //
        // Four times the object's decay, chosen by measurement: the
        // audible note then lands at 0.47x the asked T60 (0.31x at two,
        // 0.56x at six, saturating because the vactrol's early drop is
        // fast whatever its full close time). It is always shorter than
        // the object's own ring, and that is the instrument rather than a
        // defect -- a low-pass gate IS an amplitude envelope. The Decay
        // tooltip states the factor rather than implying the note lasts
        // as long as the object does.
        gate_.setDecayScale (4.0 * settings_.decaySeconds
                               / dsp::LowpassGate::kNaturalCloseSeconds);

        rebuildModes (lockScale);
        applyDrop();
        buildListeningWeights();

        // Hardness resolved once, here, so a roll's re-strikes stay the
        // hardness the note was struck with rather than drifting if the knob
        // moves mid-ring. `h * 1.0 + v * 0.0` is exactly `h`, which is what
        // makes the zero amount bit-exact rather than merely close.
        const double amount = settings_.hardnessVelocity < 0.0 ? 0.0
                            : settings_.hardnessVelocity > 1.0 ? 1.0
                            : settings_.hardnessVelocity;

        hardness_ = settings_.hardness * (1.0 - amount) + velocity_ * amount;

        const double blend = settings_.exciterBlend < 0.0 ? 0.0
                           : settings_.exciterBlend > 1.0 ? 1.0
                           : settings_.exciterBlend;

        // A lerp on the excitation amounts, `a * (1 - t) + b * t`, spelt as
        // two weights. Exact at *both* ends -- the reason it is written this
        // way rather than as `a + t * (b - a)`, which is only exact at zero --
        // and a weight of exactly zero skips its slot entirely, so neither
        // exciter can leave a trace at the end that belongs to the other.
        weightA_ = 1.0 - blend;
        weightB_ = blend;

        bowing_ = false;
        rolling_ = false;
        contactWeight_ = 0.0;

        applyExciter (settings_.exciter, weightA_);
        applyExciter (settings_.exciterB, weightB_);

        // One gate ping and one scrape burst for the whole contact, from the
        // summed weight, because neither is linear in it -- see strikeModes.
        // Two slots that both strike sum to exactly 1, so a single-exciter
        // note is `velocity_ * 1.0`, which is `velocity_`.
        if (! dsp::isExactlyZero (contactWeight_))
        {
            if (settings_.noiseAmount > 0.0)
                noise_.trigger (hardness_,
                                settings_.noiseAmount * velocity_ * contactWeight_);

            gate_.ping (velocity_ * contactWeight_);
        }

        // The bow is continuous, so its hold has to be the *total* the two
        // slots asked for rather than whichever ran last. Both slots being
        // Bow is a legal setting and this is what makes it mean "bowed".
        if (bowing_)
        {
            bow_.setPressure (settings_.bowPressure * velocity_ * bowWeight_);
            bow_.setSpeed (settings_.bowSpeed);
            gate_.setHold (velocity_ * bowWeight_);
        }
    }

    /// The hardness this note was actually struck with, after the velocity
    /// amount. Exposed because the law is the claim and a rendered note is a
    /// blunt way to check it: the difference between the two spellings of a
    /// lerp is one ulp, and one ulp of hardness is inaudible but is exactly
    /// what the exactness rule is about.
    [[nodiscard]] double getStrikeHardness() const noexcept { return hardness_; }

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

    /// Renders and ADDS `count` samples into `left` and `right`.
    ///
    /// With `listenAmount` at zero the two taps are the same number, computed
    /// once -- the mono path that shipped, unchanged and unbranched inside the
    /// sample loop.
    void render (double* left, double* right, int count) noexcept
    {
        if (! isActive())
            return;

        // Either slot can be the bow or the roll, so these are the flags the
        // note-on dispatch set rather than a second reading of the choice.
        const bool bowing = held_ && bowing_;
        const bool rolling = held_ && rolling_;

        for (int n = 0; n < count; ++n)
        {
            if (rolling)
            {
                const double restrike = roll_.next();

                // Scaled by the roll slot's own weight, so a roll blended
                // half-and-half with a pluck keeps rolling at half strength
                // rather than at full.
                if (restrike > 0.0)
                    strike (restrike * rollWeight_);
            }

            double input = noise_.next();

            if (bowing)
                input += bow_.force (bank_.contactVelocity());

            const double rung = bank_.process (input);

            if (! listening_)
            {
                const double sample = gate_.process (dcBlocker_.process (rung));

                left[n] += sample;
                right[n] += sample;
                continue;
            }

            // The two taps, from the terms `process` just summed.
            double tapLeft = 0.0;
            double tapRight = 0.0;

            const int partials = bank_.getModeCount();

            for (int mode = 0; mode < partials; ++mode)
            {
                const double term = bank_.modeOutput (mode);

                tapLeft += listenLeft_[mode] * term;
                tapRight += listenRight_[mode] * term;
            }

            // A DC blocker per tap, because it is a filter and filters are
            // per-signal -- but **one** gate, because a vactrol is a physical
            // part and there is one of it. Running `gate_.process` twice would
            // advance the cell twice a sample and halve the note's length.
            double l = dcBlocker_.process (tapLeft);
            double r = dcBlockerRight_.process (tapRight);

            gate_.processStereo (l, r);

            left[n] += l;
            right[n] += r;
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
    /// The object this note's settings describe, plus the decay and drive
    /// weights that go with it. The frequencies come from the shared
    /// buildModeFrequencies() so the editor's picture cannot disagree with
    /// what actually sounds.
    void rebuildModes (const dsp::Scale& lockScale) noexcept
    {
        const int partials = settings_.partials < 1 ? 1
                           : settings_.partials > dsp::ModalResonator::kMaxModes
                               ? dsp::ModalResonator::kMaxModes
                               : settings_.partials;

        bank_.setModeCount (partials);

        (void) buildModeFrequencies (base_, settings_, fundamental_, lockScale,
                                     sampleRate_);

        const double ceiling = 0.45 * sampleRate_;

        for (int mode = 0; mode < partials; ++mode)
        {
            const double ratio = dsp::ModeShapes::ratioAt (settings_.material,
                                                           mode, settings_.stretch);
            const bool audible = base_[mode] < ceiling;

            t60_[mode] = settings_.decaySeconds
                       * std::pow (ratio, -2.0 * settings_.tilt);
            gain_[mode] = audible ? 1.0 / partials : 0.0;

            // The mode shape at the contact point, NOT divided by the
            // partial count: this is how hard a force applied there drives
            // each mode, and it is also the path the bow's own feedback
            // travels. Dividing it by the count put a 64-partial object's
            // bow 64x below its onset -- silent, while the same settings
            // sang on the bare bank. Output loudness is the gain's job,
            // just below.
            bank_.setInputWeight (mode,
                audible ? positionWeight (mode + 1, settings_.position) : 0.0);
        }
    }

    void applyDrop() noexcept
    {
        const double multiplier = drop_.multiplier();
        const int partials = bank_.getModeCount();

        for (int mode = 0; mode < partials; ++mode)
            bank_.setMode (mode, base_[mode] * multiplier, t60_[mode], gain_[mode]);
    }

    /// The two listening taps' per-mode weights, from the positions and the
    /// amount. Built once at note-on: they depend on the mode *index* and the
    /// positions, neither of which moves during a note, so the sample loop is
    /// two dot products and nothing else.
    ///
    /// `(1 - a) * 1 + a * sin(k pi q)` -- the lerp form, exact at both ends,
    /// so at amount 0 every weight is exactly 1.0 and the taps are exactly the
    /// sum `process` computed. The render skips them entirely there anyway;
    /// this is what makes the two paths agree rather than merely nearly agree.
    void buildListeningWeights() noexcept
    {
        const double amount = settings_.listenAmount < 0.0 ? 0.0
                            : settings_.listenAmount > 1.0 ? 1.0
                            : settings_.listenAmount;

        listening_ = ! dsp::isExactlyZero (amount);

        if (! listening_)
            return;

        const int partials = bank_.getModeCount();

        for (int mode = 0; mode < partials; ++mode)
        {
            const int k = mode + 1;

            listenLeft_[mode] = (1.0 - amount)
                                  + amount * positionWeight (k, settings_.listenLeft);
            listenRight_[mode] = (1.0 - amount)
                                   + amount * positionWeight (k, settings_.listenRight);
        }
    }

    /// One slot's contribution, at `weight`. Zero does nothing at all, which
    /// is what makes a blend at either end bit for bit the single exciter.
    void applyExciter (Exciter which, double weight) noexcept
    {
        if (dsp::isExactlyZero (weight))
            return;

        switch (which)
        {
            case Exciter::Mallet:
                strikeModes (weight);
                contactWeight_ += weight;
                break;

            case Exciter::Pluck:
                pluckModes (weight);
                contactWeight_ += weight;
                break;

            case Exciter::Roll:
                // The clock is started once however many slots roll, for the
                // same reason the bow resets once: a second `trigger` restarts
                // the bouncing-ball sequence and throws away the first slot's.
                // The weight accumulates, so Roll in both slots rolls at full
                // strength rather than at whatever the second slot asked for.
                if (! rolling_)
                {
                    roll_.trigger (settings_.rollStartSeconds, settings_.rollRatio,
                                   settings_.rollMinimumSeconds, settings_.rollHumanise);
                    rollWeight_ = 0.0;
                }

                rolling_ = true;
                rollWeight_ += weight;
                strikeModes (weight);
                contactWeight_ += weight;
                break;

            case Exciter::Bow:
                // Reset once however many slots are bowing: a second reset
                // would throw away the first slot's setup. The pressure is
                // applied by the caller, from the summed weight.
                if (! bowing_)
                {
                    bow_.reset();
                    bow_.resetClampExcess();
                    bowWeight_ = 0.0;
                }

                bowing_ = true;
                bowWeight_ += weight;
                break;

            case Exciter::count:
                break;
        }
    }

    /// One mallet contact at the CURRENT mode frequencies (mid-drop, a
    /// re-strike excites the glided object, as a real hand would) -- the
    /// modal excitation only.
    ///
    /// Split from the gate ping and the scrape burst because **neither of
    /// those is linear in the amount**, so two slots each asking for half
    /// cannot each call them. `LowpassGate::ping` takes the *maximum* of what
    /// it is given, so two pings at 0.5 leave the gate at 0.5 where one at 1.0
    /// leaves it at 1.0; `NoiseBurst::trigger` restarts the burst, so the
    /// second slot's call discards the first's. Measured before the split: a
    /// mallet blended half-and-half with a pluck rendered at RMS 0.0016
    /// against 0.0038 and 0.0027 at its two ends -- a hole in the middle of
    /// the control, from the gate rather than from anything acoustic.
    void strikeModes (double amount) noexcept
    {
        double frequencies[dsp::ModalResonator::kMaxModes];
        double amounts[dsp::ModalResonator::kMaxModes];
        const int partials = bank_.getModeCount();

        for (int mode = 0; mode < partials; ++mode)
            frequencies[mode] = bank_.getModeFrequency (mode);

        malletWeights (amounts, frequencies, partials, settings_.position,
                       hardness_, velocity_ * amount);

        for (int mode = 0; mode < partials; ++mode)
            if (gain_[mode] > 0.0)
                bank_.excite (mode, amounts[mode]);

    }

    /// The whole contact: modes, scrape and gate. What a re-strike is.
    void strike (double amount) noexcept
    {
        strikeModes (amount);

        if (settings_.noiseAmount > 0.0)
            noise_.trigger (hardness_,
                            settings_.noiseAmount * velocity_ * amount);

        gate_.ping (velocity_ * amount);
    }

    /// One pluck at `amount` of full strength -- the modal excitation only,
    /// for the same reason `strikeModes` exists.
    void pluckModes (double amount) noexcept
    {
        double amounts[dsp::ModalResonator::kMaxModes];
        const int partials = bank_.getModeCount();

        pluckWeights (amounts, partials, settings_.position, velocity_ * amount);

        for (int mode = 0; mode < partials; ++mode)
            if (gain_[mode] > 0.0)
                bank_.excite (mode, amounts[mode]);
    }

    double sampleRate_ { 44100.0 };

    dsp::ModalResonator bank_;
    TensionDrop drop_;
    dsp::LowpassGate gate_;
    NoiseBurst noise_;
    RollClock roll_;
    Bow bow_;
    dsp::DcBlocker<double> dcBlocker_;
    dsp::DcBlocker<double> dcBlockerRight_;

    VoiceSettings settings_;

    /// Resolved at note-on from Hardness and the velocity amount, so a roll's
    /// re-strikes and the scrape burst all agree with the first contact.
    double hardness_ { 0.5 };

    /// The two slots' weights, and which continuous exciters are running.
    double weightA_ { 1.0 };
    double weightB_ { 0.0 };
    double rollWeight_ { 0.0 };
    double bowWeight_ { 0.0 };
    double contactWeight_ { 0.0 };
    bool bowing_ { false };
    bool rolling_ { false };

    /// False at amount 0, which is the mono path that shipped.
    bool listening_ { false };
    double listenLeft_[dsp::ModalResonator::kMaxModes] {};
    double listenRight_[dsp::ModalResonator::kMaxModes] {};

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
