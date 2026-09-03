// Copyright (c) 2026 The Tezla <thetezla@proton.me>
// Created by The Tezla -- https://github.com/wingit33/tezla.tech
// Music: https://soundcloud.com/thetezla | https://thetezla.bandcamp.com
// Built with development assistance from Claude (Anthropic).
// SPDX-License-Identifier: AGPL-3.0-only
// GNU AGPLv3 (see LICENSE), plus NOTICE.md's attribution term. Keep intact.

#pragma once

// Sonitus -- the growl and reese instrument.
//
// Framework-free: no JUCE, no VST3 headers, no MIDI. It takes note-on and
// note-off calls and writes into double buffers, so the whole instrument can be
// played and measured offline before any of it reaches a DAW.
//
// ---------------------------------------------------------------------------
// The thesis
// ---------------------------------------------------------------------------
//
// **Every reese and every growl is one dense source and a moving comb.**
//
// Detuned saws beat, and the beating is a comb whose notches sweep -- you just
// cannot reach it. A flanger is that comb with a handle on it. A vowel filter
// is the same comb shaped like a mouth. They are one idea at three time
// constants, and the instrument is arranged to say so:
//
//     make dense harmonics -> comb them under total control -> drive them
//                          -> keep the sub out of it
//
// That last clause is what makes it usable on a real track, and it is why the
// split is *inside* the instrument rather than three plugins later.
//
// ---------------------------------------------------------------------------
// The chain
// ---------------------------------------------------------------------------
//
//   MIDI --> VOICE x8   (or mono / legato with glide)
//              osc A + osc B (sync, PM) + sub -> ring -> fold -> filter -> VCA
//                                  |
//                                  v  sum of voices
//   +--- GLOBAL MANGLE --------------------------------------------------+
//   |                                                                    |
//   |  SPLIT at X Hz --+-- SUB  : mono, DC-blocked, bypasses everything -+
//   |                  |                                                 |
//   |                  +-- BODY : [ ORDER SWITCH ]                       |
//   |                        TUBE  <->  COMB (flange | phase)            |
//   |                        FORMANT morph                               |
//   |                        tilt                                        |
//   |                                                            sum ----+
//   +--------------------------------------------------------------------+
//                                  |
//                                  v  output trim
//
// Voices are per-note; the mangle is global. That is the cheap arrangement and
// also the right one -- it is what a hardware chain does, and it leaves the CPU
// for unison.
//
// ---------------------------------------------------------------------------
// The order switch is not a convenience
// ---------------------------------------------------------------------------
//
// Tube before comb and comb before tube are different instruments, for the same
// reason a tone stack in front of a distortion is a different amplifier from
// one behind it. In front, the comb decides *what gets distorted* -- the tube
// then generates harmonics of a signal that already has holes in it, and the
// holes stay holes. Behind, the tube fills the comb's notches with harmonics it
// made itself, and the comb then cuts those too, which is a much denser and
// less tuned sound. Anvil's voicings make the same distinction about where
// their tone stack sits, and for the same reason.

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>

#include <tezla/dsp/Divisions.hpp>
#include <tezla/dsp/Biquad.hpp>
#include <tezla/dsp/Exact.hpp>
#include <tezla/dsp/Comb.hpp>
#include <tezla/dsp/Crossover.hpp>
#include <tezla/dsp/DcBlocker.hpp>
#include <tezla/dsp/Denormals.hpp>
#include <tezla/dsp/Formant.hpp>
#include <tezla/dsp/Lfo.hpp>
#include <tezla/dsp/Oversampler.hpp>
#include <tezla/dsp/Phaser.hpp>
#include <tezla/dsp/SmoothedValue.hpp>
#include <tezla/dsp/StepSequencer.hpp>
#include <tezla/dsp/TriodeStage.hpp>

#include "VoiceManager.hpp"

