// Copyright (c) 2026 The Tezla <thetezla@proton.me>
// Created by The Tezla -- https://github.com/wingit33/tezla.tech
// Music: https://soundcloud.com/thetezla | https://thetezla.bandcamp.com
// Built with development assistance from Claude (Anthropic).
// SPDX-License-Identifier: AGPL-3.0-only
// GNU AGPLv3 (see LICENSE), plus NOTICE.md's attribution term. Keep intact.

#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace tezla::stryda {

namespace {

[[nodiscard]] juce::String rateText (double hz)
{
    return juce::String (hz / 1000.0, hz < 100000.0 ? 1 : 0) + " kHz";
}

/// The house shape for a knob's range: enough resolution that a value can be
/// typed, skewed so the useful part sits mid-travel.
[[nodiscard]] juce::NormalisableRange<float> skewed (float low, float high, float centre)
{
    juce::NormalisableRange<float> range { low, high };
    range.setSkewForCentre (centre);
    return range;
}

[[nodiscard]] juce::ParameterID versioned (const juce::String& id, int schema = kSchemaV1)
{
    return { id, schema };
}

} // namespace

// ---------------------------------------------------------------------------
// Parameters
// ---------------------------------------------------------------------------

juce::AudioProcessorValueTreeState::ParameterLayout StrydaProcessor::createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    // **The formatter is not decoration.** Without one, a skewed range
    // round-trips its default through the 0-1 normalisation and comes back as
    // 0.999999, which JUCE then prints in full -- so a Ratio of 1 reads
    // "0.99999..." on the panel and in the host's automation lane, and looks
    // exactly like a bug. Two places, and the unit beside the number.
    const auto add = [&layout] (const juce::String& id,
                                const juce::String& name,
                                juce::NormalisableRange<float> range,
                                float defaultValue,
                                const juce::String& suffix = {},
                                int places = 2,
                                int schema = kSchemaV1)
    {
        layout.add (std::make_unique<juce::AudioParameterFloat> (
            versioned (id, schema), name, range, defaultValue,
            juce::AudioParameterFloatAttributes()
                .withLabel (suffix)
                .withStringFromValueFunction (
                    [suffix, places] (float value, int)
                    {
                        auto text = juce::String (value, places);
                        return suffix.isEmpty() ? text : text + " " + suffix;
                    })));
    };

    for (int op = 0; op < kNumOperators; ++op)
    {
        const juce::String label = "Op " + juce::String (op + 1) + " \xc2\xb7 ";

        // Ratio to the note. 32 at the top because a modulator at 32x a bass
        // note is 1.8 kHz, which is where the metallic end of the range lives.
        add (ids::op (op, "Ratio"), label + "Ratio", skewed (0.25f, 32.0f, 4.0f), 1.0f);
        add (ids::op (op, "Fine"), label + "Fine", { -100.0f, 100.0f }, 0.0f, "c", 1);

        // 0 = classic phase modulation, 1 = ModFM. See dsp/FmOperator.hpp.
        add (ids::op (op, "Character"), label + "Character", { 0.0f, 1.0f }, 0.0f);

        // Only operator 1 carries the output by default, which is what makes a
        // fresh instance a single clean sine rather than six of them at once.
        add (ids::op (op, "Level"), label + "Level", { 0.0f, 1.0f }, op == 0 ? 1.0f : 0.0f);
        add (ids::op (op, "Pan"), label + "Pan", { -1.0f, 1.0f }, 0.0f);
        add (ids::op (op, "Feedback"), label + "Feedback", { 0.0f, 1.0f }, 0.0f, "cyc");

        add (ids::op (op, "Attack"), label + "Attack", skewed (0.0f, 4.0f, 0.05f), 0.002f, "s", 3);
        add (ids::op (op, "Decay"), label + "Decay", skewed (0.005f, 12.0f, 0.6f), 1.5f, "s", 3);
        add (ids::op (op, "Sustain"), label + "Sustain", { 0.0f, 1.0f }, 1.0f);
        add (ids::op (op, "Release"), label + "Release", skewed (0.005f, 12.0f, 0.4f), 0.25f, "s", 3);

        // ---- F4, appended at schema 2 --------------------------------------

        add (ids::op (op, "Fold"), label + "Fold", { 0.0f, 1.0f }, 0.0f, {}, 2, kSchemaV2);

        add (ids::op (op, "Formant"), label + "Formant",
             skewed (50.0f, 8000.0f, 900.0f), 800.0f, "Hz", 0, kSchemaV2);
        add (ids::op (op, "Width"), label + "Formant width",
             skewed (0.0f, 8.0f, 1.0f), 1.0f, "cyc", 2, kSchemaV2);

        add (ids::op (op, "KeyBreak"), label + "Key break",
             { 21.0f, 108.0f }, 60.0f, {}, 0, kSchemaV2);
        add (ids::op (op, "KeyLeft"), label + "Key below", { -1.0f, 1.0f }, 0.0f, {}, 2, kSchemaV2);
        add (ids::op (op, "KeyRight"), label + "Key above", { -1.0f, 1.0f }, 0.0f, {}, 2, kSchemaV2);

        add (ids::op (op, "VelLevel"), label + "Vel level", { 0.0f, 1.0f }, 0.0f, {}, 2, kSchemaV2);
        add (ids::op (op, "VelIndex"), label + "Vel index", { 0.0f, 1.0f }, 0.0f, {}, 2, kSchemaV2);

        layout.add (std::make_unique<juce::AudioParameterChoice> (
            versioned (ids::op (op, "Mode"), kSchemaV2), label + "Mode",
            choices::operatorMode, 0));
    }

    // The thirty off-diagonal cells. The diagonal is Feedback, above.
    for (int to = 0; to < kNumOperators; ++to)
        for (int from = 0; from < kNumOperators; ++from)
            if (to != from)
                add (ids::cell (to, from),
                     juce::String (from + 1) + " \xe2\x86\x92 " + juce::String (to + 1),
                     skewed (0.0f, 8.0f, 1.0f), 0.0f, "cyc");

    for (int to = 0; to < kNumOperators; ++to)
        add (ids::noise (to), "Noise \xe2\x86\x92 " + juce::String (to + 1),
             skewed (0.0f, 4.0f, 0.5f), 0.0f, "cyc");

    layout.add (std::make_unique<juce::AudioParameterChoice> (
        versioned (ids::oversampling), "Oversampling", choices::oversampling, 0));

    layout.add (std::make_unique<juce::AudioParameterChoice> (
        versioned (ids::renderOversampling), "Render quality", choices::render, 0));

    layout.add (std::make_unique<juce::AudioParameterChoice> (
        versioned (ids::indexCap), "Index cap", choices::indexCap, 0));

    layout.add (std::make_unique<juce::AudioParameterInt> (
        versioned (ids::polyphony), "Voices", 1, StrydaEngine::kMaxVoices, 8));

    add (ids::master, "Master", { -60.0f, 12.0f }, 0.0f, "dB", 1);

    return layout;
}

