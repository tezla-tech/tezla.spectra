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
#include <cmath>

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
    double lfo1Smooth { 0.0 };

    dsp::Lfo::Wave lfo2Wave { dsp::Lfo::Wave::triangle };
    double lfo2RateHz { 0.25 };
    double lfo2Smooth { 0.0 };

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

    double phaseFrequencyHz { 800.0 };
    int phaseStages { 4 };

    double formantMorph { 0.0 };
    double formantSharpness { 0.5 };
    double formantMix { 0.0 };

    /// One knob of tone: negative tips the balance towards the bass, positive
    /// towards the top. Both ends pivot at 700 Hz.
    double tilt { 0.0 };                    ///< -1 .. +1

    // ---- global ------------------------------------------------------------

    double outputDb { 0.0 };

    dsp::OversamplingMode oversampling { dsp::OversamplingMode::Auto };
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
    void setTransport (double ppqPosition, double beatsPerMinute, bool playing) noexcept
    {
        ppq_ = ppqPosition;
        bpm_ = beatsPerMinute > 0.0 ? beatsPerMinute : 120.0;
        transportRunning_ = playing && ppqPosition >= 0.0;
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

    /// The current values of the global modulation sources. For the UI's
    /// modulation display, and for tests.
    [[nodiscard]] const GlobalSources& getGlobalSources() const noexcept { return sources_; }

    /// Which step the sequencer is on. The panel lights it, which is the only
    /// way to tell a pattern that is running from one that is stopped -- and
    /// with the rate at a quarter of a step per beat, "stopped" and "slow" look
    /// identical for four seconds at a time.
    [[nodiscard]] int getSequencerStep() const noexcept { return sequencer_.getStepIndex(); }

    /// Where the comb's first notch is *actually* sitting -- modulation and key
    /// tracking included. The panel shows this rather than a figure worked out
    /// from the knob, because the knob is wrong whenever anything is sweeping
    /// it, which in this instrument is most of the time.
    [[nodiscard]] double getCombNotchHz() const noexcept { return comb_.firstNotchHz(); }

    /// The phaser's centre as it is actually running, likewise.
    [[nodiscard]] double getPhaseFrequencyHz() const noexcept { return phaser_.getFrequencyHz(); }

private:
    void applyPending() noexcept;
    void applyGlobalModulation() noexcept;
    void aimComb() noexcept;
    void rebuildForRate() noexcept;
    void advanceGlobalSources (int samples) noexcept;
    void renderChunk (double* left, double* right, int numSamples) noexcept;
    void mangle (double& left, double& right) noexcept;
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

    dsp::Oversampler oversampler_;

    dsp::LinkwitzRiley4<double> split_[2];
    dsp::DcBlocker<double> subBlocker_[2];

    dsp::TriodeStage tube_[2];
    dsp::Comb comb_;
    dsp::Phaser phaser_;
    dsp::Formant formant_;

    dsp::Biquad<double> tiltLow_[2];
    dsp::Biquad<double> tiltHigh_[2];

    dsp::SmoothedValue<double> outputGain_;
    dsp::SmoothedValue<double> tubeGain_;

    /// The global matrix's contribution to the comb's delay, as a ratio. Kept
    /// rather than recomputed because `combDelaySeconds` is const and is asked
    /// for the *running* delay, not the knob's.
    double combModulation_ { 1.0 };

    /// How many consecutive internal samples the whole chain has produced
    /// **exactly** zero for, with no voice sounding. See `kIdleSamplesSeconds`.
    int idleSamples_ { 0 };

    double ppq_ { -1.0 };
    double bpm_ { 120.0 };
    double beatsIntoBlock_ { 0.0 };
    bool transportRunning_ { false };
    int sinceControl_ { 0 };
};

} // namespace tezla::sonitus
