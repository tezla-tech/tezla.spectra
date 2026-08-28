#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include <tezla/dsp/BypassMixer.hpp>
#include <tezla/dsp/Modulation.hpp>
#include <tezla/dsp/VuMeter.hpp>

#include <tezla/ui/AbCompare.hpp>
#include <tezla/ui/ModulationIds.hpp>
#include <tezla/ui/ModulationParameters.hpp>

#include "EmberdriveEngine.hpp"

namespace tezla::emberdrive
{

/// Parameter IDs. These are permanent: renaming one silently resets that
/// parameter in every project that already uses the plugin.
namespace ids
{
inline constexpr auto drive        = "drive";
inline constexpr auto character    = "character";
inline constexpr auto tone         = "tone";
inline constexpr auto ceiling      = "ceiling";
inline constexpr auto knee         = "knee";
inline constexpr auto speed        = "speed";
inline constexpr auto release      = "release";
inline constexpr auto autoRelease  = "autoRelease";
inline constexpr auto mix          = "mix";
inline constexpr auto output       = "output";
inline constexpr auto autoTrim     = "autoTrim";
inline constexpr auto oversampling = "oversampling";
inline constexpr auto bypass       = "bypass";

// Added in schema version 2. Appended, never inserted: VST3 hosts and FL's own
// automation index by ID, and reordering silently repoints every automation
// lane in every project that already uses the plugin.
inline constexpr auto foldAmount    = "foldAmount";
inline constexpr auto foldRange     = "foldRange";

inline constexpr auto multiband     = "multiband";
inline constexpr auto crossoverLow  = "crossoverLow";
inline constexpr auto crossoverHigh = "crossoverHigh";
inline constexpr auto bandLowDrive  = "bandLowDrive";
inline constexpr auto bandMidDrive  = "bandMidDrive";
inline constexpr auto bandHighDrive = "bandHighDrive";
inline constexpr auto bandLowState  = "bandLowState";
inline constexpr auto bandMidState  = "bandMidState";
inline constexpr auto bandHighState = "bandHighState";

inline constexpr auto expertEnabled = "expertEnabled";
inline constexpr auto expBias       = "expBias";
inline constexpr auto expHeadBumpHz = "expHeadBumpHz";
inline constexpr auto expHeadBumpDb = "expHeadBumpDb";
inline constexpr auto expGapLossHz  = "expGapLossHz";
inline constexpr auto expGapLossDb  = "expGapLossDb";
inline constexpr auto expHeadroom   = "expHeadroom";
inline constexpr auto expDcHz       = "expDcHz";
inline constexpr auto expStereoLink = "expStereoLink";
inline constexpr auto expDetectorRms = "expDetectorRms";
inline constexpr auto expAdaa       = "expAdaa";

// Added in schema version 3: the rest of the MANGLE page.
inline constexpr auto rectify       = "rectify";
inline constexpr auto crush         = "crush";
inline constexpr auto downsample    = "downsample";
inline constexpr auto feedback      = "feedback";
inline constexpr auto feedbackTime  = "feedbackTime";

// Added in schema version 4: modulation. Appended for the same reason, and
// every default is neutral -- every slot's source is Off, so nothing else here
// can reach the signal path in a project saved before they existed.
//
// References to the shared table rather than a second copy of it. Both plugins
// have to spell forty-five names identically, because the MOD strip and the
// assignment rings are shared components that look them up by string; a plugin
// that spelt one differently would get a control that silently did nothing.
inline constexpr auto envAttack      = ui::modIds::envAttack;
inline constexpr auto envRelease     = ui::modIds::envRelease;
inline constexpr auto envSensitivity = ui::modIds::envSensitivity;

inline constexpr auto& modSource      = ui::modIds::source;
inline constexpr auto& modDestination = ui::modIds::destination;
inline constexpr auto& modDepth       = ui::modIds::depth;

inline constexpr auto& lfoWave     = ui::modIds::lfoWave;
inline constexpr auto& lfoRate     = ui::modIds::lfoRate;
inline constexpr auto& lfoSync     = ui::modIds::lfoSync;
inline constexpr auto& lfoDivision = ui::modIds::lfoDivision;
inline constexpr auto& lfoPhase    = ui::modIds::lfoPhase;
inline constexpr auto& lfoSmooth   = ui::modIds::lfoSmooth;
} // namespace ids

/// Everything a modulation source can be pointed at.
///
/// **This list is append-only, forever, exactly like a parameter ID.** A
/// modulation slot stores its destination as an *index* into it, so inserting an
/// entry silently repoints every saved modulation in every project that uses the
/// plugin -- the same failure as renumbering a parameter, and easier to cause
/// because a list of names looks like an ordinary array. New destinations go on
/// the end. See CLAUDE.md section 8.
///
/// Only continuous controls appear. Choices and switches are excluded because
/// they reconfigure rather than adjust: oversampling changes the internal rate,
/// the fold range and the band states restructure the signal path, and Bypass
/// and the Expert/Multiband enables are modes rather than sound.
///
namespace dest
{
enum Index : int
{
    drive = 0,
    character,
    tone,
    mix,
    output,
    ceiling,
    knee,
    speed,
    release,
    foldAmount,
    rectify,
    crush,
    downsample,
    feedback,
    feedbackTime,
    crossoverLow,
    crossoverHigh,
    bandLowDrive,
    bandMidDrive,
    bandHighDrive,
    expBias,
    expHeadBumpHz,
    expHeadBumpDb,
    expGapLossHz,
    expGapLossDb,
    expHeadroom,
    expDcHz,
    expStereoLink,
    expDetectorRms,

