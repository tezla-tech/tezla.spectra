// Copyright (c) 2026 The Tezla <thetezla@proton.me>
// Created by The Tezla -- https://github.com/wingit33/tezla.tech
// Music: https://soundcloud.com/thetezla | https://thetezla.bandcamp.com
// Built with development assistance from Claude (Anthropic).
// SPDX-License-Identifier: AGPL-3.0-only
// GNU AGPLv3 (see LICENSE), plus NOTICE.md's attribution term. Keep intact.

#include "PluginProcessor.h"
#include "PluginEditor.h"

#include <algorithm>
#include <cstring>

#include <tezla/ui/StateIds.hpp>

namespace tezla::crossbar {

namespace
{
constexpr auto kStateTypeName = "CrossbarState";
constexpr int kSchemaV1 = 1;
constexpr int kStateSchemaVersion = kSchemaV1;

[[nodiscard]] float valueOf (const juce::AudioProcessorValueTreeState& state,
                             const juce::String& id)
{
    if (auto* raw = state.getRawParameterValue (id))
        return raw->load();

    return 0.0f;
}

/// One control set to one value.
struct Setting
{
    juce::String id;
    float value;
};

/// A preset is a name and a list of departures from the defaults, and every
/// preset is applied by resetting every parameter to its default first -- so a
/// preset is a complete parameter set rather than a patch over whatever was
/// loaded before it (the house pattern since Sonitus).
///
/// A preset changes the *line and the envelope*. It does not change the dial
/// string, because a phone number is content rather than a setting: nobody
/// wants their number replaced by auditioning a tone colour.
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
            // The defaults, and deliberately not a clean setting: this is a
            // telephone plugin, so out of the box it is a telephone --
            // mu-law at 8 kHz through the 300-3400 toll band.
            "Handset -- G.711 as it comes",
            {}
        },
        // -------------------------------------------------------------------
        {
            // Section 7's genuinely clean setting: every part of the line
            // switched off, which is bit-exact identity rather than a
            // transparent-sounding approximation of it.
            "Clean -- no line at all",
            {
                { ids::codec, 0.0f },
                { ids::rate, 0.0f },
                { ids::band, 0.0f },
            }
        },
        // -------------------------------------------------------------------
        {
            // A payphone: the same channel, a worse handset, and enough hiss
            // to hear the copper.
            "Payphone -- narrow, and you can hear the line",
            {
                { ids::band, 3.0f },      // handset 500-2800
                { ids::noise, 0.35f },
                { ids::level, -2.0f },
            }
        },
        // -------------------------------------------------------------------
        {
            // A bad long-distance line: fewer samples, fewer bits, more hiss.
            // Six bits of mu-law is the robbed-bit sound taken further than
            // any real span took it.
            "Bad line -- 4 kHz, six bits, hiss",
            {
                { ids::rate, 7.0f },      // 4 kHz
                { ids::bits, 6.0f },
                { ids::noise, 0.55f },
                { ids::band, 3.0f },
            }
        },
        // -------------------------------------------------------------------
        {
            // What "HD voice" widened it to: G.722's 16 kHz and 50-7000 Hz.
            // Cleaner, and still unmistakably a telephone.
            "Wideband -- G.722, the modern call",
            {
                { ids::rate, 3.0f },      // 16 kHz
                { ids::band, 2.0f },      // wideband 50-7000
            }
        },
        // -------------------------------------------------------------------
        {
            // A speakerphone, or a phone on the table across the room.
            "Speakerphone -- across the room",
            {
                { ids::band, 4.0f },      // 700-2200
                { ids::noise, 0.25f },
                { ids::codec, 2.0f },     // A-law
            }
        },
        // -------------------------------------------------------------------
        {
            // The European side of G.711, on a machine that has been in a
            // cupboard since 1994.
            "Answering machine -- A-law and dust",
            {
                { ids::codec, 2.0f },     // A-law
                { ids::bits, 7.0f },
                { ids::noise, 0.4f },
                { ids::region, 1.0f },    // and the tones to match
            }
        },
        // -------------------------------------------------------------------
        {
            // Pulse dialling, and the inter-digit pause a real rotary dial
            // needs -- about 700 ms, because the dial has to return.
            "Rotary -- pulse dialling, 700 ms between digits",
            {
                { ids::dialMode, 1.0f },
                { ids::dialGap, 700.0f },
                { ids::band, 3.0f },
                { ids::noise, 0.2f },
            }
        },
        // -------------------------------------------------------------------
        {
            // The keypad as a pad: long envelopes, no cadence, so every key
            // is a sustained two-tone drone and the call-progress row becomes
            // a chord.
            "Held tones -- the keypad as a pad",
            {
                { ids::cadence, 2.0f },   // steady
                { ids::attack, 400.0f },
                { ids::release, 900.0f },
                { ids::band, 0.0f },
                { ids::codec, 0.0f },
                { ids::rate, 0.0f },
            }
        },
        // -------------------------------------------------------------------
        {
            // The crunch, which is the *opposite* of authenticity and is on
            // purpose: with no band limit in front of it, the rate reduction
            // has everything to fold and does. Linear coding rather than
            // companded, because a fixed step is what makes a decay fizz.
            "Crunch -- no band limit, so the images come back",
            {
                { ids::band, 0.0f },
                { ids::rate, 5.0f },      // 8 kHz
                { ids::codec, 3.0f },     // linear
                { ids::bits, 6.0f },
            }
        },
        // -------------------------------------------------------------------
        {
            // Named for what it does rather than what would read better in a
            // list: with nothing played the output is bit-exact zero, and a
            // key through it is 40 dB down. Quiet, not silent.
            "Idle reference -- 40 dB down",
            {
                { ids::level, -40.0f },
            }
        },
    };

    return list;
}
} // namespace

