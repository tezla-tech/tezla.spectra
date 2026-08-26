#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include <tezla/dsp/BypassMixer.hpp>
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
        std::array<std::atomic<float>, kNumBands> bandGainReductionDb {};
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

    dsp::VuMeter inputMeter_[Engine::kMaxChannels];
    dsp::VuMeter outputMeter_[Engine::kMaxChannels];
    MeterValues meters_;

    double sampleRate_ { 44100.0 };
    int currentProgram_ { 0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EmberdriveProcessor)
};

} // namespace tezla::emberdrive
