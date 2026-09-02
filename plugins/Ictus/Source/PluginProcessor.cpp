// Copyright (c) 2026 The Tezla <thetezla@proton.me>
// Created by The Tezla -- https://github.com/wingit33/tezla.tech
// Music: https://soundcloud.com/thetezla | https://thetezla.bandcamp.com
// Built with development assistance from Claude (Anthropic).
// SPDX-License-Identifier: AGPL-3.0-only
// GNU AGPLv3 (see LICENSE), plus NOTICE.md's attribution term. Keep intact.

#include "PluginProcessor.h"

#include <algorithm>
#include <cmath>

#include <tezla/ui/StateIds.hpp>

#include "PluginEditor.h"

namespace tezla::ictus {

namespace
{
constexpr auto kStateTypeName = "IctusState";

/// Every parameter carries the schema version it was born at, forever: the
/// hint feeds the VST3 parameter ID (CLAUDE.md section 8).
constexpr int kSchemaV1 = 1;
constexpr int kStateSchemaVersion = kSchemaV1;

/// The pads' notes travel as state-tree properties, one per pad.
const char* padNoteProperty (PadIndex pad)
{
    static const char* const names[kPadCount] {
        "kick1Note", "snare1Note", "hatClosedNote", "hatOpenNote",
        "clapNote", "percNote", "kick2Note", "snare2Note"
    };

    return names[static_cast<int> (pad)];
}

[[nodiscard]] float valueOf (juce::AudioProcessorValueTreeState& state, const juce::String& id)
{
    if (auto* raw = state.getRawParameterValue (id))
        return raw->load();

    return 0.0f;
}

juce::String rateText (double rate)
{
    return juce::String (rate / 1000.0, rate < 100000.0 ? 1 : 0) + " kHz";
}

/// One control set to one value.
struct Setting
{
    juce::String id;
    float value;
};

/// A preset is a name and a list of departures from the defaults, applied
/// after every parameter has been reset to its default -- a complete
/// parameter set, never a patch over the previous one (the Sonitus pattern,
/// docs/PLUGIN-CONVENTIONS.md).
struct Preset
{
    const char* name;
    std::vector<Setting> settings;
};

const std::vector<Preset>& presets()
{
    static const std::vector<Preset> list
    {
        // -------------------------------------------------------------------
        {
            // The defaults: a plain kick with a drop and a sigh, no
            // harmonics, no click, no tone -- the body the others are built
            // from, and the one that proves the engine adds nothing it was
            // not asked for.
            "Init Kit -- the plain body",
            {}
        },
        // -------------------------------------------------------------------
        {
            // Short, high, bright: a fast drop that lands quickly, a
            // filtered attack that opens for the first cycles, and a click
            // that carries the velocity. The one that sits on a busy break.
            "DnB Tight",
            {
                { ids::k1Tune, 55.0f },
                { ids::k1Start, 36.0f },
                { ids::k1Drop, 18.0f },
                { ids::k1Sigh, 1.0f },
                { ids::k1SighTime, 300.0f },
                { ids::k1Harmonics, 35.0f },
                { ids::k1Even, 60.0f },
                { ids::k1ToneOn, 1.0f },
                { ids::k1Tone, 6.0f },
                { ids::k1Click, 45.0f },
                { ids::k1ClickTone, 3500.0f },
                { ids::k1Noise, 25.0f },
                { ids::k1Decay, 220.0f },
                { ids::k1Shape, 20.0f },
                { ids::k1Level, 85.0f },
            }
        },
        // -------------------------------------------------------------------
        {
            // Low and long: a slower drop onto 45 Hz, a deeper sigh, and the
            // tail carrying the landed pitch on after the body has gone.
            "Sub Long",
            {
                { ids::k1Tune, 45.0f },
                { ids::k1Start, 24.0f },
                { ids::k1Drop, 40.0f },
                { ids::k1Sigh, 2.0f },
                { ids::k1SighTime, 800.0f },
                { ids::k1Harmonics, 15.0f },
                { ids::k1Even, 30.0f },
                { ids::k1Decay, 600.0f },
                { ids::k1Tail, 60.0f },
                { ids::k1TailTime, 1500.0f },
                { ids::k1Click, 15.0f },
                { ids::k1Level, 90.0f },
            }
        },
        // -------------------------------------------------------------------
        {
            // Snappy and mid-forward: a very fast drop from high up, the body
            // started part way into its cycle so the first sample already
            // moves, hard click and noise, harmonics up, short.
            "Jungle Snap",
            {
                { ids::k1Tune, 62.0f },
                { ids::k1Start, 48.0f },
                { ids::k1Drop, 12.0f },
                { ids::k1Sigh, 0.5f },
                { ids::k1Phase, 30.0f },
                { ids::k1Harmonics, 55.0f },
                { ids::k1Even, 40.0f },
                { ids::k1ToneOn, 1.0f },
                { ids::k1Tone, 10.0f },
                { ids::k1Click, 70.0f },
                { ids::k1ClickTone, 4500.0f },
                { ids::k1Noise, 45.0f },
                { ids::k1NoiseTime, 1.5f },
                { ids::k1Decay, 160.0f },
                { ids::k1Shape, 35.0f },
                { ids::k1Level, 85.0f },
                { ids::k1VelClick, 80.0f },
            }
        },
    };

    return list;
}
} // namespace

// ---------------------------------------------------------------------------
// Construction and parameters
// ---------------------------------------------------------------------------

IctusProcessor::IctusProcessor()
    : juce::AudioProcessor (BusesProperties()
                                .withOutput ("Main", juce::AudioChannelSet::stereo(), true)),
      state_ (*this, nullptr, kStateTypeName, createParameterLayout())
{
    for (int pad = 0; pad < kPadCount; ++pad)
        padNotes_[pad].store (kDefaultPadNotes[pad]);
}

juce::AudioProcessorValueTreeState::ParameterLayout
IctusProcessor::createParameterLayout()
{
    using Parameter = juce::AudioParameterFloat;
    using Switch = juce::AudioParameterBool;
    using ChoiceParameter = juce::AudioParameterChoice;
    using Range = juce::NormalisableRange<float>;

    std::vector<std::unique_ptr<juce::RangedAudioParameter>> parameters;

    const auto attributes = [] (const char* label)
    {
        return juce::AudioParameterFloatAttributes{}.withLabel (label);
    };

    // A range whose useful part sits mid-travel: the skew that puts `centre`
    // at the knob's half-way point (docs/PLUGIN-CONVENTIONS.md, Parameters).
    const auto skewed = [] (float low, float high, float step, float centre)
    {
        Range range (low, high, step);
        range.setSkewForCentre (centre);
        return range;
    };

    // ---- KICK 1: pitch ----------------------------------------------------

    parameters.push_back (std::make_unique<Parameter> (
        juce::ParameterID { ids::k1Tune, kSchemaV1 }, "Kick 1 Tune",
        skewed (20.0f, 400.0f, 0.1f, 60.0f), 50.0f, attributes ("Hz")));

    parameters.push_back (std::make_unique<Switch> (
        juce::ParameterID { ids::k1FollowKey, kSchemaV1 }, "Kick 1 Follow key", false));

    parameters.push_back (std::make_unique<Parameter> (
        juce::ParameterID { ids::k1Start, kSchemaV1 }, "Kick 1 Start",
        Range (0.0f, 60.0f, 0.1f), 30.0f, attributes ("st")));

    parameters.push_back (std::make_unique<Parameter> (
        juce::ParameterID { ids::k1Drop, kSchemaV1 }, "Kick 1 Drop",
        skewed (2.0f, 200.0f, 0.1f, 30.0f), 30.0f, attributes ("ms")));

    parameters.push_back (std::make_unique<Parameter> (
        juce::ParameterID { ids::k1Sigh, kSchemaV1 }, "Kick 1 Sigh",
        Range (-12.0f, 12.0f, 0.01f), 1.5f, attributes ("st")));

    parameters.push_back (std::make_unique<Parameter> (
        juce::ParameterID { ids::k1SighTime, kSchemaV1 }, "Kick 1 Sigh time",
        skewed (100.0f, 2000.0f, 1.0f, 500.0f), 500.0f, attributes ("ms")));

    parameters.push_back (std::make_unique<Parameter> (
        juce::ParameterID { ids::k1Phase, kSchemaV1 }, "Kick 1 Phase",
        Range (0.0f, 90.0f, 0.1f), 0.0f, attributes ("deg")));

    // ---- KICK 1: colour ---------------------------------------------------

    parameters.push_back (std::make_unique<Parameter> (
        juce::ParameterID { ids::k1Harmonics, kSchemaV1 }, "Kick 1 Harmonics",
        Range (0.0f, 100.0f, 0.1f), 0.0f, attributes ("%")));

    parameters.push_back (std::make_unique<Parameter> (
        juce::ParameterID { ids::k1Even, kSchemaV1 }, "Kick 1 Even",
        Range (0.0f, 100.0f, 0.1f), 50.0f, attributes ("%")));

    parameters.push_back (std::make_unique<Switch> (
        juce::ParameterID { ids::k1ToneOn, kSchemaV1 }, "Kick 1 Tone on", false));

    parameters.push_back (std::make_unique<Parameter> (
        juce::ParameterID { ids::k1Tone, kSchemaV1 }, "Kick 1 Tone",
        skewed (1.0f, 64.0f, 0.01f, 8.0f), 8.0f, attributes ("x")));

    // ---- KICK 1: click ----------------------------------------------------

    parameters.push_back (std::make_unique<Parameter> (
        juce::ParameterID { ids::k1Click, kSchemaV1 }, "Kick 1 Click",
        Range (0.0f, 100.0f, 0.1f), 0.0f, attributes ("%")));

    parameters.push_back (std::make_unique<Parameter> (
        juce::ParameterID { ids::k1ClickTone, kSchemaV1 }, "Kick 1 Click tone",
        skewed (200.0f, 8000.0f, 1.0f, 2000.0f), 3000.0f, attributes ("Hz")));

    parameters.push_back (std::make_unique<Parameter> (
        juce::ParameterID { ids::k1Noise, kSchemaV1 }, "Kick 1 Noise",
        Range (0.0f, 100.0f, 0.1f), 0.0f, attributes ("%")));

    parameters.push_back (std::make_unique<Parameter> (
        juce::ParameterID { ids::k1NoiseTime, kSchemaV1 }, "Kick 1 Noise time",
        skewed (0.5f, 8.0f, 0.01f, 2.0f), 2.0f, attributes ("ms")));

    // ---- KICK 1: amplitude ------------------------------------------------

    parameters.push_back (std::make_unique<Parameter> (
        juce::ParameterID { ids::k1Attack, kSchemaV1 }, "Kick 1 Attack",
        Range (0.0f, 20.0f, 0.01f), 0.0f, attributes ("ms")));

    parameters.push_back (std::make_unique<Parameter> (
        juce::ParameterID { ids::k1Hold, kSchemaV1 }, "Kick 1 Hold",
        Range (0.0f, 50.0f, 0.1f), 0.0f, attributes ("ms")));

    parameters.push_back (std::make_unique<Parameter> (
        juce::ParameterID { ids::k1Decay, kSchemaV1 }, "Kick 1 Decay",
        skewed (20.0f, 2000.0f, 1.0f, 300.0f), 350.0f, attributes ("ms")));

    parameters.push_back (std::make_unique<Parameter> (
        juce::ParameterID { ids::k1Shape, kSchemaV1 }, "Kick 1 Shape",
        Range (0.0f, 100.0f, 0.1f), 0.0f, attributes ("%")));

    parameters.push_back (std::make_unique<Parameter> (
        juce::ParameterID { ids::k1Tail, kSchemaV1 }, "Kick 1 Tail",
        Range (0.0f, 100.0f, 0.1f), 0.0f, attributes ("%")));

    parameters.push_back (std::make_unique<Parameter> (
        juce::ParameterID { ids::k1TailTime, kSchemaV1 }, "Kick 1 Tail time",
        skewed (100.0f, 4000.0f, 1.0f, 1000.0f), 1000.0f, attributes ("ms")));

    parameters.push_back (std::make_unique<Parameter> (
        juce::ParameterID { ids::k1Level, kSchemaV1 }, "Kick 1 Level",
        Range (0.0f, 100.0f, 0.1f), 80.0f, attributes ("%")));

    // ---- KICK 1: velocity -------------------------------------------------

    parameters.push_back (std::make_unique<Parameter> (
        juce::ParameterID { ids::k1VelLevel, kSchemaV1 }, "Kick 1 Velocity to level",
        Range (0.0f, 100.0f, 0.1f), 100.0f, attributes ("%")));

    parameters.push_back (std::make_unique<Parameter> (
        juce::ParameterID { ids::k1VelClick, kSchemaV1 }, "Kick 1 Velocity to click",
        Range (0.0f, 100.0f, 0.1f), 60.0f, attributes ("%")));

    parameters.push_back (std::make_unique<Parameter> (
        juce::ParameterID { ids::k1VelDrop, kSchemaV1 }, "Kick 1 Velocity to drop",
        Range (0.0f, 100.0f, 0.1f), 30.0f, attributes ("%")));

    parameters.push_back (std::make_unique<Parameter> (
        juce::ParameterID { ids::k1VelDecay, kSchemaV1 }, "Kick 1 Velocity to decay",
        Range (0.0f, 100.0f, 0.1f), 0.0f, attributes ("%")));

    // ---- GLOBAL ----------------------------------------------------------

    parameters.push_back (std::make_unique<Parameter> (
        juce::ParameterID { ids::output, kSchemaV1 }, "Output",
        Range (-24.0f, 12.0f, 0.1f), 0.0f, attributes ("dB")));

    parameters.push_back (std::make_unique<ChoiceParameter> (
        juce::ParameterID { ids::oversampling, kSchemaV1 }, "Oversampling",
        choices::oversampling, 0));

    parameters.push_back (std::make_unique<ChoiceParameter> (
        juce::ParameterID { ids::renderOversampling, kSchemaV1 }, "Render quality",
        choices::renderOversampling, 0));

    return { parameters.begin(), parameters.end() };
}

// ---------------------------------------------------------------------------
// Audio
// ---------------------------------------------------------------------------

bool IctusProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    return layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
}