namespace tezla::sonitus {

/// Which comb topology the mangle section runs.
///
/// **Append-only** -- a choice parameter stores an index.
enum class CombMode
{
    off = 0,
    flange,
    phase,

    count
};

/// Where the tube sits relative to the comb. See the header.
///
/// **Append-only.**
enum class MangleOrder
{
    tubeThenComb = 0,
    combThenTube,

    count
};

/// What a global modulation slot is driven by.
///
/// **Append-only** -- a choice parameter stores an index.
///
/// Only the global sources are here, and deliberately: the per-voice ones (an
/// amp envelope, velocity, a note's random) have one value *per sounding note*,
/// and the mangle is one chain shared by all of them. "Amp envelope drives the
/// comb" has no answer with eight voices down. The per-voice matrix in
/// `VoiceParameters` is where those belong.
enum class GlobalSource
{
    none = 0,
    lfo1,
    lfo2,
    sequencer,

    /// **The tracked note's envelopes and velocity.** Appended, because the
    /// mangle needs to be envelopable and an LFO is not an envelope.
    ///
    /// The objection to per-voice sources here was that they have one value per
    /// sounding note and the mangle is one chain -- which is true, and the
    /// answer is the same one the comb and the formant already use: **follow
    /// the tracked note.** The most recently started voice that is still
    /// sounding is what the comb tracks the period of and the formant tracks
    /// the harmonics of, so taking its envelopes too makes the whole mangle
    /// follow one note rather than three stages disagreeing about which.
    ///
    /// With nothing sounding these read zero, which is the right answer for an
    /// envelope: a released keyboard is a closed envelope.
    ampEnvelope,
    modEnvelope1,
    modEnvelope2,
    velocity,

    /// The tracked note's ADV envelopes. **Appended** -- stored by index,
    /// CLAUDE.md section 8.
    advEnv1,
    advEnv2,
    advEnv3,

    /// The four macros, appended, and the same four values the voice matrix
    /// sees. A macro is the one control shape a matrix cannot express: one
    /// knob into several destinations at once, each with its own depth.
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

/// What a global modulation slot drives: the mangle's continuous controls.
///
/// **Append-only.**
///
/// `combTime` is the one this instrument exists for. The brief's original trick
/// was a flanger with its rate pinned at zero so the *depth* knob became a
/// direct comb control, drawn by hand on an automation lane. This is that
/// control with a modulation source behind it -- an envelope, another LFO, or
/// the step sequencer -- which is the thing an automation lane cannot do in
/// time with the note.
enum class GlobalDestination
{
    none = 0,
    combTime,
    combFeedback,
    combMix,
    phaseFrequency,
    formantMorph,
    tubeDrive,
    output,

    /// Which partial the harmonic lock selects. **Appended**, because the list
    /// is indexed by a choice parameter -- CLAUDE.md section 8.
    ///
    /// This is the one the overtone line is drawn with: point the sequencer at
    /// it and the melody walks the harmonic series of whatever is being played.
    formantHarmonic,

    /// Where the anti-formant sits, in octaves.
    formantNotch,

    /// **Phase 5, all appended** -- a stored slot is an index (CLAUDE.md §8).
    ///
    /// The size of the throat, in octaves of formant scaling; the depth of the
    /// machine's instability; and how fast the Shepard glissando climbs.
    tract,
    sagDepth,
    shepardRate,

    count
};

struct GlobalSlot
{
    GlobalSource source { GlobalSource::none };
    GlobalDestination destination { GlobalDestination::none };
    double depth { 0.0 };               ///< -1 .. +1
};

struct EngineParameters
{
    /// Three global slots. Fewer than the voice's six, because there are eight
    /// destinations rather than sixteen and only three sources to point at them.
    static constexpr int kGlobalSlots = 3;

    std::array<GlobalSlot, kGlobalSlots> globalSlots {};

    // ---- the voice ---------------------------------------------------------

    VoiceParameters voice;

