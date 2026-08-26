#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include <tezla/dsp/BypassMixer.hpp>
#include <tezla/dsp/Modulation.hpp>
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

/// Everything a modulation source can be pointed at.
///
/// **This list is append-only, forever, exactly like a parameter ID.** A
/// modulation slot stores its destination as an *index* into it, so inserting
/// an entry silently repoints every saved modulation in every project that uses
/// the plugin -- the same failure as renumbering a parameter, and easier to
/// cause because a list of names looks like an ordinary array. New destinations
/// go on the end. See CLAUDE.md section 8.
///
/// Only continuous controls appear. Choices and switches are excluded because
/// they reconfigure rather than adjust: oversampling changes the internal rate,
/// the generator runs a crossfade, the band mode resets filters, and Listen and
/// Bypass are monitoring rather than sound.
namespace dest
{
enum Index : int
{
    focus = 0,
    drive,
    colour,
    track,
    punch,
    width,
    amount,
    floorHz,
    ceilingHz,
    input,
    output,
    harm2, harm3, harm4, harm5, harm6, harm7, harm8,
    chebIndex,
    chebTilt,

    count   ///< always last
};

/// The parameter each destination drives, in the same order. Checked against
/// the enum at construction rather than trusted.
inline constexpr const char* parameterIds[] {
    ids::focus, ids::drive, ids::colour, ids::track, ids::punch, ids::width,
    ids::amount, ids::floorHz, ids::ceilingHz, ids::input, ids::output,
    ids::harmonics[0], ids::harmonics[1], ids::harmonics[2], ids::harmonics[3],
    ids::harmonics[4], ids::harmonics[5], ids::harmonics[6],
    ids::chebIndex, ids::chebTilt
};

static_assert (static_cast<int> (std::size (parameterIds)) == count,
               "every destination needs its parameter, and in the same order");
} // namespace dest

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

    /// The modulation sources and slots, shared with the editor so the strip can
    /// draw what they are doing.
    [[nodiscard]] dsp::Modulation& getModulation() noexcept { return modulation_; }

    /// What Auto is doing right now, so the tooltip can say it in plain words
    /// rather than making the user work it out.
    [[nodiscard]] juce::String describeOversampling() const;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    template <typename FloatType>
    void processInternal (juce::AudioBuffer<FloatType>& buffer);

    /// Runs the engine over one span, having pushed the parameters for it.
    void processSpan (int offset, int numSamples, int numChannels);

    /// Reads every parameter once per block: the raw value, and the normalised
    /// one modulation is added to.
    void readBaseParameters();

    /// Pushes the LFO and slot settings into the matrix. Once per block --
    /// these are controls, not audio.
    void updateModulationSettings();

    /// Turns the matrix's normalised offsets back into parameter values.
    /// A destination nothing points at keeps its base value *exactly*, which is
    /// what makes an unmodulated control bit-identical to one that could not be
    /// modulated at all.
    void applyModulation();

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

    // ---- modulation ---------------------------------------------------------

    /// How much audio each modulation update covers.
    ///
    /// 32 samples is a ~1.5 kHz update rate at 48 kHz, far above anything an
    /// LFO does, and the engine's own 20 ms smoothers round off whatever is
    /// left. Shorter costs a parameter push per span for nothing; longer makes
    /// a fast square arrive as a staircase.
    static constexpr int kModulationChunkSamples = 32;

    dsp::Modulation modulation_;

    /// The parameter behind each destination, resolved once at construction.
    std::array<juce::RangedAudioParameter*, dest::count> destinationParameters_ {};

    /// And its raw value, from the same place pullParameters() always read it.
    ///
    /// Both, deliberately. The base value has to be the *exact* float the
    /// plugin used before modulation existed, and recovering it as
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

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (HaloProcessor)
};

} // namespace tezla::halo