    count   ///< always last
};

/// The parameter each destination drives, in the same order. Checked against
/// the enum at construction rather than trusted.
inline constexpr const char* parameterIds[] {
    ids::drive, ids::character, ids::tone, ids::mix, ids::output,
    ids::ceiling, ids::knee, ids::speed, ids::release,
    ids::foldAmount, ids::rectify, ids::crush, ids::downsample, ids::feedback,
    ids::feedbackTime, ids::crossoverLow, ids::crossoverHigh,
    ids::bandLowDrive, ids::bandMidDrive, ids::bandHighDrive,
    ids::expBias, ids::expHeadBumpHz, ids::expHeadBumpDb,
    ids::expGapLossHz, ids::expGapLossDb, ids::expHeadroom,
    ids::expDcHz, ids::expStereoLink, ids::expDetectorRms
};

static_assert (static_cast<int> (std::size (parameterIds)) == count,
               "every destination needs its parameter, and in the same order");

/// What a slot's target reads as in the host and on the panel. Short, because
/// it appears in a combo box that shares a row with three other controls.
inline constexpr const char* displayNames[] {
    "Drive", "Character", "Tone", "Mix", "Output",
    "Ceiling", "Knee", "Speed", "Release",
    "Fold", "Rectify", "Crush", "Downsample", "Feedback", "FB Time",
    "Low/Mid Hz", "Mid/High Hz", "Low Drive", "Mid Drive", "High Drive",
    "Bias", "Bump Hz", "Bump dB", "Gap Hz", "Gap dB", "Headroom",
    "DC Block", "St Link", "Detector"
};

static_assert (static_cast<int> (std::size (displayNames)) == count,
               "every destination needs a name, and in the same order");

static_assert (count <= dsp::Modulation::kMaxDestinations,
               "the matrix has a fixed destination array so that it never allocates");
} // namespace dest

class EmberdriveProcessor final : public juce::AudioProcessor
{
public:
    EmberdriveProcessor();
    ~EmberdriveProcessor() override = default;

