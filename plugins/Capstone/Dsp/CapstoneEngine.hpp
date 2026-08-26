#pragma once

// Capstone -- a true-peak brickwall limiter and clipper for the end of a chain.
//
// Framework-free: no JUCE, no VST3 headers. It takes double buffers and a
// sample rate, so the ceiling can be swept across the whole parameter space
// offline before any of it reaches a DAW.
//
// Signal flow, per block:
//
//   in --+------------------------------------------- dry (BypassMixer) --+
//        |                                                                |
//        +-- threshold (input drive, makeup is automatic) --+             |
//                                                           |             |
//              +-- driven, delayed ------------------------ | -- Listen --+
//              |                                            |             |
//              |   [ oversampled x1/x2/x4/x8 ]              |             v
//              |     CLIP  x + T*g_adaa(x/T)                |           out trim --> out
//              |   [ /oversampled ]                         |             ^
//              |                                            v             |
//              +-------------------------------- LIMIT (LimiterCore) -----+
//
// Two stages that live in the same slot and are different tools:
//
//   CLIP   cuts the waveform, which is the only thing that holds a ceiling
//          with no look-ahead at all. There is no zero-latency brickwall that
//          does not distort -- without future samples the gain cannot come
//          down before the peak, so the peak has to go. That is the definition
//          rather than a compromise, and on a drum bus it is the technique.
//
//   LIMIT  brings the gain down ahead of the peak, which costs latency and
//          buys transparency. Its ceiling is provable; see LimiterCore.hpp.
//
// Clipping first is deliberate. It shaves the transient tips so the limiter is
// not asked to duck the whole mix by 10 dB every time a kick lands, which is
// where a limiter starts to pump. Listen is how that stays inspectable: it
// solos what the two stages between them removed.

#include <array>
#include <vector>

#include <tezla/dsp/Adaa.hpp>
#include <tezla/dsp/BypassMixer.hpp>
#include <tezla/dsp/LimiterCore.hpp>
#include <tezla/dsp/Oversampler.hpp>
#include <tezla/dsp/SmoothedValue.hpp>
#include <tezla/dsp/Waveshapers.hpp>

namespace tezla::capstone
{

struct Parameters
{
    // ---- drive ---------------------------------------------------------------

    /// How hard the signal is pushed into the ceiling, in dB below unity.
    ///
    /// The L1 workflow: the makeup is automatic, so lowering this raises the
    /// level going in without raising the level coming out. 0 is no drive at
    /// all, and at 0 with both stages off the plugin is bit-exact.
    double thresholdDb { 0.0 };       ///< 0 .. -30

    /// The level the output is not allowed past. Above 0 dBFS is deliberate:
    /// nothing in a floating-point chain has to stop at full scale, and
    /// catching only the extremes is a real use.
    double ceilingDb { -0.3 };        ///< -24 .. +6

    // ---- clip ----------------------------------------------------------------

    bool clipOn { false };

    /// Where the clipper starts cutting, absolute rather than relative to the
    /// ceiling. Set it above Ceiling and the clipper takes the transient tips
    /// while the limiter handles the body; set it at or below Ceiling and the
    /// clipper does all of the work, which is what 0 ms limiting means.
    double clipThresholdDb { 0.0 };   ///< -12 .. +6

    /// 0 is a hard corner, 1 is a tanh. See dsp::SoftClip.
    double clipShape { 0.0 };         ///< 0 .. 1

    dsp::OversamplingMode clipOversampling { dsp::OversamplingMode::Auto };

    // ---- limit ---------------------------------------------------------------

    bool limitOn { true };

    /// Off pins the look-ahead at zero, which is the only way the reported
    /// latency reaches exactly zero. The limiter then behaves like a very fast
    /// clipper, which is why Clip exists alongside it.
    bool lookaheadOn { true };

    /// The look-ahead, which is also the attack and also the reported latency.
    double attackMs { 1.0 };          ///< 0 .. 20

    /// How long the gain stays down after a peak. Free: it widens the minimum
    /// window backwards, so it needs no extra look-ahead and costs no latency.
    double holdMs { 5.0 };            ///< 0 .. 100

    double releaseMs   { 100.0 };     ///< 1 .. 2000
    bool   autoRelease { false };

    /// How far below the ceiling the curve starts bending. 0 is a hard corner.
    double kneeDb { 0.0 };            ///< 0 .. 24

    /// 1 keeps the centre image still; 0 lets each channel follow its own
    /// peaks, which is wider and looser.
    double stereoLink { 1.0 };        ///< 0 .. 1

    /// Off / Standard / Strict. The interpolation ratio behind each follows the
    /// host rate -- see dsp::truePeakFactorFor -- so the *accuracy* is what the
    /// control picks, not the arithmetic.
    dsp::TruePeakMode truePeak { dsp::TruePeakMode::Standard };

    // ---- global --------------------------------------------------------------