void IctusProcessor::prepareToPlay (double sampleRate, int maximumExpectedSamplesPerBlock)
{
    sampleRate_ = sampleRate;

    // The parameters go in BEFORE prepare, so the graph is built at the
    // factor the saved state asks for rather than rebuilt on the first
    // block. The offline flag likewise: the VST3 wrapper sets it in
    // setupProcessing, before activation, which is exactly this order.
    pullParameters();
    engine_.setParameters (parameters_);
    engine_.setOffline (isNonRealtime());

    engine_.prepare (sampleRate_, std::max (maximumExpectedSamplesPerBlock, 1));

    scratch_.setSize (2, std::max (maximumExpectedSamplesPerBlock, 1), false, false, true);

    reportedLatency_ = engine_.getLatencySamples();
    setLatencySamples (reportedLatency_);

    prepared_ = true;
}

void IctusProcessor::pullParameters()
{
    auto& k = parameters_.kick1;

    k.tuneHz = valueOf (state_, ids::k1Tune);
    k.followKey = valueOf (state_, ids::k1FollowKey) > 0.5f;
    k.startSemitones = valueOf (state_, ids::k1Start);
    k.dropSeconds = valueOf (state_, ids::k1Drop) * 0.001;
    k.sighSemitones = valueOf (state_, ids::k1Sigh);
    k.sighSeconds = valueOf (state_, ids::k1SighTime) * 0.001;
    k.phaseDegrees = valueOf (state_, ids::k1Phase);

    k.harmonics = valueOf (state_, ids::k1Harmonics) * 0.01;
    k.even = valueOf (state_, ids::k1Even) * 0.01;
    k.toneEnabled = valueOf (state_, ids::k1ToneOn) > 0.5f;
    k.toneRatio = valueOf (state_, ids::k1Tone);

    k.click = valueOf (state_, ids::k1Click) * 0.01;
    k.clickToneHz = valueOf (state_, ids::k1ClickTone);
    k.clickNoise = valueOf (state_, ids::k1Noise) * 0.01;
    k.clickNoiseSeconds = valueOf (state_, ids::k1NoiseTime) * 0.001;

    k.attackSeconds = valueOf (state_, ids::k1Attack) * 0.001;
    k.holdSeconds = valueOf (state_, ids::k1Hold) * 0.001;
    k.decaySeconds = valueOf (state_, ids::k1Decay) * 0.001;
    k.shape = valueOf (state_, ids::k1Shape) * 0.01;
    k.tailMix = valueOf (state_, ids::k1Tail) * 0.01;
    k.tailSeconds = valueOf (state_, ids::k1TailTime) * 0.001;
    k.level = valueOf (state_, ids::k1Level) * 0.01;

    k.velocityLevel = valueOf (state_, ids::k1VelLevel) * 0.01;
    k.velocityClick = valueOf (state_, ids::k1VelClick) * 0.01;
    k.velocityDrop = valueOf (state_, ids::k1VelDrop) * 0.01;
    k.velocityDecay = valueOf (state_, ids::k1VelDecay) * 0.01;

    for (int pad = 0; pad < kPadCount; ++pad)
        parameters_.padNotes[pad] = padNotes_[pad].load();

    parameters_.masterDb = valueOf (state_, ids::output);

    parameters_.oversampling = static_cast<dsp::OversamplingMode> (
        juce::jlimit (0, static_cast<int> (dsp::OversamplingMode::X8),
                      static_cast<int> (std::lround (valueOf (state_, ids::oversampling)))));

    parameters_.renderOversampling = static_cast<dsp::RenderOversampling> (
        juce::jlimit (0, static_cast<int> (dsp::RenderOversampling::X8),
                      static_cast<int> (std::lround (valueOf (state_, ids::renderOversampling)))));
}