// ---------------------------------------------------------------------------

StrydaProcessor::StrydaProcessor()
    : juce::AudioProcessor (BusesProperties().withOutput ("Output",
                                                          juce::AudioChannelSet::stereo(),
                                                          true)),
      state_ (*this, nullptr, "STRYDA", createParameterLayout())
{
    // Build the bandwidth predictor's order tables here, on the thread that
    // constructs the plugin, rather than leaving them to whichever thread
    // happens to ask first. They cost about 80 ms once per process and are
    // shared by every instance; `prepareToPlay` is not guaranteed to be off the
    // audio thread in every host, and 80 ms there is a dropout.
    (void) dsp::fm::significantOrder (1.0);
    (void) dsp::fm::feedbackOrder (0.5);
}

bool StrydaProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    return layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo()
             && layouts.getMainInputChannelSet().isDisabled();
}

void StrydaProcessor::prepareToPlay (double sampleRate, int maximumExpectedSamplesPerBlock)
{
    sampleRate_ = sampleRate;

    engine_.prepare (sampleRate, maximumExpectedSamplesPerBlock);
    scratch_.setSize (2, juce::jmax (1, maximumExpectedSamplesPerBlock), false, false, true);

    prepared_ = true;
    reportedLatency_ = -1;
}

void StrydaProcessor::pullParameters()
{
    for (int op = 0; op < kNumOperators; ++op)
    {
        auto& settings = parameters_.operators[static_cast<std::size_t> (op)];

        settings.ratio = raw (ids::op (op, "Ratio"));
        settings.fineCents = raw (ids::op (op, "Fine"));
        settings.character = raw (ids::op (op, "Character"));
        settings.level = raw (ids::op (op, "Level"));
        settings.pan = raw (ids::op (op, "Pan"));
        settings.feedback = raw (ids::op (op, "Feedback"));

        settings.attack = raw (ids::op (op, "Attack"));
        settings.decay = raw (ids::op (op, "Decay"));
        settings.sustain = raw (ids::op (op, "Sustain"));
        settings.release = raw (ids::op (op, "Release"));

        settings.fold = raw (ids::op (op, "Fold"));
        settings.mode = static_cast<int> (std::lround (raw (ids::op (op, "Mode"))));
        settings.formantHz = raw (ids::op (op, "Formant"));
        settings.formantDepth = raw (ids::op (op, "Width"));

        settings.keyBreak = raw (ids::op (op, "KeyBreak"));
        settings.keyLeft = raw (ids::op (op, "KeyLeft"));
        settings.keyRight = raw (ids::op (op, "KeyRight"));

        settings.velLevel = raw (ids::op (op, "VelLevel"));
        settings.velIndex = raw (ids::op (op, "VelIndex"));

        for (int from = 0; from < kNumOperators; ++from)
            parameters_.indices[static_cast<std::size_t> (op)][static_cast<std::size_t> (from)]
                = op == from ? 0.0 : raw (ids::cell (op, from));

        parameters_.noiseIndices[static_cast<std::size_t> (op)] = raw (ids::noise (op));
    }

    parameters_.masterLevel = juce::Decibels::decibelsToGain (raw (ids::master), -60.0f);

    // Off / Soft / Hard, as the amount the prediction is leaned on.
    const int cap = static_cast<int> (std::lround (raw (ids::indexCap)));
    parameters_.indexCap = cap == 0 ? 0.0 : (cap == 1 ? 0.6 : 1.0);

    engine_.setPolyphony (static_cast<int> (std::lround (raw (ids::polyphony))));
    engine_.setOversamplingMode (static_cast<dsp::OversamplingMode> (
        static_cast<int> (std::lround (raw (ids::oversampling)))));
    engine_.setRenderOversampling (static_cast<dsp::RenderOversampling> (
        static_cast<int> (std::lround (raw (ids::renderOversampling)))));
}

