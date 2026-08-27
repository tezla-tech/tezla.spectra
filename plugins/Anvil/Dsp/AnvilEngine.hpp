#pragma once

// Anvil -- a valve amplifier and cabinet, modelled from the mechanisms rather
// than fitted to a curve.
//
// Framework-free: no JUCE, no VST3 headers. It takes double buffers and a
// sample rate, so the whole amplifier can be swept and measured offline before
// any of it reaches a DAW.
//
// ---------------------------------------------------------------------------
// The thesis
// ---------------------------------------------------------------------------
//
// **The character is in the time constants and the load, not the transfer
// curve.** A waveshaper, however carefully drawn, does the same thing to the
// hundredth chord as to the first, and the same thing to a low E as to a lead
// line two octaves up. A valve amplifier does neither, and every mechanism in
// this chain is one of the reasons why:
//
//   cathode bias shift      ~33 ms   the operating point moves under load
//   grid conduction         ~22 ms   the coupling capacitor charges and blocks
//   supply sag              ~45 ms   the rail falls and the headroom with it
//   transformer core flux            saturation that depends on *pitch*
//   speaker impedance                the amplifier's own tone control
//
// The fourth is the one worth the trouble. Flux is the integral of voltage, so
// the same voltage puts twice as much of it into the core an octave down: a low
// note saturates the transformer while a high note sails through, measured here
// at about 18 dB of THD per octave. That is a frequency-dependent distortion
// with nothing in the code that tests frequency, and it is most of the
// difference between a power amplifier being worked and a clipper being driven.
//
// ---------------------------------------------------------------------------
// The chain
// ---------------------------------------------------------------------------
//
//   in --+------------------------------------------ dry (BypassMixer) --+
//        |                                                               |
//        +-- input trim                                                  |
//              |                                                         |
//              |   [ oversampled x1/x2/x4/x8 ]                           |
//              |     TriodeStage 1..4      cascaded preamp valves        |
//              |       (tone stack inserted where the voicing says)      |
//              |     ToneStack             passive FMV, solved as a      |
//              |                           circuit rather than as an EQ  |
//              |     master trim                                         |
//              |     PowerAmp              class AB, sag, feedback with  |
//              |                           presence and resonance, and   |
//              |                           the transformer               |
//              |     SpeakerLoad           the impedance divider         |
//              |     Cabinet               driver, box and microphone    |
//              |   [ /oversampled ]                                      |
//              |                                                         v
//              +----------------------------------- output trim -----> out
//
// Everything nonlinear is inside the oversampled section, and so is everything
// linear that follows it -- the speaker load and the cabinet are the shape the
// power amplifier is driving into, so they belong on the same side of the
// resampling as the thing driving them.
//
// ---------------------------------------------------------------------------
// Where the tone stack goes, and why it is not a detail
// ---------------------------------------------------------------------------
//
// A passive tone stack in front of the distortion and one behind it are
// different instruments. In front, it decides what gets distorted -- scooping
// the mids before three cascaded valves is the entire modern high-gain sound.
// Behind, it only decides what you hear of what was already made. Real
// amplifiers put it in different places for exactly this reason, and the
// voicings here follow suit: the clean lane has one valve and then the stack,
// the vintage lane has the stack between two, and the high-gain lane has two
// valves in front of it and one behind.

#include <array>
#include <vector>

#include <tezla/dsp/BypassMixer.hpp>
#include <tezla/dsp/Cabinet.hpp>
#include <tezla/dsp/HalfbandFir.hpp>
#include <tezla/dsp/Oversampler.hpp>
#include <tezla/dsp/PowerAmp.hpp>
#include <tezla/dsp/SmoothedValue.hpp>
#include <tezla/dsp/SpeakerLoad.hpp>
#include <tezla/dsp/ToneStack.hpp>
#include <tezla/dsp/TriodeStage.hpp>

namespace tezla::anvil
{

/// The three amplifier lanes.
///
/// They differ in what a real amplifier differs in: how many valves the signal
/// passes before the tone stack, how much global feedback the output stage has,
/// how stiff the supply and the transformer are, and which tone stack it is.
enum class Voicing
{
    /// One valve, a lot of feedback, a stiff supply and a large transformer.
    /// The lane that has to be genuinely transparent at low drive rather than a
    /// quieter version of the dirty one -- CLAUDE.md priority 2.
    clean = 0,

    /// Two valves with the stack between them, almost no feedback, a valve
    /// rectifier that sags, and a transformer small enough to feel it. The lane
    /// where the amplifier moves under you.
    vintage,

    /// Three valves with the stack after the second, a scooped modern stack, a
    /// tight supply and a moderate loop. The lane that stays articulate at gain
    /// settings the other two fall apart at.
    modern,