    KeyboardMode keyboard { KeyboardMode::poly };
    int polyphony { 8 };
    double glideSeconds { 0.0 };

    // ---- global modulation sources -----------------------------------------

    dsp::Lfo::Wave lfo1Wave { dsp::Lfo::Wave::sine };
    double lfo1RateHz { 2.0 };

    /// Tempo sync. On, the rate knob stands aside and `lfo1Division` sets the
    /// speed from the host tempo; with retrigger off and the transport
    /// running, the *phase* is nailed to the bar as well -- the same bar is
    /// the same wobble on every pass, which is the whole reason producers
    /// sync LFOs. Retriggered, the phase belongs to the note and only the
    /// rate is synced.
    bool lfo1Sync { false };
    int lfo1Division { dsp::defaultDivision };

    double lfo1Smooth { 0.0 };

    /// Restart the LFO from the top of its cycle on every note-on.
    ///
    /// Free-running is the right default for a pad -- the movement is ambient
    /// and should not restart -- and exactly wrong for a bass line, where the
    /// wobble has to begin where the note does or every note lands on a
    /// different part of the cycle.
    /// How long the LFO's depth takes to come up from nothing after a note.
    ///
    /// **Restarted on every note-on, whether or not the LFO itself
    /// retriggers.** The two are separate ideas -- one is about the waveform's
    /// phase and the other about its depth -- and tying the fade to the
    /// retrigger switch would make a control that silently does nothing half
    /// the time. At 0 the fade is over before it starts and the depth is
    /// exactly 1, which is what the LFO did before this existed.
    double lfo1AttackSeconds { 0.0 };
    double lfo2AttackSeconds { 0.0 };

    bool lfo1Retrigger { false };

    /// How far the LFO's rate follows the played note, 0 to 1.
    ///
    /// At 1 the rate is proportional to the note's frequency, so an octave up
    /// is twice the speed. **This is how a reese phase stays in proportion**:
    /// the beating that gives a reese its character is a fraction of the note,
    /// not a fixed number of hertz, so a wobble that does not track turns into
    /// a different sound as you move up the keyboard.
    double lfo1KeyTrack { 0.0 };

    dsp::Lfo::Wave lfo2Wave { dsp::Lfo::Wave::triangle };
    double lfo2RateHz { 0.25 };
    bool lfo2Sync { false };
    int lfo2Division { dsp::defaultDivision };
    double lfo2Smooth { 0.0 };
    bool lfo2Retrigger { false };
    double lfo2KeyTrack { 0.0 };

    /// The step sequencer's rate, in steps per second when free-running.
    double sequencerRateHz { 8.0 };
    int sequencerLength { 16 };
    double sequencerGlide { 0.0 };
    std::array<double, dsp::StepSequencer::kMaxSteps> sequencerSteps {};

    /// Pointing the sequencer at this is the brief's old trick, built in: the
    /// LFO steps through a pattern of speeds instead of being drawn on an
    /// automation lane.
    double sequencerToLfo1Rate { 0.0 };     ///< octaves of rate

    // ---- the split ---------------------------------------------------------

    /// Where the sub is taken out of the mangle path. Below this, nothing
    /// happens to the signal but a DC blocker and a fold to mono.
    double splitHz { 120.0 };

    /// Whether the sub band is summed to mono. On by default, because a wide
    /// sub is the single most common way to lose a bass on a club system.
    bool subMono { true };

    /// Whether the split exists at all. Off routes the **whole** signal down
    /// the body chain -- no crossover, no sub mono, nothing between the voices
    /// and the mangle but one 5 Hz DC blocker on the way out. The "pure"
    /// setting, for people who split on a DAW mixer bus instead and do not
    /// want the LR4's phase rotation in the path twice.
    bool subSplit { true };

    // ---- the mangle --------------------------------------------------------

    MangleOrder order { MangleOrder::tubeThenComb };