// The choice lists and the enums are married at compile time. A list that
// grew an entry its enum does not have -- or the reverse -- is a control that
// silently selects the wrong thing, and nothing else would catch it (section
// 8).
static_assert (std::size (choices::regions) == 2,
               "The region list is APPEND-ONLY and matches Region.");
static_assert (std::size (choices::cadences) == 3,
               "The cadence list is APPEND-ONLY and matches CadenceMode.");
static_assert (std::size (choices::codecs) == 4,
               "The codec list is APPEND-ONLY and matches dsp::CompandingLaw.");
static_assert (std::size (choices::rates) == static_cast<std::size_t> (kRateCount),
               "The rate list and kRateHz have diverged. Both are APPEND-ONLY.");
static_assert (std::size (choices::bands) == static_cast<std::size_t> (kBandModeCount),
               "The band list and kBandEdges have diverged. Both are APPEND-ONLY.");
static_assert (std::size (choices::dialModes) == 2,
               "The dial mode list is APPEND-ONLY.");

CrossbarProcessor::CrossbarProcessor()
    : juce::AudioProcessor (BusesProperties()
                                .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      state_ (*this, nullptr, kStateTypeName, createParameterLayout())
{
    // A number to dial out of the box, so the DIAL key does something the
    // first time it is pressed. 555-0199 is the North American fiction
    // reservation -- it cannot ring anybody.
    setDialNumber ("555 0199");
}

juce::AudioProcessorValueTreeState::ParameterLayout
CrossbarProcessor::createParameterLayout()
{
    using Parameter = juce::AudioParameterFloat;
    using IntParameter = juce::AudioParameterInt;
    using ChoiceParameter = juce::AudioParameterChoice;
    using Range = juce::NormalisableRange<float>;

    std::vector<std::unique_ptr<juce::RangedAudioParameter>> parameters;

    const auto attributes = [] (const char* label)
    {
        return juce::AudioParameterFloatAttributes{}.withLabel (label);
    };

    const auto list = [] (const auto& names)
    {
        return juce::StringArray (names, static_cast<int> (std::size (names)));
    };

    // ---- TONE ------------------------------------------------------------

    parameters.push_back (std::make_unique<ChoiceParameter> (
        juce::ParameterID { ids::region, kSchemaV1 }, "Region",
        list (choices::regions), 0));

    parameters.push_back (std::make_unique<ChoiceParameter> (
        juce::ParameterID { ids::cadence, kSchemaV1 }, "Cadence",
        list (choices::cadences), 0));

    // ITU-T Q.24's receivers accept 8 dB of normal twist and 4 dB of reverse;
    // the range covers both directions of that, and the default is the +2 dB
    // real transmitters use.
    parameters.push_back (std::make_unique<Parameter> (
        juce::ParameterID { ids::twist, kSchemaV1 }, "Twist",
        Range (-8.0f, 8.0f, 0.01f), 2.0f, attributes ("dB")));

    // The map is 37 keys, so the highest root that still fits under MIDI 127
    // is 91. C1 = 36 is where a drum map starts and where this one does.
    parameters.push_back (std::make_unique<IntParameter> (
        juce::ParameterID { ids::mapRoot, kSchemaV1 }, "Map root",
        0, 127 - kToneCount + 1, kDefaultMapRoot));

    // ---- ENVELOPE --------------------------------------------------------

    parameters.push_back (std::make_unique<Parameter> (
        juce::ParameterID { ids::attack, kSchemaV1 }, "Attack",
        Range (0.0f, 2000.0f, 0.1f, 0.35f), 2.0f, attributes ("ms")));

    parameters.push_back (std::make_unique<Parameter> (
        juce::ParameterID { ids::decay, kSchemaV1 }, "Decay",
        Range (0.0f, 5000.0f, 0.1f, 0.35f), 100.0f, attributes ("ms")));

    parameters.push_back (std::make_unique<Parameter> (
        juce::ParameterID { ids::sustain, kSchemaV1 }, "Sustain",
        Range (0.0f, 1.0f, 0.001f), 1.0f));

    parameters.push_back (std::make_unique<Parameter> (
        juce::ParameterID { ids::release, kSchemaV1 }, "Release",
        Range (0.0f, 5000.0f, 0.1f, 0.35f), 20.0f, attributes ("ms")));

    // ---- LINE ------------------------------------------------------------

    parameters.push_back (std::make_unique<ChoiceParameter> (
        juce::ParameterID { ids::codec, kSchemaV1 }, "Codec",
        list (choices::codecs), 1));

    parameters.push_back (std::make_unique<IntParameter> (
        juce::ParameterID { ids::bits, kSchemaV1 }, "Bits", 1, 16, 8));

    parameters.push_back (std::make_unique<ChoiceParameter> (
        juce::ParameterID { ids::rate, kSchemaV1 }, "Rate",
        list (choices::rates), kDefaultRateIndex));

    parameters.push_back (std::make_unique<ChoiceParameter> (
        juce::ParameterID { ids::band, kSchemaV1 }, "Band",
        list (choices::bands), 1));

    parameters.push_back (std::make_unique<Parameter> (
        juce::ParameterID { ids::noise, kSchemaV1 }, "Line noise",
        Range (0.0f, 1.0f, 0.001f), 0.0f));

    // ---- DIAL ------------------------------------------------------------

    parameters.push_back (std::make_unique<ChoiceParameter> (
        juce::ParameterID { ids::dialMode, kSchemaV1 }, "Dial mode",
        list (choices::dialModes), 0));

    parameters.push_back (std::make_unique<Parameter> (
        juce::ParameterID { ids::dialDigit, kSchemaV1 }, "Digit",
        Range (20.0f, 500.0f, 1.0f), 100.0f, attributes ("ms")));

    // Up to a second, because a rotary dial's inter-digit pause is about 700
    // ms and a plugin that could not reach it would be lying about the mode.
    parameters.push_back (std::make_unique<Parameter> (
        juce::ParameterID { ids::dialGap, kSchemaV1 }, "Gap",
        Range (20.0f, 1000.0f, 1.0f), 100.0f, attributes ("ms")));

    // ---- OUTPUT ----------------------------------------------------------

    parameters.push_back (std::make_unique<Parameter> (
        juce::ParameterID { ids::level, kSchemaV1 }, "Level",
        Range (-60.0f, 12.0f, 0.1f), 0.0f, attributes ("dB")));

    return { parameters.begin(), parameters.end() };
}

bool CrossbarProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    return layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
}