    count
};

/// Which cabinet is on the end, or none.
enum class CabinetChoice
{
    /// Straight out of the power amplifier and the speaker load, with no
    /// acoustic model at all. For re-amping into a real cabinet, or into
    /// somebody else's impulse response.
    none = 0,

    combo,
    british,
    vintage,

    count
};

struct Parameters
{
    // ---- the amplifier -------------------------------------------------------

    Voicing voicing { Voicing::vintage };

    /// How hard the preamp is driven, in dB. The single most important control.
    double gainDb { 18.0 };            ///< -6 .. +48

    /// How many cascaded preamp valves, over and above what the voicing asks
    /// for. Zero leaves the voicing alone; this is the control that takes the
    /// clean lane somewhere it was never meant to go.
    int extraStages { 0 };             ///< 0 .. 2

    // ---- the tone stack ------------------------------------------------------

    double bass { 0.5 };               ///< 0 .. 1
    double middle { 0.5 };             ///< 0 .. 1
    double treble { 0.5 };             ///< 0 .. 1

    // ---- the power amplifier -------------------------------------------------

    /// How hard the output stage is driven, in dB.
    ///
    /// Attenuation only, because that is what a master volume is: a
    /// potentiometer between the preamp and the phase inverter. It cannot
    /// amplify. Turning it up does not add gain, it stops removing it -- and
    /// what then distorts is the phase inverter and the output valves, which is
    /// a completely different kind of dirt from Gain.
    double masterDb { -6.0 };          ///< -40 .. 0

    /// Shunts the high end out of the feedback loop, so the output stage's own
    /// gain and its own distortion show through up there. Not a treble control.
    double presence { 0.3 };           ///< 0 .. 1

    /// The same at the other end, which with a core that saturates on flux is
    /// how a low note is made to bloom.
    double resonance { 0.2 };          ///< 0 .. 1

    /// How far the rail falls under load, scaling the voicing's own figure.
    double sag { 1.0 };                ///< 0 .. 2

    /// The frequency at which a full swing just fills the output transformer's
    /// core. Raise it and the low end goes woolly and harmonically rich while
    /// everything above stays exactly as it was.
    ///
    /// The creative control, and for music that lives under 60 Hz it is the one
    /// that matters. The range goes far past any real transformer.
    double coreHz { 45.0 };            ///< 20 .. 400

    // ---- the cabinet ---------------------------------------------------------

    CabinetChoice cabinet { CabinetChoice::british };

    /// 0 is the dust cap, 1 is the surround. Two octaves of lowpass.
    double micPosition { 0.25 };       ///< 0 .. 1

    double micDistanceCm { 5.0 };      ///< 2 .. 50

    /// Nominal load over the amplifier's output impedance. Below 1 is a valve
    /// amplifier with no loop; 20 is solid state and flat.
    double damping { 1.0 };            ///< 0.2 .. 20

    // ---- global --------------------------------------------------------------

    double outputDb { 0.0 };           ///< -24 .. +24

    /// Dry/wet. At 0 the plugin is bit-exact.
    double mix { 1.0 };                ///< 0 .. 1

    /// Auto here targets ~384 kHz internally, not the ~192 kHz of CLAUDE.md's
    /// house table, and that is a measured departure rather than a preference.
    /// See Engine::autoFactorFor.
    dsp::OversamplingMode oversampling { dsp::OversamplingMode::Auto };

    bool bypass { false };

    [[nodiscard]] bool operator== (const Parameters&) const = default;
};

class Engine
{
public:
    static constexpr int kMaxChannels = 2;

    /// The most cascaded preamp valves the engine will build.
    static constexpr int kMaxStages = 5;

    // The bounds the engine enforces, so a caller cannot ask for something the
    // model does not mean. The UI ranges are these.
    static constexpr double kMinimumCoreHz = 20.0;
    static constexpr double kMaximumCoreHz = 400.0;
    static constexpr double kMinimumDamping = 0.2;
    static constexpr double kMaximumDamping = 20.0;
    static constexpr double kMinimumMicCm = 2.0;
    static constexpr double kMaximumMicCm = 50.0;

    /// Allocates, for the worst case rather than for the current settings.
    /// Never call from the audio thread.
    void prepare (double sampleRate, int maxBlockSize, int numChannels);

    void reset();

    /// Cheap; safe once per block from the audio thread. Returns true if the
    /// reported latency changed, in which case the host must be told.
    bool setParameters (const Parameters& parameters);

    /// In-place. `channels` holds `numChannels` pointers to `numSamples`
    /// doubles.
    void process (double* const* channels, int numChannels, int numSamples) noexcept;