void StrydaProcessor::handleMidi (const juce::MidiMessage& message)
{
    if (message.isNoteOn())
    {
        lastNote_ = message.getNoteNumber();
        engine_.noteOn (message.getNoteNumber(), message.getFloatVelocity());
    }
    else if (message.isNoteOff())
    {
        engine_.noteOff (message.getNoteNumber());
    }
    else if (message.isAllNotesOff() || message.isAllSoundOff())
    {
        engine_.allNotesOff();
    }
}

template <typename FloatType>
void StrydaProcessor::processInternal (juce::AudioBuffer<FloatType>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals guard;

    const int numSamples = buffer.getNumSamples();
    buffer.clear();

    if (numSamples <= 0 || ! prepared_)
        return;

    pullParameters();
    engine_.setParameters (parameters_);
    engine_.updateFactor (isNonRealtime());

    if (scratch_.getNumSamples() < numSamples)
        scratch_.setSize (2, numSamples, false, false, true);

    double* const engineLeft = scratch_.getWritePointer (0);
    double* const engineRight = scratch_.getWritePointer (1);

    // Sample-accurate MIDI: the render is cut at every event, so a note lands
    // where it was played. The engine cuts again at its own control grid, which
    // is why 64- and 512-sample blocks are bit-identical.
    int rendered = 0;

    const auto renderSpan = [&] (int from, int count)
    {
        engine_.process (engineLeft, engineRight, count);

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

    activeVoices_.store (engine_.getActiveVoiceCount());

    // The bandwidth readout, computed once per block on the note last played --
    // never per sample, and never on the message thread, which cannot see the
    // engine's current tuning.
    {
        dsp::FmBandwidth bandwidth;
        bandwidth.setOperatorCount (kNumOperators);

        const double fundamental = engine_.getTuning().frequencyFor (lastNote_);

        for (int op = 0; op < kNumOperators; ++op)
        {
            const auto& settings = parameters_.operators[static_cast<std::size_t> (op)];
            bandwidth.setOperatorFrequency (op, fundamental * settings.ratio);
            bandwidth.setFeedback (op, settings.feedback);

            for (int from = 0; from < kNumOperators; ++from)
                bandwidth.setIndex (from, op,
                                    parameters_.indices[static_cast<std::size_t> (op)]
                                                       [static_cast<std::size_t> (from)]);
        }

        predictedTopHz_.store (bandwidth.topSidebandHz());
        internalNyquist_.store (0.5 * engine_.getInternalRate());
    }

    const int latency = engine_.getLatencySamples();

    if (latency != reportedLatency_)
    {
        reportedLatency_ = latency;
        setLatencySamples (latency);
    }
}

void StrydaProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    processInternal (buffer, midi);
}

