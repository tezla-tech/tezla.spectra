#include "SonitusEngine.hpp"

namespace tezla::sonitus {

namespace {

/// Auto uses the house policy from `Oversampler.hpp` -- x4 at 44.1/48 kHz,
/// x2 at 88.2/96, off at 176.4/192 -- which lands every nonlinear stage near
/// 192 kHz internally.
///
/// Anvil overrode this and doubled it, because a cascade of valve stages feeds
/// each one's aliasing into the next. Sonitus has four nonlinearities in series
/// too -- the wave folder, the filter's rail, the tube and the comb's feedback
/// loop -- so the same question had to be asked, and the answer here is
/// different: measured with the swept, non-divisor probes Anvil established,
/// x4 already clears CLAUDE.md section 7's -60 dBFS at every setting the
/// instrument reaches. The figures are in tests/test_Sonitus.cpp.
///
/// The reason it does and Anvil's did not is that the *source* is band-limited
/// here. A polyBLEP saw arrives with its harmonics already rolled off, where an
/// amplifier is handed whatever a guitar and three pedals produced.
[[nodiscard]] int factorFor (dsp::OversamplingMode mode, double sampleRate) noexcept
{
    return dsp::oversamplingFactor (mode, sampleRate);
}

[[nodiscard]] double decibelsToGain (double decibels) noexcept
{
    return std::pow (10.0, decibels / 20.0);
}

} // namespace

void Engine::prepare (double sampleRate, int maxBlockSize)
{
    sampleRate_ = sampleRate > 0.0 ? sampleRate : 48000.0;
    maxBlockSize_ = std::max (maxBlockSize, 1);

    oversampler_.prepare (maxBlockSize_, 2, factorFor (pending_.oversampling, sampleRate_));

    rebuildForRate();

    reset();
}

void Engine::rebuildForRate() noexcept
{
    internalRate_ = sampleRate_ * oversampler_.getFactor();

    // The voices run at the internal rate too, and that is deliberate: they are
    // the loudest source of aliasing in the instrument, so leaving them outside
    // the oversampled section would resample after the damage rather than
    // before it.
    voices_.prepare (internalRate_);

    lfo1_.prepare (internalRate_);
    lfo2_.prepare (internalRate_);
    sequencer_.prepare (internalRate_);

    for (int channel = 0; channel < 2; ++channel)
    {
        split_[channel].prepare (internalRate_);

        // 5 Hz, chosen so it does not thin the sub -- CLAUDE.md section 7. The
        // sub band is the one place in this instrument where a steep high-pass
        // would be actively harmful.
        subBlocker_[channel].prepare (internalRate_, 5.0);

        tube_[channel].prepare (internalRate_);
    }

    comb_.prepare (internalRate_);
    phaser_.prepare (internalRate_);
    formant_.prepare (internalRate_);

    outputGain_.prepare (internalRate_, 0.02);
    tubeGain_.prepare (internalRate_, 0.02);

    configured_ = false;
}

void Engine::reset() noexcept
{
    voices_.reset();

    lfo1_.reset();
    lfo2_.reset();
    sequencer_.reset();

    oversampler_.reset();

    for (int channel = 0; channel < 2; ++channel)
    {
        split_[channel].reset();
        subBlocker_[channel].reset();
        tube_[channel].reset();
        tiltLow_[channel].reset();
        tiltHigh_[channel].reset();
    }

    comb_.reset();
    phaser_.reset();
    formant_.reset();

    outputGain_.setCurrentAndTarget (decibelsToGain (pending_.outputDb));
    tubeGain_.setCurrentAndTarget (1.0);

    sinceControl_ = 0;
    beatsIntoBlock_ = 0.0;
    sources_ = {};
}

void Engine::applyPending() noexcept
{
    const bool tiltChanged = ! configured_ || active_.tilt != pending_.tilt;
    const bool splitChanged = ! configured_ || active_.splitHz != pending_.splitHz;

    active_ = pending_;
    configured_ = true;

    voices_.setMode (active_.keyboard);
    voices_.setPolyphony (active_.polyphony);
    voices_.setGlideSeconds (active_.glideSeconds);

    lfo1_.setWave (active_.lfo1Wave);
    lfo1_.setSmooth (active_.lfo1Smooth);
    lfo2_.setWave (active_.lfo2Wave);
    lfo2_.setRateHz (active_.lfo2RateHz);
    lfo2_.setSmooth (active_.lfo2Smooth);

    sequencer_.setLength (active_.sequencerLength);
    sequencer_.setGlide (active_.sequencerGlide);
    sequencer_.setRateHz (active_.sequencerRateHz);

    for (int step = 0; step < dsp::StepSequencer::kMaxSteps; ++step)
        sequencer_.setStep (step, active_.sequencerSteps[static_cast<std::size_t> (step)]);

    // Guarded, because both of these rebuild filter coefficients and one of
    // them resets state. CLAUDE.md section 7: a setter that clears state must
    // refuse a no-op, and these are called every control chunk.
    if (splitChanged)
        for (auto& split : split_)
            split.setCrossover (active_.splitHz);

    if (tiltChanged)
        updateTilt();

    // Drive in front and a matching trim behind, so the control adds harmonics
    // rather than volume -- the same arrangement Anvil's preamp uses.
    tubeGain_.setTarget (decibelsToGain (std::clamp (active_.tubeDriveDb, 0.0,
                                                     kMaximumTubeDriveDb)));

    outputGain_.setTarget (decibelsToGain (active_.outputDb));

    comb_.setKeyTrack (active_.combKeyTrack);
    comb_.setFeedback (active_.combFeedback);
    comb_.setDamping (active_.combDamping);
    comb_.setSpread (active_.combSpread);
    comb_.setWetInverted (active_.combInverted);
    comb_.setMix (active_.combMode == CombMode::flange ? active_.combMix : 0.0);

    phaser_.setFrequencyHz (active_.phaseFrequencyHz);
    phaser_.setStages (active_.phaseStages);
    phaser_.setFeedback (active_.combFeedback);
    phaser_.setSpread (active_.combSpread);
    phaser_.setWetInverted (active_.combInverted);
    phaser_.setMix (active_.combMode == CombMode::phase ? active_.combMix : 0.0);

    formant_.setMorph (active_.formantMorph);
    formant_.setSharpness (active_.formantSharpness);
    formant_.setMix (active_.formantMix);
}

void Engine::updateTilt() noexcept
{
    // One knob, two shelves moving in opposite directions. A tilt rather than
    // a pair of shelves because it is a *balance*: turning it moves where the
    // energy sits rather than how much of it there is.
    const double decibels = std::clamp (active_.tilt, -1.0, 1.0) * kTiltRangeDb;

    const auto low = dsp::design::lowShelf<double> (kTiltPivotHz, 0.5, -decibels, internalRate_);
    const auto high = dsp::design::highShelf<double> (kTiltPivotHz, 0.5, decibels, internalRate_);

    for (int channel = 0; channel < 2; ++channel)
    {
        tiltLow_[channel].setCoefficients (low);
        tiltHigh_[channel].setCoefficients (high);
    }
}

double Engine::combDelaySeconds() const noexcept
{
    return std::clamp (active_.combTimeMs * 0.001, dsp::Comb::kMinimumSeconds,
                       dsp::Comb::kMaximumSeconds);
}

void Engine::aimComb() noexcept
{
    // The comb's delay and the note it tracks are re-aimed on the control
    // boundary rather than on the parameter change, because both are
    // modulation destinations and the tracked note changes as voices come and
    // go.
    comb_.setDelaySeconds (combDelaySeconds());
    comb_.setNoteHz (voices_.trackedFrequency());
}

void Engine::advanceGlobalSources (int samples) noexcept
{
    // The sequencer first, because it may be driving the LFO's rate -- which is
    // the brief's old automation trick, built in.
    if (transportRunning_)
    {
        // The host reports ppq at the start of the block, so the position has
        // to keep moving inside it: without this a 512-sample block would be
        // one step long however fast the pattern is set.
        sources_.sequencer = sequencer_.setPhaseFromPpq (ppq_ + beatsIntoBlock_,
                                                         active_.sequencerRateHz);
    }
    else
    {
        sources_.sequencer = sequencer_.advance (samples);
    }

    beatsIntoBlock_ += samples / internalRate_ * bpm_ / 60.0;

    // The destination that the brief was reaching for through an automation
    // lane: the sequencer steps the LFO through a pattern of speeds.
    const double octaves = active_.sequencerToLfo1Rate * sources_.sequencer;

    lfo1_.setRateHz (std::clamp (active_.lfo1RateHz * std::pow (2.0, octaves), 0.0, 100.0));

    sources_.lfo1 = lfo1_.advance (samples);
    sources_.lfo2 = lfo2_.advance (samples);
}

void Engine::mangle (double& left, double& right) noexcept
{
    // ---- the split ---------------------------------------------------------

    double subLeft = 0.0;
    double subRight = 0.0;
    double bodyLeft = 0.0;
    double bodyRight = 0.0;

    split_[0].process (left, subLeft, bodyLeft);
    split_[1].process (right, subRight, bodyRight);

    // A DC blocker on the sub and nowhere else. Everything above the split has
    // a nonlinearity in front of it and makes DC by construction; the sub is
    // the one band where the high-pass itself would be audible.
    subLeft = subBlocker_[0].process (subLeft);
    subRight = subBlocker_[1].process (subRight);

    if (active_.subMono)
    {
        const double sum = 0.5 * (subLeft + subRight);

        subLeft = sum;
        subRight = sum;
    }

    // ---- the body ----------------------------------------------------------

    const double drive = tubeGain_.next();

    const auto runTube = [&] ()
    {
        if (dsp::isExactly (drive, 1.0))
            return;

        bodyLeft = tube_[0].process (bodyLeft * drive) / drive;
        bodyRight = tube_[1].process (bodyRight * drive) / drive;
    };

    const auto runComb = [&] ()
    {
        switch (active_.combMode)
        {
            case CombMode::flange: comb_.process (bodyLeft, bodyRight); break;
            case CombMode::phase:  phaser_.process (bodyLeft, bodyRight); break;

            case CombMode::off:
            case CombMode::count:
            default: break;
        }
    };

    if (active_.order == MangleOrder::tubeThenComb)
    {
        runTube();
        runComb();
    }
    else
    {
        runComb();
        runTube();
    }

    formant_.process (bodyLeft, bodyRight);

    bodyLeft = tiltHigh_[0].process (tiltLow_[0].process (bodyLeft));
    bodyRight = tiltHigh_[1].process (tiltLow_[1].process (bodyRight));

    left = subLeft + bodyLeft;
    right = subRight + bodyRight;
}

void Engine::renderChunk (double* left, double* right, int numSamples) noexcept
{
    for (int i = 0; i < numSamples; ++i)
    {
        double sampleLeft = 0.0;
        double sampleRight = 0.0;

        voices_.process (sampleLeft, sampleRight);

        mangle (sampleLeft, sampleRight);

        const double gain = outputGain_.next();

        left[i] = sampleLeft * gain;
        right[i] = sampleRight * gain;
    }
}

void Engine::process (double* const* output, int numSamples) noexcept
{
    const dsp::ScopedNoDenormals guard;

    if (numSamples <= 0)
        return;

    // The oversampling factor is a graph change rather than a control move, so
    // it is taken before anything else and the whole chain is rebuilt if it
    // differs. `prepare()` runs before any parameter is known, which is why
    // this is checked against what was actually built rather than against a
    // "have the parameters arrived" flag -- CLAUDE.md section 7, and the bug
    // that made Emberdrive's oversampling control silently inert on load.
    const int wanted = factorFor (pending_.oversampling, sampleRate_);

    if (wanted != oversampler_.getFactor())
    {
        // **A clean stop, deliberately.** Changing the factor changes the rate
        // every filter's coefficients were computed at and every oscillator's
        // increment was derived from, so the whole graph is rebuilt and none of
        // its state survives. Rebuilding underneath a sounding note leaves the
        // note's bookkeeping alive with all its audio state zeroed, which is a
        // voice that is "playing" silence until it is released.
        //
        // Cutting the notes first makes that an audible, obvious stop instead
        // of a silent one. It is a rare control move -- a CPU decision made
        // while auditioning -- and a stop the player can hear is better than a
        // half-state they cannot.
        allNotesOff();

        oversampler_.setFactor (wanted);
        rebuildForRate();
    }

    applyPending();

    const int factor = oversampler_.getFactor();
    const int internalSamples = numSamples * factor;

    // An instrument has nothing to upsample -- it makes its audio at the
    // internal rate -- so it writes straight into the oversampler's own buffers
    // and only the decimation filters run.
    double* const* internal = oversampler_.internalBuffers();

    beatsIntoBlock_ = 0.0;

    int done = 0;

    while (done < internalSamples)
    {
        // **Cut at the control boundary, not at the callback's.** CLAUDE.md
        // section 7: rebuilding once per block makes the output depend on the
        // host's buffer size, and no arrangement of a per-call timer fixes it.
        // Emberdrive measured 0.296 of full scale between 64- and 512-sample
        // blocks before this was done properly.
        if (sinceControl_ <= 0)
        {
            advanceGlobalSources (Voice::kControlIntervalSamples);
            voices_.advanceGlide (Voice::kControlIntervalSamples);
            voices_.applyControls (active_.voice, sources_);

            aimComb();

            sinceControl_ = Voice::kControlIntervalSamples;
        }

        const int take = std::min (sinceControl_, internalSamples - done);

        renderChunk (internal[0] + done, internal[1] + done, take);

        done += take;
        sinceControl_ -= take;
    }

    oversampler_.downsample (output, numSamples);
}

} // namespace tezla::sonitus
