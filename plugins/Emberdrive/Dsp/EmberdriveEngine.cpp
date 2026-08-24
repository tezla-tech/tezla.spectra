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
//
// These are the defaults the Character control interpolates between. The expert
// panel can override each of them directly.
constexpr double kMaxBias      = 0.8;    // asymmetry at the valve end
constexpr double kHeadBumpHz   = 90.0;
constexpr double kHeadBumpDb   = 1.5;
constexpr double kGapLossHz    = 8000.0;
constexpr double kGapLossDb    = -2.5;

// Tone is a tilt, not a pair of independent shelves: one control, opposite
// directions, so it never changes the overall level much.
constexpr double kToneLowHz    = 250.0;
constexpr double kToneHighHz   = 2500.0;
constexpr double kToneRangeDb  = 5.0;
constexpr double kToneQ        = 0.7;

// Headroom into the shaper. Without it, drive at 0 dB still runs a -20 dBFS
// signal at 0.1 into a tanh, which is 0.09% THD -- respectable for analogue
// gear but not the "genuinely transparent, not a quieter version of the dirty
// setting" that CLAUDE.md asks for as priority two. Backing the shaper off by
// four moves drive 0 to -85 dB THD while still leaving the top of the drive
// range thoroughly destroyed.
constexpr double kShaperHeadroom = 4.0;

constexpr double kDcBlockerHz      = 10.0;   // low enough not to thin a sub
constexpr double kSmoothingSeconds = 0.02;

// Detector averaging window for the peak-to-RMS blend. Short enough to still
// catch transients, long enough that "RMS" means something.
constexpr double kDetectorSeconds = 0.010;

/// Reference level the auto-trim matches at: a typical mix level rather than
/// full scale, so the compensation is right where material actually sits.
constexpr double kAutoTrimReferenceDb = -12.0;

/// The master limiter in multiband mode is a safety net on the sum, not a
/// second character stage, so it gets a hard corner rather than the user's knee.
constexpr double kMasterKneeDb = 1.0;

/// The master limiter's attack. Fixed and fast rather than following the Speed
/// control: three bands summing can overshoot what any one of them was limited
/// to, and catching that is the master's only job. At the user's Speed it
/// overshot by 2 dB where a single band overshot by 1.
constexpr double kMasterAttackMs = 0.2;
} // namespace

double Engine::biasForCharacter() const noexcept
{
    if (parameters_.expert.enabled)
        return parameters_.expert.bias;

    return std::clamp (parameters_.character, 0.0, 1.0) * kMaxBias;
}

double Engine::headroom() const noexcept
{
    return parameters_.expert.enabled ? std::clamp (parameters_.expert.shaperHeadroom, 1.0, 16.0)
                                      : kShaperHeadroom;
}

void Engine::prepare (double sampleRate, int maxBlockSize, int numChannels)
{
    sampleRate_   = sampleRate > 0.0 ? sampleRate : 44100.0;
    maxBlockSize_ = std::max (maxBlockSize, 1);
    numChannels_  = std::clamp (numChannels, 1, kMaxChannels);

    const int factor = dsp::oversamplingFactor (parameters_.oversampling, sampleRate_);
    oversampler_.prepare (maxBlockSize_, numChannels_, factor);

    const double oversampledRate = getOversampledRate();

    channels_.assign (static_cast<std::size_t> (numChannels_), ChannelState {});
    dry_.assign (static_cast<std::size_t> (numChannels_),
                 std::vector<double> (static_cast<std::size_t> (maxBlockSize_ * factor), 0.0));
    workPointers_.assign (static_cast<std::size_t> (numChannels_), nullptr);

    const double dcHz = parameters_.expert.enabled
                      ? std::clamp (parameters_.expert.dcBlockerHz, 1.0, 40.0) : kDcBlockerHz;

    preparedFactor_ = factor;
    preparedDcHz_   = dcHz;

    for (auto& channel : channels_)
    {
        channel.splitter.prepare (oversampledRate);
        for (auto& band : channel.bands)
            band.dcBlocker.prepare (oversampledRate, dcHz);
    }

    // Everything time-based is set from the actual running rate, so the plugin
    // behaves identically at 44.1 and 192 kHz.
    for (auto& band : bandEnvelopes_)
        for (auto& envelope : band)
            envelope.prepare (oversampledRate);
    for (auto& envelope : masterEnvelopes_)
        envelope.prepare (oversampledRate);

    detectorCoefficient_ = std::exp (-1.0 / (kDetectorSeconds * oversampledRate));

    masterGainComputer_.setKneeDb (kMasterKneeDb);

    driveGain_ .prepare (oversampledRate, kSmoothingSeconds);
    foldGain_  .prepare (oversampledRate, kSmoothingSeconds);
    mix_       .prepare (oversampledRate, kSmoothingSeconds);
    outputGain_.prepare (sampleRate_,     kSmoothingSeconds);
    bias_      .prepare (oversampledRate, kSmoothingSeconds);
    tone_      .prepare (oversampledRate, kSmoothingSeconds);

    updateDerivedParameters();
    reset();
}

