#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include <tezla/dsp/ReferenceCurve.hpp>
#include <tezla/dsp/SpectrumAnalyser.hpp>
#include <tezla/ui/AbCompare.hpp>

#include "TranspectusEngine.hpp"

namespace tezla::transpectus
{

/// Parameter IDs. Permanent: renaming one silently resets that parameter in
/// every project that already uses the plugin. See CLAUDE.md §8.
namespace ids
{
inline constexpr auto target      = "target";
inline constexpr auto truePeak    = "truePeak";
inline constexpr auto monoCheckHz = "monoCheckHz";
inline constexpr auto bypass      = "bypass";
} // namespace ids

namespace choices
{
/// **Append-only, forever.** A choice parameter stores an index, so inserting
/// or reordering an entry silently repoints every saved use of it -- and this
/// one is generated from the engine's table, so the two cannot drift apart.
[[nodiscard]] juce::StringArray targetNames();

inline const juce::StringArray truePeak { "Off", "Standard", "Strict" };
} // namespace choices

class TranspectusProcessor final : public juce::AudioProcessor
{
public:
    /// How many display bins the spectrum is folded onto. The editor's analyser
    /// and the reference curve have to agree on this, so it lives in one place.
    static constexpr int kSpectrumBins = Engine::kNumBins;

    TranspectusProcessor();
    ~TranspectusProcessor() override = default;

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

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return "Default"; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    [[nodiscard]] juce::AudioProcessorValueTreeState& getState() noexcept { return state_; }
    [[nodiscard]] Engine& getEngine() noexcept { return engine_; }
    [[nodiscard]] ui::AbCompare& getAbCompare() noexcept { return abCompare_; }

    /// The captured reference, shared with the editor. Lives here rather than in
    /// the editor so it survives the window being closed and is saved with the
    /// project.
    [[nodiscard]] dsp::ReferenceCurve& getReferenceCurve() noexcept { return reference_; }

    /// Clears the integration, the true-peak hold and the peak-hold bars.
    void resetMeasurement() noexcept { engine_.resetMeasurement(); }

    /// Whether prepareToPlay has run, so the panel can tell "nothing yet" from
    /// "nothing there".
    [[nodiscard]] bool isPrepared() const noexcept { return prepared_; }

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    template <typename FloatType>
    void processInternal (juce::AudioBuffer<FloatType>& buffer);

    void pullParameters();

    juce::AudioProcessorValueTreeState state_;

    Engine engine_;
    Parameters parameters_;

    /// Double-precision scratch: the measurement runs in double, so a float
    /// host buffer is converted rather than measured at lower precision.
    juce::AudioBuffer<double> scratch_;
    std::array<const double*, Engine::kMaxChannels> channelPointers_ {};

    dsp::ReferenceCurve reference_;

    ui::AbCompare abCompare_ { state_, { ids::bypass } };

    bool prepared_ { false };
    double sampleRate_ { 48000.0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TranspectusProcessor)
};

} // namespace tezla::transpectus
