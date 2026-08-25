#pragma once

// Emberdrive -- tube/tape saturation into a soft-knee limiter, with a
// wavefolder for destruction and an optional three-band split.
//
// Framework-free: no JUCE, no VST3 headers, nothing platform specific. It takes
// double buffers and a sample rate, which is what lets the whole thing be
// measured offline before it ever reaches a DAW.
//
// Signal flow, per block:
//
//   in --+------------------------------------------------------- dry --+
//        |                                                              |
//        +-- [ oversampled x1/x2/x4/x8 ] -----------------------------+  |
//              tone tilt + character voicing                          |  |
//              three-band split (multiband mode only)                 |  |
//              per band:  drive trim                                  |  |
//                         fold      (ADAA sine folder)                |  |
//                         saturate  (ADAA biased tanh)                |  |
//                         DC block                                    |  |
//                         soft-knee limiter                           |  v
//              sum bands -> master limiter (multiband only)           | mix --> trim --> out
//              mix against the oversampled dry ----------------------+
//            [ /oversampled ]
//
// The dry/wet mix happens *inside* the oversampled section, against the
// upsampled input rather than the original. That costs nothing and buys exact
// alignment: the wet path's half-sample ADAA delay and the oversampler's own
// group delay apply to both sides, so partial mix settings cannot comb.

#include <array>
#include <vector>

#include <tezla/dsp/Adaa.hpp>
#include <tezla/dsp/Biquad.hpp>
#include <tezla/dsp/Bitcrusher.hpp>
#include <tezla/dsp/Crossover.hpp>
#include <tezla/dsp/DcBlocker.hpp>
#include <tezla/dsp/EnvelopeFollower.hpp>
#include <tezla/dsp/GainComputer.hpp>
#include <tezla/dsp/HalfbandFir.hpp>
#include <tezla/dsp/Oversampler.hpp>
#include <tezla/dsp/SmoothedValue.hpp>
#include <tezla/dsp/Waveshapers.hpp>

namespace tezla::emberdrive
{

inline constexpr int kNumBands = 3;

enum class BandState
{
    On = 0,
    Mute,
    Solo
};

struct BandParameters
{
    double    driveTrimDb { 0.0 };          ///< -24 .. +24, relative to the global drive
    BandState state       { BandState::On };
};

/// Direct access to the constants the analogue modelling normally derives from
/// the Character control. Off by default: when disabled, Character drives all
/// of these exactly as it did before they existed.
struct ExpertParameters
{
    bool   enabled        { false };
    double bias           { 0.0 };      ///< raw asymmetry, -2 .. +2
    double headBumpHz     { 90.0 };     ///< tape low-frequency resonance
    double headBumpDb     { 1.5 };
    double gapLossHz      { 8000.0 };   ///< tape high-frequency loss
    double gapLossDb      { -2.5 };
    double shaperHeadroom { 4.0 };      ///< 1 .. 16; how soon the curve bends
    double dcBlockerHz    { 10.0 };     ///< 1 .. 40
    double stereoLink     { 1.0 };      ///< 0 = independent channels, 1 = fully linked
    double detectorRms    { 0.0 };      ///< 0 = peak detector, 1 = RMS
    bool   adaaEnabled    { true };     ///< off to hear what antialiasing is doing
};

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

    // ---- mangle -----------------------------------------------------------
    double foldAmount { 0.0 };    ///< 0 .. 1
    double foldRange  { 1.0 };    ///< 1, 10 or 100 -- the multiplier on fold gain
    double rectify    { 0.0 };    ///< 0 .. 1, blend toward full-wave rectification
    double feedback   { 0.0 };    ///< 0 .. 0.95, output fed back into the drive stage
    double feedbackMs { 8.0 };    ///< 0.1 .. 50, the loop's delay

    /// These two run at the host's rate, after the oversampled block, because
    /// with a bit crusher the aliasing is the instrument rather than a defect.
    double crush      { 0.0 };    ///< 0 .. 1, 0 is bypassed exactly
    double downsample { 1.0 };    ///< 1 .. 64, 1 is bypassed exactly

    // ---- multiband --------------------------------------------------------
    bool   multiband       { false };
    double crossoverLowHz  { 120.0 };
    double crossoverHighHz { 2500.0 };
    std::array<BandParameters, kNumBands> bands {};

    ExpertParameters expert {};
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