void Engine::reset()
{
    oversampler_.reset();

    for (auto& band : bandEnvelopes_)
        for (auto& envelope : band)
            envelope.reset();
    for (auto& envelope : masterEnvelopes_)
        envelope.reset();

    for (auto& channel : channels_)
    {
        channel.toneLow.reset();
        channel.toneHigh.reset();
        channel.characterLow.reset();
        channel.characterHigh.reset();
        channel.splitter.reset();
        channel.masterDetectorMeanSquare = 0.0;

        for (auto& band : channel.bands)
        {
            band.folder.reset();
            band.saturator.reset();
            band.dcBlocker.reset();
            band.detectorMeanSquare = 0.0;
        }
    }

    for (auto& buffer : dry_)
        std::fill (buffer.begin(), buffer.end(), 0.0);

    // Smoothed values jump to where they are heading. Without this, reset()
    // leaves the ramps part-way through, two runs of the same material do not
    // match, and the first 20 ms after a transport restart is not what the
    // settings say it should be.
    driveGain_ .setCurrentAndTarget (driveGain_ .getTarget());
    foldGain_  .setCurrentAndTarget (foldGain_  .getTarget());
    mix_       .setCurrentAndTarget (mix_       .getTarget());
    outputGain_.setCurrentAndTarget (outputGain_.getTarget());
    bias_      .setCurrentAndTarget (bias_      .getTarget());
    tone_      .setCurrentAndTarget (tone_      .getTarget());

    updateFilters();

    gainReductionDb_ = 0.0;
    bandGainReductionDb_.fill (0.0);
}

bool Engine::setParameters (const Parameters& parameters)
{
    // Compared against what prepare() actually built, not against the previous
    // parameters. prepare() has to run before any parameters exist, so it
    // necessarily uses the defaults; checking "have we been given parameters
    // before?" meant the first call after prepare() was ignored, and a project
    // saved with oversampling set to anything but Auto reopened running Auto.
    const int requestedFactor = dsp::oversamplingFactor (parameters.oversampling, sampleRate_);
    const double requestedDcHz = parameters.expert.enabled
                               ? std::clamp (parameters.expert.dcBlockerHz, 1.0, 40.0) : kDcBlockerHz;

    const bool factorChanged = requestedFactor != preparedFactor_;

    // A tolerance rather than an exact compare: any real change to the corner
    // is far larger than this, and an exact float comparison is both a warning
    // under -Wfloat-equal and a way to rebuild the whole engine because a
    // parameter round-tripped through a host at slightly different precision.
    const bool dcChanged = std::abs (requestedDcHz - preparedDcHz_) > 1.0e-9;

    parameters_ = parameters;

    if (factorChanged || dcChanged)
    {
        // The oversampling factor changes the internal rate, which changes
        // every time constant and every filter coefficient -- and the latency,
        // which the host has to be told about. Rebuilding is the honest way to
        // handle it; the caller reports the new latency.
        prepare (sampleRate_, maxBlockSize_, numChannels_);
        return factorChanged;
    }

    updateDerivedParameters();
    return false;
}