    /// How hard the tube is driven, in dB. 0 leaves it bit-exactly out.
    double tubeDriveDb { 0.0 };

    CombMode combMode { CombMode::off };

    double combTimeMs { 3.0 };
    double combKeyTrack { 0.0 };
    double combFeedback { 0.0 };
    double combDamping { 0.0 };
    double combSpread { 0.0 };
    double combMix { 0.0 };
    bool combInverted { false };

    /// Snaps the comb's resonant pitch onto the loaded scale.
    ///
    /// The comb key-tracks, but its delay is a *continuous* frequency, so on a
    /// microtuned patch -- which is half of why Sonitus exists -- it resonates
    /// between the scale's notes and fights the tuning it is meant to serve.
    /// Off by default, and off is bit-exact: the ratio it applies is exactly
    /// 1.0 and the comb multiplies by it unconditionally.
    bool combScaleLock { false };

    /// The four macros, 0 .. 1, published into `GlobalSources` so both
    /// matrices read the same number. Zero contributes nothing anywhere.
    std::array<double, 4> macros {};

    /// **Shepard.** How fast the glissando climbs, in octaves per second,
    /// signed -- negative falls. Zero is a legitimate setting and is a *held*
    /// windowed octave stack, which is its own sound; the panel reads it as
    /// "held" the way LFO rate 0 already does.
    ///
    /// One rate for the whole instrument, because the phase is one accumulator
    /// for the whole instrument -- see `GlobalSources::shepardOctaves`. It
    /// means nothing unless an oscillator's Stack is set to Shepard.
    double shepardRate { 0.0 };

    /// Tempo sync for it: one octave per division, taking its direction from
    /// `shepardRate`'s sign. The same pattern the LFOs use.
    bool shepardSync { false };
    int shepardDivision { dsp::defaultDivision };

    double phaseFrequencyHz { 800.0 };
    int phaseStages { 4 };

    double formantMorph { 0.0 };
    double formantSharpness { 0.5 };
    double formantMix { 0.0 };

    /// The overtone-singing controls. All neutral by default, so a patch saved
    /// before they existed reopens sounding the same.
    double formantHarmonic { 1.0 };
    double formantLock { 0.0 };
    double formantNotchHz { 1000.0 };
    double formantNotchDepth { 0.0 };

    /// One knob of tone: negative tips the balance towards the bass, positive
    /// towards the top. Both ends pivot at 700 Hz.
    double tilt { 0.0 };                    ///< -1 .. +1

    // ---- global ------------------------------------------------------------

    double outputDb { 0.0 };

    dsp::OversamplingMode oversampling { dsp::OversamplingMode::Auto };

    /// What an offline bounce runs at. `sameAsLive` is neutral -- the render
    /// is exactly what the session played -- and anything else takes effect
    /// only while the host reports it is rendering offline; see
    /// `Engine::setOffline`. Appended, defaulting to neutral: a project saved
    /// before it existed bounces the same.
    dsp::RenderOversampling renderOversampling { dsp::RenderOversampling::sameAsLive };
};

class Engine
{
public:
    /// The tilt filter's pivot. 700 Hz is where the ear's sense of "weight"
    /// against "air" divides, and it is the same pivot Emberdrive uses.
    static constexpr double kTiltPivotHz = 700.0;
    static constexpr double kTiltRangeDb = 9.0;

    /// The widest the tube's drive control reaches.
    static constexpr double kMaximumTubeDriveDb = 36.0;

    /// How long the chain has to have been **bit-exactly** silent, with no
    /// voice sounding, before the render is skipped altogether.
    ///
    /// An idle instrument is not free otherwise, and this one measured 17.9
    /// ms/s -- 1.8% of a core -- with nothing playing at all: the mangle's
    /// filters and the oversampler's decimation FIRs run whether or not there
    /// is anything to run them on. Ten idle instances in a project is a fifth
    /// of a core spent on silence.
    ///
    /// A whole second of it, because the test has to be one that cannot be
    /// passed by a signal happening to cross zero: a resonance that could stay
    /// under the threshold for a second is below 1 Hz.
    static constexpr double kIdleSecondsBeforeSkipping = 1.0;

