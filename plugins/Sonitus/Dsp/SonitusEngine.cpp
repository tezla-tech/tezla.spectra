// Copyright (c) 2026 The Tezla <thetezla@proton.me>
// Created by The Tezla -- https://github.com/wingit33/tezla.tech
// Music: https://soundcloud.com/thetezla | https://thetezla.bandcamp.com
// Built with development assistance from Claude (Anthropic).
// SPDX-License-Identifier: AGPL-3.0-only
// GNU AGPLv3 (see LICENSE), plus NOTICE.md's attribution term. Keep intact.

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

    oversampler_.prepare (maxBlockSize_, 2, factorFor (effectiveOversampling(), sampleRate_));

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
        fullBlocker_[channel].prepare (internalRate_, 5.0);

        tube_[channel].prepare (internalRate_);
    }

    comb_.prepare (internalRate_);
    phaser_.prepare (internalRate_);
    formant_.prepare (internalRate_);

    outputGain_.prepare (internalRate_, 0.02);
    tubeGain_.prepare (internalRate_, 0.02);
    splitMix_.prepare (internalRate_, 0.03);

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
        fullBlocker_[channel].reset();
        tube_[channel].reset();
        tiltLow_[channel].reset();
        tiltHigh_[channel].reset();
    }

    comb_.reset();
    phaser_.reset();
    formant_.reset();

    outputGain_.setCurrentAndTarget (decibelsToGain (pending_.outputDb));
    tubeGain_.setCurrentAndTarget (1.0);
    splitMix_.setCurrentAndTarget (pending_.subSplit ? 1.0 : 0.0);

    sinceControl_ = 0;
    seenNoteOns_ = voices_.getNoteOnCount();
    beatsIntoBlock_ = 0.0;
    sources_ = {};
    combModulation_ = 1.0;
    idleSamples_ = 0;
}

void Engine::applyPending() noexcept
{
    const bool tiltChanged = ! configured_ || ! dsp::isExactly (active_.tilt, pending_.tilt);
    const bool splitChanged = ! configured_ || ! dsp::isExactly (active_.splitHz, pending_.splitHz);

    active_ = pending_;
    configured_ = true;

    voices_.setMode (active_.keyboard);
    voices_.setPolyphony (active_.polyphony);
    voices_.setGlideSeconds (active_.glideSeconds);

    lfo1_.setWave (active_.lfo1Wave);
    lfo1_.setSmooth (active_.lfo1Smooth);
    lfo2_.setWave (active_.lfo2Wave);
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

    // A target, not a jump: the SPLIT switch is a routing change, and a
    // routing change lands as a 30 ms crossfade -- CLAUDE.md section 7 puts
    // discrete switches behind crossfades. Setting an unchanged target is a
    // natural no-op, so this needs no guard.
    splitMix_.setTarget (active_.subSplit ? 1.0 : 0.0);

    if (tiltChanged)
        updateTilt();

    // The unmodulated half of the mangle: the controls no global slot can
    // reach, so once per block is exactly often enough. Everything the matrix
    // *can* drive is set in `applyGlobalModulation` instead, on the control
    // boundary -- setting it here as well would write the unmodulated value
    // over the modulated one once per callback, which is a buffer-size
    // dependence rather than a control.
    comb_.setKeyTrack (active_.combKeyTrack);
    comb_.setDamping (active_.combDamping);
    comb_.setSpread (active_.combSpread);
    comb_.setWetInverted (active_.combInverted);

    phaser_.setStages (active_.phaseStages);
    phaser_.setSpread (active_.combSpread);
    phaser_.setWetInverted (active_.combInverted);

    formant_.setSharpness (active_.formantSharpness);
    formant_.setMix (active_.formantMix);
    formant_.setHarmonicLock (active_.formantLock);
}

