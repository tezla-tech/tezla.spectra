#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include <tezla/dsp/VuMeter.hpp>

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
} // namespace ids

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
    };

    MeterValues& getMeterValues() noexcept { return meters_; }

    /// What Auto is doing right now, so the tooltip can say it in plain words
    /// rather than making the user work it out.
    [[nodiscard]] juce::String describeOversampling() const;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    template <typename FloatType>
    void processInternal (juce::AudioBuffer<FloatType>& buffer);

    void pullParameters();
    void updateLatency (int engineLatencySamples);

    juce::AudioProcessorValueTreeState state_;
    juce::AudioParameterBool* bypassParameter_ { nullptr };

    Engine engine_;
    Parameters parameters_;

    // Double-precision scratch: the DSP is double throughout, so a float host
    // buffer is converted here rather than compromising the processing.
    juce::AudioBuffer<double> scratch_;
    std::array<double*, Engine::kMaxChannels> channelPointers_ {};

    // Bypass has to be latency-matched, or A/B comparison is a lie: the
    // bypassed signal would arrive earlier than the processed one and sound
    // tighter for reasons that have nothing to do with the plugin.
    juce::AudioBuffer<double> bypassDelay_;
    int bypassDelayWrite_ { 0 };
    int reportedLatency_  { 0 };

    // Crossfade in and out of bypass so the switch does not click.
    double bypassMix_ { 0.0 };

    dsp::VuMeter inputMeter_[Engine::kMaxChannels];
    dsp::VuMeter outputMeter_[Engine::kMaxChannels];
    MeterValues meters_;

    double sampleRate_ { 44100.0 };
    int currentProgram_ { 0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EmberdriveProcessor)
};

} // namespace tezla::emberdrive