void CrossbarProcessor::prepareToPlay (double sampleRate, int maximumExpectedSamplesPerBlock)
{
    sampleRate_ = sampleRate;

    engine_.prepare (sampleRate);
    scratch_.setSize (2, juce::jmax (16, maximumExpectedSamplesPerBlock), false, false, true);

    // prepare() resets, so the dial string has to be pushed again -- what
    // prepare built must be re-checked against what it actually built rather
    // than against whether anything has been set yet (the Emberdrive lesson,
    // section 7).
    dialPending_.store (true);

    prepared_ = true;
}

void CrossbarProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    processInternal (buffer, midi);
}

void CrossbarProcessor::processBlock (juce::AudioBuffer<double>& buffer, juce::MidiBuffer& midi)
{
    processInternal (buffer, midi);
}

void CrossbarProcessor::pullParameters()
{
    const auto choice = [this] (const char* id, int count)
    {
        return juce::jlimit (0, count - 1, static_cast<int> (valueOf (state_, id)));
    };

    settings_.region = static_cast<Region> (choice (ids::region, 2));
    settings_.cadence = static_cast<CadenceMode> (choice (ids::cadence, 3));
    settings_.twistDb = valueOf (state_, ids::twist);
    settings_.mapRoot = static_cast<int> (valueOf (state_, ids::mapRoot));

    settings_.attackSeconds = valueOf (state_, ids::attack) * 0.001;
    settings_.decaySeconds = valueOf (state_, ids::decay) * 0.001;
    settings_.sustain = valueOf (state_, ids::sustain);
    settings_.releaseSeconds = valueOf (state_, ids::release) * 0.001;

    settings_.codec = static_cast<dsp::CompandingLaw> (choice (ids::codec, 4));
    settings_.bits = static_cast<int> (valueOf (state_, ids::bits));
    settings_.rateIndex = choice (ids::rate, kRateCount);
    settings_.band = static_cast<BandMode> (choice (ids::band, kBandModeCount));
    settings_.noise = valueOf (state_, ids::noise);

    settings_.pulseDial = choice (ids::dialMode, 2) == 1;
    settings_.dialDigitSeconds = valueOf (state_, ids::dialDigit) * 0.001;
    settings_.dialGapSeconds = valueOf (state_, ids::dialGap) * 0.001;

    settings_.levelDb = valueOf (state_, ids::level);

    engine_.setParameters (settings_);
}

