// Copyright (c) 2026 The Tezla <thetezla@proton.me>
// Created by The Tezla -- https://github.com/wingit33/tezla.tech
// Music: https://soundcloud.com/thetezla | https://thetezla.bandcamp.com
// Built with development assistance from Claude (Anthropic).
// SPDX-License-Identifier: AGPL-3.0-only
// GNU AGPLv3 (see LICENSE), plus NOTICE.md's attribution term. Keep intact.

#include "PluginProcessor.h"

#include <cmath>

#include <tezla/dsp/ScalaFile.hpp>

namespace tezla::svarayantra {

namespace
{
constexpr auto kStateTypeName = "SvarayantraState";
constexpr int kSchemaV1 = 1;
constexpr int kStateSchemaVersion = kSchemaV1;

// The same property names as Sonitus, so the tuning workflow reads
// identically across the two instruments.
constexpr auto kScaleTextProperty = "scalaText";
constexpr auto kScaleNameProperty = "scaleName";
constexpr auto kKeyboardMapProperty = "keyboardMapText";
constexpr auto kConcertPitchProperty = "concertPitch";
constexpr auto kTooltipsProperty = "tooltipsEnabled";

constexpr auto kFontPathProperty = "fontPath";
constexpr auto kFontBankProperty = "fontBank";
constexpr auto kFontProgramProperty = "fontProgram";

[[nodiscard]] float valueOf (juce::AudioProcessorValueTreeState& state,
                             const juce::String& id)
{
    if (auto* raw = state.getRawParameterValue (id))
        return raw->load();

    return 0.0f;
}

[[nodiscard]] double timecentsToSecondsCapped (double timecents)
{
    if (timecents <= -11950.0)
        return 0.0;

    const double seconds = std::exp2 (timecents / 1200.0);
    return seconds > 20.0 ? 20.0 : seconds;
}
} // namespace

SvarayantraProcessor::SvarayantraProcessor()
    : juce::AudioProcessor (BusesProperties()
                                .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      state_ (*this, nullptr, kStateTypeName, createParameterLayout())
{
    // scales:: rather than the Tuning class's bare builder, so the default
    // scale arrives with the construction and story the tuning panel shows.
    scale_ = dsp::scales::twelveToneEqual();
    scaleName_ = scale_.name;
    previewTuning_.setScale (scale_);
}

SvarayantraProcessor::~SvarayantraProcessor()
{
    // Audio has stopped; every slot is safe to drain here and nowhere else.
    delete pendingFont_.exchange (nullptr);
    delete retiredFont_.exchange (nullptr);
    delete currentFont_;
}

juce::AudioProcessorValueTreeState::ParameterLayout
SvarayantraProcessor::createParameterLayout()
{
    using Parameter = juce::AudioParameterFloat;
    using IntParameter = juce::AudioParameterInt;

    std::vector<std::unique_ptr<juce::RangedAudioParameter>> parameters;

    parameters.push_back (std::make_unique<Parameter> (
        juce::ParameterID { ids::outputTrim, kSchemaV1 }, "Output trim",
        juce::NormalisableRange<float> (-24.0f, 24.0f, 0.01f), 0.0f,
        juce::AudioParameterFloatAttributes{}.withLabel ("dB")));

    parameters.push_back (std::make_unique<IntParameter> (
        juce::ParameterID { ids::bendRange, kSchemaV1 }, "Bend range", 0, 24, 2,
        juce::AudioParameterIntAttributes{}.withLabel ("st")));

    return { parameters.begin(), parameters.end() };
}

bool SvarayantraProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    return layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
}

void SvarayantraProcessor::prepareToPlay (double sampleRate, int maximumExpectedSamplesPerBlock)
{
    sampleRate_ = sampleRate;

    engine_.prepare (sampleRate);
    scratch_.setSize (2, juce::jmax (16, maximumExpectedSamplesPerBlock), false, false, true);

    // The tuning and preset choice survive a prepare; publish them again so
    // the freshly reset engine picks both up at the first block.
    publishTuning();
    presetDirty_.store (true, std::memory_order_release);

    prepared_ = true;
}

void SvarayantraProcessor::processBlock (juce::AudioBuffer<float>& buffer,
                                         juce::MidiBuffer& midi)
{
    processInternal (buffer, midi);
}

void SvarayantraProcessor::processBlock (juce::AudioBuffer<double>& buffer,
                                         juce::MidiBuffer& midi)
{
    processInternal (buffer, midi);
}

void SvarayantraProcessor::pullParameters()
{
    const double trimDb = valueOf (state_, ids::outputTrim);
    engine_.setOutputGain (std::pow (10.0, trimDb / 20.0));
    engine_.setBendRangeSemitones (valueOf (state_, ids::bendRange));
}

void SvarayantraProcessor::handleMidi (const juce::MidiMessage& message)
{
    if (message.isNoteOn())
    {
        engine_.noteOn (message.getNoteNumber(), message.getVelocity());
        return;
    }

    if (message.isNoteOff())
    {
        engine_.noteOff (message.getNoteNumber());
        return;
    }

    if (message.isAllNotesOff() || message.isAllSoundOff())
    {
        engine_.allNotesOff();
        return;
    }

    if (message.isSustainPedalOn())  { engine_.setSustainPedal (true);  return; }
    if (message.isSustainPedalOff()) { engine_.setSustainPedal (false); return; }

    if (message.isPitchWheel())
    {
        engine_.setPitchBend ((message.getPitchWheelValue() - 8192) / 8192.0);
        return;
    }

    if (message.isControllerOfType (1))
    {
        engine_.setModWheel (message.getControllerValue() / 127.0);
        return;
    }

    // Bank select MSB arms the bank; the program change applies it, per the
    // MIDI convention. LSB banks are not distinguished by the format.
    if (message.isControllerOfType (0))
    {
        desiredBank_.store (message.getControllerValue(), std::memory_order_relaxed);
        return;
    }

    if (message.isProgramChange())
    {
        desiredProgram_.store (message.getProgramChangeNumber(), std::memory_order_relaxed);
        presetDirty_.store (true, std::memory_order_release);
    }
}

template <typename FloatType>
void SvarayantraProcessor::processInternal (juce::AudioBuffer<FloatType>& buffer,
                                            juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals guard;

    const int numSamples = buffer.getNumSamples();

    buffer.clear();

    if (numSamples <= 0 || ! prepared_)
        return;

    pullParameters();

    // Fonts and tuning first, so a note-on in this block plays the soundfont
    // and scale the player just loaded rather than the previous ones.
    collectFont();
    collectTuning();

    if (presetDirty_.exchange (false, std::memory_order_acquire))
        engine_.selectPreset (desiredBank_.load (std::memory_order_relaxed),
                              desiredProgram_.load (std::memory_order_relaxed));

    if (scratch_.getNumSamples() < numSamples)
        scratch_.setSize (2, numSamples, false, false, true);

    channelPointers_[0] = scratch_.getWritePointer (0);
    channelPointers_[1] = scratch_.getWritePointer (1);

    // Sample-accurate MIDI: the render is cut at every event, so a note
    // lands where it was played rather than at the start of the next block.
    int rendered = 0;

    auto renderSpan = [&] (int from, int count)
    {
        engine_.process (channelPointers_[0], channelPointers_[1], count);

        for (int channel = 0; channel < 2; ++channel)
            for (int i = 0; i < count; ++i)
                buffer.setSample (channel, from + i, static_cast<FloatType> (
                    channelPointers_[static_cast<std::size_t> (channel)][i]));
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

    activeVoices_.store (engine_.activeVoiceCount());
}

// ---------------------------------------------------------------------------
// The soundfont
// ---------------------------------------------------------------------------

juce::String SvarayantraProcessor::loadFontFile (const juce::File& file)
{
    if (! file.existsAsFile())
    {
        fontError_ = "File not found: " + file.getFullPathName();
        fontPath_ = file.getFullPathName();
        return fontError_;
    }

    juce::MemoryBlock bytes;

    if (! file.loadFileAsData (bytes))
    {
        fontError_ = "Could not read: " + file.getFullPathName();
        fontPath_ = file.getFullPathName();
        return fontError_;
    }

    auto font = std::make_unique<FontData>();
    const auto result = font->file.parse (
        static_cast<const std::uint8_t*> (bytes.getData()), bytes.getSize());

    if (! result.ok)
    {
        fontError_ = "Not a usable SoundFont (" + juce::String (result.chunk)
                       + "): " + juce::String (result.message);
        fontPath_ = file.getFullPathName();
        return fontError_;
    }

    font->model.build (font->file);

    // The host's tail is the longest release any zone can ask for.
    double tail = 0.0;

    for (const auto& preset : font->model.presets)
        for (const auto& zone : preset.zones)
            tail = std::max (tail, timecentsToSecondsCapped (
                zone.volumeEnvelope.releaseTimecents));

    font->tailSeconds = tail;

    fontPath_ = file.getFullPathName();
    fontName_ = juce::String (font->file.name);
    fontError_.clear();
    tailSeconds_.store (font->tailSeconds);

    rebuildPresetChoices (font->model);

    // Keep the stored bank/program when the new font still has it; fall to
    // the first preset otherwise.
    bool stillThere = false;

    for (const auto& choice : presetChoices_)
        stillThere = stillThere || (choice.bank == bank_ && choice.program == program_);

    if (! stillThere && ! presetChoices_.empty())
    {
        bank_ = presetChoices_.front().bank;
        program_ = presetChoices_.front().program;
    }

    publishFont (std::move (font));
    setPresetChoice (bank_, program_);

    return {};
}

void SvarayantraProcessor::clearFont()
{
    publishFont (nullptr);
    fontPath_.clear();
    fontName_.clear();
    fontError_.clear();
    presetChoices_.clear();
    tailSeconds_.store (0.0);
}

void SvarayantraProcessor::publishFont (std::unique_ptr<FontData> font)
{
    // Drain both slots first: anything still parked is now safe to free,
    // and doing it here is what bounds the whole scheme at one pending plus
    // one retired.
    delete retiredFont_.exchange (nullptr);
    delete pendingFont_.exchange (font.release());
}

void SvarayantraProcessor::collectFont() noexcept
{
    if (auto* next = pendingFont_.exchange (nullptr, std::memory_order_acq_rel))
    {
        retiredFont_.store (currentFont_, std::memory_order_release);
        currentFont_ = next;

        engine_.setFont (&currentFont_->file, &currentFont_->model);
        presetDirty_.store (true, std::memory_order_release);
    }
}

void SvarayantraProcessor::rebuildPresetChoices (const Sf2Model& model)
{
    presetChoices_.clear();
    presetChoices_.reserve (model.presets.size());

    for (const auto& preset : model.presets)
    {
        PresetChoice choice;
        choice.bank = preset.bank;
        choice.program = preset.program;
        choice.label = juce::String (preset.bank).paddedLeft ('0', 3) + ":"
                         + juce::String (preset.program).paddedLeft ('0', 3) + "  "
                         + juce::String (preset.name);
        presetChoices_.push_back (std::move (choice));
    }

    // Bank-then-program order, the way every soundfont editor lists them.
    std::sort (presetChoices_.begin(), presetChoices_.end(),
               [] (const PresetChoice& a, const PresetChoice& b)
               {
                   return a.bank != b.bank ? a.bank < b.bank
                                           : a.program < b.program;
               });
}

void SvarayantraProcessor::setPresetChoice (int bank, int program)
{
    bank_ = bank;
    program_ = program;
    desiredBank_.store (bank, std::memory_order_relaxed);
    desiredProgram_.store (program, std::memory_order_relaxed);
    presetDirty_.store (true, std::memory_order_release);
}

double SvarayantraProcessor::getTailLengthSeconds() const
{
    return tailSeconds_.load();
}

// ---------------------------------------------------------------------------
// Host programs = font presets
// ---------------------------------------------------------------------------

int SvarayantraProcessor::getNumPrograms()
{
    return juce::jmax (1, static_cast<int> (presetChoices_.size()));
}

int SvarayantraProcessor::getCurrentProgram()
{
    for (std::size_t i = 0; i < presetChoices_.size(); ++i)
        if (presetChoices_[i].bank == bank_ && presetChoices_[i].program == program_)
            return static_cast<int> (i);

    return 0;
}

void SvarayantraProcessor::setCurrentProgram (int index)
{
    if (index < 0 || index >= static_cast<int> (presetChoices_.size()))
        return;

    const auto& choice = presetChoices_[static_cast<std::size_t> (index)];
    setPresetChoice (choice.bank, choice.program);
}

const juce::String SvarayantraProcessor::getProgramName (int index)
{
    if (index < 0 || index >= static_cast<int> (presetChoices_.size()))
        return "(no soundfont)";

    return presetChoices_[static_cast<std::size_t> (index)].label;
}

// ---------------------------------------------------------------------------
// The tuning (mirrors Sonitus)
// ---------------------------------------------------------------------------

void SvarayantraProcessor::publishTuning()
{
    const juce::SpinLock::ScopedLockType lock (tuningLock_);

    pendingScale_ = scale_;
    pendingMap_ = hasKeyboardMap_ ? keyboardMap_ : dsp::KeyboardMap {};
    pendingConcertHz_ = concertPitchHz_;

    tuningPending_.store (true, std::memory_order_release);

    // The message-thread twin used for previews follows the same values.
    previewTuning_.setScale (scale_);
    previewTuning_.setKeyboardMap (hasKeyboardMap_ ? keyboardMap_ : dsp::KeyboardMap {});
    previewTuning_.setConcertPitch (concertPitchHz_);
}

void SvarayantraProcessor::collectTuning() noexcept
{
    if (! tuningPending_.load (std::memory_order_acquire))
        return;

    const juce::SpinLock::ScopedTryLockType lock (tuningLock_);

    if (! lock.isLocked())
        return;

    engine_.tuning().swapScale (pendingScale_);
    engine_.tuning().swapKeyboardMap (pendingMap_);
    engine_.tuning().setConcertPitch (pendingConcertHz_);

    tuningPending_.store (false, std::memory_order_release);
}

juce::String SvarayantraProcessor::loadScalaText (const juce::String& text,
                                                  const juce::String& name)
{
    dsp::Scale parsed;

    const auto result = dsp::parseScl (text.toStdString(), parsed);

    if (! result.ok)
        return "Line " + juce::String (result.line) + ": "
                 + juce::String (result.message);

    scale_ = parsed;
    scalaText_ = text;
    scaleName_ = name.isNotEmpty() ? name : juce::String (parsed.name);

    publishTuning();
    return {};
}

juce::String SvarayantraProcessor::loadKeyboardMapText (const juce::String& text)
{
    dsp::KeyboardMap parsed;

    const auto result = dsp::parseKbm (text.toStdString(), parsed);

    if (! result.ok)
        return "Line " + juce::String (result.line) + ": "
                 + juce::String (result.message);

    keyboardMap_ = parsed;
    keyboardMapText_ = text;
    hasKeyboardMap_ = true;

    publishTuning();
    return {};
}

bool SvarayantraProcessor::selectBuiltInScale (const juce::String& name)
{
    for (const auto& scale : dsp::scales::all())
    {
        if (name == juce::String (scale.name))
        {
            scale_ = scale;
            scaleName_ = name;
            scalaText_.clear();

            publishTuning();
            return true;
        }
    }

    return false;
}

void SvarayantraProcessor::resetTuning()
{
    scale_ = dsp::scales::twelveToneEqual();
    scaleName_ = scale_.name;
    scalaText_.clear();
    keyboardMap_ = {};
    keyboardMapText_.clear();
    hasKeyboardMap_ = false;

    publishTuning();
}

void SvarayantraProcessor::setConcertPitch (double hz)
{
    concertPitchHz_ = std::clamp (hz, dsp::Tuning::kMinimumConcertHz,
                                  dsp::Tuning::kMaximumConcertHz);
    publishTuning();
}

double SvarayantraProcessor::previewFrequencyFor (int midiNote) const
{
    return previewTuning_.frequencyFor (midiNote);
}

// ---------------------------------------------------------------------------
// State
// ---------------------------------------------------------------------------

void SvarayantraProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = state_.copyState();

    state.setProperty ("schemaVersion", kStateSchemaVersion, nullptr);

    // The FONT travels as a path (see the header for why), the TUNING as its
    // full text -- a few hundred bytes that make the project self-contained.
    state.setProperty (kFontPathProperty, fontPath_, nullptr);
    state.setProperty (kFontBankProperty, bank_, nullptr);
    state.setProperty (kFontProgramProperty, program_, nullptr);

    state.setProperty (kScaleNameProperty, scaleName_, nullptr);
    state.setProperty (kScaleTextProperty, scalaText_, nullptr);
    state.setProperty (kKeyboardMapProperty, keyboardMapText_, nullptr);
    state.setProperty (kConcertPitchProperty, concertPitchHz_, nullptr);
    state.setProperty (kTooltipsProperty, tooltipsEnabled_, nullptr);

    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void SvarayantraProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    auto xml = getXmlFromBinary (data, sizeInBytes);

    if (xml == nullptr || ! xml->hasTagName (state_.state.getType()))
        return;

    const auto tree = juce::ValueTree::fromXml (*xml);

    state_.replaceState (tree);

    tooltipsEnabled_ = tree.getProperty (kTooltipsProperty, true);

    // Tuning first (concert pitch before scale so every publish carries it),
    // falling back to 12-TET when the stored scale does not parse.
    concertPitchHz_ = std::clamp (
        double (tree.getProperty (kConcertPitchProperty, 440.0)),
        dsp::Tuning::kMinimumConcertHz, dsp::Tuning::kMaximumConcertHz);

    const juce::String name = tree.getProperty (kScaleNameProperty, "").toString();
    const juce::String text = tree.getProperty (kScaleTextProperty, "").toString();
    const juce::String map = tree.getProperty (kKeyboardMapProperty, "").toString();

    resetTuning();

    if (text.isNotEmpty())
    {
        if (loadScalaText (text, name).isNotEmpty())
            resetTuning();
    }
    else if (name.isNotEmpty() && name != juce::String (scale_.name))
    {
        selectBuiltInScale (name);
    }

    if (map.isNotEmpty())
        loadKeyboardMapText (map);

    // The font, by path. A missing file keeps its path and the reason on
    // show; the instrument plays nothing rather than the wrong thing.
    bank_ = tree.getProperty (kFontBankProperty, 0);
    program_ = tree.getProperty (kFontProgramProperty, 0);

    const juce::String path = tree.getProperty (kFontPathProperty, "").toString();

    if (path.isNotEmpty())
        (void) loadFontFile (juce::File (path));
    else
        clearFont();

    setPresetChoice (bank_, program_);
}

juce::AudioProcessorEditor* SvarayantraProcessor::createEditor()
{
    // The real editor lands in the next phase; the generic panel keeps the
    // plugin usable (and the validator honest) until it does.
    return new juce::GenericAudioProcessorEditor (*this);
}

} // namespace tezla::svarayantra

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new tezla::svarayantra::SvarayantraProcessor();
}