void IctusProcessor::handleMidi (const juce::MidiMessage& message)
{
    if (message.isNoteOn())
        engine_.noteOn (message.getNoteNumber(), message.getFloatVelocity());
    else if (message.isNoteOff())
        engine_.noteOff (message.getNoteNumber());
    else if (message.isAllNotesOff() || message.isAllSoundOff())
        engine_.allNotesOff();
}

void IctusProcessor::triggerHit (PadIndex pad) noexcept
{
    pendingHits_.fetch_or (1u << static_cast<unsigned> (pad));
}

void IctusProcessor::setPadNote (PadIndex pad, int note) noexcept
{
    padNotes_[static_cast<int> (pad)].store (juce::jlimit (0, 127, note));
}

int IctusProcessor::getPadNote (PadIndex pad) const noexcept
{
    return padNotes_[static_cast<int> (pad)].load();
}

template <typename FloatType>
void IctusProcessor::processInternal (juce::AudioBuffer<FloatType>& buffer,
                                      juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals guard;

    const int numSamples = buffer.getNumSamples();

    buffer.clear();

    if (numSamples <= 0 || ! prepared_)
        return;

    // Every knob is snapshotted into a hit at note-on, so pushing the whole
    // set once per block is all the engine needs; only the output level is
    // continuous, and the engine smooths it.
    pullParameters();
    engine_.setParameters (parameters_);
    engine_.setOffline (isNonRealtime());

    if (scratch_.getNumSamples() < numSamples)
        scratch_.setSize (2, numSamples, false, false, true);

    double* const engineLeft = scratch_.getWritePointer (0);
    double* const engineRight = scratch_.getWritePointer (1);
    double* const engineOut[2] { engineLeft, engineRight };

    // The HIT button lands at the top of the block, at full velocity.
    if (const unsigned hits = pendingHits_.exchange (0); hits != 0)
        for (int pad = 0; pad < kPadCount; ++pad)
            if (hits & (1u << static_cast<unsigned> (pad)))
                engine_.noteOn (padNotes_[pad].load(), 1.0);

    // Sample-accurate MIDI: the render is cut at every event, so a hit lands
    // where it was played. The engine cuts again at its own control grid,
    // which is why 64- and 512-sample blocks are bit-identical.
    int rendered = 0;

    auto renderSpan = [&] (int from, int count)
    {
        engine_.process (engineOut, count);

        for (int i = 0; i < count; ++i)
        {
            buffer.setSample (0, from + i, static_cast<FloatType> (engineLeft[i]));
            buffer.setSample (1, from + i, static_cast<FloatType> (engineRight[i]));
        }
    };

    for (const auto metadata : midi)
    {
        const int position = juce::jlimit (0, numSamples, metadata.samplePosition);

        if (position > rendered)
        {
            renderSpan (rendered, position - rendered);
            rendered = position;
        }

        handleMidi (metadata.getMessage());
    }

    if (rendered < numSamples)
        renderSpan (rendered, numSamples - rendered);

    activeHits_.store (engine_.activeHitCount());

    // Latency changes when the oversampling factor does, and a host that is
    // not told simply plays the drum late. CLAUDE.md section 2.2.
    const int latency = engine_.getLatencySamples();

    if (latency != reportedLatency_)
    {
        reportedLatency_ = latency;
        setLatencySamples (latency);
    }
}

void IctusProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    processInternal (buffer, midi);
}

void IctusProcessor::processBlock (juce::AudioBuffer<double>& buffer, juce::MidiBuffer& midi)
{
    processInternal (buffer, midi);
}

// ---------------------------------------------------------------------------
// Tooltips that read the live state
// ---------------------------------------------------------------------------

juce::String IctusProcessor::describeOversampling() const
{
    const int mode = static_cast<int> (std::lround (
        state_.getRawParameterValue (ids::oversampling)->load()));

    const int factor = engine_.getOversamplingFactor();
    const double internal = sampleRate_ * factor;

    const int render = static_cast<int> (std::lround (
        state_.getRawParameterValue (ids::renderOversampling)->load()));

    if (isNonRealtime() && render != 0)
        return "Rendering offline, so Render quality is in force instead: x" + juce::String (factor)
                 + " (" + rateText (internal) + " internally). This setting comes back when "
                   "playback does.";

    juce::String latency = factor > 1
        ? " Latency " + juce::String (engine_.getLatencySamples()) + " samples, compensated."
        : juce::String (" No latency.");

    if (mode == 0)
    {
        juce::String text = "Auto -- your session is at " + rateText (sampleRate_) + ", so this is ";

        text += factor == 1 ? juce::String ("off: the headroom is already there.")
                            : "running x" + juce::String (factor) + ", giving "
                                + rateText (internal) + " internally.";

        text += " Every drum, its harmonics and its click are made at that rate, so a "
                "3 kHz click never aliases and the Harmonics curves fold nothing back.";

        return text + latency;
    }

    return "x" + juce::String (factor) + " -- " + rateText (internal) + " internally, "
             + juce::String (factor) + " times the CPU of Off. Auto would pick x"
             + juce::String (dsp::autoOversamplingFactor (sampleRate_)) + " here." + latency;
}