    [[nodiscard]] int getLatencySamples() const noexcept { return latency_; }
    [[nodiscard]] double getSampleRate() const noexcept { return sampleRate_; }

    /// What the oversampler is actually running at, which is what a tooltip has
    /// to say rather than what the control is set to.
    [[nodiscard]] int getOversamplingFactor() const noexcept { return oversampler_.getFactor(); }

    /// How many preamp valves the current voicing and Stages setting build.
    [[nodiscard]] int getStageCount() const noexcept { return stageCount_; }

    /// Which stage the tone stack sits after. 0 means in front of everything.
    [[nodiscard]] int getToneStackPosition() const noexcept { return toneStackAfter_; }

    /// Anvil's own Auto, which targets roughly **384 kHz** internally where the
    /// house table in CLAUDE.md section 6 targets 192.
    ///
    /// | host rate     | factor | effective   |
    /// |---------------|--------|-------------|
    /// | 44.1 / 48 k   | x8     | 353 - 384 k |
    /// | 88.2 / 96 k   | x4     | 353 - 384 k |
    /// | 176.4 / 192 k | x2     | 353 - 384 k |
    /// | above         | x1     | as given    |
    ///
    /// The house figure was set for a plugin with one shaper in the path. This
    /// one has three cascaded valve stages and an output stage, and a cascade
    /// compounds: each stage distorts the harmonics the last one made, so the
    /// energy reaching the internal Nyquist is far greater than any single
    /// shaper produces. Measured at maximum gain, feeding 1000.49 Hz -- a
    /// frequency chosen because it does *not* divide the host rate, so aliases
    /// land where they can be seen rather than on a harmonic bin:
    ///
    ///     lane        x2        x4        x8
    ///     clean    -49.9     -59.9     -70.3   dBFS
    ///     vintage  -46.5     -59.7     -75.2
    ///     modern   -33.1     -47.2     -72.3
    ///
    /// CLAUDE.md section 7 asks for nothing above -60 dBFS at maximum drive.
    /// Only x8 delivers that on all three, so that is what Auto picks. The
    /// priority order in CLAUDE.md section 1 puts fidelity above CPU in as many
    /// words, and this is the case it was written for: x8 costs about 23% of
    /// one core for a stereo instance at 48 kHz against 11% at x4.
    ///
    /// The manual settings are unchanged and mean exactly what they say, so
    /// anyone who would rather have the CPU back can take x4 and know what it
    /// costs.
    [[nodiscard]] static int autoFactorFor (double sampleRate) noexcept;

    // ---- metering, written once per block, read from the message thread ------

    /// How far the rail fell in the last block, 0 to 1.
    [[nodiscard]] double getSag() const noexcept { return sagMeter_; }

    /// Peak core flux in the last block, in units of the core's capacity. Above
    /// 1 the transformer is saturating and the low end is going with it.
    [[nodiscard]] double getFlux() const noexcept { return fluxMeter_; }

    /// How far the first preamp stage's operating point drifted, in knees.
    [[nodiscard]] double getBiasShift() const noexcept { return biasMeter_; }

private:
    struct VoicingSpec
    {
        int stages;
        int toneStackAfter;
        dsp::ToneStackVoicing stack;

        /// Scales what Gain means for this lane, so the control is comparable
        /// across the three. A clean amplifier's first valve has more headroom
        /// than a high-gain one's; without this, Gain at +12 dB is spotless on
        /// one lane and shredded on another and the number means nothing.
        double inputScale;

        double stageGain;          ///< linear, applied between valves
        double powerDrive;         ///< how hard master-at-full drives the output stage
        /// Trims the lane to land near full scale when it is fully driven.
        ///
        /// Negative, and that is not a mistake: a saturated output stage into a
        /// cabinet with 7 dB of cone breakup and a proximity shelf on top runs
        /// hot, so the lane needs taking *down* rather than up. Calibrated by
        /// measurement -- master at 0 dB and gain at maximum lands each lane
        /// within a decibel of -1 dBFS.
        double makeupDb;

        dsp::PowerAmpParameters power;
        dsp::TriodeStageParameters triode;
    };

    [[nodiscard]] static VoicingSpec specFor (Voicing voicing) noexcept;

    [[nodiscard]] int factorFor (const Parameters& parameters) const noexcept;

    bool updateLatency();

    /// Builds the graph: every prepare(), every allocation, every reset.
    ///
    /// Called when the host rate or the oversampling factor moves, and never
    /// for a parameter change -- prepare() resets, and a filter's state is
    /// meaningful, so using it to apply a parameter zeroes the amplifier every
    /// time a knob moves. CLAUDE.md section 7 has the worked example.
    void configure();

    /// Applies the parameters that pick a topology or a component value, using
    /// setters that preserve state. Never resets anything.
    void applyControls();