void Engine::updateDerivedParameters()
{
    driveGain_.setTarget (dsp::dbToGain (parameters_.driveDb));
    mix_.setTarget (std::clamp (parameters_.mix, 0.0, 1.0));
    outputGain_.setTarget (dsp::dbToGain (parameters_.outputDb));
    bias_.setTarget (biasForCharacter());
    tone_.setTarget (std::clamp (parameters_.toneTilt, -1.0, 1.0));

    // Fold gain is amount x range. The range switch is what turns a wavefolder
    // into a destruction device: at x100 a full-scale signal is folded about 32
    // times per half cycle and the spectrum stops resembling distortion at all.
    foldGain_.setTarget (std::clamp (parameters_.foldAmount, 0.0, 1.0)
                         * std::max (parameters_.foldRange, 1.0));

    gainComputer_.setCeilingDb (parameters_.ceilingDb);
    gainComputer_.setKneeDb (parameters_.kneeDb);
    masterGainComputer_.setCeilingDb (parameters_.ceilingDb);

    for (auto& band : bandEnvelopes_)
        for (auto& envelope : band)
        {
            envelope.setAttackMs (parameters_.attackMs);
            envelope.setReleaseMs (parameters_.releaseMs);
            envelope.setProgramDependent (parameters_.autoRelease);
        }

    for (auto& envelope : masterEnvelopes_)
    {
        envelope.setAttackMs (kMasterAttackMs);
        envelope.setReleaseMs (parameters_.releaseMs);
        envelope.setProgramDependent (parameters_.autoRelease);
    }

    // Band routing. Solo on any band silences the rest; mute always wins.
    const bool anySolo = std::any_of (parameters_.bands.begin(), parameters_.bands.end(),
                                      [] (const BandParameters& band) { return band.state == BandState::Solo; });

    for (int band = 0; band < kNumBands; ++band)
    {
        const auto b = static_cast<std::size_t> (band);
        const auto& settings = parameters_.bands[b];

        bandTrimGain_[b] = dsp::dbToGain (settings.driveTrimDb);
        bandAudible_[b] = parameters_.multiband
                        ? (settings.state != BandState::Mute && (! anySolo || settings.state == BandState::Solo))
                        : (band == 0);
    }

    if (! parameters_.multiband)
    {
        bandTrimGain_[0] = 1.0;
        bandAudible_[1] = bandAudible_[2] = false;
    }

    updateAutoTrim();
}

void Engine::updateFilters()
{
    const double oversampledRate = getOversampledRate();
    const double tone = tone_.getCurrent();
    const auto& expert = parameters_.expert;

    shaper_.setBias (bias_.getCurrent());
    folder_.setGain (foldGain_.getCurrent());

    const double tapeAmount = expert.enabled ? 1.0 : 1.0 - std::clamp (parameters_.character, 0.0, 1.0);

    const double headBumpHz = expert.enabled ? expert.headBumpHz : kHeadBumpHz;
    const double headBumpDb = expert.enabled ? expert.headBumpDb : tapeAmount * kHeadBumpDb;
    const double gapLossHz  = expert.enabled ? expert.gapLossHz  : kGapLossHz;
    const double gapLossDb  = expert.enabled ? expert.gapLossDb  : tapeAmount * kGapLossDb;

    const auto toneLow  = dsp::design::lowShelf  (kToneLowHz,  kToneQ, -tone * kToneRangeDb, oversampledRate);
    const auto toneHigh = dsp::design::highShelf (kToneHighHz, kToneQ,  tone * kToneRangeDb, oversampledRate);
    const auto headBump = dsp::design::lowShelf  (headBumpHz,  0.7, headBumpDb, oversampledRate);
    const auto gapLoss  = dsp::design::highShelf (gapLossHz,   0.7, gapLossDb,  oversampledRate);

    for (auto& channel : channels_)
    {
        channel.toneLow.setCoefficients (toneLow);
        channel.toneHigh.setCoefficients (toneHigh);
        channel.characterLow.setCoefficients (headBump);
        channel.characterHigh.setCoefficients (gapLoss);
        channel.splitter.setCrossovers (parameters_.crossoverLowHz, parameters_.crossoverHighHz);
    }

    updateAutoTrim();
}

