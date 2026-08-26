#include "HaloEngine.hpp"

#include <algorithm>
#include <array>
#include <cmath>

#include <tezla/dsp/Decibels.hpp>
#include <tezla/dsp/Denormals.hpp>

namespace tezla::halo
{

namespace
{
constexpr double kSmoothingSeconds = 0.02;

/// The envelope-derived values are recomputed this often, in Hz. Fixed in Hz
/// rather than in samples so the plugin behaves identically at every session
/// rate and every oversampling factor.
constexpr double kControlRateHz = 24000.0;

/// Band envelope. Fast enough to follow a drum transient, slow enough to hold
/// steady on a 40 Hz sine -- the release has to outlast a full cycle of the
/// lowest band the plugin will be asked to work on, or the envelope droops
/// between peaks and the fundamental trim it feeds goes with it.
/// Averaging window for the band's mean square, per stage. Two stages.
///
/// One time constant per stage, not an attack and a release: an asymmetric
/// follower on a squared signal does not converge on the mean square at all --
/// it drifts up towards the peak of the square, and the root of twice *that*
/// over-read the amplitude by 40%. The trim then over-compensated and put a
/// copy of the source back into the wet path, measuring -15 dB at the
/// fundamental instead of -51.
///
/// Two stages rather than one because the mean square of a tone ripples at
/// twice its frequency, and everything downstream -- the trims, the even
/// half's scaling, Track's normalisation -- is a gain riding on this number.
/// A single pole leaves enough of that ripple to modulate the whole wet path
/// into a dense sideband skirt: uniform bins at -78 dBFS spread either side of
/// every harmonic, which summed to a -50 dB "aliasing" figure that was not
/// aliasing at all. A second pole costs one multiply-add and takes the skirt
/// with it.
constexpr double kEnvelopeWindowMs = 30.0;

/// Punch detector: the same band followed twice, quickly and slowly. Their
/// difference is what a transient looks like without any detection worth the
/// name, and it is what the Aphex patent calls transient discrimination.
constexpr double kPunchFastAttackMs  = 0.5;
constexpr double kPunchFastReleaseMs = 20.0;
constexpr double kPunchSlowAttackMs  = 20.0;
constexpr double kPunchSlowReleaseMs = 200.0;

/// Floor for the envelope, and so for the division that normalises the band.
///
/// For the Curve generator it only decides how far down the normalisation keeps
/// working before it gives up and stops amplifying noise -- silence comes out
/// silent regardless, because that generator maps 0 to exactly 0.
///
/// The Chebyshev generator does not. T_even(0) is +/-1, so at true silence it
/// returns a constant, and multiplying that by a floored envelope would leave
/// a -90 dBFS offset that *moves* with the signal. So the divisor is floored
/// and the multiplier is gated: below this level the wet path is a real zero
/// rather than a small number. See the recombine loop.
constexpr double kEnvelopeFloorDb = -90.0;

/// How long a generator change takes to cross over.
///
/// The two generators produce quite different signals from the same input, so
/// switching between them is a discrete change and gets a crossfade rather than
/// a step -- CLAUDE.md 7. Long enough not to click, short enough to feel like a
/// switch rather than a fade.
constexpr double kGeneratorFadeSeconds = 0.015;

/// Two first-order stages on the wet path, not one.
///
/// The even half of the generator sits on a DC pedestal that grows and shrinks
/// with the signal. Most of it is subtracted at the source (see
/// HarmonicGenerator.hpp), but the closed-form fit that does so is good to
/// about 0.0035, and what is left still moves -- so it arrives as a few Hz of
/// wander rather than as a static offset, and one pole at 12 Hz barely touches
/// it. Measured on a sweep it was -50 dBFS below 900 Hz; two poles take it to
/// -70.
///
/// CLAUDE.md 7 asks for a gentle first-order blocker and warns against a steep
/// one, because sub bass is the point of this music. The reason that warning
/// does not apply here is structural: this filter is on the *harmonics* path
/// only. The dry signal passes through a delay line and nothing else, so the
/// sub is not merely preserved, it is bit-exact. The cost is about 1 dB at
/// 40 Hz on the generated harmonics in Below mode, and 0.3 dB at 80 Hz where
/// the second harmonic of a 40 Hz sub actually lands.
constexpr double kDcBlockerHz = 15.0;

/// Fourth-order Butterworth section Qs, for Floor and Ceiling. Two cascaded
/// biquads at these Qs are maximally flat; two at 0.7071 would be
/// Linkwitz-Riley, which is the right answer for a crossover and the wrong one
/// for a band limit that is supposed to leave the passband alone.
constexpr double kButterworthQ1 = 0.54119610;
constexpr double kButterworthQ2 = 1.30656296;

/// Auto trim follows the programme rather than the transients. Long enough that
/// it reads as "this setting is louder" and not as a compressor.
constexpr double kAutoTrimSeconds = 0.25;

/// Below this the block is silence as far as the trim and the meter are
/// concerned, and both hold their previous value rather than dividing by it.
constexpr double kTrimSilenceEnergy = 1.0e-18;

/// Drive maps to the generator's gain through a square law, so the useful
/// harmonic range sits across the middle of the control's travel rather than
/// all bunched into the last tenth.
constexpr double kMaxDriveGain = 30.0;

[[nodiscard]] double coefficientFor (double milliseconds, double sampleRate) noexcept
{
    const double samples = std::max (milliseconds, 0.001) * 0.001 * sampleRate;
    return std::exp (-1.0 / std::max (samples, 1.0));
}

/// One-pole follower step, with separate attack and release.
[[nodiscard]] inline double followEnvelope (double state, double magnitude,
                                            double attack, double release) noexcept
{
    const double coefficient = magnitude > state ? attack : release;
    return magnitude + coefficient * (state - magnitude);
}

} // namespace

double Engine::driveGainFor (double drive) noexcept
{
    const double clamped = std::clamp (drive, 0.0, 1.0);
    return kMaxDriveGain * clamped * clamped;
}

void Engine::prepare (double sampleRate, int maxBlockSize, int numChannels)
{
    sampleRate_   = sampleRate > 0.0 ? sampleRate : 44100.0;
    maxBlockSize_ = std::max (maxBlockSize, 1);
    numChannels_  = std::clamp (numChannels, 1, kMaxChannels);

    const int factor = dsp::oversamplingFactor (parameters_.oversampling, sampleRate_);
    oversampler_.prepare (maxBlockSize_, numChannels_, factor);
    preparedFactor_ = factor;

    const double oversampledRate = getOversampledRate();

    channels_.assign (static_cast<std::size_t> (numChannels_), ChannelState {});
    dryInput_.assign (static_cast<std::size_t> (numChannels_),
                      std::vector<double> (static_cast<std::size_t> (maxBlockSize_), 0.0));

    for (auto& channel : channels_)
    {
        channel.focus.prepare (oversampledRate);
        channel.dcBlockerA.prepare (oversampledRate, kDcBlockerHz);
        channel.dcBlockerB.prepare (oversampledRate, kDcBlockerHz);

        // The delay only ever has to cover the oversampler's own latency, which
        // is a whole number of base-rate samples by design.
        channel.dryDelay.prepare (oversampler_.getLatencySamples() + 2);
    }

    controlInterval_  = std::max (1, static_cast<int> (std::lround (oversampledRate / kControlRateHz)));
    controlCountdown_ = 0;

    envelopeCoefficient_ = 1.0 - coefficientFor (kEnvelopeWindowMs, oversampledRate);
    punchFastAttack_  = coefficientFor (kPunchFastAttackMs,  oversampledRate);
    punchFastRelease_ = coefficientFor (kPunchFastReleaseMs, oversampledRate);
    punchSlowAttack_  = coefficientFor (kPunchSlowAttackMs,  oversampledRate);
    punchSlowRelease_ = coefficientFor (kPunchSlowReleaseMs, oversampledRate);
    envelopeFloor_    = dsp::dbToGain (kEnvelopeFloorDb, -200.0);

    // Base rate: these apply outside the oversampled block, so smoothing them
    // at the internal rate would make the ramp four times too fast at 48 kHz
    // and correct only at 192.
    inputGain_  .prepare (sampleRate_, kSmoothingSeconds);
    amountGain_ .prepare (sampleRate_, kSmoothingSeconds);
    outputGain_ .prepare (sampleRate_, kSmoothingSeconds);
    widthAmount_.prepare (sampleRate_, kSmoothingSeconds);

    // Short: these follow the envelope, which is already smooth. All this has
    // to do is take the corners off the control-rate steps.
    scale_    .prepare (oversampledRate, 0.0005);
    punchGain_.prepare (oversampledRate, 0.0005);

    focus_    .prepare (oversampledRate, kSmoothingSeconds);
    driveGain_.prepare (oversampledRate, kSmoothingSeconds);
    colour_   .prepare (oversampledRate, kSmoothingSeconds);
    track_    .prepare (oversampledRate, kSmoothingSeconds);
    chebIndex_.prepare (oversampledRate, kSmoothingSeconds);
    chebTilt_ .prepare (oversampledRate, kSmoothingSeconds);
    bandLimit_.prepare (oversampledRate, kSmoothingSeconds);

    for (auto& gain : chebGains_)
        gain.prepare (oversampledRate, kSmoothingSeconds);

    // Per sample, because it is a crossfade and not a coefficient.
    generatorFade_.prepare (oversampledRate, kGeneratorFadeSeconds);

    updateDerivedParameters();

    inputGain_ .setCurrentAndTarget (inputGain_ .getTarget());
    amountGain_.setCurrentAndTarget (amountGain_.getTarget());
    outputGain_ .setCurrentAndTarget (outputGain_ .getTarget());
    widthAmount_.setCurrentAndTarget (widthAmount_.getTarget());
    focus_     .setCurrentAndTarget (focus_     .getTarget());
    driveGain_ .setCurrentAndTarget (driveGain_ .getTarget());
    colour_    .setCurrentAndTarget (colour_    .getTarget());
    track_     .setCurrentAndTarget (track_     .getTarget());
    chebIndex_ .setCurrentAndTarget (chebIndex_ .getTarget());
    chebTilt_  .setCurrentAndTarget (chebTilt_  .getTarget());
    bandLimit_ .setCurrentAndTarget (bandLimit_ .getTarget());
    generatorFade_.setCurrentAndTarget (generatorFade_.getTarget());

    for (auto& gain : chebGains_)
        gain.setCurrentAndTarget (gain.getTarget());
    scale_     .setCurrentAndTarget (1.0);
    punchGain_ .setCurrentAndTarget (1.0);

    updateFilters();
    reset();
}

void Engine::reset()
{
    oversampler_.reset();

    for (auto& channel : channels_)
    {
        channel.focus.reset();
        channel.generator.reset();
        channel.dcBlockerA.reset();
        channel.dcBlockerB.reset();
        channel.floorA.reset();
        channel.floorB.reset();
        channel.ceilingA.reset();
        channel.ceilingB.reset();
        channel.limitA.reset();
        channel.limitB.reset();
        channel.dryDelay.reset();
    }

    meanSquareA_    = 0.0;
    meanSquare_     = 0.0;
    envelope_       = 0.0;
    chebMeanSquare_ = 0.0;
    chebEnvelope_   = 0.0;
    punchFast_  = 0.0;
    punchSlow_  = 0.0;

    controlCountdown_ = 0;
    autoTrimGain_     = 1.0;
    harmonicsDb_      = kAmountSilenceDb;

    // Smoothers snap to their targets rather than being left mid-ramp: two runs
    // of the same audio from a fresh reset must produce the same output, and a
    // smoother caught halfway through a move is the classic way for that to
    // quietly stop being true.
    inputGain_ .setCurrentAndTarget (inputGain_ .getTarget());
    amountGain_.setCurrentAndTarget (amountGain_.getTarget());
    outputGain_ .setCurrentAndTarget (outputGain_ .getTarget());
    widthAmount_.setCurrentAndTarget (widthAmount_.getTarget());
    focus_     .setCurrentAndTarget (focus_     .getTarget());
    driveGain_ .setCurrentAndTarget (driveGain_ .getTarget());
    colour_    .setCurrentAndTarget (colour_    .getTarget());
    track_     .setCurrentAndTarget (track_     .getTarget());
    chebIndex_ .setCurrentAndTarget (chebIndex_ .getTarget());
    chebTilt_  .setCurrentAndTarget (chebTilt_  .getTarget());
    bandLimit_ .setCurrentAndTarget (bandLimit_ .getTarget());
    generatorFade_.setCurrentAndTarget (generatorFade_.getTarget());

    for (auto& gain : chebGains_)
        gain.setCurrentAndTarget (gain.getTarget());
    scale_     .setCurrentAndTarget (1.0);
    punchGain_ .setCurrentAndTarget (1.0);
}

bool Engine::setParameters (const Parameters& parameters)
{
    // Compared against what prepare() actually built, not against the previous
    // parameters. prepare() has to run before any parameters exist, so it
    // necessarily uses the defaults; checking "have we been given parameters
    // before?" meant the first call after prepare() was ignored, and a project
    // saved with oversampling set to anything but Auto reopened running Auto.
    const int requestedFactor = dsp::oversamplingFactor (parameters.oversampling, sampleRate_);
    const bool factorChanged = requestedFactor != preparedFactor_;

    const bool floorTurnedOn   = parameters.floorOn   && ! parameters_.floorOn;
    const bool ceilingTurnedOn = parameters.ceilingOn && ! parameters_.ceilingOn;
    const bool modeChanged     = parameters.bandMode  != parameters_.bandMode;

    const bool limitTurnedOn = parameters.generator == Generator::Chebyshev
                            && parameters_.generator != Generator::Chebyshev;

    // The Curve generator's ADAA holds the previous sample, and while Chebyshev
    // is selected it stops being called -- so on the way back its history is
    // whatever it was left with, and its first sample would be a step. Clearing
    // it makes that first sample a plain evaluate() instead, which the crossfade
    // is weighting at nearly zero anyway. Chebyshev needs no equivalent: it
    // keeps no history at all.
    const bool curveReturning = parameters.generator == Generator::Curve
                             && parameters_.generator != Generator::Curve;

    parameters_ = parameters;

    if (factorChanged)
    {
        // The factor changes the internal rate, and with it every time
        // constant, every filter coefficient, and the latency the host has to
        // be told about. Rebuilding is the honest way to handle it.
        prepare (sampleRate_, maxBlockSize_, numChannels_);
        return true;
    }

    // A filter that has been switched out carries whatever state it had when it
    // stopped being called. Switching it back in without clearing it dumps that
    // straight into the signal as a click.
    for (auto& channel : channels_)
    {
        if (floorTurnedOn)   { channel.floorA.reset();   channel.floorB.reset(); }
        if (ceilingTurnedOn) { channel.ceilingA.reset(); channel.ceilingB.reset(); }
        if (limitTurnedOn)   { channel.limitA.reset();   channel.limitB.reset(); }
        if (curveReturning)  { channel.generator.reset(); }

        // Only one branch of the split is ever called, so the other one holds
        // whatever state it had when the mode last changed. Switching to it
        // without clearing dumps that into the signal.
        if (modeChanged) channel.focus.reset();
    }

    updateDerivedParameters();
    return false;
}

void Engine::updateDerivedParameters()
{
    inputGain_ .setTarget (dsp::dbToGain (parameters_.inputDb));
    outputGain_.setTarget (dsp::dbToGain (parameters_.outputDb));

    // Stored as the *departure* from unity rather than as the width itself, so
    // that the neutral setting multiplies by exactly zero. See the recombine
    // loop for why that matters.
    widthAmount_.setTarget (std::clamp (parameters_.width, 0.0, 2.0) - 1.0);

    // The floor is a real zero, not a very small number: at the bottom of its
    // travel Amount has to be silence so the plugin can be taken out of circuit
    // from the front panel.
    amountGain_.setTarget (dsp::dbToGain (parameters_.amountDb, kAmountSilenceDb));

    focus_    .setTarget (std::clamp (parameters_.focusHz, 20.0, 20000.0));
    driveGain_.setTarget (driveGainFor (parameters_.drive));
    colour_   .setTarget (std::clamp (parameters_.colour, 0.0, 1.0));
    track_    .setTarget (std::clamp (parameters_.track, 0.0, 1.0));

    chebIndex_.setTarget (std::clamp (parameters_.chebIndex, 0.0, 2.0));
    chebTilt_ .setTarget (std::clamp (parameters_.chebTilt, -1.0, 1.0));

    for (std::size_t i = 0; i < chebGains_.size(); ++i)
        chebGains_[i].setTarget (std::max (parameters_.harmonics[i], 0.0));

    const bool chebyshev = parameters_.generator == Generator::Chebyshev;
    generatorFade_.setTarget (chebyshev ? 1.0 : 0.0);

    // How far up the band the Chebyshev generator may be fed. T_n of content at
    // B lands at n*B, so anything above internalNyquist / n folds -- and this is
    // the corner that stops it existing. Derived from the parameters rather than
    // from the smoothed levels so it does not chatter while a level is moving.
    //
    // In Below mode this is almost never the binding constraint: a 120 Hz band
    // allows harmonic 800. In Above mode it is, which is the honest shape of the
    // trade and why the number is shown on the page.
    const double nyquist = getOversampledRate() * 0.5;
    const int highest = highestRequestedHarmonic();

    bandLimit_.setTarget (highest > 0 ? std::clamp (nyquist / highest, 200.0, nyquist * 0.45)
                                      : nyquist * 0.45);

    floorActive_   = parameters_.floorOn;
    ceilingActive_ = parameters_.ceilingOn;
}

int Engine::highestRequestedHarmonic() const noexcept
{
    if (parameters_.chebIndex < dsp::ChebyshevGenerator::kMinIndex)
        return 0;

    int highest = 0;

    for (int i = 0; i < dsp::ChebyshevGenerator::kNumHarmonics; ++i)
        if (parameters_.harmonics[static_cast<std::size_t> (i)] > 0.0)
            highest = dsp::ChebyshevGenerator::kFirstHarmonic + i;

    return highest;
}

/// Rebuilds every filter coefficient, and records what it built.
///
/// The record is the point. Checking "did a parameter change?" is not the same
/// question as "does the filter currently match the parameter", and the first
/// time these came apart the Ceiling control was silently inert: prepare() built
/// it at the default corner and nothing afterwards ever rebuilt it, so the
/// measured attenuation at the requested frequency was 0.0 dB.
void Engine::updateFilters()
{
    const double oversampledRate = getOversampledRate();
    const double nyquist = oversampledRate * 0.5;

    const double focusHz = std::clamp (focus_.getCurrent(), 20.0, nyquist * 0.45);

    const double floorHz = std::clamp (parameters_.floorHz, 20.0, nyquist * 0.45);
    const double ceilingHz = std::clamp (parameters_.ceilingHz, 200.0, nyquist * 0.45);
    const double limitHz = std::clamp (bandLimit_.getCurrent(), 200.0, nyquist * 0.45);

    builtFocusHz_   = focusHz;
    builtFloorHz_   = floorHz;
    builtCeilingHz_ = ceilingHz;
    builtLimitHz_   = limitHz;

    const auto floorLow  = dsp::design::highpass (floorHz, kButterworthQ1, oversampledRate);
    const auto floorHigh = dsp::design::highpass (floorHz, kButterworthQ2, oversampledRate);
    const auto ceilLow   = dsp::design::lowpass  (ceilingHz, kButterworthQ1, oversampledRate);
    const auto ceilHigh  = dsp::design::lowpass  (ceilingHz, kButterworthQ2, oversampledRate);
    const auto limitLow  = dsp::design::lowpass  (limitHz, kButterworthQ1, oversampledRate);
    const auto limitHigh = dsp::design::lowpass  (limitHz, kButterworthQ2, oversampledRate);

    for (auto& channel : channels_)
    {
        channel.focus.setCrossover (focusHz);
        channel.floorA.setCoefficients (floorLow);
        channel.floorB.setCoefficients (floorHigh);
        channel.ceilingA.setCoefficients (ceilLow);
        channel.ceilingB.setCoefficients (ceilHigh);
        channel.limitA.setCoefficients (limitLow);
        channel.limitB.setCoefficients (limitHigh);
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
    const bool aboveMode = parameters_.bandMode == BandMode::Above;
    const bool listen = parameters_.listen;

    // Filter coefficients follow smoothed values but are rebuilt once per block
    // rather than per sample: designing a biquad costs several transcendentals,
    // and these are set-and-forget controls. The smoothing still runs at sample
    // rate, so the trajectory is continuous -- it is only sampled at block
    // boundaries.
    focus_.skip (oversampledSamples);
    driveGain_.skip (oversampledSamples);
    colour_.skip (oversampledSamples);
    bandLimit_.skip (oversampledSamples);
    const double track = track_.skip (oversampledSamples);

    // Against what was built, not against what changed. A tolerance rather than
    // an exact compare: any real move is far larger, and an exact float
    // comparison is a -Wfloat-equal warning and a way to redesign every filter
    // because a parameter round-tripped through a host at slightly different
    // precision.
    constexpr double kFilterTolerance = 1.0e-9;

    if (std::abs (focus_.getCurrent()     - builtFocusHz_)   > kFilterTolerance
     || std::abs (parameters_.floorHz     - builtFloorHz_)   > kFilterTolerance
     || std::abs (parameters_.ceilingHz   - builtCeilingHz_) > kFilterTolerance
     || std::abs (bandLimit_.getCurrent() - builtLimitHz_)   > kFilterTolerance)
        updateFilters();

    // Unconditionally, not only when a smoother is moving. prepare() runs
    // before any parameter is known and leaves the smoothers snapped to their
    // targets, so a "has anything moved?" guard here would mean the generator
    // never got its drive set at all on the first block -- the same shape of
    // bug that once left Emberdrive's oversampling control inert.
    generator_.setDrive (driveGain_.getCurrent());
    generator_.setColour (colour_.getCurrent());

    // One call rather than nine: setAll rebuilds the pedestal and fundamental
    // corrections once, and above index 1 that rebuild is a quadrature.
    std::array<double, dsp::ChebyshevGenerator::kNumHarmonics> chebGains {};
    for (std::size_t i = 0; i < chebGains_.size(); ++i)
        chebGains[i] = chebGains_[i].skip (oversampledSamples);

    chebyshev_.setAll (chebGains.data(),
                       chebTilt_ .skip (oversampledSamples),
                       chebIndex_.skip (oversampledSamples));

    const double punchDepth = std::clamp (parameters_.punch, 0.0, 1.0);

    // Input trim first, so both paths see the same signal and the dry copy
    // taken next is genuinely what the wet path was derived from.
    //
    // Samples outer, channels inner, throughout: a smoother advanced once per
    // channel would run at twice the rate in stereo and at a different rate
    // again in mono.
    for (int i = 0; i < numSamples; ++i)
    {
        const double gain = inputGain_.next();

        for (int channel = 0; channel < activeChannels; ++channel)
            channels[channel][i] *= gain;
    }

    for (int channel = 0; channel < activeChannels; ++channel)
    {
        const auto c = static_cast<std::size_t> (channel);
        std::copy (channels[channel], channels[channel] + numSamples, dryInput_[c].begin());
    }

    double* const* work = oversampler_.upsample (channels, numSamples);

    for (int i = 0; i < oversampledSamples; ++i)
    {
        // Read before the split, because the band limit fades with it. A
        // boolean here instead was a click: switching generator flipped the
        // limit in one sample, and the outgoing generator -- still carrying
        // almost all the weight -- saw its band step.
        const double fade = generatorFade_.next();

        // ---- band split, stereo-linked peak --------------------------------
        double band[kMaxChannels] {};
        double linkedPeak = 0.0;

        for (int channel = 0; channel < activeChannels; ++channel)
        {
            const auto c = static_cast<std::size_t> (channel);
            const double x = work[channel][i];

            band[channel] = aboveMode ? channels_[c].focus.processHigh (x)
                                      : channels_[c].focus.processLow (x);

            // Ahead of the envelope, not after it: the normalisation has to
            // describe the signal the generator will actually see, or the unit
            // amplitude the whole method rests on is measured off the wrong
            // waveform.
            //
            // Faded in with the generator that needs it, and skipped outright
            // when it is not wanted, so Curve mode pays nothing and neither
            // generator ever sees its input step.
            if (fade > 0.0)
            {
                const double limited = channels_[c].limitB.process (channels_[c].limitA.process (band[channel]));
                band[channel] = fade >= 1.0 ? limited
                                            : band[channel] + fade * (limited - band[channel]);
            }

            linkedPeak = std::max (linkedPeak, std::abs (band[channel]));
        }

        // ---- amplitude: mean square, per sample --------------------------------
        //
        // Root mean square times root two, which for a sine *is* its amplitude,
        // and which is the quantity the generator's trims are derived for. Two
        // cheaper-looking estimators were tried first and both were wrong in
        // ways that only measurement showed:
        //
        //   * the maximum of |band| over a control interval depends on where
        //     that window falls within the waveform, so it ripples at the beat
        //     between the signal and the control rate. The odd half's trim
        //     rides on this, so that beat modulated a gain-4 copy of the band
        //     and sprayed it across the spectrum: -50 dBFS below 900 Hz on a
        //     sweep.
        //   * a per-sample peak follower cannot reach the peak of a 4 kHz tone
        //     with any attack slow enough not to ripple, so it reads low and
        //     the fundamental cancellation fell from -51 to -31 dB.
        //
        // A mean square has neither problem: it is smooth by construction and
        // exact for a steady tone.
        const double squared = linkedPeak * linkedPeak;
        meanSquareA_ += envelopeCoefficient_ * (squared - meanSquareA_);
        meanSquare_  += envelopeCoefficient_ * (meanSquareA_ - meanSquare_);
        envelope_     = std::sqrt (2.0 * meanSquare_);

        // A third pole, for the Chebyshev path alone. See chebMeanSquare_.
        chebMeanSquare_ += envelopeCoefficient_ * (meanSquare_ - chebMeanSquare_);
        chebEnvelope_    = std::sqrt (2.0 * chebMeanSquare_);

        punchFast_ = followEnvelope (punchFast_, linkedPeak, punchFastAttack_, punchFastRelease_);
        punchSlow_ = followEnvelope (punchSlow_, linkedPeak, punchSlowAttack_, punchSlowRelease_);

        // ---- control rate: only what costs a transcendental -------------------
        if (controlCountdown_ <= 0)
        {
            const double envelope = std::max (envelope_, envelopeFloor_);

            // envelope^Track. At Track 0 the band goes in raw and the harmonics
            // grow with the square of the source, the way a nonlinearity makes
            // them; at Track 1 the band is normalised completely and the
            // harmonic-to-source ratio stops depending on level at all.
            //
            // The pow is why this is at control rate: affordable a few thousand
            // times a second, not 384,000.
            scale_.setTarget (track <= 0.0 ? 1.0 : std::pow (envelope, track));

            // Transient discrimination: how far the fast follower sits above
            // the slow one. Zero on anything steady.
            const double slow = std::max (punchSlow_, envelopeFloor_);
            const double transient = std::clamp ((punchFast_ - slow) / slow, 0.0, 1.0);

            // Punch 0 is exactly 1, so the stage is bit-exact at its neutral
            // setting rather than merely inaudible there.
            punchGain_.setTarget (1.0 - punchDepth * (1.0 - transient));

            controlCountdown_ = controlInterval_;
        }

        --controlCountdown_;

        const double scale = std::max (scale_.next(), envelopeFloor_);
        const double inverseScale = 1.0 / scale;
        const double punch = punchGain_.next();

        // What amplitude the generator is actually seeing, which is what its
        // fundamental trim has to be computed from -- not the band's amplitude
        // and not a constant.
        generator_.setInputAmplitude (envelope_ * inverseScale);

        // Chebyshev has its own normalisation, and it differs from Track's in
        // two ways that both matter.
        //
        // It is pinned at Track 1 -- divide by the envelope, multiply back by
        // it -- because T_n(a cos t) is the nth harmonic only at a = 1, so
        // anything else is not the mode the control claims to be.
        //
        // And the multiplier is *gated* where the divisor is floored. The Curve
        // generator maps 0 to exactly 0, so a floored multiplier is harmless
        // there; T_even(0) is +/-1, so with a floored multiplier true silence
        // would come out as a -90 dBFS constant that then rides the envelope on
        // the way down. A real zero below the floor is what makes silence in
        // silence out, rather than a slow thump.
        const double chebDivisor = std::max (chebEnvelope_, envelopeFloor_);
        const double chebScale   = chebEnvelope_ > envelopeFloor_ ? chebEnvelope_ : 0.0;

        for (int channel = 0; channel < activeChannels; ++channel)
        {
            const auto c = static_cast<std::size_t> (channel);
            auto& state = channels_[c];

            // Both branches run only while the crossfade is in flight; in steady
            // state one of them is skipped outright, so the generator that is
            // not selected costs a comparison.
            double wet = 0.0;

            if (fade < 1.0)
                wet += (1.0 - fade)
                     * state.generator.process (band[channel] * inverseScale, generator_) * scale;

            if (fade > 0.0)
                wet += fade * chebyshev_.evaluate (band[channel] / chebDivisor) * chebScale;

            // The even half of the generator sits on a DC pedestal by
            // construction, so this is not optional.
            wet = state.dcBlockerB.process (state.dcBlockerA.process (wet));

            if (floorActive_)
                wet = state.floorB.process (state.floorA.process (wet));

            if (ceilingActive_)
                wet = state.ceilingB.process (state.ceilingA.process (wet));

            work[channel][i] = wet * punch;
        }
    }

    oversampler_.downsample (channels, numSamples);

    // ---- recombine at base rate ---------------------------------------------
    //
    // The dry path has been nowhere but a delay line, so at Amount = silence the
    // output is the input to the last bit.
    const int latency = oversampler_.getLatencySamples();

    // The trim from the previous block. Deriving it from this block's energies
    // and applying it to the same block would be circular; at a quarter-second
    // time constant one block of lag is nothing.
    const double trim = (parameters_.autoTrim && ! listen) ? autoTrimGain_ : 1.0;

    double dryEnergy = 0.0;
    double wetEnergy = 0.0;

    const bool stereo = activeChannels == 2;

    for (int i = 0; i < numSamples; ++i)
    {
        const double amount = amountGain_.next();
        const double gain   = outputGain_.next() * trim;
        const double widen  = widthAmount_.next();

        // ---- width, on the harmonics alone ----------------------------------
        //
        // Written as a departure from unity rather than as a mid/side rebuild,
        // because the obvious form is not an identity at the neutral setting:
        // (L+R)/2 + (L-R)/2 rounds twice and does not return L bit for bit. This
        // form multiplies the side by exactly zero at width 1 and adds nothing,
        // so the neutral setting is a true identity.
        //
        // Only the wet path is touched. The dry signal has been nowhere but a
        // delay line, so the sub underneath stays exactly where it was however
        // wide the air gets -- which is the whole reason this control can exist
        // here and cannot on an exciter whose wet path carries a copy of the
        // source.
        if (stereo && widen != 0.0)
        {
            const double left  = channels[0][i];
            const double right = channels[1][i];
            const double side  = 0.5 * (left - right);

            channels[0][i] = left  + widen * side;
            channels[1][i] = right - widen * side;
        }

        for (int channel = 0; channel < activeChannels; ++channel)
        {
            const auto c = static_cast<std::size_t> (channel);
            auto& state = channels_[c];

            state.dryDelay.push (dryInput_[c][static_cast<std::size_t> (i)]);

            const double delayed = state.dryDelay.at (latency);
            const double wet = channels[channel][i] * amount;

            dryEnergy += delayed * delayed;
            wetEnergy += wet * wet;

            channels[channel][i] = (listen ? wet : delayed + wet) * gain;
        }
    }

    // ---- metering and auto trim ---------------------------------------------
    if (wetEnergy <= 0.0)
    {
        // Nothing is being added, so there is nothing to compensate for. Set
        // rather than approach: an exponential that merely tends towards 1
        // would leave the "bypassed from the front panel" case a hair off.
        autoTrimGain_ = 1.0;
        harmonicsDb_  = kAmountSilenceDb;
    }
    else if (dryEnergy > kTrimSilenceEnergy)
    {
        const double ratio = wetEnergy / dryEnergy;
        harmonicsDb_ = 10.0 * std::log10 (ratio);

        // Harmonics and source share no partials, so they sum in power. Undoing
        // that is one square root rather than a loudness model.
        const double target = 1.0 / std::sqrt (1.0 + ratio);
        const double coefficient = 1.0 - std::exp (-static_cast<double> (numSamples)
                                                   / (kAutoTrimSeconds * sampleRate_));
        autoTrimGain_ += coefficient * (target - autoTrimGain_);
    }
}

} // namespace tezla::halo
