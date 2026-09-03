// Copyright (c) 2026 The Tezla <thetezla@proton.me>
// Created by The Tezla -- https://github.com/wingit33/tezla.tech
// Music: https://soundcloud.com/thetezla | https://thetezla.bandcamp.com
// Built with development assistance from Claude (Anthropic).
// SPDX-License-Identifier: AGPL-3.0-only
// GNU AGPLv3 (see LICENSE), plus NOTICE.md's attribution term. Keep intact.

#pragma once

// One Sonitus voice: everything that happens per note.
//
// Framework-free. It takes a note number, a velocity and a sample rate, and
// writes into a stereo pair -- so the whole instrument can be played, swept and
// measured offline before any of it reaches a DAW.
//
// ---------------------------------------------------------------------------
// The chain
// ---------------------------------------------------------------------------
//
//   OSC A -- unison x1..7 --+-- ring mod --+
//     |  (sync master)      |              |
//   OSC B -- unison x1..7 --+              +-- mix --> FOLDER --> FILTER --> VCA
//     ^  (sync slave)                      |                        ^
//     +-- PM from OSC A                    |                        |
//   SUB  (sine or square, -1 / -2 oct) ----+                  OSC A at audio rate
//
// ---------------------------------------------------------------------------
// Two things here are not the obvious choice
// ---------------------------------------------------------------------------
//
// **The sub bypasses the filter.** Every other source runs through it; the sub
// is added afterwards, at the VCA. That is not a shortcut -- it is the reason
// the instrument is usable on a real track. A growl is a filter being swept
// hard, and a sub that goes with it is a sub that disappears every time the
// cutoff drops. Keeping the sub out of the filter means the low end stays
// exactly where it was put while everything above it is being mangled. The
// global mangle section splits the two again later for the same reason.
//
// **The filter FM source is OSC A's raw output, not the mixed signal.** Feeding
// the filter's own input back into its cutoff is a feedback path around a
// nonlinearity with no bound; feeding it a dedicated oscillator is a modulator,
// which is what filter FM is. It also means the FM depth is independent of how
// loud the note is, which is what a player expects.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <numbers>

#include <tezla/dsp/Adaa.hpp>
#include <tezla/dsp/Adsr.hpp>
#include <tezla/dsp/MultiEnvelope.hpp>
#include <tezla/dsp/Denormals.hpp>
#include <tezla/dsp/Exact.hpp>
#include <tezla/dsp/Oscillator.hpp>
#include <tezla/dsp/SmoothedValue.hpp>
#include <tezla/dsp/SvfFilter.hpp>
#include <tezla/dsp/UnisonBank.hpp>
#include <tezla/dsp/Waveshapers.hpp>

#include "StackShapes.hpp"

namespace tezla::sonitus {

using dsp::Adsr;
using dsp::OscShape;
using dsp::Oscillator;
using dsp::SvfFilter;
using dsp::SvfMode;
using dsp::UnisonBank;

/// What the sub oscillator plays.
///
/// **Append-only** -- a choice parameter stores an index. CLAUDE.md section 8.
enum class SubShape
{
    sine = 0,
    square,

    count
};

/// Where a modulation slot gets its value.
///
/// **Append-only.** A slot stores its source as an index, so inserting an entry
/// silently repoints every saved modulation in every project -- the plugin
/// still loads, still runs, and quietly modulates from the wrong place.
/// CLAUDE.md section 8, and the same rule the `dest::` lists in the other
/// plugins live under.
///
/// The first four are **per voice**: each note has its own envelopes, its own
/// velocity and its own random value, which is what makes a chord sound like
/// several notes. The last three are **global** and tempo-locked, because two
/// voices whose LFOs had drifted apart would be two instruments.
enum class ModSource
{
    none = 0,
    ampEnvelope,
    modEnvelope1,
    modEnvelope2,
    velocity,
    keyTrack,
    noteRandom,
    lfo1,
    lfo2,
    sequencer,

    /// The three ADV envelopes. **Appended** -- a stored slot is an index into
    /// this list, CLAUDE.md section 8. A disabled ADV envelope reads 0.
    advEnv1,
    advEnv2,
    advEnv3,

    /// The four macros. Appended for the same reason.
    ///
    /// A macro is one knob wired to several things at once, which is the thing
    /// a matrix structurally cannot give: a matrix row has one source and one
    /// destination, so "this knob opens the filter *and* adds drive *and*
    /// widens the unison" costs three rows and three depths that then have to
    /// be kept in step by hand. Assign the same macro in three rows and one
    /// control moves all three, each by its own amount.
    ///
    /// They are plain values rather than generators -- there is nothing to
    /// tick, and a macro at its default of 0 contributes exactly nothing, so
    /// an unassigned macro is free and a project that never heard of them is
    /// untouched.
    macro1,
    macro2,
    macro3,
    macro4,

    /// **Sag** -- the machine's temperature, in [-1, 1]. Appended, like every
    /// entry here (CLAUDE.md section 8).
    ///
    /// Global rather than per note, like the LFOs and the sequencer already in
    /// this list, and it reads the same number here as in the global matrix --
    /// there is one walk for the whole instrument, which is the point of it.
    /// It reads the walk even when the Sag depth is 0: the depth knob is how
    /// much reaches the voice *directly*, not whether the machine has a
    /// temperature.
    sag,

    count
};

/// What a modulation slot moves.
///
/// **Append-only**, for the same reason and with more at stake: a destination
/// index that shifts turns a filter sweep into a pitch sweep.
///
/// Continuous controls only, and by construction rather than by care. A choice
/// or a switch reconfigures rather than adjusts -- modulating one means a
/// filter rebuild or a crossfade per chunk -- so the shapes, the filter mode
/// and the sync switch are not here even though they are parameters like any
/// other.
enum class ModDestination
{
    none = 0,
    cutoff,            ///< octaves
    resonance,
    filterDrive,
    pmIndex,
    pulseWidthA,
    pulseWidthB,
    detuneA,
    detuneB,
    oscMix,            ///< A against B
    subLevel,
    ringAmount,
    foldAmount,
    pitch,             ///< cents, the whole voice
    pitchB,            ///< cents, oscillator B alone -- the sync sweep
    level,

    /// How deep the period doubling goes. **Appended**, because a stored slot
    /// is an index into this list -- CLAUDE.md section 8.
    kargyraa,

    /// The two morphs. Appended for the same reason.
    morphA,
    morphB,

    /// Phase 4, and appended for the same reason again: the two operator
    /// feedbacks and the reverse phase-modulation path.
    feedbackA,
    feedbackB,
    pmReverse,         ///< B modulating A -- the other half of the FM pair

    /// The filter's position on the lowpass -> bandpass -> highpass axis,
    /// bipolar and centred on the mode switch. Appended, as every entry in
    /// this list has been -- a stored slot is an index (CLAUDE.md section 8).
    ///
    /// It exists because the mode is a **choice** and a choice cannot be a
    /// destination, which left the filter's character the one thing in a voice
    /// no envelope could sweep.
    filterMorph,

    count
};

/// One row of the matrix.
struct ModSlot
{
    ModSource source { ModSource::none };
    ModDestination destination { ModDestination::none };

    /// Bipolar. The units are the destination's own -- octaves for the cutoff,
    /// cents for the pitches, and 0..1 for everything that is already a
    /// fraction.
    double depth { 0.0 };
};

/// The values of the sources the engine owns, handed to every voice.
/// The modulation depth law, shared by both matrices.
///
/// **Square, not linear**, and bipolar. The reason is a tension that a linear
/// knob cannot resolve: this instrument wants sweeps measured in octaves -- a
/// sync scream is four or five of them -- and it also wants a dialable vibrato.
/// At a linear five octaves, ten percent of the knob is already six semitones
/// and the subtle half of the control has vanished.
///
/// Squaring keeps both. At the +-7200 cents the pitch destination uses -- six
/// octaves -- a tenth of the knob is 72 cents, half is an octave and a half,
/// and the end is six octaves. Fine at the bottom, enormous at the top, and
/// monotonic all the way.
///
/// It is the same answer the resonance control gives (geometric Q, so the last
/// quarter turn is not the only useful part) and the envelope times give
/// (skewed, after a linear range put the whole attack in the top third). Three
/// controls, one problem, one shape.
[[nodiscard]] inline double shapedDepth (double depth) noexcept
{
    return std::copysign (depth * depth, depth);
}

/// **What Sag moves at full depth, and all of it from the one walk.**
///
/// Three things moving together from one cause is what reads as *one machine*;
/// three separate knobs would read as three effects. The same argument
/// `voiceDrift` makes for pairing cutoff and resonance, applied across the
/// whole instrument instead of within a voice.
///
/// - **pitch**, in cents: the capstan slipping, and the audible half.
/// - **cutoff**, in octaves: the sound dulling as it sags.
/// - **level**, in decibels: the amplifier drooping.
inline constexpr double kSagPitchCents = 40.0;
inline constexpr double kSagCutoffOctaves = 0.4;
inline constexpr double kSagLevelDb = 1.5;

struct GlobalSources
{
    double lfo1 { 0.0 };
    double lfo2 { 0.0 };
    double sequencer { 0.0 };