double Engine::globalModulationFor (GlobalDestination destination) const noexcept
{
    double total = 0.0;

    for (const auto& slot : active_.globalSlots)
    {
        if (slot.destination != destination || slot.source == GlobalSource::none)
            continue;

        double value = 0.0;

        switch (slot.source)
        {
            case GlobalSource::lfo1:      value = sources_.lfo1; break;
            case GlobalSource::lfo2:      value = sources_.lfo2; break;
            case GlobalSource::sequencer: value = sources_.sequencer; break;

            case GlobalSource::macro1:    value = sources_.macros[0]; break;
            case GlobalSource::macro2:    value = sources_.macros[1]; break;
            case GlobalSource::macro3:    value = sources_.macros[2]; break;
            case GlobalSource::macro4:    value = sources_.macros[3]; break;

            // The tracked note -- the same one the comb and the formant
            // follow, so the whole mangle moves with one note rather than three
            // stages each picking their own. Nothing sounding reads zero, which
            // is what a closed envelope is.
            case GlobalSource::ampEnvelope:
            case GlobalSource::modEnvelope1:
            case GlobalSource::modEnvelope2:
            case GlobalSource::velocity:
            case GlobalSource::advEnv1:
            case GlobalSource::advEnv2:
            case GlobalSource::advEnv3:
            {
                const Voice* voice = voices_.trackedVoice();

                if (voice == nullptr)
                    break;

                switch (slot.source)
                {
                    case GlobalSource::ampEnvelope:  value = voice->getAmpLevel(); break;
                    case GlobalSource::modEnvelope1: value = voice->getModEnvelopeLevel (0); break;
                    case GlobalSource::modEnvelope2: value = voice->getModEnvelopeLevel (1); break;
                    case GlobalSource::velocity:     value = voice->getVelocity(); break;

                    case GlobalSource::advEnv1:      value = voice->getAdvLevel (0); break;
                    case GlobalSource::advEnv2:      value = voice->getAdvLevel (1); break;
                    case GlobalSource::advEnv3:      value = voice->getAdvLevel (2); break;

                    // Unreachable -- the outer switch has already sorted these
                    // out -- but listed rather than defaulted, so a source
                    // added to the enum and forgotten here stops the build.
                    case GlobalSource::none:
                    case GlobalSource::lfo1:
                    case GlobalSource::lfo2:
                    case GlobalSource::sequencer:
                    case GlobalSource::macro1:
                    case GlobalSource::macro2:
                    case GlobalSource::macro3:
                    case GlobalSource::macro4:
                    case GlobalSource::count:
                    default: break;
                }

                break;
            }

            case GlobalSource::none:
            case GlobalSource::count:
            default: break;
        }

        // Squared, like the voice matrix's -- one law for both, so a depth
        // knob reads the same wherever it is.
        total += shapedDepth (slot.depth) * value;
    }

    return total;
}