double Engine::measureStageGain (double driveGain) const noexcept
{
    // Measure what fold-then-saturate does to the level of a reference sine and
    // report it. Doing this analytically rather than from a running level
    // detector means the compensation cannot pump, and it is deterministic --
    // the same settings always give the same trim.
    constexpr int numPoints = 512;
    const double referenceAmplitude = dsp::dbToGain (kAutoTrimReferenceDb);
    const double h = headroom();

    double sumOfSquares = 0.0;
    for (int i = 0; i < numPoints; ++i)
    {
        const double phase = 2.0 * std::numbers::pi * static_cast<double> (i) / static_cast<double> (numPoints);
        double value = driveGain * referenceAmplitude * std::sin (phase);
        value = folder_.evaluate (value);
        value = h * shaper_.evaluate (value / h);
        sumOfSquares += value * value;
    }

    const double outputRms = std::sqrt (sumOfSquares / static_cast<double> (numPoints));
    const double inputRms  = referenceAmplitude / std::numbers::sqrt2_v<double>;

    return outputRms > 1.0e-12 ? inputRms / outputRms : 1.0;
}

void Engine::updateAutoTrim()
{
    if (! parameters_.autoTrim)
    {
        bandAutoTrim_.fill (1.0);
        return;
    }

    // Each band drives the stage by a different amount, so each needs its own
    // trim -- otherwise turning a band's drive up makes it louder rather than
    // dirtier, which is the thing auto-trim exists to prevent.
    const double drive = driveGain_.getTarget();
    for (int band = 0; band < kNumBands; ++band)
    {
        const auto b = static_cast<std::size_t> (band);
        bandAutoTrim_[b] = measureStageGain (drive * bandTrimGain_[b]);
    }
}