    /// The four macros, which are the same four numbers in both matrices --
    /// one knob, one value, wherever it is pointed. Carried here rather than
    /// in `VoiceParameters` so the voice and the mangle read the identical
    /// figure rather than two copies that could drift by a control chunk.
    std::array<double, 4> macros {};

    /// **Sag** -- the machine's temperature, in [-1, 1], from one slow walk in
    /// the engine. Common-mode across every voice, which is what makes it read
    /// as one machine failing rather than as thirty-two.
    double sag { 0.0 };

    /// The Shepard glissando's position, in **octaves travelled**, from one
    /// accumulator in the engine.
    ///
    /// One accumulator and not one per voice, and that is the design rather
    /// than a saving: a held chord has to glide as one thing, and voices with
    /// independent phases would smear a rise into a wash. Both oscillators
    /// share it too. Carried here for the same reason the macros are -- so
    /// every voice reads the identical figure on the identical chunk.
    double shepardOctaves { 0.0 };
};

/// Everything a voice is told, all of it shared across voices and set from the
/// engine once per control chunk.
///
/// A plain struct rather than a set of setters, because a voice has forty
/// controls and the engine rebuilds all of them together: a partial update is
/// a bug the type system may as well rule out.
struct VoiceParameters
{
    // ---- oscillator A --------------------------------------------------------

    OscShape shapeA { OscShape::saw };
    double octaveA { 0.0 };            ///< -3 .. +3, whole octaves
    double semitonesA { 0.0 };         ///< -12 .. +12
    double centsA { 0.0 };             ///< -100 .. +100
    double widthA { 0.5 };             ///< pulse width / triangle skew
    double morphA { 0.0 };             ///< the shape's own tweak, 0 = canonical
    int unisonA { 1 };                 ///< 1 .. 7
    double detuneA { 0.0 };            ///< cents
    double spreadA { 0.0 };            ///< 0 .. 1
    double driftA { 0.0 };             ///< cents of analogue wander
    double levelA { 1.0 };

    /// **Stack** -- where the unison copies go. `detune` is what shipped and is
    /// bit-exact; `stackStepA` is keys per copy and means something only in
    /// Scale mode. See `StackShapes.hpp`.
    StackMode stackA { StackMode::detune };
    int stackStepA { 1 };

    /// **Sag** -- how deep the machine's shared instability reaches this voice.
    /// The walk itself arrives in `GlobalSources::sag`; this is only the depth.
    /// Exactly 0 is bit-exactly out of the path.
    double sagDepth { 0.0 };

    // ---- oscillator B --------------------------------------------------------

    OscShape shapeB { OscShape::saw };
    double octaveB { 0.0 };
    double semitonesB { 0.0 };
    double centsB { 0.0 };
    double widthB { 0.5 };
    double morphB { 0.0 };
    int unisonB { 1 };
    double detuneB { 0.0 };
    double spreadB { 0.0 };
    double driftB { 0.0 };
    double levelB { 0.0 };

    StackMode stackB { StackMode::detune };
    int stackStepB { 1 };

    /// B is hard-synced to A. The Pro-53 trick: B's own pitch stops setting the
    /// note and starts setting the *timbre*, so sweeping it is a formant sweep
    /// rather than a detune.
    bool syncB { false };

    /// How far A phase-modulates B, in cycles. **Phase** modulation rather than
    /// frequency modulation: integrating a modulator with any DC in it walks
    /// the carrier's pitch away permanently, and a unison stack has DC in it by
    /// construction.
    double pmIndex { 0.0 };            ///< 0 .. 8

    /// **Operator feedback**, per oscillator, in cycles of self-deviation.
    /// Zero is off and is bit-exact. See `Oscillator::setFeedback` for why the
    /// loop cannot run away and what it can still do.
    double feedbackA { 0.0 };          ///< 0 .. 1
    double feedbackB { 0.0 };          ///< 0 .. 1

    /// **B modulating A**, the other half of the pair, in cycles.
    ///
    /// With `pmIndex` non-zero as well this closes a loop: A modulates B and B
    /// modulates A. It is broken by one sample -- A reads the *previous*
    /// sample's B -- which is what makes it computable at all, and which is
    /// also why the two directions do not sound alike at the same depth.
    double pmReverse { 0.0 };          ///< 0 .. 8

    // ---- sub -----------------------------------------------------------------

    SubShape subShape { SubShape::sine };
    /// Which octave the sub sits in, relative to the note: **-2 to +2**, where
    /// 0 doubles the note. Not only downward -- it is called a sub because that
    /// is what it is usually for, not because the oscillator cannot go up.
    int subOctave { -1 };
    double subLevel { 0.0 };

    // ---- destruction ---------------------------------------------------------

    /// A times B, blended in. Inharmonic and metallic, and the fastest route to
    /// a growl that is not a filter sweep.
    double ringAmount { 0.0 };         ///< 0 .. 1

    /// Sine folding. Past pi/2 the curve turns over and keeps folding, so the
    /// spectrum evolves instead of converging on a square.
    double foldAmount { 0.0 };         ///< 0 .. 1

    // ---- kargyraa ------------------------------------------------------------

    /// How much of the alternate cycle is taken away, 0 to 1. Zero is bit-exact
    /// bypass -- see `Voice::process`.
    double kargyraaDepth { 0.0 };

    /// How sharp the dip is, 0 to 1. Low is a smooth subharmonic; high is a
    /// narrow rasp with more of the series present.
    double kargyraaRasp { 0.5 };

    /// How many of the oscillator's cycles one modulator cycle spans.
    ///
    /// **2 is kargyraa.** The ventricular folds vibrate at exactly half the
    /// true folds' rate, so every second glottal pulse is the damped one. 3 and
    /// 4 are not anything a throat does; they are here because the machinery is
    /// the same and a third-harmonic subdivision is a sound this instrument
    /// should be able to make.
    int kargyraaDivisor { 2 };

    // ---- filter --------------------------------------------------------------

    SvfMode filterMode { SvfMode::lowpass };
    double cutoffHz { 1000.0 };
    double resonance { 0.0 };
    double filterDrive { 0.0 };

    /// -1 .. +1 along lowpass -> bandpass -> highpass, centred on
    /// `filterMode`. 0 is the mode itself, bit-exactly.
    double filterMorph { 0.0 };

    /// How much the played note moves the cutoff. 1 is one octave of cutoff per
    /// octave of note, which keeps the timbre constant across the keyboard.
    double filterKeyTrack { 0.0 };     ///< 0 .. 1

    /// Audio-rate filter FM from OSC A, as a multiplier swing on the cutoff.
    double filterFm { 0.0 };           ///< 0 .. 1

    /// How much velocity opens the filter.
    double filterVelocity { 0.0 };     ///< 0 .. 1

    /// The voice card's temperature, in cents of cutoff. A fixed per-voice
    /// mismatch plus a slow wander that carries on between notes, moving the
    /// cutoff by up to this many cents, the resonance with it, and the whole
    /// voice's tuning a little. 0 is bit-exactly off. See `Voice::advanceDrift`.
    double voiceDrift { 0.0 };

    // ---- envelopes -----------------------------------------------------------

    /// One envelope's five times and three tensions.
    ///
    /// A struct rather than fifteen loose fields, because there are three of
    /// these and every one of them grew a hold and two more tensions at once --
    /// which is forty-five fields written out longhand, and forty-five chances
    /// to wire envelope 2's release to envelope 1's.
    struct Envelope
    {
        double attack { 0.005 };
        double hold { 0.0 };
        double decay { 0.200 };
        double sustain { 0.8 };
        double release { 0.150 };