    /// How quiet counts as silent, in absolute amplitude.
    ///
    /// **Not exactly zero, and that was measured rather than assumed.** A comb
    /// at half feedback decays geometrically, so it reaches 4.1e-29 in three
    /// seconds and takes another sixteen to reach the smallest double the
    /// denormal flush leaves. Waiting for a bit-exact zero would mean the skip
    /// essentially never fired with the comb switched on.
    ///
    /// -240 dBFS is forty decibels below one step of 24-bit audio, and every
    /// stage downstream of the frozen state is linear or attenuating -- so a
    /// tail parked here cannot become audible again however the controls are
    /// moved, and resuming from it is continuous rather than a step.
    static constexpr double kIdleThreshold = 1.0e-12;

    void prepare (double sampleRate, int maxBlockSize);
    void reset() noexcept;

    void setParameters (const EngineParameters& parameters) noexcept { pending_ = parameters; }
    [[nodiscard]] const EngineParameters& getParameters() const noexcept { return pending_; }

    // ---- playing -----------------------------------------------------------

    void noteOn (int note, double velocity) { voices_.noteOn (note, velocity); }
    void noteOff (int note) { voices_.noteOff (note); }
    void setSustain (bool down) { voices_.setSustain (down); }
    void setBendSemitones (double semitones) noexcept { voices_.setBendSemitones (semitones); }
    void allNotesOff() noexcept { voices_.allNotesOff(); }

    /// The Shepard glissando's accumulator, in octaves travelled. For the test
    /// that pins the wrap, and for a display.
    [[nodiscard]] double getShepardOctaves() const noexcept { return shepardOctaves_; }

    dsp::Tuning& tuning() noexcept { return voices_.tuning(); }
    [[nodiscard]] const dsp::Tuning& tuning() const noexcept { return voices_.tuning(); }

    [[nodiscard]] int activeVoiceCount() const noexcept { return voices_.activeVoiceCount(); }

    /// Locks the global sources to the host's transport. Call once per block,
    /// before `process`.
    ///
    /// The tempo is needed as well as the position: a host reports ppq at the
    /// *start* of a block, and the sequencer's position has to keep moving
    /// inside it or a 512-sample block would be one step long however fast the
    /// pattern is set. Without a running transport the sources free-run from
    /// their own clocks.
    void setTransport (double ppqPosition, double beatsPerMinute, bool playing,
                       int beatsPerBar = 4) noexcept
    {
        ppq_ = ppqPosition;
        bpm_ = beatsPerMinute > 0.0 ? beatsPerMinute : 120.0;
        transportRunning_ = playing && ppqPosition >= 0.0;

        readouts_.bpm.store (bpm_, std::memory_order_relaxed);
        readouts_.beatsPerBar.store (beatsPerBar > 0 ? beatsPerBar : 4,
                                     std::memory_order_relaxed);
    }

    /// Renders `numSamples` into `output`, which must have two channels.
    void process (double* const* output, int numSamples) noexcept;

    /// How much latency the oversampler is adding, in samples at the host rate.
    [[nodiscard]] int getLatencySamples() const noexcept
    {
        return oversampler_.getLatencySamples();
    }

    [[nodiscard]] int getOversamplingFactor() const noexcept
    {
        return oversampler_.getFactor();
    }