    /// Applied after the ceiling, so it is the control that goes past 0 dBFS.
    double outputDb { 0.0 };          ///< -12 .. +12

    /// Solo what the clipper and the limiter removed.
    bool listen { false };

    bool bypass { false };

    [[nodiscard]] bool operator== (const Parameters&) const = default;
};

class Engine
{
public:
    static constexpr int kMaxChannels = dsp::LimiterCore::kMaxChannels;

    /// Allocates, for the worst case rather than for the current settings.
    /// Never call from the audio thread.
    void prepare (double sampleRate, int maxBlockSize, int numChannels);

    /// Clears every filter, envelope and delay line. No state may survive this.
    void reset();

    /// Cheap; safe to call once per block from the audio thread. Returns true
    /// if the reported latency changed, in which case the host must be told.
    bool setParameters (const Parameters& parameters);

    /// In-place. `channels` holds `numChannels` pointers to `numSamples` doubles.
    void process (double* const* channels, int numChannels, int numSamples) noexcept;

    [[nodiscard]] int getLatencySamples() const noexcept { return latency_; }

    [[nodiscard]] double getSampleRate() const noexcept { return sampleRate_; }

    /// What the clip oversampler is actually running at, which is what the
    /// tooltip has to say rather than what the control is set to.
    [[nodiscard]] int getClipOversamplingFactor() const noexcept
    {
        return parameters_.clipOn ? oversampler_.getFactor() : 1;
    }

    /// The interpolation ratio the true-peak detector is actually running, as
    /// opposed to the mode the control is set to. The tooltip reads this rather
    /// than the mode, because at 192 kHz Standard is not oversampling at all.
    [[nodiscard]] int getTruePeakFactor() const noexcept
    {
        return dsp::truePeakFactorFor (parameters_.truePeak, sampleRate_);
    }

    // ---- metering, read from the message thread, written once per block ------

    /// The most the limiter pulled down in the last block, in dB. Always <= 0.
    [[nodiscard]] double getLimiterReductionDb() const noexcept { return limiterReductionDb_; }

    /// The most the clipper cut in the last block, in dB. Always <= 0.
    ///
    /// Separate from the limiter's figure on purpose: the two stages sound
    /// completely different, and a single meter that added them together would
    /// hide which one was doing the work.
    [[nodiscard]] double getClipReductionDb() const noexcept { return clipReductionDb_; }

    /// How much the limiter's final clamp had to remove in the last block.
    ///
    /// Not a panel reading -- it is how the ceiling guarantee is tested. The
    /// clamp holds the ceiling whatever reaches it, so measuring the output
    /// peak says nothing about whether the chain in front of it is right.
    /// See dsp::LimiterCore::getClampExcess().
    [[nodiscard]] double getLimiterClampExcess() const noexcept { return limiterClampExcess_; }

private:
    /// The oversampling factor the clip control asks for at this host rate.
    [[nodiscard]] int clipFactorFor (const Parameters& parameters) const noexcept;

    /// Recomputes the reported latency and the delay the dry path needs to
    /// match it. Returns true if it moved.
    bool updateLatency();

    void processClip (double* const* channels, int active, int numSamples) noexcept;

    double sampleRate_   { 44100.0 };
    int    maxBlockSize_ { 512 };
    int    numChannels_  { 2 };
    int    latency_      { 0 };
    int    maxLatency_   { 1 };

    Parameters parameters_;

    // What prepare() actually configured, as opposed to what the parameters now
    // ask for. Comparing against this rather than a "have parameters been set
    // yet" flag is what makes the very first setParameters() call take effect --
    // prepare() necessarily runs before any parameters are known, so a flag
    // silently swallows the first one.
    int preparedClipFactor_ { 0 };

    dsp::Oversampler oversampler_;
    dsp::LimiterCore limiter_;
    dsp::BypassMixer bypass_;

    struct ChannelState
    {
        dsp::Adaa1<dsp::SoftClipExcess> clipAdaa;

        /// The driven signal, delayed to line up with the limiter's output so
        /// Listen can subtract one from the other.
        std::vector<double> drivenDelay;
        int drivenWrite { 0 };
    };

    std::array<ChannelState, kMaxChannels> channels_ {};

    dsp::SoftClipExcess clipShaper_;

    dsp::SmoothedValue<double> inputGain_;
    dsp::SmoothedValue<double> outputGain_;
    dsp::SmoothedValue<double> clipGain_;
    dsp::SmoothedValue<double> clipShape_;

    /// The raw input, kept for the bypass crossfade.
    std::array<std::vector<double>, kMaxChannels> dry_ {};
    std::array<double*, kMaxChannels> dryPointers_ {};

    double limiterReductionDb_  { 0.0 };
    double clipReductionDb_     { 0.0 };
    double limiterClampExcess_  { 0.0 };
};

} // namespace tezla::capstone
