#include "CapstoneEngine.hpp"

#include <algorithm>
#include <cmath>

#include <tezla/dsp/Decibels.hpp>
#include <tezla/dsp/Denormals.hpp>

namespace tezla::capstone
{

namespace
{
/// How long a continuous control takes to reach its target. Long enough that
/// nothing zippers, short enough that a fast Ceiling move still feels immediate.
constexpr double kSmoothingSeconds = 0.020;

/// Below this the clip meter reports nothing rather than dividing by a level
/// that is not there. -100 dBFS.
constexpr double kMeterFloor = 1.0e-5;
} // namespace

void Engine::prepare (double sampleRate, int maxBlockSize, int numChannels)
{
    sampleRate_   = sampleRate > 0.0 ? sampleRate : 44100.0;
    maxBlockSize_ = std::max (maxBlockSize, 1);
    numChannels_  = std::clamp (numChannels, 1, kMaxChannels);

    // Sized for the worst case in every direction, because none of it may
    // allocate once the audio thread is running: x8 oversampling, the longest
    // look-ahead, and the strictest true-peak filter.
    oversampler_.prepare (maxBlockSize_, numChannels_, 8);
    limiter_.prepare (sampleRate_, numChannels_);

    maxLatency_ = dsp::Oversampler::latencyForFactor (8)
                + dsp::LimiterCore::maximumLatencySamples (sampleRate_);

    bypass_.prepare (sampleRate_, maxLatency_, numChannels_);

    for (int channel = 0; channel < kMaxChannels; ++channel)
    {
        const auto c = static_cast<std::size_t> (channel);

        channels_[c].drivenDelay.assign (static_cast<std::size_t> (maxLatency_ + 1), 0.0);
        dry_[c].assign (static_cast<std::size_t> (maxBlockSize_), 0.0);
        dryPointers_[c] = dry_[c].data();
    }

    inputGain_.prepare (sampleRate_, kSmoothingSeconds);
    outputGain_.prepare (sampleRate_, kSmoothingSeconds);
    clipGain_.prepare (sampleRate_, kSmoothingSeconds);
    clipShape_.prepare (sampleRate_, kSmoothingSeconds);

    preparedClipFactor_ = 0;
    updateLatency();
    reset();
}

void Engine::reset()
{
    oversampler_.reset();
    limiter_.reset();
    bypass_.reset (parameters_.bypass);

    for (auto& state : channels_)
    {
        state.clipAdaa.reset();
        std::fill (state.drivenDelay.begin(), state.drivenDelay.end(), 0.0);
        state.drivenWrite = 0;
    }

    inputGain_.setCurrentAndTarget (dsp::dbToGain (-parameters_.thresholdDb));
    outputGain_.setCurrentAndTarget (dsp::dbToGain (parameters_.outputDb));
    clipGain_.setCurrentAndTarget (dsp::dbToGain (parameters_.clipThresholdDb));
    clipShape_.setCurrentAndTarget (parameters_.clipShape);

    limiterReductionDb_ = 0.0;
    clipReductionDb_    = 0.0;
    limiterClampExcess_ = 0.0;
}

int Engine::clipFactorFor (const Parameters& parameters) const noexcept
{
    if (! parameters.clipOn)
        return 1;

    return dsp::oversamplingFactor (parameters.clipOversampling, sampleRate_);
}

bool Engine::updateLatency()
{
    const int previous = latency_;

    const int clipLatency = parameters_.clipOn
                          ? dsp::Oversampler::latencyForFactor (oversampler_.getFactor())
                          : 0;

    const int limitLatency = parameters_.limitOn ? limiter_.getLatencySamples() : 0;

    latency_ = std::min (clipLatency + limitLatency, maxLatency_);

    bypass_.setLatency (latency_);

    return latency_ != previous;
}

bool Engine::setParameters (const Parameters& parameters)
{
    parameters_ = parameters;

    inputGain_.setTarget (dsp::dbToGain (-parameters_.thresholdDb));
    outputGain_.setTarget (dsp::dbToGain (parameters_.outputDb));
    clipGain_.setTarget (dsp::dbToGain (parameters_.clipThresholdDb));
    clipShape_.setTarget (parameters_.clipShape);

    limiter_.setCeilingDb (parameters_.ceilingDb);
    limiter_.setKneeDb (parameters_.kneeDb);

    // Look-ahead off is a hard zero rather than a very short attack: the whole
    // point of the switch is that the reported latency reaches exactly zero,
    // and "0.1 ms" would report one or two samples at 192 kHz.
    limiter_.setAttackMs (parameters_.lookaheadOn ? parameters_.attackMs : 0.0);
    limiter_.setHoldMs (parameters_.holdMs);
    limiter_.setReleaseMs (parameters_.releaseMs);
    limiter_.setAutoRelease (parameters_.autoRelease);
    limiter_.setStereoLink (parameters_.stereoLink);
    limiter_.setTruePeakFactor (parameters_.truePeakFactor);

    const int wantedFactor = clipFactorFor (parameters_);

    if (wantedFactor != preparedClipFactor_)
    {
        // Allocation-free -- prepare() built every stage up to x8 precisely so
        // this can happen on the audio thread.
        oversampler_.setFactor (wantedFactor);
        preparedClipFactor_ = wantedFactor;

        for (auto& state : channels_)
            state.clipAdaa.reset();
    }

    bypass_.setBypassed (parameters_.bypass);

    return updateLatency();
}

void Engine::processClip (double* const* channels, int active, int numSamples) noexcept
{
    // The clipper works on x / T and its output is scaled back by T, so the
    // shaper's own +/-1 corner lands exactly on the threshold. Written as
    // x + T * g(x/T) rather than T * f(x/T): g is exactly zero below the
    // corner, so a signal that never reaches the threshold comes out of this
    // untouched to the bit, with no divide-multiply round trip to lose it.
    for (int i = 0; i < numSamples; ++i)
    {
        const double threshold = clipGain_.next();
        clipShaper_.setKnee (clipShape_.next());

        const double inverse = 1.0 / std::max (threshold, 1.0e-9);

        for (int channel = 0; channel < active; ++channel)
        {
            auto& state = channels_[static_cast<std::size_t> (channel)];

            const double x = channels[channel][i];
            const double scaled = x * inverse;

            channels[channel][i] = x + threshold * state.clipAdaa.process (scaled, clipShaper_);
        }
    }
}

void Engine::process (double* const* channels, int numChannels, int numSamples) noexcept
{
    const dsp::ScopedNoDenormals noDenormals;

    if (numSamples <= 0 || numSamples > maxBlockSize_)
        return;

    const int active = std::min (numChannels, numChannels_);

    // ---- the raw input, kept for the bypass crossfade -------------------------

    for (int channel = 0; channel < active; ++channel)
        std::copy_n (channels[channel], numSamples, dry_[static_cast<std::size_t> (channel)].begin());

    // ---- input drive ----------------------------------------------------------
    //
    // The non-finite guard lives here rather than at the output. A NaN from the
    // host would otherwise reach the smoother's running sums and the delay
    // lines, where it stays forever: everything downstream of this point is
    // stateful, and the output is not.

    for (int i = 0; i < numSamples; ++i)
    {
        const double gain = inputGain_.next();

        for (int channel = 0; channel < active; ++channel)
        {
            const double x = channels[channel][i];
            channels[channel][i] = std::isfinite (x) ? x * gain : 0.0;
        }
    }

    // ---- clip -----------------------------------------------------------------

    double clipWorstDb = 0.0;

    if (parameters_.clipOn)
    {
        // Peak in, so the meter can say what the stage removed. Taken before
        // the oversampler rather than inside it, so the number is about the
        // clipper and not about the halfband filters' overshoot.
        double peakIn = 0.0;

        for (int channel = 0; channel < active; ++channel)
            for (int i = 0; i < numSamples; ++i)
                peakIn = std::max (peakIn, std::abs (channels[channel][i]));

        if (oversampler_.getFactor() > 1)
        {
            auto* const* upsampled = oversampler_.upsample (channels, numSamples);
            processClip (upsampled, active, numSamples * oversampler_.getFactor());
            oversampler_.downsample (channels, numSamples);
        }
        else
        {
            processClip (channels, active, numSamples);
        }

        double peakOut = 0.0;

        for (int channel = 0; channel < active; ++channel)
            for (int i = 0; i < numSamples; ++i)
                peakOut = std::max (peakOut, std::abs (channels[channel][i]));

        if (peakIn > kMeterFloor)
            clipWorstDb = std::min (0.0, dsp::gainToDb (peakOut, -100.0)
                                       - dsp::gainToDb (peakIn, -100.0));
    }

    clipReductionDb_ = clipWorstDb;

    // ---- the driven signal, delayed so Listen can subtract ---------------------
    //
    // Captured after the clipper rather than before it, because Listen has to
    // show what *both* stages removed and the clipper has already removed its
    // share by here. Written before the limiter runs, since the limiter works
    // in place.

    for (int channel = 0; channel < active; ++channel)
    {
        auto& state = channels_[static_cast<std::size_t> (channel)];
        const int size = static_cast<int> (state.drivenDelay.size());

        for (int i = 0; i < numSamples; ++i)
        {
            state.drivenDelay[static_cast<std::size_t> (state.drivenWrite)] = channels[channel][i];

            if (++state.drivenWrite >= size)
                state.drivenWrite = 0;
        }
    }

    // ---- limit ----------------------------------------------------------------

    if (parameters_.limitOn)
    {
        limiter_.process (channels, active, numSamples);
        limiterReductionDb_ = limiter_.getGainReductionDb();
        limiterClampExcess_ = limiter_.getClampExcess();
    }
    else
    {
        limiterReductionDb_ = 0.0;
        limiterClampExcess_ = 0.0;
    }

    // ---- Listen, then the output trim -----------------------------------------

    if (parameters_.listen)
    {
        for (int channel = 0; channel < active; ++channel)
        {
            auto& state = channels_[static_cast<std::size_t> (channel)];
            const int size = static_cast<int> (state.drivenDelay.size());

            // Where the sample that entered the limiter numSamples ago is now.
            int read = state.drivenWrite - numSamples - latency_;

            while (read < 0)
                read += size;

            for (int i = 0; i < numSamples; ++i)
            {
                channels[channel][i] = state.drivenDelay[static_cast<std::size_t> (read)]
                                     - channels[channel][i];

                if (++read >= size)
                    read = 0;
            }
        }
    }

    for (int i = 0; i < numSamples; ++i)
    {
        const double gain = outputGain_.next();

        for (int channel = 0; channel < active; ++channel)
            channels[channel][i] *= gain;
    }

    // ---- bypass ----------------------------------------------------------------

    bypass_.process (channels, dryPointers_.data(), active, numSamples);
}

} // namespace tezla::capstone