    /// Tells the engine whether the host is rendering offline -- bouncing a
    /// track rather than playing it -- which is the only time
    /// `EngineParameters::renderOversampling` applies.
    ///
    /// Taken at the next `process` (or the next `prepare`) like every other
    /// graph change: a different factor is a rebuild, and the rebuild cuts the
    /// sounding notes, the clean stop a factor change has always been here. A
    /// host that flips the flag and then re-prepares, which is what the VST3
    /// contract asks of it, gets a graph built at the render factor before the
    /// first offline sample and never hears the cut.
    void setOffline (bool offline) noexcept { offline_ = offline; }
    [[nodiscard]] bool isOffline() const noexcept { return offline_; }

    /// The oversampling mode actually in force -- the render setting while
    /// offline, the live one otherwise.
    [[nodiscard]] dsp::OversamplingMode effectiveOversampling() const noexcept
    {
        return dsp::effectiveOversamplingMode (pending_.oversampling,
                                               pending_.renderOversampling, offline_);
    }

    /// The current values of the global modulation sources. **Same thread
    /// only** -- for tests and for the offline measurement tool. Anything on
    /// the message thread reads `readouts()` instead.
    [[nodiscard]] const GlobalSources& getGlobalSources() const noexcept { return sources_; }

    /// What the panel shows, published for the message thread to read.
    ///
    /// Atomics rather than plain members, and not as a formality: a `double`
    /// read on one thread while another writes it is a data race, and the whole
    /// point of these is that the audio thread is moving them. Anvil's meters
    /// are built the same way. Relaxed ordering throughout -- each of these is
    /// a single independent number and nothing is inferred from seeing one
    /// before another.
    struct Readouts
    {
        /// Where the comb's first notch is *actually* sitting -- modulation and
        /// key tracking included. Not a figure worked out from the knob,
        /// because the knob is wrong whenever anything is sweeping it, which in
        /// this instrument is most of the time.
        std::atomic<double> combNotchHz { 0.0 };

        /// Which step the sequencer is on. The panel lights it, which is the
        /// only way to tell a pattern that is running from one that is stopped
        /// -- with the rate at a quarter of a step per beat, "stopped" and
        /// "slow" look identical for four seconds at a time.
        std::atomic<int> sequencerStep { 0 };

        std::atomic<double> lfo1 { 0.0 };
        std::atomic<double> lfo2 { 0.0 };
        std::atomic<double> sequencer { 0.0 };

        /// The tracked note's three envelopes, so the panel can draw a playhead
        /// on the curve it is editing. The **tracked** note rather than a sum,
        /// for the same reason the comb and the formant follow it: an envelope
        /// has one value per voice, and averaging eight of them describes none
        /// of them. Index 0 is the amplitude envelope, 1 and 2 the mod ones.
        std::atomic<double> envelopeLevels[3] { { 0.0 }, { 0.0 }, { 0.0 } };

        /// The tempo and bar length the engine is actually snapping against,
        /// so the envelope rulers draw the grid the sound is on rather than a
        /// tempo the panel guessed. Published from `setTransport`, which is
        /// where the host's own numbers arrive.
        ///
        /// 120 and 4 are the fallbacks, not a default anybody chose: a host
        /// with no transport reports nothing, and an envelope still has to
        /// draw something.
        std::atomic<double> bpm { 120.0 };
        std::atomic<int> beatsPerBar { 4 };
    };

    [[nodiscard]] const Readouts& readouts() const noexcept { return readouts_; }

    /// Where formant `index` is actually sitting, harmonic lock included.
    /// Same-thread only, like `getGlobalSources`.
    [[nodiscard]] double getFormantHz (int index) const noexcept
    {
        return formant_.formantHz (index);
    }

    /// The phaser's centre as it is actually running. Same-thread only, like
    /// `getGlobalSources`.
    [[nodiscard]] double getPhaseFrequencyHz() const noexcept { return phaser_.getFrequencyHz(); }

