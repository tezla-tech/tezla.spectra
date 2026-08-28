#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include <tezla/dsp/VuMeter.hpp>
#include <tezla/ui/AbCompare.hpp>

#include "AnvilEngine.hpp"

namespace tezla::anvil
{

/// Parameter IDs. These are permanent: renaming one silently resets that
/// parameter in every project that already uses the plugin. See CLAUDE.md §8.
namespace ids
{
inline constexpr auto voicing      = "voicing";
inline constexpr auto gain         = "gain";
inline constexpr auto stages       = "stages";

inline constexpr auto bass         = "bass";
inline constexpr auto middle       = "middle";
inline constexpr auto treble       = "treble";

inline constexpr auto master       = "master";
inline constexpr auto presence     = "presence";
inline constexpr auto resonance    = "resonance";
inline constexpr auto sag          = "sag";
inline constexpr auto core         = "core";

inline constexpr auto cabinet      = "cabinet";
inline constexpr auto micPosition  = "micPosition";
inline constexpr auto micDistance  = "micDistance";
inline constexpr auto damping      = "damping";

inline constexpr auto output       = "output";
inline constexpr auto mix          = "mix";
inline constexpr auto oversampling = "oversampling";
inline constexpr auto bypass       = "bypass";
} // namespace ids

/// The option lists behind the three choice parameters.
///
/// **Append-only, forever.** A choice parameter stores an *index*, not a name,
/// so inserting or reordering an entry silently repoints every saved use of it
/// -- the plugin still loads, still runs, and quietly plays through a different
/// amplifier. CLAUDE.md §8.
///
/// Each is checked against the enum it is read back as at compile time, rather
/// than by care.
namespace choices
{
inline const juce::StringArray voicing { "Clean", "Vintage", "Modern" };
inline const juce::StringArray cabinet { "None", "Combo 1x12", "British 4x12", "Vintage 4x12" };
inline const juce::StringArray oversampling { "Auto", "Off", "x2", "x4", "x8" };

static_assert (static_cast<int> (Voicing::clean)   == 0
            && static_cast<int> (Voicing::vintage) == 1
            && static_cast<int> (Voicing::modern)  == 2
            && static_cast<int> (Voicing::count)   == 3,
               "the voicing option list is indexed straight into Voicing");

static_assert (static_cast<int> (CabinetChoice::none)    == 0
            && static_cast<int> (CabinetChoice::combo)   == 1
            && static_cast<int> (CabinetChoice::british) == 2
            && static_cast<int> (CabinetChoice::vintage) == 3
            && static_cast<int> (CabinetChoice::count)   == 4,
               "the cabinet option list is indexed straight into CabinetChoice");

static_assert (static_cast<int> (dsp::OversamplingMode::Auto) == 0
            && static_cast<int> (dsp::OversamplingMode::Off)  == 1
            && static_cast<int> (dsp::OversamplingMode::X8)   == 4,
               "the oversampling option list is indexed straight into OversamplingMode");
} // namespace choices

class AnvilProcessor final : public juce::AudioProcessor
{
public:
    AnvilProcessor();
    ~AnvilProcessor() override = default;

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

        /// How far the rail has fallen, 0 to 1. This is the one that tells you
        /// whether the amplifier is being worked or merely being loud.
        std::atomic<float> sag { 0.0f };

        /// Peak core flux, in units of the transformer's capacity. Above 1 the
        /// core is saturating and the low end is going with it.
        std::atomic<float> flux { 0.0f };

        /// How far the first valve's operating point has drifted, in knees.
        std::atomic<float> bias { 0.0f };
    };

    [[nodiscard]] MeterValues& getMeterValues() noexcept { return meters_; }

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

    /// What the oversampling control is costing and buying, in plain words, at
    /// the host's actual rate. The tooltip reads this rather than making the
    /// user work it out. CLAUDE.md §6.
    [[nodiscard]] juce::String describeOversampling() const;

    /// What the current voicing builds: how many valves, and where the stack
    /// sits among them.
    [[nodiscard]] juce::String describeVoicing() const;

    /// The transformer's core, in the terms the control is in.
    [[nodiscard]] juce::String describeCore() const;

    /// Whether prepareToPlay has run, so the panel can tell "no latency" apart
    /// from "no figure yet".
    [[nodiscard]] bool isPrepared() const noexcept { return prepared_; }

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
    /// never prepares anything.
    bool prepared_ { false };

    ui::AbCompare abCompare_ { state_, { ids::bypass } };


    /// See getTooltipsEnabled. Defaults on: the tooltips are how this

    /// workshop documents itself, so a new user gets them.

    bool tooltipsEnabled_ { true };

    dsp::VuMeter inputMeter_[Engine::kMaxChannels];
    dsp::VuMeter outputMeter_[Engine::kMaxChannels];
    MeterValues meters_;

    double sampleRate_ { 44100.0 };
    int currentProgram_ { 0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AnvilProcessor)
};

} // namespace tezla::anvil
