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

/// How often the voicing filters, the shaper bias and the auto-trim are rebuilt
/// from their smoothers, while one of them is moving.
///
/// Chosen by measurement rather than by argument. Sweeping a modulated Bias and
/// looking for a step at the rebuild boundary:
///
///   10 ms    2.16x the roughness of the signal between boundaries
///    5 ms    1.75x
///    2.5 ms  at the noise floor
///    1.25 ms at the noise floor, and 18% more CPU for nothing
///
/// 2.5 ms is also a tenth of the smoothers' own 20 ms time constant, so their
/// trajectory is sampled well above its own bandwidth.
constexpr double kVoicingIntervalSeconds = 0.0025;

/// The master limiter in multiband mode is a safety net on the sum, not a
/// second character stage, so it gets a hard corner rather than the user's knee.
constexpr double kMasterKneeDb = 1.0;

/// The master limiter's attack. Fixed and fast rather than following the Speed
/// control: three bands summing can overshoot what any one of them was limited
/// to, and catching that is the master's only job. At the user's Speed it
/// overshot by 2 dB where a single band overshot by 1.
constexpr double kMasterAttackMs = 0.2;

/// Longest feedback delay, and therefore how much delay line to allocate.
constexpr double kMaxFeedbackSeconds = 0.050;

/// Hard ceiling on the feedback amount. Together with the soft clip in the loop
/// this is what makes runaway impossible rather than merely unlikely.
constexpr double kMaxFeedback = 0.95;
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

    // Everything that allocates lives here, sized for the *worst* case rather
    // than for the current settings, so changing the oversampling factor later
    // can be a switch rather than a rebuild. See setOversamplingFactor().
    oversampler_.prepare (maxBlockSize_, numChannels_, dsp::oversamplingFactor (
        parameters_.oversampling, sampleRate_));

    channels_.assign (static_cast<std::size_t> (numChannels_), ChannelState {});
    dryInput_.assign (static_cast<std::size_t> (numChannels_),
                      std::vector<double> (static_cast<std::size_t> (maxBlockSize_), 0.0));
    workPointers_.assign (static_cast<std::size_t> (numChannels_), nullptr);

    // Both delay lines are sized for x8 whatever factor is running: the dry one
    // covers the oversampler's latency, the feedback one covers its longest
    // time at the highest internal rate. Sizing them for the current factor is
    // what made a factor change allocate.
    const int maxFeedbackSamples =
        static_cast<int> (std::ceil (kMaxFeedbackSeconds * sampleRate_ * 8.0)) + 2;

    for (auto& channel : channels_)
    {
        channel.dryDelay.prepare (dsp::Oversampler::latencyForFactor (8) + 2);

        for (auto& band : channel.bands)
            band.feedbackDelay.prepare (maxFeedbackSamples);
    }

    masterGainComputer_.setKneeDb (kMasterKneeDb);

    // Base rate: these apply after the oversampled block, because crush and
    // downsample are wet-only and live out there. The factor does not touch
    // them, which is why they are set here rather than below.
    mix_       .prepare (sampleRate_, kSmoothingSeconds);
    outputGain_.prepare (sampleRate_, kSmoothingSeconds);

    setOversamplingFactor (oversampler_.getFactor());
}

