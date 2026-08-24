#include "EmberdriveEngine.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>

#include <tezla/dsp/Decibels.hpp>

namespace tezla::emberdrive
{

namespace
{
// Character voicing. Tape and valve differ in more than the shape of the
// transfer curve -- the frequency response around the nonlinearity is most of
// what makes one sound like tape and the other like a valve stage.
//
// Tape end: a low-frequency head bump and the high-frequency loss that comes
// from head gap and self-erasure. Valve end: flatter, slightly brighter, and
// asymmetric so it makes even harmonics.
constexpr double kMaxBias          = 0.8;    // asymmetry at the valve end
constexpr double kHeadBumpHz       = 90.0;
constexpr double kHeadBumpDb       = 1.5;
constexpr double kGapLossHz        = 8000.0;
constexpr double kGapLossDb        = -2.5;

// Tone is a tilt, not a pair of independent shelves: one control, opposite
// directions, so it never changes the overall level much.
constexpr double kToneLowHz        = 250.0;
constexpr double kToneHighHz       = 2500.0;
constexpr double kToneRangeDb      = 5.0;
constexpr double kToneQ            = 0.7;

// Headroom into the shaper. Without it, drive at 0 dB still runs a -20 dBFS
// signal at 0.1 into a tanh, which is 0.09% THD -- respectable for analogue
// gear but not the "genuinely transparent, not a quieter version of the dirty
// setting" that CLAUDE.md asks for as priority two. Backing the shaper off by
// four moves drive 0 to -85 dB THD while still leaving the top of the drive
// range thoroughly destroyed.
constexpr double kShaperHeadroom   = 4.0;

constexpr double kDcBlockerHz      = 10.0;   // low enough not to thin a sub
constexpr double kSmoothingSeconds = 0.02;

/// Reference level the auto-trim matches at: a typical mix level rather than
/// full scale, so the compensation is right where material actually sits.
constexpr double kAutoTrimReferenceDb = -12.0;
} // namespace

void Engine::prepare (double sampleRate, int maxBlockSize, int numChannels)
{
    sampleRate_   = sampleRate > 0.0 ? sampleRate : 44100.0;
    maxBlockSize_ = std::max (maxBlockSize, 1);
    numChannels_  = std::clamp (numChannels, 1, kMaxChannels);

    const int factor = dsp::oversamplingFactor (parameters_.oversampling, sampleRate_);
    oversampler_.prepare (maxBlockSize_, numChannels_, factor);

    const double oversampledRate = getOversampledRate();

    channels_.assign (static_cast<std::size_t> (numChannels_), Channel {});
    dry_.assign (static_cast<std::size_t> (numChannels_),
                 std::vector<double> (static_cast<std::size_t> (maxBlockSize_ * factor), 0.0));
    workPointers_.assign (static_cast<std::size_t> (numChannels_), nullptr);

    for (auto& channel : channels_)
        channel.dcBlocker.prepare (oversampledRate, kDcBlockerHz);

    // Everything time-based is set from the actual running rate, so the plugin
    // behaves identically at 44.1 and 192 kHz.
    envelope_.prepare (oversampledRate);

    driveGain_.prepare (oversampledRate, kSmoothingSeconds);
    mix_      .prepare (oversampledRate, kSmoothingSeconds);
    outputGain_.prepare (sampleRate_,    kSmoothingSeconds);
    bias_     .prepare (oversampledRate, kSmoothingSeconds);
    tone_     .prepare (oversampledRate, kSmoothingSeconds);

    updateDerivedParameters();

    driveGain_.setCurrentAndTarget (driveGain_.getTarget());
    mix_      .setCurrentAndTarget (mix_.getTarget());
    outputGain_.setCurrentAndTarget (outputGain_.getTarget());
    bias_     .setCurrentAndTarget (bias_.getTarget());
    tone_     .setCurrentAndTarget (tone_.getTarget());

    updateFilters();
    reset();
}

void Engine::reset()
{
    oversampler_.reset();
    envelope_.reset();

    for (auto& channel : channels_)
    {
        channel.toneLow.reset();
        channel.toneHigh.reset();
        channel.characterLow.reset();
        channel.characterHigh.reset();
        channel.saturator.reset();
        channel.dcBlocker.reset();
    }

    for (auto& buffer : dry_)
        std::fill (buffer.begin(), buffer.end(), 0.0);

    // Smoothed values jump to where they are heading. Without this, reset()
    // leaves the ramps part-way through, two runs of the same material do not
    // match, and the first 20 ms after a transport restart is not what the
    // settings say it should be.
    driveGain_ .setCurrentAndTarget (driveGain_ .getTarget());
    mix_       .setCurrentAndTarget (mix_       .getTarget());
    outputGain_.setCurrentAndTarget (outputGain_.getTarget());
    bias_      .setCurrentAndTarget (bias_      .getTarget());
    tone_      .setCurrentAndTarget (tone_      .getTarget());

    updateFilters();

    gainReductionDb_ = 0.0;
}

bool Engine::setParameters (const Parameters& parameters)
{
    const int previousFactor = oversampler_.getFactor();
    const int requestedFactor = dsp::oversamplingFactor (parameters.oversampling, sampleRate_);

    const bool factorChanged = parametersInitialised_ && requestedFactor != previousFactor;

    parameters_ = parameters;
    parametersInitialised_ = true;

    if (factorChanged)
    {
        // The oversampling factor changes the internal rate, which changes
        // every time constant and every filter coefficient -- and the latency,
        // which the host has to be told about. Rebuilding is the honest way to
        // handle it; the caller reports the new latency.
        prepare (sampleRate_, maxBlockSize_, numChannels_);
        return true;
    }

    updateDerivedParameters();
    return false;
}

void Engine::updateDerivedParameters()
{
    driveGain_.setTarget (dsp::dbToGain (parameters_.driveDb));
    mix_.setTarget (std::clamp (parameters_.mix, 0.0, 1.0));
    outputGain_.setTarget (dsp::dbToGain (parameters_.outputDb));
    bias_.setTarget (std::clamp (parameters_.character, 0.0, 1.0) * kMaxBias);
    tone_.setTarget (std::clamp (parameters_.toneTilt, -1.0, 1.0));

    gainComputer_.setCeilingDb (parameters_.ceilingDb);
    gainComputer_.setKneeDb (parameters_.kneeDb);

    envelope_.setAttackMs (parameters_.attackMs);
    envelope_.setReleaseMs (parameters_.releaseMs);
    envelope_.setProgramDependent (parameters_.autoRelease);

    // The trim compensates for what drive does to the level, so it has to be
    // recomputed whenever drive moves -- not only when the voicing filters are
    // rebuilt. Leaving it stale is silent: the plugin just gets louder with
    // drive while claiming it does not.
    updateAutoTrim();
}

void Engine::updateFilters()
{
    const double oversampledRate = getOversampledRate();
    const double tone = tone_.getCurrent();
    const double character = std::clamp (parameters_.character, 0.0, 1.0);
    const double tapeAmount = 1.0 - character;

    shaper_.setBias (bias_.getCurrent());

    const auto toneLow  = dsp::design::lowShelf  (kToneLowHz,  kToneQ, -tone * kToneRangeDb, oversampledRate);
    const auto toneHigh = dsp::design::highShelf (kToneHighHz, kToneQ,  tone * kToneRangeDb, oversampledRate);

    const auto headBump = dsp::design::lowShelf  (kHeadBumpHz, 0.7, tapeAmount * kHeadBumpDb, oversampledRate);
    const auto gapLoss  = dsp::design::highShelf (kGapLossHz,  0.7, tapeAmount * kGapLossDb,  oversampledRate);

    for (auto& channel : channels_)
    {
        channel.toneLow.setCoefficients (toneLow);
        channel.toneHigh.setCoefficients (toneHigh);
        channel.characterLow.setCoefficients (headBump);
        channel.characterHigh.setCoefficients (gapLoss);
    }

    updateAutoTrim();
}

void Engine::updateAutoTrim()
{
    if (! parameters_.autoTrim)
    {
        autoTrimGain_ = 1.0;
        return;
    }

    // Measure what the saturation stage does to the level of a reference sine,
    // and undo it. Doing this analytically rather than from a running level
    // detector means the compensation cannot pump, and it is deterministic --
    // the same settings always give the same trim.
    constexpr int numPoints = 256;
    const double referenceAmplitude = dsp::dbToGain (kAutoTrimReferenceDb);
    const double drive = driveGain_.getTarget();

    double sumOfSquares = 0.0;
    for (int i = 0; i < numPoints; ++i)
    {
        const double phase = 2.0 * std::numbers::pi * static_cast<double> (i) / static_cast<double> (numPoints);
        const double output = kShaperHeadroom
                            * shaper_.evaluate (drive * referenceAmplitude * std::sin (phase) / kShaperHeadroom);
        sumOfSquares += output * output;
    }

    const double outputRms = std::sqrt (sumOfSquares / static_cast<double> (numPoints));
    const double inputRms  = referenceAmplitude / std::numbers::sqrt2_v<double>;

    autoTrimGain_ = outputRms > 1.0e-12 ? inputRms / outputRms : 1.0;
}

void Engine::process (double* const* channels, int numChannels, int numSamples) noexcept
{
    const dsp::ScopedNoDenormals noDenormals;

    if (numSamples <= 0 || channels_.empty())
        return;

    const int activeChannels = std::min (numChannels, numChannels_);
    const int factor = oversampler_.getFactor();
    const int oversampledSamples = numSamples * factor;

    // Filter coefficients and the shaper bias follow smoothed values, but are
    // rebuilt once per block rather than per sample: designing a biquad costs
    // several transcendentals, and these are set-and-forget controls. The
    // smoothing still runs at sample rate, so the trajectory is continuous --
    // it is only sampled at block boundaries.
    const bool voicingMoving = bias_.isSmoothing() || tone_.isSmoothing();
    bias_.skip (oversampledSamples);
    tone_.skip (oversampledSamples);
    if (voicingMoving)
        updateFilters();

    double* const* work = oversampler_.upsample (channels, numSamples);

    for (int channel = 0; channel < activeChannels; ++channel)
    {
        const auto c = static_cast<std::size_t> (channel);
        std::copy (work[channel], work[channel] + oversampledSamples, dry_[c].begin());
        workPointers_[c] = work[channel];
    }

    double blockGainReductionDb = 0.0;

    for (int i = 0; i < oversampledSamples; ++i)
    {
        const double drive = driveGain_.next();
        const double mix   = mix_.next();

        // Saturate each channel independently -- that is what a stereo pair of
        // analogue channels does -- but derive the dynamics from both together
        // so the centre image cannot wander.
        double saturated[kMaxChannels] {};
        double peak = 0.0;

        for (int channel = 0; channel < activeChannels; ++channel)
        {
            auto& state = channels_[static_cast<std::size_t> (channel)];

            double x = workPointers_[static_cast<std::size_t> (channel)][i];

            x = state.characterLow.process (x);
            x = state.characterHigh.process (x);
            x = state.toneLow.process (x);
            x = state.toneHigh.process (x);

            x = kShaperHeadroom * state.saturator.process (drive * x / kShaperHeadroom, shaper_);

            // Asymmetry makes even harmonics, which is the point of the valve
            // end of the Character control -- and it also makes DC, which grows
            // with drive and would otherwise eat headroom and thump on bypass.
            x = state.dcBlocker.process (x);

            x *= autoTrimGain_;

            saturated[channel] = x;
            peak = std::max (peak, std::abs (x));
        }

        const double gainDb = envelope_.process (gainComputer_.computeGainReductionDb (dsp::gainToDb (peak)));
        const double gain = dsp::dbToGain (gainDb);
        blockGainReductionDb = std::min (blockGainReductionDb, gainDb);

        for (int channel = 0; channel < activeChannels; ++channel)
        {
            const auto c = static_cast<std::size_t> (channel);
            const double wet = saturated[channel] * gain;
            workPointers_[c][i] = dry_[c][static_cast<std::size_t> (i)] * (1.0 - mix) + wet * mix;
        }
    }

    oversampler_.downsample (channels, numSamples);

    for (int i = 0; i < numSamples; ++i)
    {
        const double gain = outputGain_.next();
        for (int channel = 0; channel < activeChannels; ++channel)
            channels[channel][i] *= gain;
    }

    gainReductionDb_ = blockGainReductionDb;
}

} // namespace tezla::emberdrive