void Engine::process (double* const* channels, int numChannels, int numSamples) noexcept
{
    const dsp::ScopedNoDenormals noDenormals;

    if (numSamples <= 0 || channels_.empty())
        return;

    const int activeChannels = std::min (numChannels, numChannels_);
    const int factor = oversampler_.getFactor();
    const int oversampledSamples = numSamples * factor;

    const auto& expert = parameters_.expert;
    const bool multiband = parameters_.multiband;
    const bool useAdaa = ! expert.enabled || expert.adaaEnabled;
    const double h = headroom();
    const double stereoLink = expert.enabled ? std::clamp (expert.stereoLink, 0.0, 1.0) : 1.0;
    const double rmsBlend = expert.enabled ? std::clamp (expert.detectorRms, 0.0, 1.0) : 0.0;

    // Filter coefficients and the shaper bias follow smoothed values, but are
    // rebuilt once per block rather than per sample: designing a biquad costs
    // several transcendentals, and these are set-and-forget controls. The
    // smoothing still runs at sample rate, so the trajectory is continuous --
    // it is only sampled at block boundaries.
    const bool voicingMoving = bias_.isSmoothing() || tone_.isSmoothing() || foldGain_.isSmoothing();
    bias_.skip (oversampledSamples);
    tone_.skip (oversampledSamples);
    foldGain_.skip (oversampledSamples);
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
    std::array<double, kNumBands> blockBandReductionDb {};

    for (int i = 0; i < oversampledSamples; ++i)
    {
        const double drive = driveGain_.next();
        const double mix   = mix_.next();

        // ---- voicing and band split ---------------------------------------
        double bandSignal[kMaxChannels][kNumBands] {};

        for (int channel = 0; channel < activeChannels; ++channel)
        {
            auto& state = channels_[static_cast<std::size_t> (channel)];

            double x = workPointers_[static_cast<std::size_t> (channel)][i];
            x = state.characterLow.process (x);
            x = state.characterHigh.process (x);
            x = state.toneLow.process (x);
            x = state.toneHigh.process (x);

            if (multiband)
                state.splitter.process (x, bandSignal[channel][0], bandSignal[channel][1], bandSignal[channel][2]);
            else
                bandSignal[channel][0] = x;
        }

        double summed[kMaxChannels] {};

        // ---- per band: drive, fold, saturate, DC, limit ---------------------
        for (int band = 0; band < kNumBands; ++band)
        {
            const auto b = static_cast<std::size_t> (band);
            if (! bandAudible_[b])
                continue;

            double shaped[kMaxChannels] {};
            double linkedLevel = 0.0;
            double ownLevel[kMaxChannels] {};

            for (int channel = 0; channel < activeChannels; ++channel)
            {
                auto& bandState = channels_[static_cast<std::size_t> (channel)].bands[b];

                double x = bandSignal[channel][band] * drive * bandTrimGain_[b];

                // Fold first: it creates the harmonics, and the saturator that
                // follows rounds off the corners the folder leaves behind.
                x = useAdaa ? bandState.folder.process (x, folder_) : folder_.evaluate (x);

                x = useAdaa ? h * bandState.saturator.process (x / h, shaper_)
                            : h * shaper_.evaluate (x / h);

                // Asymmetry makes even harmonics, which is the point of the
                // valve end of the Character control -- and it also makes DC,
                // which grows with drive and would otherwise eat headroom and
                // thump on bypass.
                x = bandState.dcBlocker.process (x);
                x *= bandAutoTrim_[b];

                shaped[channel] = x;

                const double magnitude = std::abs (x);
                bandState.detectorMeanSquare = x * x
                    + detectorCoefficient_ * (bandState.detectorMeanSquare - x * x);
                const double rms = std::sqrt (bandState.detectorMeanSquare);

                ownLevel[channel] = (1.0 - rmsBlend) * magnitude + rmsBlend * rms;
                linkedLevel = std::max (linkedLevel, ownLevel[channel]);
            }

            for (int channel = 0; channel < activeChannels; ++channel)
            {
                // A partial link is a blend between what this channel is doing
                // and what the loudest channel is doing. Fully linked keeps the
                // centre image still; fully independent is wider and looser.
                const double level = stereoLink * linkedLevel + (1.0 - stereoLink) * ownLevel[channel];
                const double target = gainComputer_.computeGainReductionDb (dsp::gainToDb (level));
                const double gainDb = bandEnvelopes_[b][static_cast<std::size_t> (channel)].process (target);

                blockBandReductionDb[b] = std::min (blockBandReductionDb[b], gainDb);
                summed[channel] += shaped[channel] * dsp::dbToGain (gainDb);
            }
        }

        // ---- master limiter on the sum -------------------------------------
        if (multiband)
        {
            double linkedLevel = 0.0;
            double ownLevel[kMaxChannels] {};

            for (int channel = 0; channel < activeChannels; ++channel)
            {
                auto& state = channels_[static_cast<std::size_t> (channel)];
                const double x = summed[channel];
                const double magnitude = std::abs (x);

                state.masterDetectorMeanSquare = x * x
                    + detectorCoefficient_ * (state.masterDetectorMeanSquare - x * x);

                ownLevel[channel] = (1.0 - rmsBlend) * magnitude
                                  + rmsBlend * std::sqrt (state.masterDetectorMeanSquare);
                linkedLevel = std::max (linkedLevel, ownLevel[channel]);
            }

            for (int channel = 0; channel < activeChannels; ++channel)
            {
                const double level = stereoLink * linkedLevel + (1.0 - stereoLink) * ownLevel[channel];
                const double target = masterGainComputer_.computeGainReductionDb (dsp::gainToDb (level));
                const double gainDb = masterEnvelopes_[static_cast<std::size_t> (channel)].process (target);

                blockGainReductionDb = std::min (blockGainReductionDb, gainDb);
                summed[channel] *= dsp::dbToGain (gainDb);
            }
        }
        else
        {
            blockGainReductionDb = std::min (blockGainReductionDb, blockBandReductionDb[0]);
        }

        for (int channel = 0; channel < activeChannels; ++channel)
        {
            const auto c = static_cast<std::size_t> (channel);
            workPointers_[c][i] = dry_[c][static_cast<std::size_t> (i)] * (1.0 - mix) + summed[channel] * mix;
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
    for (int band = 0; band < kNumBands; ++band)
        bandGainReductionDb_[static_cast<std::size_t> (band)] =
            blockBandReductionDb[static_cast<std::size_t> (band)];
}

} // namespace tezla::emberdrive