        /// Bipolar, -1 to +1. Positive is the analogue shape -- fast at first
        /// and decelerating; negative is the same curve reflected; zero is
        /// straight. See `dsp::Adsr`.
        double attackTension { 0.35 };
        double decayTension { 0.35 };
        double releaseTension { 0.35 };

        /// Quantise the four times to note lengths at the host tempo. Applied
        /// by the *engine*, which is the one that knows the tempo -- see
        /// Engine::snappedVoice. Off by default: the raw seconds are the
        /// pre-snap behaviour, bit for bit.
        bool snap { false };
    };

    /// One ADV envelope's whole description -- the multi-stage breakpoint
    /// envelopes, off by default so a project that never heard of them is
    /// untouched. `sustain` and `loopStart` are 0-based here; the parameters
    /// display them 1-based.
    struct AdvEnvelope
    {
        bool enable { false };
        bool loop { false };
        bool snap { false };
        int points { 4 };
        int sustain { 2 };
        int loopStart { 0 };

        // Sixteen of each. The first four are the ADSR-ish default the
        // envelope opens with; the rest are parked flat so lengthening the
        // envelope adds silent, level-0 legs rather than a shape nobody asked
        // for. The trailing braces are left to zero-initialise on purpose --
        // 0.1 s and level 0 is what `points` past four means.
        std::array<double, dsp::MultiEnvelope::kMaxPoints> seconds {
            0.01, 0.25, 0.05, 0.2, 0.1, 0.1, 0.1, 0.1,
            0.1, 0.1, 0.1, 0.1, 0.1, 0.1, 0.1, 0.1 };
        std::array<double, dsp::MultiEnvelope::kMaxPoints> level {
            1.0, 0.5, 0.5, 0.0, 0.0, 0.0, 0.0, 0.0,
            0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0 };
        std::array<double, dsp::MultiEnvelope::kMaxPoints> tension {
            0.35, 0.35, 0.0, 0.35, 0.0, 0.0, 0.0, 0.0,
            0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0 };
    };

    Envelope amp { 0.005, 0.0, 0.200, 0.8, 0.150, 0.35, 0.35, 0.35 };

    /// How much velocity affects loudness. 0 is an organ, 1 is a piano.
    double ampVelocity { 0.5 };        ///< 0 .. 1

    Envelope mod1 { 0.005, 0.0, 0.300, 0.0, 0.200, 0.35, 0.35, 0.35 };
    Envelope mod2 { 0.005, 0.0, 0.300, 0.0, 0.200, 0.35, 0.35, 0.35 };

    /// The three ADV envelopes, all disabled by default.
    std::array<AdvEnvelope, 3> adv {};

    double level { 0.5 };

    /// The modulation matrix. Six slots, each free to point anywhere.
    static constexpr int kSlots = 6;

    ModSlot slots[kSlots] {};
};

/// One voice. Held by value in an array, so it must be cheap to construct and
/// must never allocate outside `prepare`.
class Voice
{
public:
    /// How often the per-sample loop stops to take a new modulation reading.
    ///
    /// Counted in **samples**, not blocks -- CLAUDE.md section 7. Rebuilding
    /// "once per block" makes the output depend on the host's buffer size, and
    /// Emberdrive measured 0.296 of full scale between 64- and 512-sample
    /// blocks before that was fixed. The engine cuts its loop at this boundary.
    ///
    /// It is a **contract with the caller** rather than something the voice can
    /// enforce, and break-checking made that plain: changing this to 1 breaks
    /// no test here, because a caller that honours the boundary gets consistent
    /// audio at any value. What the number buys is CPU -- forty controls
    /// rebuilt once per 32 samples instead of once per sample -- and what
    /// enforces the boundary is SonitusEngine's loop, where the block-size test
    /// does bite.
    static constexpr int kControlIntervalSamples = 32;

    /// The voice card's temperature -- see `advanceDrift` -- as constants.
    ///
    /// Its wander's corner, in Hz. Slower than the oscillators' 0.35 Hz: a
    /// filter board is thermally heavier than a VCO's transistor pair.
    static constexpr double kVoiceDriftHz = 0.15;

    /// How often the wander picks a new target, in seconds.
    static constexpr double kVoiceDriftTargetSeconds = 0.5;

    /// The share of the cutoff drift that reaches the tuning, and its cap in
    /// cents. A quarter, because the same warmth moves a VCO's converter and a
    /// VCF's alike but only the VCO gets autotuned at power-up; capped so the
    /// creative end of the control moves the filter, not the tuning.
    static constexpr double kVoiceDriftPitchShare = 0.25;
    static constexpr double kVoiceDriftPitchCapCents = 15.0;

    /// How far the resonance moves at the control's full travel: +/-0.2 at
    /// 600 cents, so +/-0.01 at the 30 cents a warm polysynth actually does.
    static constexpr double kVoiceDriftFullCents = 600.0;
    static constexpr double kVoiceDriftResonanceAtFull = 0.2;

    /// How fast a cutoff or a level moves to a new modulated value.
    ///
    /// Short, because these are re-aimed every 32 samples and the smoothing is
    /// only there to take the corners off a stepped control signal. Long enough
    /// and the modulation would lag audibly behind the sequencer.
    static constexpr double kSmoothingSeconds = 0.004;

    /// `seed` distinguishes this voice's randomness from every other voice's.
    ///
    /// Not optional in practice: without it, eight voices draw the same
    /// note-random value and scatter their unison phases identically, so a
    /// chord is one sound played eight times rather than eight notes. The
    /// caller passes the voice index and this scrambles it -- adjacent seeds
    /// have to give distant streams, which a raw index does not.
    void prepare (double sampleRate, std::uint64_t seed = 0)
    {
        sampleRate_ = sampleRate > 0.0 ? sampleRate : 48000.0;

        noise_.seed (scramble (seed, 0x9e3779b97f4a7c15ull));
        bankA_.setSeed (scramble (seed, 0xbf58476d1ce4e5b9ull));
        bankB_.setSeed (scramble (seed, 0x94d049bb133111ebull));

        // The voice card's temperature: its own stream, so nothing that already
        // draws from noise_ (the per-note random) changes; the fixed mismatch
        // drawn once, here, and kept until the next prepare -- a real polysynth's
        // VCFs are trimmed by hand, and not to zero.
        driftRandom_.seed (scramble (seed, 0xd6e8feb86659fd93ull));
        driftStatic_ = driftRandom_.bipolar();
        driftCoefficient_ = std::clamp (6.283185307179586 * kVoiceDriftHz
                                          * kControlIntervalSamples / sampleRate_,
                                        0.0, 1.0);

        bankA_.prepare (sampleRate_);
        bankB_.prepare (sampleRate_);
        sub_.reset (0.0);

        amp_.prepare (sampleRate_);
        modEnvelope1_.prepare (sampleRate_);
        for (auto& advEnvelope : advEnvelope_)
            advEnvelope.prepare (sampleRate_);
        modEnvelope2_.prepare (sampleRate_);

        for (auto& filter : filters_)
            filter.prepare (sampleRate_);

        cutoff_.prepare (sampleRate_, kSmoothingSeconds);
        gain_.prepare (sampleRate_, kSmoothingSeconds);

        reset();
    }

    void reset() noexcept
    {
        bankA_.reset();
        bankB_.reset();
        sub_.reset (0.0);
        syncPhase_ = 0.0;
        kargyraaPhase_ = 0.0;
        kargyraaCycle_ = 0;

        for (auto& adaa : foldAdaa_)
            adaa.reset();

        amp_.reset();
        modEnvelope1_.reset();
        for (auto& advEnvelope : advEnvelope_)
            advEnvelope.reset();
        modEnvelope2_.reset();

        for (auto& filter : filters_)
            filter.reset();

        cutoff_.setCurrentAndTarget (parameters_.cutoffHz);
        gain_.setCurrentAndTarget (0.0);

        // The wander restarts with the graph, never with a note. The static
        // mismatch is the card's and survives even this.
        driftWander_ = 0.0;
        driftTarget_ = 0.0;
        driftCountdown_ = 0;

        note_ = -1;
        velocity_ = 0.0;
        frequency_ = 0.0;
        held_ = false;
        age_ = 0;
    }