void StrydaProcessor::processBlock (juce::AudioBuffer<double>& buffer, juce::MidiBuffer& midi)
{
    processInternal (buffer, midi);
}

// ---------------------------------------------------------------------------
// Tooltips that read the live state
// ---------------------------------------------------------------------------

juce::String StrydaProcessor::describeOversampling() const
{
    const int mode = static_cast<int> (std::lround (raw (ids::oversampling)));
    const int factor = engine_.getFactor();
    const double internal = engine_.getInternalRate();

    const juce::String latency = factor > 1
        ? " Latency " + juce::String (engine_.getLatencySamples()) + " samples, compensated."
        : juce::String (" No latency.");

    if (mode == 0)
    {
        juce::String text = "Auto -- your session is at " + rateText (sampleRate_) + ", so this is ";

        text += factor == 1 ? juce::String ("off: the headroom is already there.")
                            : "running x" + juce::String (factor) + ", giving "
                                + rateText (internal) + " internally.";

        text += " It matters more here than anywhere else in the suite: measured, a two-operator "
                "pair at index 16 and ratio 7 reads +2.6 dB of aliasing at the host rate and "
                "-114.6 dB at x4.";

        return text + latency;
    }

    return "x" + juce::String (factor) + " -- " + rateText (internal) + " internally, "
             + juce::String (factor) + " times the CPU of Off. Auto would pick x"
             + juce::String (dsp::autoOversamplingFactor (sampleRate_)) + " here." + latency;
}

juce::String StrydaProcessor::describeRenderQuality() const
{
    const int render = static_cast<int> (std::lround (raw (ids::renderOversampling)));

    if (render == 0)
        return "Same as live -- a bounce is the session graph, bit for bit. Change this and an "
               "offline render can afford a factor the session could not.";

    return "An offline render uses this instead of the live setting, and the latency is "
           "re-declared to match. Playback goes back to the live setting.";
}

// ---------------------------------------------------------------------------
// Presets
// ---------------------------------------------------------------------------