    void prepareToPlay (double sampleRate, int maximumExpectedSamplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;

    void processBlock (juce::AudioBuffer<float>&,  juce::MidiBuffer&) override;
    void processBlock (juce::AudioBuffer<double>&, juce::MidiBuffer&) override;
    bool supportsDoublePrecisionProcessing() const override { return true; }

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override  { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override;
    int getCurrentProgram() override { return currentProgram_; }
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorParameter* getBypassParameter() const override { return bypassParameter_; }

    juce::AudioProcessorValueTreeState& getValueTreeState() noexcept { return state_; }

    /// Live values for the editor. Written on the audio thread, read on the
    /// message thread; approximate is fine for a meter, so plain relaxed atomics
    /// rather than a lock.
    struct MeterValues
    {
        std::atomic<float> inputVuDb    { -100.0f };
        std::atomic<float> inputPeakDb  { -100.0f };
        std::atomic<float> outputVuDb   { -100.0f };
        std::atomic<float> outputPeakDb { -100.0f };
        std::atomic<float> gainReductionDb { 0.0f };
        std::array<std::atomic<float>, kNumBands> bandGainReductionDb {};
    };

    MeterValues& getMeterValues() noexcept { return meters_; }

    [[nodiscard]] ui::AbCompare& getAbCompare() noexcept { return abCompare_; }

    /// Whether the panel shows its hover tooltips.
    ///
    /// Not a parameter -- it changes nothing about the sound, so it has no
    /// business in an automation lane or in a preset. It lives here rather than
    /// in the editor because the editor is destroyed every time the window is
    /// closed, and a setting that forgot itself on every close would be worse
    /// than not having the switch.
    [[nodiscard]] bool getTooltipsEnabled() const noexcept { return tooltipsEnabled_; }
    void setTooltipsEnabled (bool enabled) noexcept { tooltipsEnabled_ = enabled; }

    /// What Auto is doing right now, so the tooltip can say it in plain words
    /// rather than making the user work it out.
    [[nodiscard]] juce::String describeOversampling() const;

    /// The modulation sources and slots, shared with the editor so the strip can
    /// draw what they are doing.
    [[nodiscard]] dsp::Modulation& getModulation() noexcept { return modulation_; }

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    template <typename FloatType>
    void processInternal (juce::AudioBuffer<FloatType>& buffer);

    /// Runs the engine over one span, having pushed the parameters for it.
    void processSpan (int offset, int numSamples, int numChannels);

    /// Reads every modulatable parameter once per block: the raw value, and the
    /// normalised one modulation is added to.
    void readBaseParameters();

    /// Pushes the LFO and slot settings into the matrix. Once per block --
    /// these are controls, not audio.
    void updateModulationSettings();

    /// Turns the matrix's normalised offsets back into parameter values.
    /// A destination nothing points at keeps its base value *exactly*, which is
    /// what makes an unmodulated control bit-identical to one that could not be
    /// modulated at all.
    void applyModulation();

    void pullParameters();
    void updateLatency (int engineLatencySamples);

    juce::AudioProcessorValueTreeState state_;
    juce::AudioParameterBool* bypassParameter_ { nullptr };

    Engine engine_;
    Parameters parameters_;

    // Double-precision scratch: the DSP is double throughout, so a float host
    // buffer is converted here rather than compromising the processing.
    juce::AudioBuffer<double> scratch_;
    juce::AudioBuffer<double> dryScratch_;
    std::array<double*, Engine::kMaxChannels> channelPointers_ {};
    std::array<const double*, Engine::kMaxChannels> dryPointers_ {};

    // Bypass has to be latency-matched, or A/B comparison is a lie: the
    // bypassed signal would arrive earlier than the processed one and sound
    // tighter for reasons that have nothing to do with the plugin.
    //
    // This used to be a ring buffer written by hand here, and it delayed by
    // zero -- so switching bypass jumped the signal by the whole reported
    // latency and the crossfade swept a comb filter across it. It is shared and
    // tested now; see BypassMixer.hpp.
    dsp::BypassMixer bypassMixer_;
    int reportedLatency_ { 0 };

    ui::AbCompare abCompare_ { state_, { ids::bypass } };


    /// See getTooltipsEnabled. Defaults on: the tooltips are how this

    /// workshop documents itself, so a new user gets them.

    bool tooltipsEnabled_ { true };

    // ---- modulation ---------------------------------------------------------

    /// How much audio each modulation update covers.
    ///
    /// 32 samples is a ~1.5 kHz update rate at 48 kHz, far above anything an
    /// LFO does, and the engine's own smoothers round off whatever is left.
    /// Shorter costs a parameter push per span for nothing; longer makes a fast
    /// square arrive as a staircase.
    static constexpr int kModulationChunkSamples = 32;

    dsp::Modulation modulation_;

    /// The parameter behind each destination, resolved once at construction.
    std::array<juce::RangedAudioParameter*, dest::count> destinationParameters_ {};

    /// And its raw value, from the same place pullParameters() always read it.
    ///
    /// Both, deliberately. The base value has to be the *exact* float the plugin
    /// used before modulation existed, and recovering it as
    /// convertFrom0to1(getValue()) is a round trip through normalised space --
    /// which on a skewed range need not come back to the same bits. The
    /// normalised value is only ever used to add an offset to.
    std::array<std::atomic<float>*, dest::count> destinationRaw_ {};

    /// Per block: what each destination reads with nothing modulating it, and
    /// the same value normalised, which is the space offsets are added in.
    std::array<double, dest::count> baseValues_ {};
    std::array<double, dest::count> baseNormalised_ {};

    /// Per chunk: what the engine is actually given.
    std::array<double, dest::count> destinationValues_ {};

    dsp::VuMeter inputMeter_[Engine::kMaxChannels];
    dsp::VuMeter outputMeter_[Engine::kMaxChannels];
    MeterValues meters_;

    double sampleRate_ { 44100.0 };
    int currentProgram_ { 0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EmberdriveProcessor)
};

} // namespace tezla::emberdrive