    // -----------------------------------------------------------------------
    // Playing
    // -----------------------------------------------------------------------

    /// Starts a note. `frequency` comes from the Tuning, so the voice never
    /// computes a pitch from a note number and the whole microtuning question
    /// lives in one place.
    void noteOn (int note, double frequency, double velocity, bool retrigger)
    {
        note_ = note;
        frequency_ = frequency > 0.0 ? frequency : 0.0;
        velocity_ = std::clamp (velocity, 0.0, 1.0);
        held_ = true;
        age_ = 0;

        // One draw per note, held for the note's whole life. A source that
        // changed while a note sounded would be a slow LFO, not a random.
        random_ = noise_.bipolar();

        if (! retrigger)
        {
            // A cold voice starts from silence and from scattered phases, which
            // is what makes a unison stack sound like several oscillators
            // rather than one loud one. `restartNote`, not `reset`: the scatter
            // and every oscillator's transient state restart exactly as they
            // always have, and the analogue drift does not -- it is the voice
            // card's wander, not the note's, and a key does not reset it. See
            // the header of UnisonBank.hpp.
            bankA_.restartNote();
            bankB_.restartNote();
            sub_.reset (0.0);
            syncPhase_ = 0.0;

            // From the top of the group, so a cold note always begins on the
            // undamped cycle. Legato deliberately does not: a phrase played
            // without gaps keeps the growl running through it rather than
            // restarting it on every note.
            kargyraaPhase_ = 0.0;
            kargyraaCycle_ = 0;

            for (auto& adaa : foldAdaa_)
                adaa.reset();

            for (auto& filter : filters_)
                filter.reset();
        }

        amp_.noteOn();
        modEnvelope1_.noteOn();
        modEnvelope2_.noteOn();

        // All three, unconditionally -- the first draft gated this on the
        // enable flag, which is only learned at the first control chunk,
        // *after* the note-on: an ADV envelope enabled in the parameters could
        // never start on the note that revealed it. Starting a disabled one is
        // free (it is never ticked and its level reads 0), and it means
        // enabling mid-note joins from the gate rather than from nowhere.
        for (auto& advEnvelope : advEnvelope_)
            advEnvelope.noteOn();
    }

    void noteOff() noexcept
    {
        held_ = false;
        amp_.noteOff();
        modEnvelope1_.noteOff();
        modEnvelope2_.noteOff();

        for (auto& advEnvelope : advEnvelope_)
            advEnvelope.noteOff();
    }

    /// Silences the voice immediately, for stealing.
    void kill() noexcept
    {
        amp_.kill();
        modEnvelope1_.kill();
        for (auto& advEnvelope : advEnvelope_)
            advEnvelope.kill();
        modEnvelope2_.kill();
        held_ = false;
    }

    [[nodiscard]] bool isActive() const noexcept { return amp_.isActive(); }
    [[nodiscard]] bool isHeld() const noexcept { return held_; }
    [[nodiscard]] int getNote() const noexcept { return note_; }
    [[nodiscard]] double getFrequency() const noexcept { return frequency_; }
    [[nodiscard]] double getVelocity() const noexcept { return velocity_; }
    [[nodiscard]] double getAmpLevel() const noexcept { return amp_.getLevel(); }

    /// How many samples this voice has been sounding. The voice manager's
    /// tie-breaker when it has to steal.
    [[nodiscard]] long long getAge() const noexcept { return age_; }

    /// The cutoff the smoother has actually reached, in Hz. For a display, and
    /// for a test that wants to read a destination rather than infer it.
    [[nodiscard]] double currentCutoffHz() const noexcept { return cutoff_.getCurrent(); }

    /// A mod envelope's level, 0 or 1. For a display and for tests.
    [[nodiscard]] double getModEnvelopeLevel (int index) const noexcept
    {
        return index == 0 ? modEnvelope1_.getLevel() : modEnvelope2_.getLevel();
    }

    /// The ADV envelopes' current levels, for the global matrix and the panel.
    [[nodiscard]] double getAdvLevel (int index) const noexcept
    {
        return advLevel_[std::clamp (index, 0, 2)];
    }

    /// This note's random draw. Fixed for the note's whole life.
    [[nodiscard]] double getNoteRandom() const noexcept { return random_; }

    /// The two unison banks, read-only, for the tests that pin what a note-on
    /// restarts (the scatter) and what it must not (the drift).
    [[nodiscard]] const UnisonBank& bankA() const noexcept { return bankA_; }
    [[nodiscard]] const UnisonBank& bankB() const noexcept { return bankB_; }

    /// Steps the voice card's temperature by one control chunk.
    ///
    /// **For every voice, sounding or not, and never restarted by a key.** It
    /// is the card's temperature, not the note's: a one-pole at kVoiceDriftHz
    /// walking towards a new uniform target every kVoiceDriftTargetSeconds,
    /// the same shape as the oscillators' drift and slower. What it moves --
    /// cutoff, resonance, a little pitch -- is applied in `applyControls`, and
    /// only when `voiceDrift` is above zero; at zero nothing here reaches the
    /// sound, bit for bit, and the walk just keeps walking.
    void advanceDrift() noexcept
    {
        if (driftCountdown_ <= 0)
        {
            driftTarget_ = driftRandom_.bipolar();
            driftCountdown_ = static_cast<int> (sampleRate_ * kVoiceDriftTargetSeconds);
        }

        driftCountdown_ -= kControlIntervalSamples;
        driftWander_ += driftCoefficient_ * (driftTarget_ - driftWander_);
    }

    /// The cutoff drift in force, in cents: the control times the card's
    /// composite temperature (half fixed mismatch, half wander), so it is
    /// bounded by the control by construction. Exactly 0 at a control of 0.
    [[nodiscard]] double getVoiceDriftCents() const noexcept
    {
        if (dsp::isExactlyZero (parameters_.voiceDrift))
            return 0.0;

        return parameters_.voiceDrift * driftComposite();
    }

    /// The share of it that reaches the tuning, in cents: wander only (VCOs
    /// get autotuned; the mismatch belongs to the filter) and capped.
    [[nodiscard]] double getVoiceDriftPitchCents() const noexcept
    {
        if (dsp::isExactlyZero (parameters_.voiceDrift))
            return 0.0;

        return std::min (kVoiceDriftPitchShare * parameters_.voiceDrift, kVoiceDriftPitchCapCents)
                 * driftWander_;
    }

    /// What the resonance is offset by, in resonance units -- the same
    /// temperature, the same direction as the cutoff.
    [[nodiscard]] double getVoiceDriftResonanceOffset() const noexcept
    {
        if (dsp::isExactlyZero (parameters_.voiceDrift))
            return 0.0;

        return kVoiceDriftResonanceAtFull * (parameters_.voiceDrift / kVoiceDriftFullCents)
                 * driftComposite();
    }

    /// The raw walk and the fixed mismatch, for the tests that pin that a key
    /// does not restart the one and a prepare draws the other.
    [[nodiscard]] double getVoiceDriftWander() const noexcept { return driftWander_; }
    [[nodiscard]] double getVoiceDriftStatic() const noexcept { return driftStatic_; }

    /// The filter's resonance as set, drift included, for the bound test.
    [[nodiscard]] double getFilterResonance() const noexcept { return filters_[0].getResonance(); }

    /// The cutoff multiplier a given modulator value produces.
    ///
    /// Exposed because the property that matters is not visible in the audio:
    /// the swing has to be symmetric in **octaves**, so a modulator of +1 and
    /// one of -1 must give reciprocal multipliers. A linear map gives 3 and -1,
    /// which the filter then clamps at 1 Hz -- rectifying the modulation and
    /// turning filter FM into a one-sided flutter. Measured through the audio
    /// the two are only 10% apart in RMS; measured here they are 3 against 4
    /// and -1 against 0.25.
    [[nodiscard]] double filterScaleFor (double modulator) const noexcept
    {
        return filterScale (modulator);
    }

    /// The instrument's tuning, for Stack's Scale mode. Set once, by the voice
    /// manager that owns it; nothing here keeps a copy.
    void setTuning (const dsp::Tuning* tuning) noexcept { tuning_ = tuning; }

    /// Moves the pitch without restarting anything -- glide, and pitch bend.
    void setFrequency (double frequency) noexcept
    {
        frequency_ = frequency > 0.0 ? frequency : 0.0;
    }

