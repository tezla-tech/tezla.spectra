#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include <tezla/dsp/VuMeter.hpp>
#include <tezla/ui/AbCompare.hpp>

#include "CapstoneEngine.hpp"

namespace tezla::capstone
{

/// Parameter IDs. These are permanent: renaming one silently resets that
/// parameter in every project that already uses the plugin. See CLAUDE.md §8.
namespace ids
{
inline constexpr auto threshold        = "threshold";
inline constexpr auto ceiling          = "ceiling";

inline constexpr auto clipOn           = "clipOn";
inline constexpr auto clipThreshold    = "clipThreshold";
inline constexpr auto clipShape        = "clipShape";
inline constexpr auto clipOversampling = "clipOversampling";

inline constexpr auto limitOn          = "limitOn";
inline constexpr auto lookaheadOn      = "lookaheadOn";
inline constexpr auto attack           = "attack";
inline constexpr auto hold             = "hold";
inline constexpr auto release          = "release";
inline constexpr auto autoRelease      = "autoRelease";
inline constexpr auto knee             = "knee";
inline constexpr auto stereoLink       = "stereoLink";

inline constexpr auto truePeak         = "truePeak";

inline constexpr auto output           = "output";
inline constexpr auto listen           = "listen";
inline constexpr auto bypass           = "bypass";
} // namespace ids

/// The option lists behind the two choice parameters.
///
/// **Append-only, forever.** A choice parameter stores an *index*, not a name,
/// so inserting or reordering an entry silently repoints every saved use of it.
/// Both of these also have to keep matching the enum they are read back as --
/// which is checked at compile time below rather than by care.
namespace choices
{
inline const juce::StringArray clipOversampling { "Auto", "Off", "x2", "x4", "x8" };
inline const juce::StringArray truePeak { "Off", "Standard", "Strict" };

static_assert (static_cast<int> (dsp::OversamplingMode::Auto) == 0
            && static_cast<int> (dsp::OversamplingMode::Off)  == 1
            && static_cast<int> (dsp::OversamplingMode::X8)   == 4,
               "the oversampling option list is indexed straight into OversamplingMode");

static_assert (static_cast<int> (dsp::TruePeakMode::Off)      == 0
            && static_cast<int> (dsp::TruePeakMode::Standard) == 1
            && static_cast<int> (dsp::TruePeakMode::Strict)   == 2,
               "the true-peak option list is indexed straight into TruePeakMode");
} // namespace choices

class CapstoneProcessor final : public juce::AudioProcessor
{
public:
    CapstoneProcessor();
    ~CapstoneProcessor() override = default;

    void prepareToPlay (double sampleRate, int maximumExpectedSamplesPerBlock) override;
    void releaseResources() override {}

    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;

    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
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

    [[nodiscard]] juce::AudioProcessorValueTreeState& getState() noexcept { return state_; }

    /// Meter values, written by the audio thread and read by the editor.
    /// Atomics rather than a lock, because the audio thread may not block.
    struct MeterValues
    {
        std::atomic<float> inputVuDb    { -100.0f };
        std::atomic<float> inputPeakDb  { -100.0f };
        std::atomic<float> outputVuDb   { -100.0f };
        std::atomic<float> outputPeakDb { -100.0f };

        /// Both stages, separately. A single meter that added them would hide
        /// which one was doing the work, and they sound nothing alike.
        std::atomic<float> limiterReductionDb { 0.0f };
        std::atomic<float> clipReductionDb    { 0.0f };
    };

    [[nodiscard]] MeterValues& getMeterValues() noexcept { return meters_; }

    [[nodiscard]] ui::AbCompare& getAbCompare() noexcept { return abCompare_; }

    /// What the true-peak control is costing and buying, in plain words, at the
    /// host's actual rate. The tooltip reads this rather than making the user
    /// work it out.
    [[nodiscard]] juce::String describeTruePeak() const;

    /// The same for the clipper's oversampling.
    [[nodiscard]] juce::String describeClipOversampling() const;

    /// Whether prepareToPlay has run, so the panel can tell "no latency" apart
    /// from "no figure yet".
    [[nodiscard]] bool isPrepared() const noexcept { return prepared_; }

    /// The reported latency in words, since it is the thing a user is most
    /// likely to be surprised by on a master bus.
    [[nodiscard]] juce::String describeLatency() const;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    template <typename FloatType>
    void processInternal (juce::AudioBuffer<FloatType>& buffer);

    void pullParameters();

    juce::AudioProcessorValueTreeState state_;
    juce::AudioParameterBool* bypassParameter_ { nullptr };

    Engine engine_;
    Parameters parameters_;

    /// Double-precision scratch: the DSP is double throughout, so a float host
    /// buffer is converted here rather than compromising the processing.
    juce::AudioBuffer<double> scratch_;
    std::array<double*, Engine::kMaxChannels> channelPointers_ {};

    int reportedLatency_ { 0 };

    /// Whether prepareToPlay has run. Not the same as "the host reports a
    /// sample rate": a standalone build with no audio device reports one and
    /// never prepares anything, and the panel then read a latency of zero as
    /// though it were a setting rather than an absence.
    bool prepared_ { false };

    ui::AbCompare abCompare_ { state_, { ids::bypass } };

    dsp::VuMeter inputMeter_[Engine::kMaxChannels];
    dsp::VuMeter outputMeter_[Engine::kMaxChannels];
    MeterValues meters_;

    double sampleRate_ { 44100.0 };
    int currentProgram_ { 0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CapstoneProcessor)
};

} // namespace tezla::capstone