void Engine::applyGlobalModulation() noexcept
{
    // Scaled into each destination's own units. The two that are frequencies
    // move in **octaves**, because a comb delay and a filter centre are pitches
    // in disguise: an additive sweep would crawl at the bottom of the range and
    // leap at the top, which is the wrong shape for the thing being swept.
    // Deliberately extreme, and usable because the depth law is squared: the
    // comb's own range is about ten octaves, so six of them is most of it.
    static constexpr double kCombOctaves = 6.0;
    static constexpr double kPhaseOctaves = 6.0;
    static constexpr double kTubeDecibels = 36.0;   // the control's own maximum

    // The one that stays where it was. Output is a *level*, and thirty-six
    // decibels of it swinging under an envelope is a hazard rather than a
    // sound -- everything else here changes timbre, this changes how loud the
    // instrument is in somebody's mix.
    static constexpr double kOutputDecibels = 24.0;

    // Drive in front and a matching trim behind, so the control adds harmonics
    // rather than volume -- the same arrangement Anvil's preamp uses.
    const double tubeDb = std::clamp (
        active_.tubeDriveDb + kTubeDecibels * globalModulationFor (GlobalDestination::tubeDrive),
        0.0, kMaximumTubeDriveDb);

    tubeGain_.setTarget (decibelsToGain (tubeDb));

    outputGain_.setTarget (decibelsToGain (
        active_.outputDb + kOutputDecibels * globalModulationFor (GlobalDestination::output)));

    const double feedback = std::clamp (
        active_.combFeedback + globalModulationFor (GlobalDestination::combFeedback), -1.0, 1.0);

    const double mix = std::clamp (
        active_.combMix + globalModulationFor (GlobalDestination::combMix), 0.0, 1.0);

    comb_.setFeedback (feedback);
    comb_.setMix (active_.combMode == CombMode::flange ? mix : 0.0);

    phaser_.setFeedback (feedback);
    phaser_.setMix (active_.combMode == CombMode::phase ? mix : 0.0);

    phaser_.setFrequencyHz (active_.phaseFrequencyHz
        * std::pow (2.0, kPhaseOctaves * globalModulationFor (GlobalDestination::phaseFrequency)));

    formant_.setMorph (std::clamp (
        active_.formantMorph + globalModulationFor (GlobalDestination::formantMorph), 0.0, 1.0));

    // Which partial the lock selects. Additive in *harmonic number* rather than
    // in octaves, because the harmonic series is what it walks: a depth of 1
    // reaches sixteen partials, which at full is two octaves of overtone line.
    // The whole 1..24 range, reachable from anywhere in it.
    static constexpr double kHarmonicSwing = 23.0;

    formant_.setHarmonic (active_.formantHarmonic
                            + kHarmonicSwing * globalModulationFor (GlobalDestination::formantHarmonic));

    // The anti-formant moves in octaves, like every other frequency here.
    static constexpr double kNotchOctaves = 6.0;

    formant_.setNotchHz (active_.formantNotchHz
                           * std::pow (2.0, kNotchOctaves
                                              * globalModulationFor (GlobalDestination::formantNotch)));

    formant_.setNotchDepth (active_.formantNotchDepth);

    // The comb's own delay, which is what this whole section is for. Negative
    // modulation is *up* in frequency, because the delay and the notch are
    // reciprocal -- a positive sweep on a comb should raise the notch, which is
    // what a player expects and the opposite of what the delay does.
    combModulation_ = std::pow (2.0,
        -kCombOctaves * globalModulationFor (GlobalDestination::combTime));
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
    return std::clamp (active_.combTimeMs * 0.001 * combModulation_, dsp::Comb::kMinimumSeconds,
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

    // **Scale lock**, applied here because this is the only place that knows
    // both numbers: the comb knows where it resonates and the tuning knows
    // what pitches exist. The comb stays framework-free and is handed a plain
    // ratio (`setTuningRatio`), which at exactly 1.0 costs one multiplication
    // and changes nothing.
    //
    // The ratio is worked out from the delay the comb has *already* settled
    // on -- key tracking, modulation and all -- rather than from the knob,
    // because the knob is wrong whenever anything is sweeping it, which in
    // this instrument is most of the time. Same argument as the notch readout
    // two lines down.
    if (active_.combScaleLock)
    {
        comb_.setTuningRatio (1.0);

        const double samples = comb_.currentDelaySamples();

        if (samples > 0.0)
        {
            const double resonant = internalRate_ / samples;
            const double snapped = voices_.tuning().nearestScaleHz (resonant);

            // Delay and frequency are reciprocals, so the ratio between the
            // two pitches is the reciprocal of the ratio between the delays.
            if (snapped > 0.0)
                comb_.setTuningRatio (resonant / snapped);
        }
    }
    else
    {
        comb_.setTuningRatio (1.0);
    }

    // The same tracked note the comb uses. The two lock to the same thing by
    // construction -- the comb onto the note's period, the formant onto its
    // harmonics -- which is what makes them agree rather than beat.
    formant_.setNoteHz (voices_.trackedFrequency());

    // Published where the panel can read it without racing the audio thread.
    // This is the boundary the comb is actually aimed on, so it is also the
    // only place the figure is true.
    readouts_.combNotchHz.store (comb_.firstNotchHz(), std::memory_order_relaxed);
    readouts_.sequencerStep.store (sequencer_.getStepIndex(), std::memory_order_relaxed);
    readouts_.lfo1.store (sources_.lfo1, std::memory_order_relaxed);
    readouts_.lfo2.store (sources_.lfo2, std::memory_order_relaxed);
    readouts_.sequencer.store (sources_.sequencer, std::memory_order_relaxed);

    // The same tracked voice again. When nothing is sounding the envelopes read
    // zero rather than holding their last value, which is the honest answer: an
    // idle instrument's envelope is not paused part-way up, it is over.
    if (const Voice* voice = voices_.trackedVoice())
    {
        readouts_.envelopeLevels[0].store (voice->getAmpLevel(), std::memory_order_relaxed);
        readouts_.envelopeLevels[1].store (voice->getModEnvelopeLevel (0), std::memory_order_relaxed);
        readouts_.envelopeLevels[2].store (voice->getModEnvelopeLevel (1), std::memory_order_relaxed);
    }
    else
    {
        for (auto& level : readouts_.envelopeLevels)
            level.store (0.0, std::memory_order_relaxed);
    }
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

    // **Retrigger.** A note-on the LFOs have not seen yet restarts whichever of
    // them asked for it. Free-running is right for a pad, where the movement is
    // ambient; it is exactly wrong for a bass line, where every note would
    // otherwise land on a different part of the cycle.
    const auto noteOns = voices_.getNoteOnCount();

    if (noteOns != seenNoteOns_)
    {
        seenNoteOns_ = noteOns;

        if (active_.lfo1Retrigger) lfo1_.reset();
        if (active_.lfo2Retrigger) lfo2_.reset();

        // **The fade restarts on every note, retriggered or not.** The two
        // controls are separate ideas -- one is about the waveform's phase and
        // the other about its depth -- and tying the fade to the retrigger
        // switch would give a knob that silently did nothing half the time.
        lfo1Fade_ = 0.0;
        lfo2Fade_ = 0.0;
    }

    // The fade itself. Raised cosine rather than a straight ramp, so it arrives
    // without a corner: a linear fade reaches full depth and stops dead, which
    // on a slow vibrato is audible as the moment the wobble "switches on".
    const auto faded = [samples, this] (double& progress, double seconds)
    {
        if (seconds <= 0.0)
        {
            progress = 1.0;
            return 1.0;
        }

        progress = std::min (1.0, progress + samples / (seconds * internalRate_));

        return 0.5 - 0.5 * std::cos (std::numbers::pi * progress);
    };

    const double lfo1Depth = faded (lfo1Fade_, active_.lfo1AttackSeconds);
    const double lfo2Depth = faded (lfo2Fade_, active_.lfo2AttackSeconds);

    // **Key tracking.** At full, the rate is proportional to the played note,
    // so an octave up is twice the speed. The beating that gives a reese its
    // character is a fraction of the note rather than a fixed number of hertz,
    // so a wobble that does not track becomes a different sound as the line
    // moves up the keyboard.
    //
    // Referred to middle C, so the control is neutral around where a bass line
    // actually sits rather than at some arbitrary corner of the range.
    const double tracked = voices_.trackedFrequency();

    const auto keyTracked = [tracked] (double rate, double amount)
    {
        if (amount <= 0.0 || tracked <= 0.0)
            return rate;

        static constexpr double kReferenceHz = 261.6255653005986;   // C4

        return rate * std::pow (tracked / kReferenceHz, amount);
    };

    // The destination that the brief was reaching for through an automation
    // lane: the sequencer steps the LFO through a pattern of speeds.
    const double octaves = active_.sequencerToLfo1Rate * sources_.sequencer;

    // **The ceiling is the control rate's, not a round number.** These sources
    // are read once every `kControlIntervalSamples`, so the fastest LFO that
    // means what it says is half of that -- above it the output aliases to some
    // other frequency and the knob becomes a lie. Deriving it rather than
    // picking one matters because the control rate moves with the oversampling
    // factor: 3000 Hz at 48 kHz x4, but 689 Hz at 44.1 kHz with oversampling
    // off, and a fixed constant would be wrong at one end or the other.
    //
    // It replaces a flat 100 Hz that was quietly below the *parameter's* own
    // maximum once key tracking or the sequencer multiplied the rate -- the
    // knob went to 40 Hz, key tracking two octaves up made that 160, and the
    // clamp threw the top third of it away with nothing to show for it.
    const double controlNyquist = 0.5 * internalRate_ / Voice::kControlIntervalSamples;

    // Tempo sync swaps the knob's rate for the division's; key tracking and
    // the sequencer multiplier still apply on top, because a synced wobble
    // that speeds up per octave or steps through the pattern is a synced
    // wobble, not a broken one.
    const double lfo1BaseHz = active_.lfo1Sync
        ? dsp::divisionRateHz (active_.lfo1Division, bpm_)
        : active_.lfo1RateHz;
    const double lfo2BaseHz = active_.lfo2Sync
        ? dsp::divisionRateHz (active_.lfo2Division, bpm_)
        : active_.lfo2RateHz;

    lfo1_.setRateHz (std::clamp (
        keyTracked (lfo1BaseHz, active_.lfo1KeyTrack) * std::pow (2.0, octaves),
        0.0, controlNyquist));

    lfo2_.setRateHz (std::clamp (
        keyTracked (lfo2BaseHz, active_.lfo2KeyTrack), 0.0, controlNyquist));

    // Synced, un-retriggered, transport running: the phase is *assigned* from
    // the song position rather than accumulated, exactly as the sequencer's
    // is above -- rewind the transport and the same bar is the same wobble.
    // The assignment makes the rate multipliers moot for that LFO, which is
    // the honest reading of "nailed to the bar"; retrigger hands the phase
    // back to the note and the multipliers with it.
    const bool lfo1Locked = active_.lfo1Sync && ! active_.lfo1Retrigger && transportRunning_;
    const bool lfo2Locked = active_.lfo2Sync && ! active_.lfo2Retrigger && transportRunning_;

    const double beatsNow = ppq_ + beatsIntoBlock_
                          - static_cast<double> (samples) / internalRate_ * bpm_ / 60.0;

    sources_.lfo1 = (lfo1Locked
        ? lfo1_.setPhaseFromPpq (beatsNow,
                                 dsp::divisions[static_cast<std::size_t> (
                                     std::clamp (active_.lfo1Division, 0, dsp::numDivisions - 1))]
                                     .cyclesPerBeat,
                                 samples)
        : lfo1_.advance (samples)) * lfo1Depth;

    sources_.lfo2 = (lfo2Locked
        ? lfo2_.setPhaseFromPpq (beatsNow,
                                 dsp::divisions[static_cast<std::size_t> (
                                     std::clamp (active_.lfo2Division, 0, dsp::numDivisions - 1))]
                                     .cyclesPerBeat,
                                 samples)
        : lfo2_.advance (samples)) * lfo2Depth;

    // The macros are knobs rather than generators -- nothing to tick, and
    // copying them here rather than reading `active_` at each use is what makes
    // both matrices see the same four numbers on the same control chunk.
    sources_.macros = active_.macros;
}

void Engine::mangle (double& left, double& right) noexcept
{
    // ---- the split ---------------------------------------------------------
    //
    // `splitMix_` is the SPLIT switch, smoothed: at 1 the split is in (the
    // path that shipped), at 0 the whole signal goes down the body chain
    // untouched and only a 5 Hz DC blocker stands between the mangle and the
    // output -- the "pure" setting for splitting on a DAW bus instead.
    //
    // The blend below is arithmetic rather than a branch on purpose.
    // Multiplying by exactly 1.0 and adding exactly 0.0 changes no bits, so
    // with the switch on this function is sample-for-sample the one that
    // shipped -- verified by raw-file comparison when the switch landed -- and
    // the toggle is a 30 ms crossfade instead of a click (CLAUDE.md section
    // 7). The split filters and both blockers stay fed in every setting so
    // their state is warm the moment the switch flips: two LR4s are noise
    // next to the mangle, and a cold crossover handed a sustained bass note
    // would spend the whole fade settling instead of crossfading.
    const double splitMix = splitMix_.next();

    double subLeft = 0.0;
    double subRight = 0.0;
    double bodyLeft = 0.0;
    double bodyRight = 0.0;

    split_[0].process (left, subLeft, bodyLeft);
    split_[1].process (right, subRight, bodyRight);

    // A DC blocker on the sub and nowhere else in the split path. Everything
    // above the split has a nonlinearity in front of it and makes DC by
    // construction; the sub is the one band where the high-pass itself would
    // be audible.
    subLeft = subBlocker_[0].process (subLeft);
    subRight = subBlocker_[1].process (subRight);

    if (active_.subMono)
    {
        const double sum = 0.5 * (subLeft + subRight);

        subLeft = sum;
        subRight = sum;
    }

    // With the split out, the body chain gets the raw signal.
    bodyLeft = splitMix * bodyLeft + (1.0 - splitMix) * left;
    bodyRight = splitMix * bodyRight + (1.0 - splitMix) * right;

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

    // The sub leg only exists while the split does.
    left = splitMix * subLeft + bodyLeft;
    right = splitMix * subRight + bodyRight;

    // The pure path's DC blocker, blended the same way. It processes in every
    // setting -- its state has to be warm for the fade -- but with the split
    // in it contributes exactly nothing, and the sum above is bit-identical
    // to the pre-switch engine.
    left = splitMix * left + (1.0 - splitMix) * fullBlocker_[0].process (left);
    right = splitMix * right + (1.0 - splitMix) * fullBlocker_[1].process (right);
}

void Engine::renderChunk (double* left, double* right, int numSamples) noexcept
{
    // Whether every sample of this chunk came out exactly zero, which is what
    // the idle skip is counted in. Tracked here rather than scanned afterwards
    // because the samples are already in registers.
    bool silent = true;

    for (int i = 0; i < numSamples; ++i)
    {
        double sampleLeft = 0.0;
        double sampleRight = 0.0;

        voices_.process (sampleLeft, sampleRight);

        mangle (sampleLeft, sampleRight);

        const double gain = outputGain_.next();

        left[i] = sampleLeft * gain;
        right[i] = sampleRight * gain;

        silent = silent && std::abs (left[i]) < kIdleThreshold
                        && std::abs (right[i]) < kIdleThreshold;
    }

    if (silent && voices_.activeVoiceCount() == 0)
        idleSamples_ += numSamples;
    else
        idleSamples_ = 0;
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
    //
    // The *effective* mode: the render setting while the host is bouncing
    // offline, the live one otherwise. A bounce at x8 is therefore the same
    // graph a live x8 would build, and a test holds it to that bit for bit.
    const int wanted = factorFor (effectiveOversampling(), sampleRate_);

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

    // **An idle instrument costs nothing.** Once the whole chain has been
    // bit-exactly silent for a second with no voice sounding, the render and
    // the decimation filters are skipped and the host gets zeros.
    //
    // The global sources are still advanced, because they free-run: a slow LFO
    // that stopped while nothing was playing would be at the wrong phase when
    // the next note arrived, and a player who set a two-second sweep would find
    // it frozen. That costs a handful of arithmetic per block against the
    // thousands of filter taps this skips.
    //
    // Nothing can wake by itself from here: every stage after the voices is
    // linear or bounded, so a state parked below -240 dBFS with no input stays
    // there, and the counter resets the instant a voice is allocated.
    if (idleSamples_ >= static_cast<int> (internalRate_ * kIdleSecondsBeforeSkipping)
        && voices_.activeVoiceCount() == 0)
    {
        advanceGlobalSources (internalSamples);

        for (int channel = 0; channel < 2; ++channel)
            std::fill (output[channel], output[channel] + numSamples, 0.0);

        return;
    }

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
            voices_.advanceDrift();
            voices_.applyControls (snappedVoice(), sources_);

            // After the sources have moved and before the comb is aimed: the
            // matrix reads the one and writes the other.
            applyGlobalModulation();

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

const VoiceParameters& Engine::snappedVoice() noexcept
{
    // Envelope snap-to-tempo, applied here and not in the JUCE layer, for two
    // reasons that are really one: the engine is what knows the live tempo,
    // and the engine is what the tests can reach. Snapping in the parameter
    // pull would freeze the grid at whatever the tempo was when the knob last
    // moved, and would put the behaviour on the wrong side of the
    // framework-free line (CLAUDE.md section 4).
    //
    // The copy is control-rate and small, and only taken when some envelope
    // actually asks for the grid.
    const auto& raw = active_.voice;

    const bool advSnaps = (raw.adv[0].enable && raw.adv[0].snap)
                       || (raw.adv[1].enable && raw.adv[1].snap)
                       || (raw.adv[2].enable && raw.adv[2].snap);

    if (! (raw.amp.snap || raw.mod1.snap || raw.mod2.snap || advSnaps))
        return raw;

    snappedVoice_ = raw;

    const auto snapEnvelope = [this] (VoiceParameters::Envelope& envelope)
    {
        if (! envelope.snap)
            return;

        envelope.attack = dsp::snapSeconds (envelope.attack, bpm_);
        envelope.hold = dsp::snapSeconds (envelope.hold, bpm_);
        envelope.decay = dsp::snapSeconds (envelope.decay, bpm_);
        envelope.release = dsp::snapSeconds (envelope.release, bpm_);
    };

    snapEnvelope (snappedVoice_.amp);
    snapEnvelope (snappedVoice_.mod1);
    snapEnvelope (snappedVoice_.mod2);

    for (auto& adv : snappedVoice_.adv)
    {
        if (! (adv.enable && adv.snap))
            continue;

        for (auto& seconds : adv.seconds)
            seconds = dsp::snapSeconds (seconds, bpm_);
    }

    return snappedVoice_;
}

} // namespace tezla::sonitus