const std::vector<Preset>& StrydaProcessor::getPresets()
{
    static const std::vector<Preset> presets = [] {
        std::vector<Preset> list;

        list.push_back ({
            "Init",
            "One operator, one sine, nothing else. This is the neutral patch and it is "
            "bit-exactly a sine wave -- the same numbers std::sin would give you, asserted "
            "in the tests. Start here and turn one matrix cell up.\n\n"
            "To learn the instrument: set 2 -> 1 to about 1.0 and sweep Op 2's Ratio. "
            "Integer ratios fuse into one tone; 4.76 is a bell.",
            {} });

        list.push_back ({
            "Neuro Growl",
            "The reason this plugin exists. Op 2 modulates Op 1 hard at a ratio of 2, with "
            "Op 3 modulating Op 2 on top -- a three-deep stack, which is what gives a growl "
            "its edge rather than its brightness.\n\n"
            "Automate Op 2's Character: at 0 the index steps through the Bessel nulls and "
            "the timbre flickers; at 1 it opens like a filter. Automate 2 -> 1 in sixteenths "
            "against the drums. Op 1's Feedback adds teeth without another operator.",
            { { ids::op (1, "Ratio"), 2.0f },
              { ids::op (2, "Ratio"), 3.0f },
              { ids::cell (0, 1), 1.2f },
              { ids::cell (1, 2), 0.7f },
              { ids::op (0, "Feedback"), 0.18f },
              { ids::op (1, "Character"), 0.55f },
              { ids::op (0, "Decay"), 6.0f },
              { ids::op (1, "Decay"), 2.0f },
              { ids::op (1, "Sustain"), 0.55f },
              { ids::op (2, "Decay"), 0.9f },
              { ids::op (2, "Sustain"), 0.3f },
              { ids::master, -4.0f } } });

        list.push_back ({
            "Bell",
            "Chowning's own territory: an inharmonic carrier-to-modulator ratio, a modulator "
            "that decays faster than the carrier, and nothing else. The ratio 1 : 3.5 is what "
            "makes it a bell rather than a horn -- rational, so the spectrum is harmonic, but "
            "with the fundamental missing.\n\n"
            "Turn Op 2's Character up and listen to the decay: at 0 the partials break into "
            "dashes as each Bessel term crosses zero; at 1 they fall away in order. That is "
            "the whole difference between the two techniques, and it is most audible here.",
            { { ids::op (1, "Ratio"), 3.5f },
              { ids::cell (0, 1), 1.8f },
              { ids::op (0, "Decay"), 4.0f },
              { ids::op (0, "Sustain"), 0.0f },
              { ids::op (1, "Decay"), 1.2f },
              { ids::op (1, "Sustain"), 0.0f },
              { ids::master, -3.0f } } });

        list.push_back ({
            "Sub Stack",
            "A bass that stays a bass. Op 1 carries alone at ratio 1 with a long decay; Op 2 "
            "sits at ratio 1 as well, so every sideband lands on a harmonic of the note and "
            "nothing lands below it.\n\n"
            "This is the patch to check the Index cap on: play it four octaves up with the cap "
            "Off and then on, and the readout tells you what it had to do. Hold the modulation "
            "index below about 2 and it will never bite at all.",
            { { ids::op (1, "Ratio"), 1.0f },
              { ids::cell (0, 1), 1.1f },
              { ids::op (1, "Character"), 1.0f },
              { ids::op (0, "Decay"), 8.0f },
              { ids::op (0, "Sustain"), 0.9f },
              { ids::op (1, "Decay"), 3.0f },
              { ids::op (1, "Sustain"), 0.5f },
              { ids::master, -2.0f } } });

        return list;
    }();

    return presets;
}

int StrydaProcessor::getNumPrograms() { return static_cast<int> (getPresets().size()); }

const juce::String StrydaProcessor::getProgramName (int index)
{
    const auto& presets = getPresets();
    return juce::isPositiveAndBelow (index, static_cast<int> (presets.size()))
             ? juce::String (presets[static_cast<std::size_t> (index)].name)
             : juce::String();
}

void StrydaProcessor::setCurrentProgram (int index)
{
    const auto& presets = getPresets();

    if (! juce::isPositiveAndBelow (index, static_cast<int> (presets.size())))
        return;

    currentProgram_ = index;

    // Every preset starts from the defaults, so a setting a preset does not
    // mention is the default rather than whatever the last patch left there.
    for (const auto* parameter : getParameters())
        if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (
                const_cast<juce::AudioProcessorParameter*> (parameter)))
            ranged->setValueNotifyingHost (ranged->getDefaultValue());

    for (const auto& setting : presets[static_cast<std::size_t> (index)].settings)
        if (auto* parameter = state_.getParameter (setting.id))
            parameter->setValueNotifyingHost (
                parameter->convertTo0to1 (setting.value));
}

// ---------------------------------------------------------------------------
// State
// ---------------------------------------------------------------------------

void StrydaProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto tree = state_.copyState();
    tree.setProperty ("schemaVersion", kStateSchemaVersion, nullptr);
    tree.setProperty ("tooltips", tooltipsEnabled_, nullptr);

    if (auto xml = tree.createXml())
        copyXmlToBinary (*xml, destData);
}

void StrydaProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (state_.state.getType()))
        {
            auto tree = juce::ValueTree::fromXml (*xml);
            tooltipsEnabled_ = tree.getProperty ("tooltips", true);
            state_.replaceState (tree);
        }
}

juce::AudioProcessorEditor* StrydaProcessor::createEditor()
{
    return new StrydaEditor (*this);
}

} // namespace tezla::stryda

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new tezla::stryda::StrydaProcessor();
}