void CrossbarProcessor::handleMidi (const juce::MidiMessage& message)
{
    if (message.isNoteOn())
        engine_.noteOn (message.getNoteNumber(), message.getFloatVelocity());
    else if (message.isNoteOff())
        engine_.noteOff (message.getNoteNumber());
    else if (message.isAllNotesOff() || message.isAllSoundOff())
        engine_.allNotesOff();
}

template <typename FloatType>
void CrossbarProcessor::processInternal (juce::AudioBuffer<FloatType>& buffer,
                                         juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals guard;

    const int numSamples = buffer.getNumSamples();

    buffer.clear();

    if (numSamples <= 0 || ! prepared_)
        return;

    pullParameters();

    // The dial string, if the message thread has left a new one. A spin lock
    // held for a memcpy of at most 49 bytes -- the audio thread never touches
    // a juce::String and never allocates.
    if (dialPending_.load())
    {
        const juce::SpinLock::ScopedTryLockType lock (dialLock_);

        if (lock.isLocked())
        {
            engine_.setDialString (pendingDial_);
            dialPending_.store (false);
        }
    }

    if (dialRequested_.exchange (false))
        engine_.noteOn (noteForTone (Tone::dialNumber, settings_.mapRoot), 1.0);

    servicePanelKeys();

    if (scratch_.getNumSamples() < numSamples)
        scratch_.setSize (2, numSamples, false, false, true);

    double* const left = scratch_.getWritePointer (0);
    double* const right = scratch_.getWritePointer (1);

    // Sample-accurate MIDI: the render is cut at every event, so a key lands
    // where it was played rather than at the start of the next block. The
    // engine has no control boundary of its own -- cadences, the dialler, the
    // smoothers and the line all count samples -- so 64 and 512 sample blocks
    // measure bit-identical.
    int rendered = 0;

    auto renderSpan = [&] (int from, int count)
    {
        engine_.process (left + from, right + from, count);

        for (int i = 0; i < count; ++i)
        {
            buffer.setSample (0, from + i, static_cast<FloatType> (left[from + i]));
            buffer.setSample (1, from + i, static_cast<FloatType> (right[from + i]));
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
    soundingTones_.store (engine_.getSoundingToneMask());
}

void CrossbarProcessor::servicePanelKeys()
{
    // `pressed` is OR'd in rather than replaced, so a key pressed and released
    // between two blocks still gets a note-on: without it a quick click on the
    // panel does nothing at all, which is the sort of thing that reads as a
    // broken keypad rather than as a race.
    const std::uint64_t pressed = panelPressed_.exchange (0);
    const std::uint64_t held = panelHeld_.load();

    const std::uint64_t starting = (pressed | held) & ~panelPrevious_;

    for (int i = 0; i < kToneCount; ++i)
        if ((starting & (std::uint64_t { 1 } << i)) != 0)
            engine_.noteOn (noteForTone (static_cast<Tone> (i), settings_.mapRoot), 1.0);

    panelPrevious_ |= starting;

    const std::uint64_t stopping = panelPrevious_ & ~held;

    for (int i = 0; i < kToneCount; ++i)
        if ((stopping & (std::uint64_t { 1 } << i)) != 0)
            engine_.noteOff (noteForTone (static_cast<Tone> (i), settings_.mapRoot));

    panelPrevious_ &= held;
}

double CrossbarProcessor::getEffectiveRateHz() const
{
    auto* raw = state_.getRawParameterValue (ids::rate);

    if (raw == nullptr)
        return sampleRate_;

    const int index = juce::jlimit (0, kRateCount - 1, static_cast<int> (raw->load()));
    const double target = kRateHz[index];

    // The engine's rule, restated: a ratio below 1 is bypassed exactly, so
    // asking for a rate at or above the host's does nothing at all.
    return target <= 0.0 ? sampleRate_ : std::min (sampleRate_, target);
}

double CrossbarProcessor::getTailLengthSeconds() const
{
    if (auto* release = state_.getRawParameterValue (ids::release))
        return static_cast<double> (release->load()) * 0.001;

    return 5.0;   // the widest the range reaches
}

void CrossbarProcessor::setPanelKeyHeld (int toneIndex, bool held) noexcept
{
    if (toneIndex < 0 || toneIndex >= kToneCount)
        return;

    const std::uint64_t bit = std::uint64_t { 1 } << toneIndex;

    if (held)
    {
        panelPressed_.fetch_or (bit);
        panelHeld_.fetch_or (bit);
    }
    else
    {
        panelHeld_.fetch_and (~bit);
    }
}

// ---------------------------------------------------------------------------
// The dial string
// ---------------------------------------------------------------------------

void CrossbarProcessor::setDialNumber (const juce::String& text)
{
    dialNumber_ = text.substring (0, Dialler::kMaxDigits * 2);

    const auto utf8 = dialNumber_.toRawUTF8();

    {
        const juce::SpinLock::ScopedLockType lock (dialLock_);

        std::memset (pendingDial_, 0, sizeof (pendingDial_));
        std::strncpy (pendingDial_, utf8, sizeof (pendingDial_) - 1);
    }

    dialPending_.store (true);
}

// ---------------------------------------------------------------------------
// Presets
// ---------------------------------------------------------------------------

int CrossbarProcessor::getNumPrograms()
{
    return static_cast<int> (presets().size());
}

const juce::String CrossbarProcessor::getProgramName (int index)
{
    const auto& list = presets();

    return list[static_cast<std::size_t> (
        juce::jlimit (0, static_cast<int> (list.size()) - 1, index))].name;
}

void CrossbarProcessor::setCurrentProgram (int index)
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

void CrossbarProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = state_.copyState();

    state.setProperty ("schemaVersion", kStateSchemaVersion, nullptr);
    state.setProperty (kDialNumberProperty, dialNumber_, nullptr);
    state.setProperty (ui::stateIds::tooltipsEnabled, tooltipsEnabled_, nullptr);

    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void CrossbarProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    auto xml = getXmlFromBinary (data, sizeInBytes);

    if (xml == nullptr || ! xml->hasTagName (state_.state.getType()))
        return;

    const auto tree = juce::ValueTree::fromXml (*xml);

    state_.replaceState (tree);

    tooltipsEnabled_ = tree.getProperty (ui::stateIds::tooltipsEnabled, true);

    setDialNumber (tree.getProperty (kDialNumberProperty, "555 0199").toString());
}

juce::AudioProcessorEditor* CrossbarProcessor::createEditor()
{
    return new CrossbarEditor (*this);
}

} // namespace tezla::crossbar

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new tezla::crossbar::CrossbarProcessor();
}