    // -----------------------------------------------------------------------
    // Control
    // -----------------------------------------------------------------------

    /// Rebuilds everything from the parameters plus this chunk's modulation.
    ///
    /// Called at most every `kControlIntervalSamples`, never per sample.
    void applyControls (const VoiceParameters& parameters, const GlobalSources& global)
    {
        parameters_ = parameters;

        // The mod envelopes run at the control rate rather than per sample.
        // They are modulation, not audio: 32 samples is 0.67 ms at 48 kHz, well
        // inside the smoothing that follows, and running them per sample would
        // cost more than the oscillators do.
        applyEnvelope (modEnvelope1_, parameters.mod1);

        for (int i = 0; i < 3; ++i)
        {
            const auto& adv = parameters.adv[static_cast<std::size_t> (i)];

            // Turning a running slot off silences it now rather than letting
            // it finish inaudibly and surprise the next enable mid-flight.
            if (advEnabled_[i] && ! adv.enable)
                advEnvelope_[i].kill();

            advEnabled_[i] = adv.enable;

            if (! adv.enable)
            {
                advLevel_[i] = 0.0;
                continue;
            }

            auto& envelope = advEnvelope_[i];

            envelope.setPointCount (adv.points);
            envelope.setSustainIndex (adv.sustain);
            envelope.setLoopStart (adv.loopStart);
            envelope.setLoop (adv.loop);

            for (int point = 0; point < dsp::MultiEnvelope::kMaxPoints; ++point)
                envelope.setPoint (point,
                                   adv.seconds[static_cast<std::size_t> (point)],
                                   adv.level[static_cast<std::size_t> (point)],
                                   adv.tension[static_cast<std::size_t> (point)]);
        }
        applyEnvelope (modEnvelope2_, parameters.mod2);

        modLevel1_ = modEnvelope1_.skip (kControlIntervalSamples);

        for (int i = 0; i < 3; ++i)
            if (advEnabled_[i])
                advLevel_[i] = advEnvelope_[i].skip (kControlIntervalSamples);
        modLevel2_ = modEnvelope2_.skip (kControlIntervalSamples);

        resolveModulation (global);

        double pitchRatio = std::pow (2.0, amount (ModDestination::pitch) / 1200.0);

        // The voice card's warmth reaching the tuning: both banks and the sub,
        // through the one ratio they share. Skipped at zero rather than
        // multiplied by a computed 1.0, so the control's default changes no bit.
        if (! dsp::isExactlyZero (parameters.voiceDrift))
            pitchRatio *= std::pow (2.0, getVoiceDriftPitchCents() / 1200.0);

        // **Sag, and it goes here rather than into `centsA`/`centsB` beside the
        // pitch bend.** That distinction is not cosmetic and it took reading
        // `subIncrement` to find: the sub reads `frequency_ * pitchRatio` and
        // ignores the cents, so sagging through the cents field would leave the
        // sub sitting perfectly in tune underneath a sagging top. Half the
        // instrument failing is not the effect.
        //
        // Common-mode: every voice reads the same walk from `GlobalSources`, so
        // a chord stays in tune with itself and the whole instrument goes flat
        // together -- which is what a slipping capstan sounds like, and the
        // opposite of what the two per-voice drifts are for.
        //
        // The zero guard is a **cost saving and not an exactness one**: at
        // depth 0 the exponent is exactly 0 and `pow(2, 0)` is exactly 1.0, so
        // the branch changes no bit either way. It skips a `pow` per voice per
        // chunk. Said plainly because the same shape of comment on Ictus's Air
        // branch claimed exactness and a break-check proved it wrong.
        if (! dsp::isExactlyZero (parameters.sagDepth))
            pitchRatio *= std::pow (2.0, kSagPitchCents * parameters.sagDepth
                                           * global.sag / 1200.0);

        const double nominalA = frequency_ * pitchRatio
                                  * ratioFor (parameters.octaveA, parameters.semitonesA,
                                              parameters.centsA);

        configureBank (bankA_, stackCacheA_, parameters.shapeA,
                       parameters.widthA + amount (ModDestination::pulseWidthA),
                       parameters.morphA + amount (ModDestination::morphA),
                       parameters.unisonA,
                       parameters.detuneA + amount (ModDestination::detuneA),
                       parameters.spreadA, parameters.driftA, nominalA,
                       parameters.stackA, parameters.stackStepA, global.shepardOctaves);

        // A's nominal pitch, not any one of its detuned oscillators -- see
        // propagateSync().
        syncIncrement_ = nominalA > 0.0 ? nominalA / sampleRate_ : 0.0;

        configureBank (bankB_, stackCacheB_, parameters.shapeB,
                       parameters.widthB + amount (ModDestination::pulseWidthB),
                       parameters.morphB + amount (ModDestination::morphB),
                       parameters.unisonB,
                       parameters.detuneB + amount (ModDestination::detuneB),
                       parameters.spreadB, parameters.driftB,
                       frequency_ * pitchRatio
                         * ratioFor (parameters.octaveB, parameters.semitonesB,
                                     parameters.centsB + amount (ModDestination::pitchB)),
                       parameters.stackB, parameters.stackStepB, global.shepardOctaves);

        // The mix destination is a crossfade between the two banks rather than
        // a level on each, so sweeping it holds the loudness.
        const double mix = std::clamp (amount (ModDestination::oscMix), -1.0, 1.0);

        levelA_ = std::clamp (parameters.levelA * (1.0 - std::max (mix, 0.0)), 0.0, 4.0);
        levelB_ = std::clamp (parameters.levelB * (1.0 + std::min (mix, 0.0)), 0.0, 4.0);

        subLevel_ = std::clamp (parameters.subLevel + amount (ModDestination::subLevel), 0.0, 4.0);
        ring_ = std::clamp (parameters.ringAmount + amount (ModDestination::ringAmount), 0.0, 1.0);
        pmIndex_ = std::clamp (parameters.pmIndex + amount (ModDestination::pmIndex), 0.0, 16.0);

        // Both feedbacks and the reverse path, clamped at the same ceilings
        // the controls carry. The clamp is what section 7 calls a bound that
        // cannot be defeated: modulation can push a control past its own range
        // and these are the only things standing between that and an operator
        // asked for sixteen cycles of self-deviation.
        pmReverse_ = std::clamp (parameters.pmReverse + amount (ModDestination::pmReverse),
                                 0.0, 16.0);

        bankA_.setFeedback (std::clamp (parameters.feedbackA
                                          + amount (ModDestination::feedbackA),
                                        0.0, dsp::Oscillator::kMaxFeedback));
        bankB_.setFeedback (std::clamp (parameters.feedbackB
                                          + amount (ModDestination::feedbackB),
                                        0.0, dsp::Oscillator::kMaxFeedback));

        sub_.setShape (parameters.subShape == SubShape::sine ? OscShape::sine : OscShape::pulse);
        sub_.setIncrement (subIncrement (pitchRatio));

        applyEnvelope (amp_, parameters.amp);

        for (auto& filter : filters_)
        {
            filter.setMode (parameters.filterMode);
            filter.setMorph (std::clamp (parameters.filterMorph
                                           + amount (ModDestination::filterMorph), -1.0, 1.0));
            // Resonance drifts with the cutoff -- paired, the same temperature
            // in the same direction -- and, like everything the card's warmth
            // touches, only when the control is above zero.
            double resonanceTarget = parameters.resonance + amount (ModDestination::resonance);

            if (! dsp::isExactlyZero (parameters.voiceDrift))
                resonanceTarget += getVoiceDriftResonanceOffset();

            filter.setResonance (std::clamp (resonanceTarget, 0.0, 1.0));
            filter.setDrive (std::clamp (parameters.filterDrive
                                           + amount (ModDestination::filterDrive), 0.0, 1.0));
        }

        // The folder's gain is a *geometric* control, so its first half is not
        // wasted: at 1.0 the curve has not folded at all and the whole
        // character is in the last third of the travel otherwise.
        kargyraaDepth_ = std::clamp (parameters.kargyraaDepth
                                       + amount (ModDestination::kargyraa), 0.0, 1.0);
        kargyraaRasp_ = std::clamp (parameters.kargyraaRasp, 0.0, 1.0);

        // Re-wrapped rather than assigned, so shortening the divisor while a
        // note is sounding cannot leave the counter pointing past the end of
        // its own cycle -- which would hold the modulator at one value until
        // the next wrap.
        kargyraaDivisor_ = std::clamp (parameters.kargyraaDivisor, kMinimumDivisor, kMaximumDivisor);
        kargyraaCycle_ %= kargyraaDivisor_;

        fold_ = std::clamp (parameters.foldAmount + amount (ModDestination::foldAmount), 0.0, 1.0);

        folder_.setGain (dsp::isExactlyZero (fold_) ? 0.0 : std::pow (kMaximumFold, fold_));

        // The cutoff drift lands on the smoother's target, so the 4 ms smoother
        // makes it click-free and the per-sample filter path is untouched. The
        // exponential-converter law: cents, like the oscillators' drift.
        double cutoffTarget = targetCutoff (amount (ModDestination::cutoff));

        if (! dsp::isExactlyZero (parameters.voiceDrift))
            cutoffTarget = std::clamp (cutoffTarget * std::pow (2.0, getVoiceDriftCents() / 1200.0),
                                       20.0, sampleRate_ * 0.45);

        double sagGain = 1.0;

        // The other two thirds of the sag, from the same walk and in the same
        // direction as the pitch -- the sound dulling and the amplifier
        // drooping as the machine goes wrong. All three moving together is what
        // makes it one machine rather than three effects.
        if (! dsp::isExactlyZero (parameters.sagDepth))
        {
            const double sag = parameters.sagDepth * global.sag;

            cutoffTarget = std::clamp (cutoffTarget * std::pow (2.0, kSagCutoffOctaves * sag),
                                       20.0, sampleRate_ * 0.45);

            sagGain = std::pow (10.0, kSagLevelDb * sag / 20.0);
        }

        cutoff_.setTarget (cutoffTarget);
        gain_.setTarget (targetGain (amount (ModDestination::level)) * sagGain);
    }

