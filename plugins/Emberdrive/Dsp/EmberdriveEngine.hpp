#pragma once

// Emberdrive -- tube/tape saturation into a soft-knee limiter.
//
// Framework-free: no JUCE, no VST3 headers, nothing platform specific. It takes
// double buffers and a sample rate, which is what lets the whole thing be
// measured offline before it ever reaches a DAW.
//
// Signal flow, per block:
//
//   in --+------------------------------------------------ dry --+
//        |                                                       |
//        +-- [ oversampled x1/x2/x4/x8 ] ----------------------+  |
//              tone tilt + character voicing                   |  |
//              drive                                           |  |
//              saturation (ADAA, biased tanh)                  |  |
//              DC blocker                                      |  v
//              soft-knee limiter (stereo linked)               | mix --> trim --> out
//              mix against the oversampled dry ----------------+
//            [ /oversampled ]
//
// The dry/wet mix happens *inside* the oversampled section, against the
// upsampled input rather than the original. That costs nothing and buys exact
// alignment: the wet path's half-sample ADAA delay and the oversampler's own
// group delay apply to both sides, so partial mix settings cannot comb.

#include <vector>

#include <tezla/dsp/Adaa.hpp>
#include <tezla/dsp/Biquad.hpp>
#include <tezla/dsp/DcBlocker.hpp>
#include <tezla/dsp/EnvelopeFollower.hpp>
#include <tezla/dsp/GainComputer.hpp>
#include <tezla/dsp/Oversampler.hpp>
#include <tezla/dsp/SmoothedValue.hpp>
#include <tezla/dsp/Waveshapers.hpp>

namespace tezla::emberdrive
{

struct Parameters
{
    double driveDb    { 0.0 };    ///< 0 .. +30
    double character  { 0.35 };   ///< 0 = tape (odd harmonics), 1 = valve (even)
    double toneTilt   { 0.0 };    ///< -1 dark .. +1 bright
    double ceilingDb  { -0.3 };   ///< -24 .. 0 dBFS
    double kneeDb     { 6.0 };    ///< 0 .. 24, how far below the ceiling the knee starts
    double attackMs   { 5.0 };    ///< 0.05 .. 100
    double releaseMs  { 200.0 };  ///< 20 .. 2000
    bool   autoRelease { false };
    double mix        { 1.0 };    ///< 0 .. 1
    double outputDb   { 0.0 };    ///< -24 .. +24
    bool   autoTrim   { true };

    dsp::OversamplingMode oversampling { dsp::OversamplingMode::Auto };
};

class Engine
{
public:
    static constexpr int kMaxChannels = 2;

    /// Allocates. Never call from the audio thread.
    void prepare (double sampleRate, int maxBlockSize, int numChannels);

    /// Clears every filter, envelope and delay line. No state may survive this.
    void reset();

    /// Cheap; safe to call once per block from the audio thread. Returns true
    /// if the oversampling factor changed, in which case the host must be told
    /// about the new latency and prepare() has already been re-run internally.
    bool setParameters (const Parameters& parameters);

    /// In-place. `channels` holds `numChannels` pointers to `numSamples` doubles.
    void process (double* const* channels, int numChannels, int numSamples) noexcept;

    [[nodiscard]] int    getLatencySamples()   const noexcept { return oversampler_.getLatencySamples(); }
    [[nodiscard]] int    getOversamplingFactor() const noexcept { return oversampler_.getFactor(); }
    [[nodiscard]] double getOversampledRate()  const noexcept { return sampleRate_ * oversampler_.getFactor(); }
    [[nodiscard]] double getSampleRate()       const noexcept { return sampleRate_; }

    /// Metering, read from the message thread. Written once per block.
    [[nodiscard]] double getGainReductionDb() const noexcept { return gainReductionDb_; }

private:
    void updateDerivedParameters();
    void updateFilters();
    void updateAutoTrim();

    double sampleRate_   { 44100.0 };
    int    maxBlockSize_ { 512 };
    int    numChannels_  { 2 };

    Parameters parameters_;
    bool parametersInitialised_ { false };

    dsp::Oversampler oversampler_;

    // Per-channel state.
    struct Channel
    {
        dsp::Biquad<double> toneLow;
        dsp::Biquad<double> toneHigh;
        dsp::Biquad<double> characterLow;
        dsp::Biquad<double> characterHigh;
        dsp::Adaa1<dsp::BiasedTanh> saturator;
        dsp::DcBlocker<double> dcBlocker;
    };

    std::vector<Channel> channels_;
    std::vector<std::vector<double>> dry_;     ///< oversampled dry copy
    std::vector<double*> workPointers_;

    dsp::BiasedTanh     shaper_ { 0.0 };
    dsp::GainComputer   gainComputer_;
    dsp::EnvelopeFollower envelope_;

    // Smoothed, per-sample.
    dsp::SmoothedValue<double> driveGain_;
    dsp::SmoothedValue<double> mix_;
    dsp::SmoothedValue<double> outputGain_;

    // Smoothed, advanced once per block.
    dsp::SmoothedValue<double> bias_;
    dsp::SmoothedValue<double> tone_;

    double autoTrimGain_    { 1.0 };
    double gainReductionDb_ { 0.0 };
};

} // namespace tezla::emberdrive