/// Everything the oversampling factor changes, and nothing that allocates.
///
/// Safe to call from the audio thread, which is the point: the factor is a
/// parameter, and rebuilding the engine when it moved was an allocation in
/// processBlock -- forbidden outright by CLAUDE.md 2.2. Worse here than in
/// Halo, because the same rebuild was also triggered by the expert DC corner,
/// which is a continuous control and can be automated.
void Engine::setOversamplingFactor (int factor)
{
    oversampler_.setFactor (factor);
    preparedFactor_ = oversampler_.getFactor();

    const double oversampledRate = getOversampledRate();

    for (auto& channel : channels_)
        channel.splitter.prepare (oversampledRate);

    // Everything time-based is set from the actual running rate, so the plugin
    // behaves identically at 44.1 and 192 kHz.
    for (auto& band : bandEnvelopes_)
        for (auto& envelope : band)
            envelope.prepare (oversampledRate);
    for (auto& envelope : masterEnvelopes_)
        envelope.prepare (oversampledRate);

    detectorCoefficient_ = std::exp (-1.0 / (kDetectorSeconds * oversampledRate));

    driveGain_     .prepare (oversampledRate, kSmoothingSeconds);
    foldGain_      .prepare (oversampledRate, kSmoothingSeconds);
    rectifyAmount_ .prepare (oversampledRate, kSmoothingSeconds);
    feedbackAmount_.prepare (oversampledRate, kSmoothingSeconds);
    bias_          .prepare (oversampledRate, kSmoothingSeconds);
    tone_          .prepare (oversampledRate, kSmoothingSeconds);

    // In samples at the internal rate, so the rebuild happens every 10 ms
    // whatever the session rate and whatever the oversampling factor is doing.
    voicingIntervalSamples_ = std::max (1, static_cast<int> (
        std::llround (kVoicingIntervalSeconds * oversampledRate)));
    voicingCountdown_ = voicingIntervalSamples_;

    updateDcBlockers();
    updateDerivedParameters();
    reset();
}

/// The DC corner alone. One coefficient per blocker, no memory touched -- which
/// is what lets the expert DC control be automated, or modulated, without
/// rebuilding the engine underneath it.
///
/// `retune`, not `prepare`. prepare() resets, and this is reached from the audio
/// thread every time the corner moves: it threw the filter's memory away on each
/// change, and a first-order highpass whose memory is gone puts out `x` instead
/// of `x - x[n-1] + R*y[n-1]`. That is a step the size of the previous sample.
/// The comment above was already making this claim before the code did.
void Engine::updateDcBlockers()
{
    const double dcHz = parameters_.expert.enabled
                      ? std::clamp (parameters_.expert.dcBlockerHz, 1.0, 40.0) : kDcBlockerHz;

    preparedDcHz_ = dcHz;

    for (auto& channel : channels_)
        for (auto& band : channel.bands)
            band.dcBlocker.retune (getOversampledRate(), dcHz);
}

void Engine::reset()
{
    oversampler_.reset();

    // Restarted, not expired: reset() rebuilds the voicing itself a few lines
    // below, so the first segment after a transport restart is a full interval
    // and lands where every later one does.
    voicingCountdown_ = voicingIntervalSamples_;

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

        channel.downsampler.reset();
        channel.dryDelay.reset();

        for (auto& band : channel.bands)
        {
            band.rectifier.reset();
            band.folder.reset();
            band.saturator.reset();
            band.dcBlocker.reset();
            band.feedbackDelay.reset();
            band.detectorMeanSquare = 0.0;
        }
    }

    for (auto& buffer : dryInput_)
        std::fill (buffer.begin(), buffer.end(), 0.0);

    // Smoothed values jump to where they are heading. Without this, reset()
    // leaves the ramps part-way through, two runs of the same material do not
    // match, and the first 20 ms after a transport restart is not what the
    // settings say it should be.
    driveGain_     .setCurrentAndTarget (driveGain_     .getTarget());
    foldGain_      .setCurrentAndTarget (foldGain_      .getTarget());
    rectifyAmount_ .setCurrentAndTarget (rectifyAmount_ .getTarget());
    feedbackAmount_.setCurrentAndTarget (feedbackAmount_.getTarget());
    mix_       .setCurrentAndTarget (mix_       .getTarget());
    outputGain_.setCurrentAndTarget (outputGain_.getTarget());
    bias_      .setCurrentAndTarget (bias_      .getTarget());
    tone_      .setCurrentAndTarget (tone_      .getTarget());

    updateFilters();
    voicingDirty_ = false;

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

    // Whether the voicing has anything to redo. Comparing the whole struct is
    // deliberately over-broad: it costs forty doubles and it cannot go stale the
    // way a hand-written list of "the fields updateFilters() reads" would the
    // first time someone adds one. Rebuilding a little too often is a few
    // microseconds; rebuilding too rarely is a control that stops working.
    voicingDirty_ = voicingDirty_ || ! (parameters == parameters_);

    parameters_ = parameters;

    if (factorChanged)
    {
        // The factor changes the internal rate, and with it every time
        // constant, every filter coefficient, and the latency the host has to
        // be told about -- but not a byte of memory. This used to call
        // prepare(), which allocated, on the audio thread.
        setOversamplingFactor (requestedFactor);
        return true;
    }

    if (dcChanged)
    {
        // A corner, not a rebuild. It shares one coefficient with nothing else,
        // and it used to drag the whole engine through prepare() -- on a
        // continuous control, from the audio thread.
        updateDcBlockers();
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

    rectifyAmount_.setTarget (std::clamp (parameters_.rectify, 0.0, 1.0));
    feedbackAmount_.setTarget (std::clamp (parameters_.feedback, 0.0, kMaxFeedback));

    // Delay is in oversampled samples, derived from the running rate, so the
    // pitch a feedback loop settles on is the same at every session rate.
    feedbackDelaySamples_ = std::max (1, static_cast<int> (std::llround (
        std::clamp (parameters_.feedbackMs, 0.1, kMaxFeedbackSeconds * 1000.0)
        * 0.001 * getOversampledRate())));

    for (auto& channel : channels_)
    {
        channel.bitcrusher.setAmount (parameters_.crush);
        channel.downsampler.setRatio (parameters_.downsample);
    }

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

    // Not updateAutoTrim() -- that probes the whole nonlinear chain 512 times
    // per band, and this runs on every parameter push. The voicing timer in
    // process() owns it now, and setParameters() decides whether there is
    // anything for it to redo.
}