    /// One sample into a stereo pair. Adds rather than overwrites, so a bank of
    /// voices sums without a scratch buffer.
    void process (double& left, double& right) noexcept
    {
        if (! amp_.isActive())
            return;

        ++age_;

        double leftA = 0.0;
        double rightA = 0.0;
        double leftB = 0.0;
        double rightB = 0.0;

        // **The reverse path is one sample old, and it has to be.** A runs
        // first because it is the sync master and the forward modulator, so
        // the only B available to it is last sample's. That single-sample
        // delay is what turns an algebraic loop into a computable one, and it
        // is why A -> B and B -> A do not sound alike at the same depth: the
        // forward path is instantaneous within the sample, the reverse is not.
        //
        // Guarded rather than always added so that zero reverse PM is the
        // identity by inspection -- the pre-phase-4 output, bit for bit.
        const double reverseMod = dsp::isExactlyZero (pmReverse_)
                                    ? 0.0
                                    : pmReverse_ * previousB_;

        bankA_.process (reverseMod, leftA, rightA);

        // Sync and phase modulation both come from A, and both are read *after*
        // A has advanced this sample -- A is the master, so its edge is what B
        // is being reset to.
        if (parameters_.syncB)
            propagateSync();

        const double modulator = 0.5 * (leftA + rightA);
        const double phaseMod = pmIndex_ * modulator;

        bankB_.process (phaseMod, leftB, rightB);

        previousB_ = 0.5 * (leftB + rightB);

        const double subValue = sub_.advance() * subLevel_;

        double mixLeft = leftA * levelA_ + leftB * levelB_;
        double mixRight = rightA * levelA_ + rightB * levelB_;

        if (! dsp::isExactlyZero (ring_))
        {
            // Ring modulation blends *towards* the product rather than adding
            // it, so the control is a crossfade and full ring is the product
            // alone. Adding it would make the control a volume knob as well.
            const double ringLeft = leftA * leftB * 2.0;
            const double ringRight = rightA * rightB * 2.0;

            mixLeft += ring_ * (ringLeft - mixLeft);
            mixRight += ring_ * (ringRight - mixRight);
        }

        // **Kargyraa -- period doubling, phase-locked to the oscillator.**
        //
        // The clock advances whatever the depth is, so engaging the control
        // mid-note starts from the phase the note is already at rather than
        // from wherever the counter happened to stop.
        const double kargyraa = advanceKargyraa();

        if (! dsp::isExactlyZero (kargyraaDepth_))
        {
            mixLeft *= kargyraa;
            mixRight *= kargyraa;
        }

        if (! dsp::isExactlyZero (fold_))
        {
            // **Antiderivative antialiasing, not just oversampling.** CLAUDE.md
            // section 7: oversampling alone never gets there, because a shaper
            // with infinite bandwidth folds back whatever rate it is run at.
            // Measured at x4 and 110 Hz, with the folder naked, the whole
            // instrument read -43.8 dB of audible aliasing at half fold and
            // -17.1 dB at full -- against -92 dB for the bare oscillator. It
            // was by a wide margin the loudest thing in the chain, and the
            // stage next to it in Emberdrive has been ADAA'd since it shipped.
            mixLeft = foldAdaa_[0].process (mixLeft, folder_);
            mixRight = foldAdaa_[1].process (mixRight, folder_);
        }

        const double cutoffScale = filterScale (modulator);
        const double cutoffNow = cutoff_.next();

        // The right channel is tuned to the same cutoff as the left, so it
        // takes the coefficient the left just computed rather than evaluating
        // the same `tan` again -- identical inputs, identical bits, and one
        // transcendental fewer per sample while the cutoff is moving.
        filters_[0].setCutoffHz (cutoffNow);
        filters_[1].adoptCutoffFrom (filters_[0]);

        double outLeft = filters_[0].process (mixLeft, cutoffScale);
        double outRight = filters_[1].process (mixRight, cutoffScale);

        // The sub joins after the filter -- see the header.
        outLeft += subValue;
        outRight += subValue;

        const double envelope = amp_.process();
        const double gain = gain_.next() * envelope;

        left += outLeft * gain;
        right += outRight * gain;
    }

private:
    /// How far the folder can be driven, as the top of a geometric range.
    ///
    /// The folding begins at a gain of pi/2, so a range that starts at 1 has
    /// nothing happening for its first third if it is linear. Geometric from 1
    /// to 12 puts the first fold at about a third of the travel and keeps
    /// adding folds after it -- Halo measured that a sine folder goes on
    /// producing new harmonics rather than converging on a square, which is
    /// what makes the top of the range worth having.
    static constexpr double kMaximumFold = 12.0;

    /// The widest the audio-rate FM can swing the cutoff, as a ratio. Two
    /// octaves either way at full depth.
    static constexpr double kMaximumFmSwing = 2.0;

    /// Sums every slot into a per-destination total.
    ///
    /// Summed rather than last-wins, because two slots pointing at the same
    /// control is a normal thing to want -- an envelope opening the filter and
    /// an LFO wobbling it are one sound, not a conflict.
    void resolveModulation (const GlobalSources& global) noexcept
    {
        for (auto& value : amounts_)
            value = 0.0;

        for (const auto& slot : parameters_.slots)
        {
            if (slot.source == ModSource::none || slot.destination == ModDestination::none)
                continue;

            if (dsp::isExactlyZero (slot.depth))
                continue;

            amounts_[static_cast<std::size_t> (slot.destination)]
              += slot.depth * sourceValue (slot.source, global);
        }
    }

    [[nodiscard]] double sourceValue (ModSource source, const GlobalSources& global) const noexcept
    {
        switch (source)
        {
            case ModSource::ampEnvelope:  return amp_.getLevel();
            case ModSource::modEnvelope1: return modLevel1_;
            case ModSource::modEnvelope2: return modLevel2_;
            case ModSource::velocity:     return velocity_;

            // Octaves from middle C, so a depth in octaves is a depth in
            // octaves and the control reads the same wherever it is pointed.
            case ModSource::keyTrack:
                return frequency_ > 0.0 ? std::log2 (frequency_ / kMiddleCHz) : 0.0;

            case ModSource::noteRandom:   return random_;
            case ModSource::advEnv1:      return advLevel_[0];
            case ModSource::advEnv2:      return advLevel_[1];
            case ModSource::advEnv3:      return advLevel_[2];
            case ModSource::lfo1:         return global.lfo1;
            case ModSource::lfo2:         return global.lfo2;
            case ModSource::sequencer:    return global.sequencer;

            case ModSource::macro1:       return global.macros[0];
            case ModSource::macro2:       return global.macros[1];
            case ModSource::macro3:       return global.macros[2];
            case ModSource::macro4:       return global.macros[3];

            case ModSource::sag:          return global.sag;

            case ModSource::none:
            case ModSource::count:
            default:                      return 0.0;
        }
    }