    [[nodiscard]] int    getLatencySamples()     const noexcept { return oversampler_.getLatencySamples(); }
    [[nodiscard]] int    getOversamplingFactor() const noexcept { return oversampler_.getFactor(); }
    [[nodiscard]] double getOversampledRate()    const noexcept { return sampleRate_ * oversampler_.getFactor(); }
    [[nodiscard]] double getSampleRate()         const noexcept { return sampleRate_; }

    /// Metering, read from the message thread. Written once per block.
    [[nodiscard]] double getGainReductionDb() const noexcept { return gainReductionDb_; }
    [[nodiscard]] double getBandGainReductionDb (int band) const noexcept
    {
        return band >= 0 && band < kNumBands ? bandGainReductionDb_[static_cast<std::size_t> (band)] : 0.0;
    }

private:
    void updateDerivedParameters();
    void updateFilters();
    void updateAutoTrim();

    /// RMS gain of the fold-then-saturate chain at a given drive, used to undo
    /// what drive does to the level.
    [[nodiscard]] double measureStageGain (double driveGain) const noexcept;

    [[nodiscard]] double biasForCharacter() const noexcept;
    [[nodiscard]] double headroom() const noexcept;

    double sampleRate_   { 44100.0 };
    int    maxBlockSize_ { 512 };
    int    numChannels_  { 2 };

    Parameters parameters_;

    // What prepare() actually configured, as opposed to what the parameters now
    // ask for. Comparing against these rather than against a "have parameters
    // been set yet" flag is what makes the very first setParameters() call take
    // effect -- prepare() necessarily runs before any parameters are known, so
    // a flag-based check silently ignored the first one.
    int    preparedFactor_ { 0 };
    double preparedDcHz_   { 0.0 };

    dsp::Oversampler oversampler_;

    /// Everything that has to exist once per band, per channel.
    struct BandChannelState
    {
        dsp::Adaa1<dsp::Rectifier>   rectifier;
        dsp::Adaa1<dsp::SineFolder>  folder;
        dsp::Adaa1<dsp::BiasedTanh>  saturator;
        dsp::DcBlocker<double>       dcBlocker;
        dsp::DelayLine               feedbackDelay;
        double                       detectorMeanSquare { 0.0 };
    };

    struct ChannelState
    {
        dsp::Biquad<double> toneLow, toneHigh, characterLow, characterHigh;
        dsp::ThreeBandSplitter<double> splitter;
        std::array<BandChannelState, kNumBands> bands {};
        double masterDetectorMeanSquare { 0.0 };

        // Base-rate stages, after the oversampled block.
        dsp::Downsampler downsampler;
        dsp::Bitcrusher  bitcrusher;

        // The dry path is delayed here rather than mixed inside the oversampled
        // block, because crush and downsample have to be wet-only and they run
        // outside it.
        dsp::DelayLine dryDelay;
    };

    std::vector<ChannelState> channels_;
    std::vector<std::vector<double>> dryInput_;   ///< base-rate copy of the input
    std::vector<double*> workPointers_;

    dsp::Rectifier   rectifier_;
    dsp::SineFolder  folder_;
    dsp::BiasedTanh  shaper_ { 0.0 };

    // The static curve is stateless, so one is shared; the envelopes are not,
    // so there is one per band per channel -- which is also what makes a
    // partial stereo link possible.
    dsp::GainComputer gainComputer_;
    std::array<std::array<dsp::EnvelopeFollower, kMaxChannels>, kNumBands> bandEnvelopes_ {};
    std::array<dsp::EnvelopeFollower, kMaxChannels> masterEnvelopes_ {};
    dsp::GainComputer masterGainComputer_;

    // Smoothed, per-sample.
    dsp::SmoothedValue<double> driveGain_;
    dsp::SmoothedValue<double> mix_;
    dsp::SmoothedValue<double> outputGain_;
    dsp::SmoothedValue<double> foldGain_;
    dsp::SmoothedValue<double> rectifyAmount_;
    dsp::SmoothedValue<double> feedbackAmount_;

    // Smoothed, advanced once per block.
    dsp::SmoothedValue<double> bias_;
    dsp::SmoothedValue<double> tone_;

    std::array<double, kNumBands> bandTrimGain_ {};
    std::array<double, kNumBands> bandAutoTrim_ {};
    std::array<bool,   kNumBands> bandAudible_  {};

    int    feedbackDelaySamples_ { 0 };   ///< at the oversampled rate
    double detectorCoefficient_  { 0.0 };
    double gainReductionDb_     { 0.0 };
    std::array<double, kNumBands> bandGainReductionDb_ {};
};

} // namespace tezla::emberdrive
