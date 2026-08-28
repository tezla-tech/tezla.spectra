#include "AnvilEngine.hpp"

#include <algorithm>
#include <cmath>

namespace tezla::anvil
{

namespace
{
constexpr double kSmoothingSeconds = 0.02;

/// The control-rate smoothers are slower, because what they move is a whole
/// network rather than a gain: 50 ms keeps a swept tone control from stepping
/// audibly between one rebuild and the next.
constexpr double kControlSmoothingSeconds = 0.05;

[[nodiscard]] double dbToGain (double db) noexcept
{
    return std::pow (10.0, db / 20.0);
}

[[nodiscard]] dsp::CabinetVoicing voicingFor (CabinetChoice choice) noexcept
{
    switch (choice)
    {
        case CabinetChoice::combo:   return dsp::cabinets::combo();
        case CabinetChoice::british: return dsp::cabinets::modernFourByTwelve();
        case CabinetChoice::vintage: return dsp::cabinets::vintageFourByTwelve();
        case CabinetChoice::none:
        case CabinetChoice::count:
        default:                     return dsp::cabinets::modernFourByTwelve();
    }
}
} // namespace

// ---------------------------------------------------------------------------
// The three lanes
// ---------------------------------------------------------------------------

Engine::VoicingSpec Engine::specFor (Voicing voicing) noexcept
{
    VoicingSpec spec {};

    // Shared starting point: the fitted 12AX7 from Triode.hpp and TriodeStage.hpp.
    spec.triode = dsp::TriodeStageParameters {};

    switch (voicing)
    {
        case Voicing::clean:
            // One valve and then the stack, so the tone controls are shaping
            // what you hear rather than what gets distorted. A large
            // transformer, a stiff supply and a lot of feedback: this lane has
            // to be able to get out of the way entirely.
            spec.stages = 1;
            spec.toneStackAfter = 1;
            spec.stack = dsp::ToneStackVoicing::american;
            spec.inputScale = 0.30;      // a clean first stage runs well inside its knee
            spec.stageGain = 1.0;
            spec.powerDrive = 2.2;
            spec.makeupDb = -5.9;

            spec.power.knee = 0.85;           // softer valves, later corner
            spec.power.crossoverDepth = 0.08; // close to class A
            spec.power.crossoverWidth = 0.03;
            spec.power.sagDepth = 0.10;       // solid-state rectifier
            spec.power.sagMs = 25.0;
            // Just under the loop's stability bound, which is what the
            // presence and resonance controls are worth: 20*log10(1.85) =
            // 5.3 dB of negative feedback for them to shunt away. See
            // PowerAmp::kMaximumLoopGain -- 0.60 gave 4.1 dB and a control
            // nobody could hear.
            spec.power.feedback = 0.85;
            spec.power.presenceHz = 900.0;
            spec.power.resonanceHz = 150.0;
            spec.power.transformerLowHz = 26.0;
            spec.power.transformerHighHz = 11000.0;
            spec.power.coreFrequencyHz = 32.0;

            // A bypassed cathode and a big coupling capacitor: neither blocking
            // nor shifting much, because neither is wanted here.
            spec.triode.biasDepth = 0.18;
            spec.triode.gridThreshold = 2.2;
            spec.triode.couplingHz = 8.0;
            spec.triode.gridStopperHz = 55000.0;   // a small stopper: one stage needs little
            spec.triode.plateCornerHz = 48000.0;   // and a wide plate: this lane wants the air
            break;

        case Voicing::vintage:
            // Two valves with the stack between them, almost no feedback, and a
            // valve rectifier. The lane that moves under you.
            spec.stages = 2;
            spec.toneStackAfter = 1;
            spec.stack = dsp::ToneStackVoicing::british;
            spec.inputScale = 0.75;
            spec.stageGain = 3.2;
            spec.powerDrive = 4.0;
            spec.makeupDb = -3.3;

            spec.power.knee = 0.7;
            spec.power.crossoverDepth = 0.20;
            spec.power.crossoverWidth = 0.04;
            spec.power.sagDepth = 0.32;       // a valve rectifier, and it shows
            spec.power.sagMs = 55.0;
            // The shallowest loop of the three, because this lane is meant to
            // move under the player -- but 0.15 was too shallow to have a
            // presence control at all: 1.2 dB of negative feedback is 1.2 dB
            // of presence. 0.70 is 4.6 dB, still visibly the loosest lane, and
            // enough for the shunts to have something to shunt.
            spec.power.feedback = 0.70;
            spec.power.presenceHz = 700.0;
            spec.power.resonanceHz = 180.0;
            spec.power.transformerLowHz = 34.0;
            spec.power.transformerHighHz = 8500.0;
            spec.power.coreFrequencyHz = 58.0;   // small enough to feel it

            spec.triode.biasDepth = 0.40;
            spec.triode.biasMs = 38.0;
            spec.triode.gridThreshold = 1.4;
            spec.triode.gridRecoveryMs = 26.0;
            spec.triode.gridStopperHz = 34000.0;
            spec.triode.plateCornerHz = 38000.0;
            break;

        case Voicing::modern:
        case Voicing::count:
        default:
            // Three valves with the stack after the second, scooped hard before
            // the last one. A tight supply and a stiff transformer, because
            // articulation at high gain is the whole point of this lane.
            spec.stages = 3;
            spec.toneStackAfter = 2;
            spec.stack = dsp::ToneStackVoicing::modern;
            spec.inputScale = 1.10;
            spec.stageGain = 4.6;
            spec.powerDrive = 5.0;
            spec.makeupDb = -5.5;

            spec.power.knee = 0.62;
            spec.power.crossoverDepth = 0.22;
            spec.power.crossoverWidth = 0.035;
            spec.power.sagDepth = 0.16;
            spec.power.sagMs = 20.0;
            // 0.32 was 2.4 dB of negative feedback. 0.88 is 5.5 dB, and this
            // lane wants the tightest loop of the three anyway.
            spec.power.feedback = 0.88;
            spec.power.presenceHz = 1100.0;
            spec.power.resonanceHz = 110.0;
            spec.power.transformerLowHz = 30.0;
            spec.power.transformerHighHz = 9500.0;
            spec.power.coreFrequencyHz = 40.0;

            spec.triode.biasDepth = 0.30;
            spec.triode.biasMs = 26.0;
            spec.triode.gridThreshold = 1.5;
            spec.triode.gridRecoveryMs = 18.0;
            spec.triode.couplingHz = 16.0;    // tighter, so the sub stays clean
            spec.triode.gridStopperHz = 22000.0;  // the classic 68k high-gain stopper
            spec.triode.plateCornerHz = 28000.0;
            break;
    }

    return spec;
}

// ---------------------------------------------------------------------------

void Engine::prepare (double sampleRate, int maxBlockSize, int numChannels)
{
    sampleRate_ = sampleRate > 0.0 ? sampleRate : 44100.0;
    maxBlockSize_ = std::max (maxBlockSize, 1);
    numChannels_ = std::clamp (numChannels, 1, kMaxChannels);

    // Every stage the engine can ever build, sized for the worst case, so
    // switching a factor or a voicing later never allocates.
    oversampler_.prepare (maxBlockSize_, numChannels_, 8);

    const int worstLatency = dsp::Oversampler::latencyForFactor (8);

    bypass_.prepare (sampleRate_, worstLatency, numChannels_);

    for (auto& channel : dry_)
        channel.assign (static_cast<std::size_t> (maxBlockSize_), 0.0);

    for (int c = 0; c < kMaxChannels; ++c)
    {
        dryPointers_[static_cast<std::size_t> (c)] = dry_[static_cast<std::size_t> (c)].data();
        channels_[static_cast<std::size_t> (c)].dryDelay.prepare (worstLatency + 2);
    }

    inputGain_.prepare (sampleRate_, kSmoothingSeconds);
    stageGain_.prepare (sampleRate_, kSmoothingSeconds);
    masterGain_.prepare (sampleRate_, kSmoothingSeconds);
    outputGain_.prepare (sampleRate_, kSmoothingSeconds);
    mix_.prepare (sampleRate_, kSmoothingSeconds);

    // The control-rate smoothers run once per interval, so preparing them at
    // the control rate keeps their time constants in seconds -- and identical
    // at every host rate and every block size.
    const double controlRate = sampleRate_ / kControlIntervalSamples;

    for (auto* smoother : { &bass_, &middle_, &treble_, &presence_, &resonance_,
                            &sag_, &coreHz_, &damping_, &micPosition_, &micDistance_ })
        smoother->prepare (controlRate, kControlSmoothingSeconds);

    preparedFactor_ = 0;      // nothing configured yet -- see the header
    configure();
    reset();
}

void Engine::reset()
{
    oversampler_.reset();
    bypass_.reset (parameters_.bypass);

    for (auto& channel : channels_)
    {
        for (auto& stage : channel.stages)
            stage.reset();

        channel.phaseInverter.reset();
        channel.toneStack.reset();
        channel.power.reset();
        channel.speaker.reset();
        channel.cabinet.reset();
        channel.dryDelay.reset();
    }

    for (auto& channel : dry_)
        std::fill (channel.begin(), channel.end(), 0.0);

    const auto spec = specFor (parameters_.voicing);

    inputGain_.setCurrentAndTarget (dbToGain (parameters_.gainDb) * spec.inputScale);
    stageGain_.setCurrentAndTarget (spec.stageGain);
    masterGain_.setCurrentAndTarget (dbToGain (std::min (parameters_.masterDb, 0.0)));
    outputGain_.setCurrentAndTarget (dbToGain (parameters_.outputDb + spec.makeupDb));
    mix_.setCurrentAndTarget (std::clamp (parameters_.mix, 0.0, 1.0));

    bass_.setCurrentAndTarget (parameters_.bass);
    middle_.setCurrentAndTarget (parameters_.middle);
    treble_.setCurrentAndTarget (parameters_.treble);
    presence_.setCurrentAndTarget (parameters_.presence);
    resonance_.setCurrentAndTarget (parameters_.resonance);
    sag_.setCurrentAndTarget (parameters_.sag);
    coreHz_.setCurrentAndTarget (parameters_.coreHz);
    damping_.setCurrentAndTarget (parameters_.damping);
    micPosition_.setCurrentAndTarget (parameters_.micPosition);
    micDistance_.setCurrentAndTarget (parameters_.micDistanceCm);

    controlCountdown_ = 0;
    applyControls();

    sagMeter_ = 0.0;
    fluxMeter_ = 0.0;
    biasMeter_ = 0.0;
}

int Engine::autoFactorFor (double sampleRate) noexcept
{
    if (sampleRate <= 50000.0)  return 8;
    if (sampleRate <= 100000.0) return 4;
    if (sampleRate <= 200000.0) return 2;
    return 1;
}

int Engine::factorFor (const Parameters& parameters) const noexcept
{
    return parameters.oversampling == dsp::OversamplingMode::Auto
             ? autoFactorFor (sampleRate_)
             : dsp::oversamplingFactor (parameters.oversampling, sampleRate_);
}

bool Engine::updateLatency()
{
    const int wanted = oversampler_.getLatencySamples();

    if (wanted == latency_)
        return false;

    latency_ = wanted;
    bypass_.setLatency (latency_);
    return true;
}

void Engine::configure()
{
    // The internal rate, which is what every stage is designed against. This is
    // the whole of CLAUDE.md section 6 in one line: nothing below is told the
    // host's rate, so nothing below changes character with it.
    const double internalRate = sampleRate_ * oversampler_.getFactor();

    for (int c = 0; c < kMaxChannels; ++c)
    {
        auto& channel = channels_[static_cast<std::size_t> (c)];

        for (auto& stage : channel.stages)
            stage.prepare (internalRate);

        channel.phaseInverter.prepare (internalRate);
        channel.toneStack.prepare (internalRate);
        channel.power.prepare (internalRate);
        channel.speaker.prepare (internalRate);
        channel.cabinet.prepare (internalRate);
    }
}

void Engine::applyControls()
{
    const auto spec = specFor (parameters_.voicing);

    stageCount_ = std::clamp (spec.stages + parameters_.extraStages, 1, kMaxStages);

    // The stack keeps its place in the voicing's own order even when extra
    // valves are added, and the extra ones go on the end -- so turning Stages up
    // cascades more distortion *after* the tone controls rather than silently
    // moving them.
    toneStackAfter_ = std::min (spec.toneStackAfter, stageCount_);

    auto power = spec.power;
    power.drive = spec.powerDrive;

    // The loop gain, which is the whole of what presence and resonance have to
    // work with -- and which PowerAmp clamps to kMaximumLoopGain.
    loopGain_ = std::min (power.feedback, dsp::PowerAmp::kMaximumLoopGain);
    power.presence = std::clamp (presence_.getCurrent(), 0.0, 1.0);
    power.resonance = std::clamp (resonance_.getCurrent(), 0.0, 1.0);
    power.sagDepth = std::clamp (spec.power.sagDepth * sag_.getCurrent(), 0.0, 1.0);
    power.coreFrequencyHz = std::clamp (coreHz_.getCurrent(), kMinimumCoreHz, kMaximumCoreHz);

    dsp::SpeakerLoadParameters speaker;
    speaker.dampingFactor = std::clamp (damping_.getCurrent(), kMinimumDamping, kMaximumDamping);

    dsp::CabinetParameters cabinet;
    cabinet.voicing = voicingFor (parameters_.cabinet);
    cabinet.micPosition = std::clamp (micPosition_.getCurrent(), 0.0, 1.0);
    cabinet.micDistanceMetres = std::clamp (micDistance_.getCurrent(),
                                            kMinimumMicCm, kMaximumMicCm) * 0.01;
    cabinet.bypassed = parameters_.cabinet == CabinetChoice::none;

    for (int c = 0; c < kMaxChannels; ++c)
    {
        auto& channel = channels_[static_cast<std::size_t> (c)];

        // Every setter here is guarded against a no-op and preserves state.
        // None of them is prepare(). CLAUDE.md section 7.
        for (auto& stage : channel.stages)
            stage.setParameters (spec.triode);

        // The phase inverter is a valve like any other, run with a wider grid
        // stopper: it is fed from a low impedance and has no cascade in front
        // of it to keep quiet.
        auto inverter = spec.triode;
        inverter.gridStopperHz = 60000.0;
        inverter.biasDepth *= 0.5;      // cathodyne, and less of a bias shift
        channel.phaseInverter.setParameters (inverter);

        channel.toneStack.setVoicing (spec.stack);
        channel.toneStack.setControls (bass_.getCurrent(), middle_.getCurrent(),
                                       treble_.getCurrent());

        channel.power.setParameters (power);
        channel.speaker.setParameters (speaker);
        channel.cabinet.setParameters (cabinet);
    }

    // The preamp's drive, and the makeup that pays for the passive stack's
    // insertion loss and the valves' own compression. Without it, turning Gain
    // up would quietly turn the plugin down.
    inputGain_.setTarget (dbToGain (parameters_.gainDb) * spec.inputScale);
    stageGain_.setTarget (spec.stageGain);

    // The master is an attenuator, in front of the phase inverter, exactly
    // where the potentiometer is. The makeup that pays for the passive stack's
    // insertion loss and the valves' compression goes on the *output* -- in
    // front of the output stage it would not be makeup, it would be drive, and
    // that is what put a hundredfold gain into a place that cannot take one.
    masterGain_.setTarget (dbToGain (std::min (parameters_.masterDb, 0.0)));
    outputGain_.setTarget (dbToGain (parameters_.outputDb + spec.makeupDb));
    mix_.setTarget (std::clamp (parameters_.mix, 0.0, 1.0));
}

void Engine::advanceControls() noexcept
{
    bass_.next();
    middle_.next();
    treble_.next();
    presence_.next();
    resonance_.next();
    sag_.next();
    coreHz_.next();
    damping_.next();
    micPosition_.next();
    micDistance_.next();
}

bool Engine::setParameters (const Parameters& parameters)
{
    if (parameters == parameters_ && preparedFactor_ != 0)
        return false;

    const bool factorMoved = factorFor (parameters) != oversampler_.getFactor()
                          || preparedFactor_ == 0;

    parameters_ = parameters;

    if (factorMoved)
    {
        oversampler_.setFactor (factorFor (parameters_));
        preparedFactor_ = oversampler_.getFactor();

        // The factor changes the internal rate, so every coefficient in the
        // chain has to be rebuilt against it -- and that means prepare(), and
        // that means a reset. Unavoidable and correct: the states belong to the
        // old rate. The bypass crossfade covers the discontinuity.
        configure();
        reset();
    }

    // Targets only. The smoothers walk to them at the control rate.
    bass_.setTarget (parameters_.bass);
    middle_.setTarget (parameters_.middle);
    treble_.setTarget (parameters_.treble);
    presence_.setTarget (parameters_.presence);
    resonance_.setTarget (parameters_.resonance);
    sag_.setTarget (parameters_.sag);
    coreHz_.setTarget (parameters_.coreHz);
    damping_.setTarget (parameters_.damping);
    micPosition_.setTarget (parameters_.micPosition);
    micDistance_.setTarget (parameters_.micDistanceCm);

    applyControls();

    bypass_.setBypassed (parameters_.bypass);

    return updateLatency();
}

void Engine::process (double* const* channels, int numChannels, int numSamples) noexcept
{
    if (numSamples <= 0)
        return;

    const int active = std::min (numChannels, numChannels_);

    // The untouched input, for Mix and for the bypass crossfade.
    for (int c = 0; c < active; ++c)
        std::copy (channels[c], channels[c] + numSamples,
                   dry_[static_cast<std::size_t> (c)].begin());

    processOversampled (channels, active, numSamples);

    // Mix, against a dry signal delayed by exactly the reported latency. The
    // oversampler's round trip is a whole number of base-rate samples by
    // design, so this is an integer delay and the dry path is bit-exact --
    // which is what makes Mix at 0 the input rather than a filtered copy of it.
    for (int i = 0; i < numSamples; ++i)
    {
        const double wet = mix_.next();

        for (int c = 0; c < active; ++c)
        {
            auto& state = channels_[static_cast<std::size_t> (c)];
            const auto index = static_cast<std::size_t> (i);

            state.dryDelay.push (dry_[static_cast<std::size_t> (c)][index]);

            if (wet < 1.0)
            {
                const double delayed = state.dryDelay.at (latency_);
                channels[c][i] = wet * channels[c][i] + (1.0 - wet) * delayed;
            }
        }
    }

    bypass_.process (channels, dryPointers_.data(), active, numSamples);
}

void Engine::processOversampled (double* const* channels, int active, int numSamples) noexcept
{
    auto* const* up = oversampler_.upsample (channels, numSamples);

    int done = 0;

    while (done < numSamples)
    {
        if (controlCountdown_ <= 0)
        {
            advanceControls();
            applyControls();
            controlCountdown_ = kControlIntervalSamples;
        }

        const int chunk = std::min (numSamples - done, controlCountdown_);

        processChunk (up, active, done, chunk);

        controlCountdown_ -= chunk;
        done += chunk;
    }

    oversampler_.downsample (channels, numSamples);

    double sag = 0.0;
    double flux = 0.0;
    double bias = 0.0;

    for (int c = 0; c < active; ++c)
    {
        const auto& state = channels_[static_cast<std::size_t> (c)];

        sag = std::max (sag, state.power.getSag());
        flux = std::max (flux, std::abs (state.power.getFlux()));
        bias = std::max (bias, std::abs (state.stages[0].getBiasShift()));
    }

    sagMeter_ = sag;
    fluxMeter_ = flux;
    biasMeter_ = bias;
}

void Engine::processChunk (double* const* oversampled, int active, int from, int numSamples) noexcept
{
    const int factor = oversampler_.getFactor();

    // Samples outside, channels inside. The gain smoothers advance once per
    // base-rate sample and both channels see the same value, which is the only
    // way a stereo pair stays a stereo pair while a control moves.
    for (int i = 0; i < numSamples; ++i)
    {
        const double input = inputGain_.next();
        const double stage = stageGain_.next();
        const double master = masterGain_.next();
        const double output = outputGain_.next();

        const int base = (from + i) * factor;

        for (int c = 0; c < active; ++c)
        {
            auto& state = channels_[static_cast<std::size_t> (c)];
            double* samples = oversampled[c];

            for (int k = 0; k < factor; ++k)
            {
                double x = samples[base + k] * input;

                for (int s = 0; s < stageCount_; ++s)
                {
                    x = state.stages[static_cast<std::size_t> (s)].process (x);

                    if (s + 1 == toneStackAfter_)
                        x = state.toneStack.process (x);

                    if (s + 1 < stageCount_)
                        x *= stage;
                }

                x = state.phaseInverter.process (x * master);
                x = state.power.process (x);
                x = state.speaker.process (x);
                x = state.cabinet.process (x);

                samples[base + k] = x * output;
            }
        }
    }
}

} // namespace tezla::anvil