    [[nodiscard]] double amount (ModDestination destination) const noexcept
    {
        return amounts_[static_cast<std::size_t> (destination)];
    }

    static constexpr double kMiddleCHz = 261.6255653005986;

    /// A splitmix64 finalizer, so voice 0 and voice 1 get streams with nothing
    /// in common rather than two positions on the same one.
    [[nodiscard]] static std::uint64_t scramble (std::uint64_t seed,
                                                 std::uint64_t salt) noexcept
    {
        std::uint64_t z = seed + salt;

        z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ull;
        z = (z ^ (z >> 27)) * 0x94d049bb133111ebull;

        return z ^ (z >> 31);
    }

    [[nodiscard]] static double ratioFor (double octaves, double semitones, double cents) noexcept
    {
        return std::pow (2.0, octaves + semitones / 12.0 + cents / 1200.0);
    }

    /// Where a bank's rank offsets came from last time, so they are not
    /// recomputed for a stack that has not moved.
    ///
    /// Only Shepard changes every chunk; a fixed interval or a scale degree
    /// depends on the mode, the copy count, the step and the played note, and
    /// none of those move inside a held note. Scale mode is the one that would
    /// hurt without this -- `Tuning::frequencyFor` is two `pow` calls, so seven
    /// copies on two banks at thirty-two voices would be five million of them a
    /// second for an answer that never changes.
    ///
    /// The key deliberately does not include the tuning. Loading a scale
    /// mid-note does not re-pitch a held note either -- the manager reads
    /// `frequencyFor` at note-on -- so caching against the note matches what
    /// the rest of the instrument already does.
    struct StackCache
    {
        StackMode mode { StackMode::count };   ///< count = nothing cached yet
        int copies { 0 };
        int step { 0 };
        int note { -2 };

        std::array<double, kMaxStackCopies> cents {};
        std::array<double, kMaxStackCopies> gains {};
    };

    /// Pushes `mode`'s rank offsets at `bank`, through the cache above.
    ///
    /// **The early return is not the guard CLAUDE.md section 7 warns about.**
    /// That rule is about a *caller* deciding whether a value change matters,
    /// which desynchronises the moment a second caller disagrees. This bank has
    /// exactly one owner, nothing else writes its rank offsets, and neither
    /// `reset` nor `restartNote` clears them -- so when the cache is fresh the
    /// bank already holds precisely these numbers and the call would be a
    /// no-op. `setRankOffsets` keeps its own guard regardless, and the
    /// measurement is why this exists: comparing fourteen doubles per bank per
    /// voice per chunk cost **3.9% of a core** at sixteen voices, which is more
    /// than the interval arithmetic it was protecting.
    void applyStack (UnisonBank& bank, StackCache& cache, StackMode mode,
                     int copies, int step, double shepardOctaves) noexcept
    {
        if (mode == StackMode::detune)
        {
            // Neutral, which is what a bank that was never told looks like --
            // and the bank's own guard makes this free once it has landed.
            bank.setRankOffsets (nullptr, nullptr, 0);
            cache.mode = StackMode::detune;
            return;
        }

        const bool sliding = mode == StackMode::shepard;

        const bool stale = sliding
                        || cache.mode != mode
                        || cache.copies != copies
                        || cache.step != step
                        || cache.note != note_;

        if (! stale)
            return;

        const dsp::Tuning& tuning = tuning_ != nullptr ? *tuning_ : fallbackTuning();

        stackRanks (mode, copies, step, shepardOctaves, tuning, note_,
                    cache.cents.data(), cache.gains.data());

        cache.mode = mode;
        cache.copies = copies;
        cache.step = step;
        cache.note = note_;

        bank.setRankOffsets (cache.cents.data(), cache.gains.data(), copies);
    }

    /// Twelve-tone equal temperament, for a voice that was never handed the
    /// instrument's tuning. Never reached in the plugin -- the manager sets the
    /// pointer at `prepare` -- and it exists so a unit test can drive a bare
    /// `Voice` without one.
    [[nodiscard]] static const dsp::Tuning& fallbackTuning() noexcept
    {
        static const dsp::Tuning defaultTuning {};
        return defaultTuning;
    }

    void configureBank (UnisonBank& bank, StackCache& cache, OscShape shape,
                        double width, double morph, int unison,
                        double detune, double spread, double drift, double frequency,
                        StackMode stack, int step, double shepardOctaves) noexcept
    {
        bank.setShape (shape);
        bank.setWidth (width);
        bank.setMorph (std::clamp (morph, 0.0, 1.0));
        bank.setVoiceCount (unison);
        bank.setDetuneCents (detune);
        bank.setSpread (spread);
        bank.setDrift (drift);

        // After the count, because the offsets are pushed for that many copies.
        applyStack (bank, cache, stack, std::clamp (unison, 1, kMaxStackCopies),
                    step, shepardOctaves);

        bank.setFrequency (frequency);
    }

    [[nodiscard]] double subIncrement (double pitchRatio) const noexcept
    {
        const int octaves = std::clamp (parameters_.subOctave, -2, 2);
        const double hz = frequency_ * pitchRatio * std::pow (2.0, octaves);

        return hz > 0.0 ? hz / sampleRate_ : 0.0;
    }

    /// Resets every one of B's oscillators from a single master phase.
    ///
    /// **One master for the whole stack**, which is the point: sync means B's
    /// cycle is defined by A's, so a synced unison stack is one timbre with a
    /// spread rather than seven timbres beating. Syncing each of B's voices to
    /// its own partner in A would just be a detuned stack again.
    ///
    /// And the master is a **dedicated phase at A's nominal pitch**, not one of
    /// A's oscillators. That is not tidiness: `UnisonBank::position(0)` is the
    /// *bottom* of the stack, so voice 0 sits half the detune flat and syncing
    /// to it would make the sounding pitch drop as the detune is turned up. An
    /// even stack has no centre voice to use instead. A separate accumulator
    /// costs one add and one compare per sample and is right at every setting.
    /// Pushes one `VoiceParameters::Envelope` at one generator. Written once,
    /// because it is called three times and the difference between the three
    /// calls should be one argument rather than fifteen lines.
    static void applyEnvelope (dsp::Adsr& envelope, const VoiceParameters::Envelope& p) noexcept
    {
        envelope.setAttackSeconds (p.attack);
        envelope.setHoldSeconds (p.hold);
        envelope.setDecaySeconds (p.decay);
        envelope.setSustain (p.sustain);
        envelope.setReleaseSeconds (p.release);
        envelope.setAttackTension (p.attackTension);
        envelope.setDecayTension (p.decayTension);
        envelope.setReleaseTension (p.releaseTension);
    }

    /// The smallest and largest subdivision the control offers.
    static constexpr int kMinimumDivisor = 2;
    static constexpr int kMaximumDivisor = 4;

    /// The sharpest the dip can be made, as the power the raised cosine is
    /// taken to. See `advanceKargyraa` for why it is an integer bound.
    static constexpr int kMaximumRaspPower = 8;