juce::String IctusProcessor::describeRenderQuality() const
{
    const auto indexOfParameter = [this] (const char* id)
    {
        return static_cast<int> (std::lround (state_.getRawParameterValue (id)->load()));
    };

    const int render = indexOfParameter (ids::renderOversampling);
    const bool offline = isNonRealtime();

    if (render == 0)
    {
        juce::String text = "Same as live -- an offline bounce runs at whatever Oversampling is set "
                            "to, so the render is exactly what you heard. Pick x8 here to bounce at "
                            "the highest quality without paying for it while you play: it applies "
                            "only while the host renders offline, and costs render time, not CPU.";

        if (offline)
            text += " The host is rendering offline right now, at the live setting.";

        return text;
    }

    const auto live = static_cast<dsp::OversamplingMode> (indexOfParameter (ids::oversampling));
    const auto mode = dsp::renderOversamplingMode (static_cast<dsp::RenderOversampling> (render), live);
    const int factor = dsp::oversamplingFactor (mode, sampleRate_);
    const int liveFactor = dsp::oversamplingFactor (live, sampleRate_);

    juce::String text = (render == 1 ? juce::String ("Auto") : "x" + juce::String (factor))
                      + " while rendering -- an offline bounce runs x" + juce::String (factor)
                      + " (" + rateText (sampleRate_ * factor) + " internally)";

    text += factor == liveFactor ? juce::String (", the same as live, so it changes nothing.")
                                 : " against x" + juce::String (liveFactor) + " live. Costs render "
                                   "time and no CPU while playing.";

    if (offline)
        text += " The host is rendering offline right now, so this is in force.";

    return text;
}