    /// Where the comb's first notch is sitting. Same-thread only.
    [[nodiscard]] double getCombNotchHz() const noexcept { return comb_.firstNotchHz(); }

private:
    void applyPending() noexcept;
    void applyGlobalModulation() noexcept;
    void aimComb() noexcept;
    void rebuildForRate() noexcept;
    void advanceGlobalSources (int samples) noexcept;
    void renderChunk (double* left, double* right, int numSamples) noexcept;
    void mangle (double& left, double& right) noexcept;
    [[nodiscard]] const VoiceParameters& snappedVoice() noexcept;
    void updateTilt() noexcept;

    [[nodiscard]] double combDelaySeconds() const noexcept;

    /// How much the global matrix is adding to one destination, in that
    /// destination's own units. Zero when nothing points at it.
    [[nodiscard]] double globalModulationFor (GlobalDestination destination) const noexcept;

    double sampleRate_ { 48000.0 };
    double internalRate_ { 48000.0 };
    int maxBlockSize_ { 512 };

    EngineParameters pending_;
    EngineParameters active_;
    bool configured_ { false };

    VoiceManager voices_;

    dsp::Lfo lfo1_;
    dsp::Lfo lfo2_;
    dsp::StepSequencer sequencer_;
    GlobalSources sources_;
    Readouts readouts_;

    dsp::Oversampler oversampler_;

    dsp::LinkwitzRiley4<double> split_[2];
    dsp::DcBlocker<double> subBlocker_[2];

    /// The SPLIT switch, smoothed to a 30 ms crossfade: 1 is split on. See the
    /// comment in `mangle` for why this is arithmetic rather than a branch.
    dsp::SmoothedValue<double> splitMix_;

    /// The pure path's only protection: with the split off, nothing separates
    /// the tube's asymmetry from the output, and CLAUDE.md section 7 calls DC
    /// a defect. First order at 5 Hz, same corner as the sub blocker, so it
    /// cannot thin the sub it exists to protect.
    dsp::DcBlocker<double> fullBlocker_[2];

    dsp::TriodeStage tube_[2];
    dsp::Comb comb_;
    dsp::Phaser phaser_;
    dsp::Formant formant_;

    dsp::Biquad<double> tiltLow_[2];
    dsp::Biquad<double> tiltHigh_[2];

    /// The voice parameters with any snapped envelope times applied -- see
    /// snappedVoice() in the cpp. A member rather than a local so the copy is
    /// storage reuse, not an allocation.
    VoiceParameters snappedVoice_;

    dsp::SmoothedValue<double> outputGain_;
    dsp::SmoothedValue<double> tubeGain_;

    /// The global matrix's contribution to the comb's delay, as a ratio. Kept
    /// rather than recomputed because `combDelaySeconds` is const and is asked
    /// for the *running* delay, not the knob's.
    double combModulation_ { 1.0 };

    /// How many consecutive internal samples the whole chain has produced
    /// **exactly** zero for, with no voice sounding. See `kIdleSamplesSeconds`.
    int idleSamples_ { 0 };

    /// What the LFOs last saw the note-on counter at, so a retrigger fires once
    /// per note rather than for as long as a note is down.
    unsigned long long seenNoteOns_ { 0 };

    /// How far each LFO's fade-in has got, 0 to 1.
    double lfo1Fade_ { 1.0 };
    double lfo2Fade_ { 1.0 };

    double ppq_ { -1.0 };
    double bpm_ { 120.0 };
    double beatsIntoBlock_ { 0.0 };

    /// Where the Shepard glissando has got to, in octaves travelled. One for
    /// the whole instrument -- both oscillators of every voice read it, so a
    /// held chord glides as one thing. Wrapped into [0, 420) because 420 is
    /// divisible by every copy count the bank allows, which makes the wrap an
    /// exact whole number of turns at any count rather than a small jump.
    double shepardOctaves_ { 0.0 };
    bool transportRunning_ { false };
    int sinceControl_ { 0 };

    /// Whether the host says it is rendering offline. See `setOffline`.
    bool offline_ { false };
};

} // namespace tezla::sonitus