    /// Advances the subharmonic clock and returns this sample's gain.
    ///
    /// ---------------------------------------------------------------------
    /// What kargyraa actually is
    /// ---------------------------------------------------------------------
    ///
    /// In the Tuvan and Tibetan style the **ventricular folds** -- the false
    /// vocal folds sitting above the true ones -- are drawn into vibration by
    /// the airflow and close at exactly *half* the true folds' rate. Every
    /// second glottal pulse is damped by them. So the voice gains a real
    /// subharmonic: the period doubles while the pitch being sung, and the
    /// formants shaping it, stay where they were.
    ///
    /// **That is not an octave divider and not a sub oscillator.** A sub adds a
    /// separate tone an octave down; this modifies alternate cycles of the
    /// waveform that is already there, so what appears is the half-integer
    /// series -- f/2, 3f/2, 5f/2 -- around every harmonic, at levels set by how
    /// different the two cycles are. It is the same voice with a doubled
    /// period, which is why it sounds like a growl rather than like two notes.
    ///
    /// The lock is by construction rather than by tuning: the modulator's phase
    /// is *derived* from the oscillator's own cycle counter, so it cannot drift
    /// against the note however long it is held, and a glide takes it along.
    ///
    /// ---------------------------------------------------------------------
    /// Why the shape is a power of a raised cosine
    /// ---------------------------------------------------------------------
    ///
    /// A gain that steps between cycles is a square wave at f/N multiplying the
    /// signal, and a square has infinite bandwidth -- CLAUDE.md section 7 calls
    /// that a defect everywhere except where it is the instrument, and here it
    /// is not. Oversampling would not save it either, for the reason the
    /// section gives about hard clipping.
    ///
    /// `(0.5 - 0.5 cos t)^k` is `sin^2k(t/2)`, and `sin^2k` expands into a
    /// **finite** cosine series: exactly `k` harmonics of `t` and nothing above
    /// them. So the modulator is band-limited by construction, at a bandwidth
    /// the Rasp control names, and the product widens the carrier by exactly
    /// `k * f/N` -- 220 Hz at the bottom of the keyboard with the default. No
    /// antiderivative, no oversampling argument, no measurement needed to know
    /// the bound; the measurement in tests/test_Sonitus.cpp confirms it anyway.
    ///
    /// Rasp interpolates between two adjacent integer powers rather than
    /// varying `k` continuously, because a fractional power is an infinite
    /// series and would throw the guarantee away. A linear blend of two
    /// band-limited signals is band-limited to the wider of the two.
    [[nodiscard]] double advanceKargyraa() noexcept
    {
        if (syncIncrement_ > 0.0)
        {
            kargyraaPhase_ += syncIncrement_;

            while (kargyraaPhase_ >= 1.0)
            {
                kargyraaPhase_ -= 1.0;
                kargyraaCycle_ = (kargyraaCycle_ + 1) % kargyraaDivisor_;
            }
        }

        if (dsp::isExactlyZero (kargyraaDepth_))
            return 1.0;

        // Where we are across the whole N-cycle span, exactly.
        const double span = (static_cast<double> (kargyraaCycle_) + kargyraaPhase_)
                              / static_cast<double> (kargyraaDivisor_);

        // sin^2, peaking once per span. The dip therefore lands on the *last*
        // cycle of each group, which is the damped one.
        const double raised = 0.5 - 0.5 * std::cos (2.0 * std::numbers::pi * span);

        const double power = 1.0 + kargyraaRasp_ * (kMaximumRaspPower - 1);
        const double lower = std::floor (power);
        const double fraction = power - lower;

        const double soft = std::pow (raised, lower);
        const double shape = dsp::isExactlyZero (fraction)
                               ? soft
                               : soft + fraction * (soft * raised - soft);

        return 1.0 - kargyraaDepth_ * shape;
    }

    void propagateSync() noexcept
    {
        if (syncIncrement_ <= 0.0)
            return;

        const double before = syncPhase_;

        syncPhase_ += syncIncrement_;

        if (syncPhase_ < 1.0)
            return;

        // The fraction of this sample at which the master crossed, which is the
        // convention Oscillator::sync expects.
        const double frac = std::clamp ((1.0 - before) / syncIncrement_, 0.0, 1.0);

        syncPhase_ -= 1.0;

        for (int index = 0; index < parameters_.unisonB; ++index)
            bankB_.voice (index).sync (frac);
    }

    [[nodiscard]] double targetCutoff (double modulation) const noexcept
    {
        // Key tracking is in octaves, referred to middle C, so a full amount
        // keeps the timbre constant across the keyboard rather than making the
        // top of it shrill.
        const double octavesFromMiddleC = frequency_ > 0.0
                                            ? std::log2 (frequency_ / 261.6255653005986)
                                            : 0.0;

        const double tracked = parameters_.filterKeyTrack * octavesFromMiddleC;
        const double byVelocity = parameters_.filterVelocity * velocity_ * 4.0;

        const double octaves = modulation + tracked + byVelocity;

        return std::clamp (parameters_.cutoffHz * std::pow (2.0, octaves), 20.0,
                           sampleRate_ * 0.45);
    }

    [[nodiscard]] double targetGain (double modulation) const noexcept
    {
        // Velocity scales the level, with the amount itself a control -- at 0
        // the instrument is an organ, at 1 it plays like a piano.
        const double byVelocity = 1.0 - parameters_.ampVelocity * (1.0 - velocity_);

        return std::clamp (parameters_.level * byVelocity * (1.0 + modulation), 0.0, 4.0);
    }

    /// Half fixed mismatch, half wander: bounded by 1 by construction.
    [[nodiscard]] double driftComposite() const noexcept
    {
        return 0.5 * driftStatic_ + 0.5 * driftWander_;
    }

    [[nodiscard]] double filterScale (double modulator) const noexcept
    {
        if (dsp::isExactlyZero (parameters_.filterFm))
            return 1.0;

        // Exponential, so the swing is symmetric in octaves rather than in
        // hertz -- a linear swing at a low cutoff would hit the floor on every
        // negative half cycle and rectify the modulation.
        return std::pow (2.0, parameters_.filterFm * kMaximumFmSwing * modulator);
    }

    double sampleRate_ { 48000.0 };

    VoiceParameters parameters_;

    UnisonBank bankA_;
    UnisonBank bankB_;
    Oscillator sub_;

    Adsr amp_;
    SvfFilter filters_[2];

    Adsr modEnvelope1_;
    dsp::MultiEnvelope advEnvelope_[3];
    Adsr modEnvelope2_;

    double advLevel_[3] { 0.0, 0.0, 0.0 };
    bool advEnabled_[3] { false, false, false };

    double modLevel1_ { 0.0 };
    double modLevel2_ { 0.0 };

    dsp::SmallRandom noise_;
    double random_ { 0.0 };

    /// The voice card's temperature -- see `advanceDrift`.
    dsp::SmallRandom driftRandom_;
    double driftStatic_ { 0.0 };        ///< fixed mismatch, drawn at prepare, [-1, 1]
    double driftWander_ { 0.0 };        ///< the slow walk, [-1, 1]
    double driftTarget_ { 0.0 };
    double driftCoefficient_ { 0.0 };
    int    driftCountdown_ { 0 };

    std::array<double, static_cast<std::size_t> (ModDestination::count)> amounts_ {};

    // Resolved once per control chunk, so the sample loop reads a number rather
    // than walking the matrix.
    double levelA_ { 1.0 };
    double levelB_ { 0.0 };
    double subLevel_ { 0.0 };
    double ring_ { 0.0 };
    double fold_ { 0.0 };
    double pmIndex_ { 0.0 };
    double pmReverse_ { 0.0 };

    /// Last sample's B, for the reverse phase-modulation path. The delay is
    /// the design; see process().
    double previousB_ { 0.0 };

    double syncPhase_ { 0.0 };

    /// The kargyraa clock: a second accumulator at oscillator A's nominal rate,
    /// plus which cycle of the group we are in.
    ///
    /// Its own rather than shared with `syncPhase_`, which only runs while hard
    /// sync is switched on -- the two features are independent and either can
    /// be used without the other. A locks the period in both cases because it
    /// is this instrument's master everywhere else, including when its level is
    /// down and only B is sounding.
    double kargyraaPhase_ { 0.0 };
    int    kargyraaCycle_ { 0 };

    double kargyraaDepth_ { 0.0 };
    double kargyraaRasp_ { 0.5 };
    int    kargyraaDivisor_ { 2 };
    double syncIncrement_ { 0.0 };

    dsp::SineFolder folder_;
    dsp::Adaa1<dsp::SineFolder> foldAdaa_[2];

    dsp::SmoothedValue<double> cutoff_;
    dsp::SmoothedValue<double> gain_;

    int note_ { -1 };

    /// The instrument's tuning, for Scale mode. Set once by the voice manager,
    /// which owns it; null only in a bare unit test -- see `fallbackTuning`.
    const dsp::Tuning* tuning_ { nullptr };

    StackCache stackCacheA_ {};
    StackCache stackCacheB_ {};
    double velocity_ { 0.0 };
    double frequency_ { 0.0 };
    bool held_ { false };
    long long age_ { 0 };
};

} // namespace tezla::sonitus
