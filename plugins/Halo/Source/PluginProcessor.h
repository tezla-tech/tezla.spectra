#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include <tezla/dsp/BypassMixer.hpp>
#include <tezla/dsp/SpectrumAnalyser.hpp>
#include <tezla/dsp/VuMeter.hpp>

#include <tezla/ui/AbCompare.hpp>

#include "HaloEngine.hpp"

namespace tezla::halo
{

/// Parameter IDs. These are permanent: renaming one silently resets that
/// parameter in every project that already uses the plugin.
namespace ids
{
inline constexpr auto bandMode     = "bandMode";
inline constexpr auto focus        = "focus";
inline constexpr auto drive        = "drive";
inline constexpr auto colour       = "colour";
inline constexpr auto track        = "track";
inline constexpr auto punch        = "punch";
inline constexpr auto floorOn      = "floorOn";
inline constexpr auto floorHz      = "floorHz";
inline constexpr auto ceilingOn    = "ceilingOn";
inline constexpr auto ceilingHz    = "ceilingHz";
inline constexpr auto amount       = "amount";
inline constexpr auto listen       = "listen";
inline constexpr auto autoTrim     = "autoTrim";
inline constexpr auto input        = "input";
inline constexpr auto output       = "output";
inline constexpr auto oversampling = "oversampling";
inline constexpr auto bypass       = "bypass";

// Added at schema version 2. Appended, never inserted: VST3 hosts and FL's own
// automation index by ID, and reordering silently repoints every automation lane
// in every project that already uses the plugin.
inline constexpr auto width        = "width";

// Added at schema version 3: Chebyshev precision mode. Appended for the same
// reason, and every one of them defaults to something that cannot reach a
// project saved before it existed -- `generator` defaults to Curve, so the rest
// are not consulted at all.
inline constexpr auto generator    = "generator";
inline constexpr auto chebIndex    = "chebIndex";
inline constexpr auto chebTilt     = "chebTilt";

/// Harmonics 2 through 8. Written out rather than generated, because a
/// parameter ID is forever and a loop that builds one from an index is one
/// refactor away from renumbering every project that uses it.
inline constexpr const char* harmonics[] {
    "harm2", "harm3", "harm4", "harm5", "harm6", "harm7", "harm8"
};
} // namespace ids

class HaloProcessor final : public juce::AudioProcessor
{
public:
    HaloProcessor();
    ~HaloProcessor() override = default;

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

        /// How much harmonic energy is being added, relative to the source.
        /// The number an exciter user actually wants and one that no level
        /// meter on the output can show them.
        std::atomic<float> harmonicsDb  { -60.0f };
    };

    MeterValues& getMeterValues() noexcept { return meters_; }

    /// What went in and what came out, for the spectrum display. The audio
    /// thread only writes to these; the editor reads them on its timer.
    [[nodiscard]] const dsp::SpectrumCapture& getInputCapture()  const noexcept { return inputCapture_; }
    [[nodiscard]] const dsp::SpectrumCapture& getOutputCapture() const noexcept { return outputCapture_; }

    [[nodiscard]] ui::AbCompare& getAbCompare() noexcept { return abCompare_; }

    /// What Auto is doing right now, so the tooltip can say it in plain words
    /// rather than making the user work it out.
    [[nodiscard]] juce::String describeOversampling() const;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    template <typename FloatType>
    void processInternal (juce::AudioBuffer<FloatType>& buffer);

    /// Real-time safe: sums to mono into a preallocated scratch and hands it to
    /// the capture, which is a copy and one atomic store.
    void captureMonoSum (const double* const* channels, int numChannels,
                         int numSamples, dsp::SpectrumCapture& capture) noexcept;

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

    // Mono sums, because the display draws one curve per side of the plugin
    // rather than one per channel. Two curves already say what the plugin did;
    // four would only say it twice.
    dsp::SpectrumCapture inputCapture_;
    dsp::SpectrumCapture outputCapture_;
    std::vector<double> captureScratch_;

    ui::AbCompare abCompare_ { state_, { ids::bypass } };

    dsp::VuMeter inputMeter_[Engine::kMaxChannels];
    dsp::VuMeter outputMeter_[Engine::kMaxChannels];
    MeterValues meters_;

    double sampleRate_ { 44100.0 };
    int currentProgram_ { 0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (HaloProcessor)
};

} // namespace tezla::halo