void Engine::updateFilters()
{
    const double oversampledRate = getOversampledRate();
    const double tone = tone_.getCurrent();
    const auto& expert = parameters_.expert;

    shaper_.setBias (bias_.getCurrent());
    folder_.setGain (foldGain_.getCurrent());
    rectifier_.setAmount (rectifyAmount_.getCurrent());

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
        value = rectifier_.evaluate (value);
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

    // Filter coefficients, the shaper bias and the auto-trim follow smoothed
    // values, but are rebuilt on a timer rather than per sample: designing four
    // biquads costs several transcendentals, and the auto-trim runs a 512-point
    // probe of the whole nonlinear chain once per band. Together they are far
    // too expensive to do at sample rate, and they are set-and-forget controls.
    // The smoothing still runs at sample rate, so the trajectory is continuous;
    // it is only sampled.
    //
    // The timer counts *samples*, not calls, and that matters twice over. It is
    // what makes the result independent of how the host blocks the audio -- the
    // rebuild lands at the same absolute position at 64 samples a block and at
    // 1024. And it is what stopped modulation costing 3.3x CPU: a modulated
    // parameter arrives every 32 samples, so a per-call rebuild ran forty-eight
    // nonlinear evaluations for every output sample.
    //
    // 10 ms is half the smoothers' own 20 ms time constant, so the trajectory
    // is sampled above its own bandwidth rather than merely often.
    // The input has to be copied before upsampling: upsample() reads these
    // buffers and downsample() writes back over them.
    for (int channel = 0; channel < activeChannels; ++channel)
    {
        const auto c = static_cast<std::size_t> (channel);
        std::copy (channels[channel], channels[channel] + numSamples, dryInput_[c].begin());
    }

    double* const* work = oversampler_.upsample (channels, numSamples);

    for (int channel = 0; channel < activeChannels; ++channel)
        workPointers_[static_cast<std::size_t> (channel)] = work[channel];

    double blockGainReductionDb = 0.0;
    std::array<double, kNumBands> blockBandReductionDb {};

    for (int segmentStart = 0; segmentStart < oversampledSamples; )
    {
        // The voicing boundary, and it is a *sample* position rather than a call
        // boundary. That is the whole point: the rebuild lands at the same
        // absolute position whether the host hands over 64 samples or 1024, so
        // the same automation settles along the same path and a bounce matches
        // what was heard. Cutting the loop here is what makes that true --
        // rebuilding once per call cannot, however the timer is arranged.
        if (voicingCountdown_ <= 0)
        {
            const bool moving = bias_.isSmoothing() || tone_.isSmoothing()
                             || foldGain_.isSmoothing() || rectifyAmount_.isSmoothing();

            if (moving || voicingDirty_)
            {
                updateFilters();
                voicingDirty_ = false;
            }

            voicingCountdown_ = voicingIntervalSamples_;
        }

        const int segment = std::min (oversampledSamples - segmentStart, voicingCountdown_);
        voicingCountdown_ -= segment;

        // Advanced by the segment, not the block, for the same reason.
        bias_.skip (segment);
        tone_.skip (segment);
        foldGain_.skip (segment);
        rectifyAmount_.skip (segment);

    for (int i = segmentStart; i < segmentStart + segment; ++i)
    {
        const double drive    = driveGain_.next();
        const double feedback = feedbackAmount_.next();

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

                double x = bandSignal[channel][band];

                // Feedback, injected before the drive stage so the loop passes
                // through the whole nonlinear chain.
                //
                // The tanh is the safety, and it is not optional or defeatable:
                // it bounds whatever comes back to +/-1 no matter how loud the
                // loop has become, and the feedback amount is separately capped
                // below 1. Between them, runaway is impossible rather than
                // merely unlikely -- and the saturator's own compression is what
                // makes the loop settle into oscillation instead of screaming.
                if (feedback > 0.0)
                    x += feedback * std::tanh (bandState.feedbackDelay.at (feedbackDelaySamples_));

                x *= drive * bandTrimGain_[b];

                // Rectify first: it is a source transformation, doubling the
                // fundamental, and the stages after it work on that instead of
                // on the original note.
                x = useAdaa ? bandState.rectifier.process (x, rectifier_) : rectifier_.evaluate (x);

                // Fold next: it creates the harmonics, and the saturator that
                // follows rounds off the corners the folder leaves behind.
                x = useAdaa ? bandState.folder.process (x, folder_) : folder_.evaluate (x);

                x = useAdaa ? h * bandState.saturator.process (x / h, shaper_)
                            : h * shaper_.evaluate (x / h);

                // Asymmetry makes even harmonics, which is the point of the
                // valve end of the Character control -- and it also makes DC,
                // which grows with drive and would otherwise eat headroom and
                // thump on bypass. Rectification makes far more of it.
                x = bandState.dcBlocker.process (x);

                // Tapped here, before the auto-trim, so the loop gain does not
                // change when the trim does.
                bandState.feedbackDelay.push (x);

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
            workPointers_[static_cast<std::size_t> (channel)][i] = summed[channel];
    }

        segmentStart += segment;
    }

    oversampler_.downsample (channels, numSamples);

    // ---- base rate: rate reduction, bit reduction, mix, output -------------
    //
    // Crush and downsample deliberately run out here rather than inside the
    // oversampled block. Everywhere else in this plugin aliasing is a defect to
    // be suppressed; in a bit crusher it is the instrument, and an antialiased
    // one just sounds like a slightly noisy version of the input.
    //
    // They are wet-only, which is why the mix moved out here too: the dry path
    // is delayed by the oversampler's latency instead of being carried through
    // the oversampled block alongside the wet one.
    const int latency = oversampler_.getLatencySamples();

    for (int i = 0; i < numSamples; ++i)
    {
        const double mix  = mix_.next();
        const double gain = outputGain_.next();

        for (int channel = 0; channel < activeChannels; ++channel)
        {
            auto& state = channels_[static_cast<std::size_t> (channel)];

            state.dryDelay.push (dryInput_[static_cast<std::size_t> (channel)][static_cast<std::size_t> (i)]);
            const double dry = state.dryDelay.at (latency);

            double wet = state.downsampler.process (channels[channel][i]);
            wet = state.bitcrusher.process (wet);

            channels[channel][i] = (dry * (1.0 - mix) + wet * mix) * gain;
        }
    }

    gainReductionDb_ = blockGainReductionDb;
    for (int band = 0; band < kNumBands; ++band)
        bandGainReductionDb_[static_cast<std::size_t> (band)] =
            blockBandReductionDb[static_cast<std::size_t> (band)];
}

} // namespace tezla::emberdrive