// ---------------------------------------------------------------------------
// Programs
// ---------------------------------------------------------------------------

int IctusProcessor::getNumPrograms()
{
    return static_cast<int> (presets().size());
}

const juce::String IctusProcessor::getProgramName (int index)
{
    const auto& list = presets();

    return list[static_cast<std::size_t> (
        juce::jlimit (0, static_cast<int> (list.size()) - 1, index))].name;
}

void IctusProcessor::setCurrentProgram (int index)
{
    const auto& list = presets();

    currentProgram_ = juce::jlimit (0, static_cast<int> (list.size()) - 1, index);

    // Everything to its default first, so a preset is a complete parameter
    // set rather than a patch over whatever was loaded before it.
    for (auto* parameter : getParameters())
        if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (parameter))
            ranged->setValueNotifyingHost (ranged->getDefaultValue());

    for (const auto& setting : list[static_cast<std::size_t> (currentProgram_)].settings)
        if (auto* parameter = state_.getParameter (setting.id))
            parameter->setValueNotifyingHost (parameter->convertTo0to1 (setting.value));
}

// ---------------------------------------------------------------------------
// State
// ---------------------------------------------------------------------------

void IctusProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = state_.copyState();

    state.setProperty ("schemaVersion", kStateSchemaVersion, nullptr);

    // Both A/B slots, or the button is a session-only convenience.
    state.appendChild (abCompare_.toValueTree(), nullptr);

    for (int pad = 0; pad < kPadCount; ++pad)
        state.setProperty (padNoteProperty (static_cast<PadIndex> (pad)),
                           padNotes_[pad].load(), nullptr);

    state.setProperty (ui::stateIds::tooltipsEnabled, tooltipsEnabled_, nullptr);

    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void IctusProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    auto xml = getXmlFromBinary (data, sizeInBytes);

    if (xml == nullptr || ! xml->hasTagName (state_.state.getType()))
        return;

    const auto tree = juce::ValueTree::fromXml (*xml);

    state_.replaceState (tree);

    abCompare_.restoreFromValueTree (tree.getChildWithName ("abCompare"));

    for (int pad = 0; pad < kPadCount; ++pad)
        padNotes_[pad].store (juce::jlimit (0, 127,
            static_cast<int> (tree.getProperty (padNoteProperty (static_cast<PadIndex> (pad)),
                                                kDefaultPadNotes[pad]))));

    tooltipsEnabled_ = tree.getProperty (ui::stateIds::tooltipsEnabled, true);
}

juce::AudioProcessorEditor* IctusProcessor::createEditor()
{
    return new IctusEditor (*this);
}

} // namespace tezla::ictus

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new tezla::ictus::IctusProcessor();
}