    /// Steps the control-rate smoothers on one by one interval.
    void advanceControls() noexcept;

    void processChunk (double* const* oversampled, int active, int from, int numSamples) noexcept;

    void processOversampled (double* const* channels, int active, int numSamples) noexcept;

    double sampleRate_   { 44100.0 };
    int    maxBlockSize_ { 512 };
    int    numChannels_  { 2 };
    int    latency_      { 0 };

    Parameters parameters_;

    /// What prepare() actually built, rather than what the parameters now ask
    /// for. Comparing against this rather than a "have parameters been set"
    /// flag is what makes the very first setParameters() take effect --
    /// prepare() necessarily runs before any parameters are known, so a flag
    /// silently swallows it. CLAUDE.md section 7.
    int preparedFactor_ { 0 };

    int stageCount_ { 2 };
    int toneStackAfter_ { 1 };

    dsp::Oversampler oversampler_;
    dsp::BypassMixer bypass_;

    struct ChannelState
    {
        std::array<dsp::TriodeStage, kMaxStages> stages {};
        dsp::ToneStack toneStack;

        /// The phase inverter: the valve between the master volume and the
        /// output stage, which every amplifier has and which was left out of
        /// the first draft of this engine.
        ///
        /// It is not a detail. Two things go wrong without it. Musically, the
        /// output stage is fed a signal with no bound and no bandwidth, so the
        /// grit people call "power amp distortion" -- much of which is really
        /// the inverter running out of swing -- is simply absent. Numerically,
        /// it is worse: ADAA averages the shaper over the segment between two
        /// samples, which is right only while the signal moves smoothly, and a
        /// preamp output multiplied by a hundred jumps clean across the
        /// clipper in a single sample. The average excess over that segment is
        /// near zero where the endpoint needs the whole of it, the two stop
        /// cancelling, and the measured output reached **70 times full scale**
        /// on an output stage that on its own peaks at 3.6.
        ///
        /// A valve bounds its output at about 3 and rolls off at its plate, so
        /// putting the real component back fixes both at once.
        dsp::TriodeStage phaseInverter;

        dsp::PowerAmp power;
        dsp::SpeakerLoad speaker;
        dsp::Cabinet cabinet;

        /// The dry path for Mix, delayed by the reported latency.
        ///
        /// A plain integer delay rather than a filter, because the
        /// oversampler's round trip is a whole number of base-rate samples by
        /// design -- so the dry signal arrives bit-for-bit untouched and Mix at
        /// 0 is exactly the input. Halo does the same and for the same reason.
        dsp::DelayLine dryDelay;
    };

    std::array<ChannelState, kMaxChannels> channels_ {};

    /// How often the networks are rebuilt, in base-rate samples.
    ///
    /// Counted in samples and the sample loop cut at its boundary, rather than
    /// rebuilt once per callback -- because "once per block" makes the output
    /// depend on the host's buffer size, and no arrangement of a per-call timer
    /// fixes that. Emberdrive's voicing measured 0.296 of full scale between
    /// 64- and 512-sample blocks before this rule went into CLAUDE.md section 7.
    ///
    /// 32 samples is 0.67 ms at 48 kHz, so a tone control moving at automation
    /// speed is resolved to well under a millisecond, and the four networks
    /// cost about 14 flops per sample per channel amortised.
    static constexpr int kControlIntervalSamples = 32;

    int controlCountdown_ { 0 };

    // Per sample: cheap, and heard directly.
    dsp::SmoothedValue<double> inputGain_;
    dsp::SmoothedValue<double> stageGain_;
    dsp::SmoothedValue<double> masterGain_;
    dsp::SmoothedValue<double> outputGain_;
    dsp::SmoothedValue<double> mix_;

    // Per control interval: each one rebuilds a network or a filter bank, and
    // each is prepared at sampleRate_/kControlIntervalSamples so its time
    // constant is in seconds regardless.
    dsp::SmoothedValue<double> bass_;
    dsp::SmoothedValue<double> middle_;
    dsp::SmoothedValue<double> treble_;
    dsp::SmoothedValue<double> presence_;
    dsp::SmoothedValue<double> resonance_;
    dsp::SmoothedValue<double> sag_;
    dsp::SmoothedValue<double> coreHz_;
    dsp::SmoothedValue<double> damping_;
    dsp::SmoothedValue<double> micPosition_;
    dsp::SmoothedValue<double> micDistance_;

    std::array<std::vector<double>, kMaxChannels> dry_ {};
    std::array<double*, kMaxChannels> dryPointers_ {};

    double sagMeter_  { 0.0 };
    double fluxMeter_ { 0.0 };
    double biasMeter_ { 0.0 };
};

} // namespace tezla::anvil
