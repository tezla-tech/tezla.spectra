// Copyright (c) 2026 The Tezla <thetezla@proton.me>
// Created by The Tezla -- https://github.com/wingit33/tezla.tech
// Music: https://soundcloud.com/thetezla | https://thetezla.bandcamp.com
// Built with development assistance from Claude (Anthropic).
// SPDX-License-Identifier: AGPL-3.0-only
// GNU AGPLv3 (see LICENSE), plus NOTICE.md's attribution term. Keep intact.

#include "PluginProcessor.h"
#include "PluginEditor.h"

#include <algorithm>
#include <cmath>

#include <tezla/dsp/ScalaFile.hpp>
#include <tezla/ui/StateIds.hpp>

namespace tezla::malleus {

namespace
{
constexpr auto kStateTypeName = "MalleusState";
constexpr int kSchemaV1 = 1;
constexpr int kStateSchemaVersion = kSchemaV1;

// The same property names as Sonitus and Svarayantra, so the tuning
// workflow reads identically across all three instruments.
constexpr auto kScaleTextProperty = "scalaText";
constexpr auto kScaleNameProperty = "scaleName";
constexpr auto kKeyboardMapProperty = "keyboardMapText";
constexpr auto kConcertPitchProperty = "concertPitch";

// const, because the editor's mode-stack preview reads parameters from a
// const processor: getRawParameterValue is itself const, so nothing here
// needs to pretend otherwise.
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
/// preset is applied by resetting every parameter to its default first --
/// so a preset is a complete parameter set rather than a patch over
/// whatever was loaded before it (the Sonitus pattern, and the reason
/// section 8 requires every parameter to default to neutral).
///
/// A preset changes the SOUND, not the tuning. Slendro Gongs and BP Bell
/// Choir are built to be played with those scales loaded, and they say so
/// in their names, but loading a scale stays the player's act: a scale
/// outlives the patch being auditioned.
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
            // Deliberately first and deliberately empty: this IS the
            // defaults -- a soft mallet on a plain bar, nothing exotic
            // switched on. The genuinely clean setting section 7 asks for.
            "Init -- one struck bar",
            {}
        },
        // -------------------------------------------------------------------
        {
            // The rig's headline trick: a struck membrane whose tension
            // falls a fifth over 90 ms. That glide IS the 808, and here it
            // is the physics rather than a pitch envelope on a sine.
            "Physical 808 -- skin, then sub",
            {
                { ids::material, 2.0f },        // membrane
                { ids::partials, 20.0f },
                { ids::decay, 1.6f },
                { ids::tilt, 0.75f },           // the skin's top dies fast
                { ids::position, 0.42f },
                { ids::hardness, 0.32f },
                { ids::noiseAmount, 0.35f },
                { ids::dropDepth, 7.0f },
                { ids::dropTime, 0.09f },
            }
        },
        // -------------------------------------------------------------------
        {
            // A tabla's gliss is the same mechanism played by a hand that
            // keeps pressing: deeper, slower, and struck off-centre.
            "Tabla Drop -- the hand stays on",
            {
                { ids::material, 2.0f },
                { ids::partials, 28.0f },
                { ids::decay, 0.9f },
                { ids::tilt, 0.55f },
                { ids::position, 0.28f },
                { ids::hardness, 0.55f },
                { ids::noiseAmount, 0.22f },
                { ids::dropDepth, 12.0f },
                { ids::dropTime, 0.22f },
                { ids::sympCount, 4.0f },
                { ids::sympLevel, 0.28f },
                { ids::sympDecay, 3.0f },
            }
        },
        // -------------------------------------------------------------------
        {
            // Neuro stabs want inharmonic and RIGID: a hard beater on a
            // bar, stretched past any real metal, with the partials locked
            // onto whatever scale the track is in so the stab agrees with
            // the bass instead of fighting it.
            "Neuro Stab -- locked and stretched",
            {
                { ids::material, 1.0f },        // bar
                { ids::stretch, 0.65f },
                { ids::lockAmount, 1.0f },
                { ids::partials, 40.0f },
                { ids::decay, 0.55f },
                { ids::tilt, 0.35f },
                { ids::position, 0.13f },
                { ids::hardness, 0.95f },
                { ids::noiseAmount, 0.18f },
            }
        },
        // -------------------------------------------------------------------
        {
            // Load a slendro scale and this is a gamelan that agrees with
            // itself: the gongs' own partials land on the same five degrees
            // the keyboard plays. No commercial modeller does this.
            "Slendro Gongs -- load a slendro scale",
            {
                { ids::material, 3.4f },        // plate into bell
                { ids::stretch, -0.12f },
                { ids::lockAmount, 0.85f },
                { ids::partials, 48.0f },
                { ids::decay, 6.5f },
                { ids::tilt, 0.42f },
                { ids::position, 0.19f },
                { ids::hardness, 0.28f },
                { ids::noiseAmount, 0.12f },
                { ids::sympCount, 5.0f },
                { ids::sympLevel, 0.42f },
                { ids::sympCoupling, 0.7f },
                { ids::sympDecay, 9.0f },
            }
        },
        // -------------------------------------------------------------------
        {
            // Bells are already inharmonic; Bohlen-Pierce has no octave.
            // Lock one to the other and the hum, tierce and quint all land
            // on tritave degrees -- a bell choir for a scale that has never
            // had one.
            "BP Bell Choir -- load Bohlen-Pierce",
            {
                { ids::material, 4.0f },        // bell
                { ids::lockAmount, 1.0f },
                { ids::partials, 44.0f },
                { ids::decay, 8.0f },
                { ids::tilt, 0.3f },
                { ids::position, 0.23f },
                { ids::hardness, 0.62f },
                { ids::sympCount, 6.0f },
                { ids::sympLevel, 0.3f },
                { ids::sympDecay, 12.0f },
                { ids::sympBrightness, 0.75f },
            }
        },
        // -------------------------------------------------------------------
        {
            // The bow on an object no bow can reach: a bowl held between
            // plate and bell, sustaining on stick-slip alone. Hold a key.
            "Bowed Bowl Drone -- hold a key",
            {
                { ids::exciter, 3.0f },         // Bow
                { ids::material, 3.2f },
                { ids::stretch, 0.08f },
                { ids::partials, 32.0f },
                { ids::decay, 5.0f },
                { ids::tilt, 0.5f },
                { ids::position, 0.31f },
                { ids::bowPressure, 0.42f },
                { ids::bowSpeed, 0.55f },
                { ids::sympCount, 7.0f },
                { ids::sympLevel, 0.35f },
                { ids::sympDrone, 0.25f },
                { ids::sympDecay, 14.0f },
            }
        },
        // -------------------------------------------------------------------
        {
            // Twelve strings tuned to the scale, ringing under a plucked
            // object that barely speaks for itself -- the taraf IS the
            // patch. Play sparsely and let it answer.
            "Sitar Cloud -- the strings answer",
            {
                { ids::exciter, 1.0f },         // Pluck
                { ids::material, 0.35f },       // string into bar
                { ids::stretch, 0.18f },
                { ids::partials, 36.0f },
                { ids::decay, 1.1f },
                { ids::tilt, 0.6f },
                { ids::position, 0.11f },
                { ids::sympCount, 12.0f },
                { ids::sympLevel, 0.72f },
                { ids::sympCoupling, 0.95f },
                { ids::sympDrone, 0.18f },
                { ids::sympDecay, 16.0f },
                { ids::sympBrightness, 0.8f },
            }
        },
        // -------------------------------------------------------------------
        {
            // A hard beater on a short, bright bar: the marimba's bar with
            // the wood taken out of it.
            "Glass Marimba -- hard and short",
            {
                { ids::material, 1.0f },
                { ids::stretch, 0.05f },
                { ids::partials, 24.0f },
                { ids::decay, 0.75f },
                { ids::tilt, 0.85f },
                { ids::position, 0.5f },        // even modes on the node
                { ids::hardness, 0.88f },
                { ids::noiseAmount, 0.08f },
            }
        },
        // -------------------------------------------------------------------
        {
            // A dropped mallet accelerating into a buzz, on a membrane --
            // the bouncing-ball clock doing what no LFO can.
            "Dropped Mallet -- the bounce",
            {
                { ids::exciter, 2.0f },         // Roll
                { ids::material, 2.2f },
                { ids::partials, 24.0f },
                { ids::decay, 0.7f },
                { ids::tilt, 0.65f },
                { ids::position, 0.36f },
                { ids::hardness, 0.72f },
                { ids::noiseAmount, 0.3f },
                { ids::rollStart, 0.11f },
                { ids::rollRatio, 0.74f },
                { ids::rollMinimum, 0.022f },
                { ids::rollHumanise, 0.45f },
            }
        },
        // -------------------------------------------------------------------
        {
            // The reference patch for a noise-floor check: everything that
            // can ring or sustain switched off, trim at the floor.
            //
            // Precisely what it guarantees, because the difference matters:
            // with nothing played the output is BIT-EXACT zero -- not a
            // small number, exactly zero, and measured that way in
            // tests/test_MalleusEngine.cpp. A note played through this
            // patch is 60 dB down, which is quiet, not silent. Load it to
            // prove the plugin adds nothing while idle; do not read it as
            // a mute.
            "Idle reference -- adds nothing",
            {
                { ids::outputTrim, -60.0f },
                { ids::noiseAmount, 0.0f },
                { ids::sympCount, 0.0f },
                { ids::sympLevel, 0.0f },
                { ids::sympDrone, 0.0f },
            }
        },
    };

    return list;
}
} // namespace

// The choice list and the enum are married at compile time: a reordered
// enum would silently repoint every saved patch's exciter (section 8).
static_assert (static_cast<int> (Exciter::count)
                 == static_cast<int> (std::size (exciterNames::list)),
               "The exciter choice list and the Exciter enum have diverged. "
               "The list is APPEND-ONLY: new entries go on the end.");

MalleusProcessor::MalleusProcessor()
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

juce::AudioProcessorValueTreeState::ParameterLayout
MalleusProcessor::createParameterLayout()
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

    // ---- OBJECT ----------------------------------------------------------

    parameters.push_back (std::make_unique<Parameter> (
        juce::ParameterID { ids::material, kSchemaV1 }, "Material",
        Range (0.0f, 4.0f, 0.001f), 1.0f));

    parameters.push_back (std::make_unique<Parameter> (
        juce::ParameterID { ids::stretch, kSchemaV1 }, "Stretch",
        Range (-0.5f, 2.0f, 0.001f), 0.0f));

    parameters.push_back (std::make_unique<Parameter> (
        juce::ParameterID { ids::lockAmount, kSchemaV1 }, "Overtone lock",
        Range (0.0f, 1.0f, 0.001f), 0.0f, attributes ("")));

    parameters.push_back (std::make_unique<IntParameter> (
        juce::ParameterID { ids::partials, kSchemaV1 }, "Partials", 8, 64, 32));

    // Decay is skewed so the musical short end -- where percussion lives --
    // occupies half the travel rather than the first eighth of it.
    parameters.push_back (std::make_unique<Parameter> (
        juce::ParameterID { ids::decay, kSchemaV1 }, "Decay",
        Range (0.05f, 20.0f, 0.001f, 0.3f), 2.0f, attributes ("s")));

    parameters.push_back (std::make_unique<Parameter> (
        juce::ParameterID { ids::tilt, kSchemaV1 }, "Damping tilt",
        Range (0.0f, 1.0f, 0.001f), 0.5f));

    parameters.push_back (std::make_unique<Parameter> (
        juce::ParameterID { ids::position, kSchemaV1 }, "Position",
        Range (0.01f, 0.99f, 0.001f), 0.29f));

    // ---- EXCITE ----------------------------------------------------------

    parameters.push_back (std::make_unique<ChoiceParameter> (
        juce::ParameterID { ids::exciter, kSchemaV1 }, "Exciter",
        juce::StringArray (exciterNames::list,
                           static_cast<int> (std::size (exciterNames::list))),
        0));

    parameters.push_back (std::make_unique<Parameter> (
        juce::ParameterID { ids::hardness, kSchemaV1 }, "Hardness",
        Range (0.0f, 1.0f, 0.001f), 0.5f));

    parameters.push_back (std::make_unique<Parameter> (
        juce::ParameterID { ids::noiseAmount, kSchemaV1 }, "Scrape",
        Range (0.0f, 1.0f, 0.001f), 0.0f));

    parameters.push_back (std::make_unique<Parameter> (
        juce::ParameterID { ids::dropDepth, kSchemaV1 }, "Drop depth",
        Range (-24.0f, 24.0f, 0.01f), 0.0f, attributes ("st")));

    parameters.push_back (std::make_unique<Parameter> (
        juce::ParameterID { ids::dropTime, kSchemaV1 }, "Drop time",
        Range (0.005f, 2.0f, 0.001f, 0.4f), 0.08f, attributes ("s")));

    parameters.push_back (std::make_unique<Parameter> (
        juce::ParameterID { ids::bowPressure, kSchemaV1 }, "Bow pressure",
        Range (0.0f, 1.0f, 0.001f), 0.35f));

    parameters.push_back (std::make_unique<Parameter> (
        juce::ParameterID { ids::bowSpeed, kSchemaV1 }, "Bow speed",
        Range (0.0f, 1.0f, 0.001f), 0.5f));

    parameters.push_back (std::make_unique<Parameter> (
        juce::ParameterID { ids::rollStart, kSchemaV1 }, "Roll start",
        Range (0.01f, 1.0f, 0.001f, 0.5f), 0.09f, attributes ("s")));

    parameters.push_back (std::make_unique<Parameter> (
        juce::ParameterID { ids::rollRatio, kSchemaV1 }, "Roll ratio",
        Range (0.4f, 1.4f, 0.001f), 0.72f));

    parameters.push_back (std::make_unique<Parameter> (
        juce::ParameterID { ids::rollMinimum, kSchemaV1 }, "Roll floor",
        Range (0.005f, 0.25f, 0.001f, 0.5f), 0.028f, attributes ("s")));

    parameters.push_back (std::make_unique<Parameter> (
        juce::ParameterID { ids::rollHumanise, kSchemaV1 }, "Roll humanise",
        Range (0.0f, 1.0f, 0.001f), 0.35f));

    // ---- RESONANCE -------------------------------------------------------

    // Zero strings is the default and it is the neutral one: a project
    // saved before this existed reopens with no taraf, sounding the same.
    parameters.push_back (std::make_unique<IntParameter> (
        juce::ParameterID { ids::sympCount, kSchemaV1 }, "Sympathetic strings",
        0, SympatheticBank::kMaxStrings, 0));

    parameters.push_back (std::make_unique<IntParameter> (
        juce::ParameterID { ids::sympRoot, kSchemaV1 }, "Sympathetic root",
        0, 115, 48));

    parameters.push_back (std::make_unique<Parameter> (
        juce::ParameterID { ids::sympLevel, kSchemaV1 }, "Sympathetic level",
        Range (0.0f, 1.0f, 0.001f), 0.5f));

    parameters.push_back (std::make_unique<Parameter> (
        juce::ParameterID { ids::sympCoupling, kSchemaV1 }, "Sympathetic coupling",
        Range (0.0f, 1.0f, 0.001f), 0.6f));

    parameters.push_back (std::make_unique<Parameter> (
        juce::ParameterID { ids::sympDrone, kSchemaV1 }, "Drone",
        Range (0.0f, 1.0f, 0.001f), 0.0f));

    parameters.push_back (std::make_unique<Parameter> (
        juce::ParameterID { ids::sympDecay, kSchemaV1 }, "Sympathetic decay",
        Range (0.1f, 30.0f, 0.01f, 0.4f), 8.0f, attributes ("s")));

    parameters.push_back (std::make_unique<Parameter> (
        juce::ParameterID { ids::sympBrightness, kSchemaV1 }, "Sympathetic brightness",
        Range (0.0f, 1.0f, 0.001f), 0.6f));

    // ---- OUTPUT ----------------------------------------------------------

    parameters.push_back (std::make_unique<Parameter> (
        juce::ParameterID { ids::outputTrim, kSchemaV1 }, "Output trim",
        Range (-60.0f, 24.0f, 0.01f), 0.0f, attributes ("dB")));

    return { parameters.begin(), parameters.end() };
}

bool MalleusProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    return layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
}

void MalleusProcessor::prepareToPlay (double sampleRate, int maximumExpectedSamplesPerBlock)
{
    sampleRate_ = sampleRate;

    engine_.prepare (sampleRate);
    scratch_.setSize (2, juce::jmax (16, maximumExpectedSamplesPerBlock), false, false, true);

    // The tuning survives a prepare; publish it again so the freshly reset
    // engine picks it up at the first block (the Emberdrive lesson: what
    // prepare() builds must be re-checked against what it actually built,
    // not against whether parameters have been seen).
    publishTuning();

    prepared_ = true;
}

void MalleusProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    processInternal (buffer, midi);
}

void MalleusProcessor::processBlock (juce::AudioBuffer<double>& buffer, juce::MidiBuffer& midi)
{
    processInternal (buffer, midi);
}

void MalleusProcessor::pullParameters()
{
    auto& settings = engine_.settings();

    settings.material = valueOf (state_, ids::material);
    settings.stretch = valueOf (state_, ids::stretch);
    settings.lockAmount = valueOf (state_, ids::lockAmount);
    settings.partials = static_cast<int> (valueOf (state_, ids::partials));
    settings.decaySeconds = valueOf (state_, ids::decay);
    settings.tilt = valueOf (state_, ids::tilt);
    settings.position = valueOf (state_, ids::position);

    const auto exciterIndex = static_cast<int> (valueOf (state_, ids::exciter));
    settings.exciter = static_cast<Exciter> (
        juce::jlimit (0, static_cast<int> (Exciter::count) - 1, exciterIndex));

    settings.hardness = valueOf (state_, ids::hardness);
    settings.noiseAmount = valueOf (state_, ids::noiseAmount);
    settings.dropSemitones = valueOf (state_, ids::dropDepth);
    settings.dropSeconds = valueOf (state_, ids::dropTime);
    settings.bowPressure = valueOf (state_, ids::bowPressure);
    settings.bowSpeed = valueOf (state_, ids::bowSpeed);
    settings.rollStartSeconds = valueOf (state_, ids::rollStart);
    settings.rollRatio = valueOf (state_, ids::rollRatio);
    settings.rollMinimumSeconds = valueOf (state_, ids::rollMinimum);
    settings.rollHumanise = valueOf (state_, ids::rollHumanise);

    // setSympathetic and setSympatheticRoot both retune the bank, which is
    // arithmetic and allocation-free -- safe here, every block.
    engine_.setSympatheticRoot (static_cast<int> (valueOf (state_, ids::sympRoot)));
    engine_.setSympathetic (static_cast<int> (valueOf (state_, ids::sympCount)),
                            valueOf (state_, ids::sympLevel),
                            valueOf (state_, ids::sympCoupling),
                            valueOf (state_, ids::sympDrone),
                            valueOf (state_, ids::sympDecay),
                            valueOf (state_, ids::sympBrightness));

    outputGain_ = std::pow (10.0, valueOf (state_, ids::outputTrim) / 20.0);
}

void MalleusProcessor::handleMidi (const juce::MidiMessage& message)
{
    if (message.isNoteOn())
        engine_.noteOn (message.getNoteNumber(), message.getFloatVelocity());
    else if (message.isNoteOff())
        engine_.noteOff (message.getNoteNumber());
    else if (message.isAllNotesOff() || message.isAllSoundOff())
        engine_.allNotesOff();
}

template <typename FloatType>
void MalleusProcessor::processInternal (juce::AudioBuffer<FloatType>& buffer,
                                        juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals guard;

    const int numSamples = buffer.getNumSamples();

    buffer.clear();

    if (numSamples <= 0 || ! prepared_)
        return;

    pullParameters();

    // The tuning first, so a note-on in this block plays -- and locks its
    // partials onto -- the scale the player just loaded.
    collectTuning();

    if (scratch_.getNumSamples() < numSamples)
        scratch_.setSize (2, numSamples, false, false, true);

    double* const engineLeft = scratch_.getWritePointer (0);
    double* const engineRight = scratch_.getWritePointer (1);

    // Sample-accurate MIDI: the render is cut at every event, so a hit
    // lands where it was played rather than at the start of the next block.
    // The engine cuts again internally at its own control boundary, so
    // neither the host's buffer size nor the event spacing can bend the
    // sound (measured bit-identical at 64 and 512 samples).
    int rendered = 0;

    auto renderSpan = [&] (int from, int count)
    {
        // **Two listening points on the object, not a widener.** Section 7
        // rules out independent per-channel nonlinearity, and this is not
        // that: there is one object, struck once, run through one mode bank
        // and one vactrol, and the two channels are two places to stand in
        // front of it. With Listen at 0 -- the default -- both taps are the
        // same number and this is the mono engine that shipped, byte for byte.
        engine_.process (engineLeft, engineRight, count);

        for (int i = 0; i < count; ++i)
        {
            buffer.setSample (0, from + i,
                              static_cast<FloatType> (engineLeft[i] * outputGain_));
            buffer.setSample (1, from + i,
                              static_cast<FloatType> (engineRight[i] * outputGain_));
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

    activeVoices_.store (engine_.activeVoiceCount());
}

double MalleusProcessor::getTailLengthSeconds() const
{
    // Whatever can still be sounding after the last note off: a voice's
    // ring, bounded by its decay, or the taraf's, whichever is longer. The
    // taraf usually wins -- it is the thing that outlives the hit.
    auto* voiceDecay = state_.getRawParameterValue (ids::decay);
    auto* sympDecay = state_.getRawParameterValue (ids::sympDecay);

    if (voiceDecay == nullptr || sympDecay == nullptr)
        return 30.0;   // the widest either range reaches

    return std::max (static_cast<double> (voiceDecay->load()),
                     static_cast<double> (sympDecay->load()));
}

MalleusProcessor::ModeStack MalleusProcessor::snapshotModeStack() const
{
    ModeStack stack;

    // A sounding voice is the truth, so it wins.
    for (int index = 0; index < MalleusEngine::kMaxVoices; ++index)
    {
        const auto& voice = engine_.voiceForTest (index);

        if (! voice.isActive())
            continue;

        stack.frequencies.clear();

        for (int mode = 0; mode < voice.getPartialCount(); ++mode)
            if (voice.modeGain (mode) > 0.0)
                stack.frequencies.push_back (voice.modeFrequency (mode));

        if (! stack.frequencies.empty())
        {
            stack.sounding = true;
            return stack;
        }
    }

    // Nothing playing: draw the object the current settings DESCRIBE,
    // at the tuning's own root. Turning Material, Stretch or Overtone Lock
    // with no key held is exactly when a player wants to see the stack
    // move, and an empty box would teach them nothing. Computed through
    // the same buildModeFrequencies() the voice uses, so the preview and
    // the sound cannot disagree.
    const double root = previewTuning_.frequencyFor (previewTuning_.getRootNote());

    if (root <= 0.0)
        return stack;

    VoiceSettings settings;
    settings.material = valueOf (state_, ids::material);
    settings.stretch = valueOf (state_, ids::stretch);
    settings.lockAmount = valueOf (state_, ids::lockAmount);
    settings.partials = static_cast<int> (valueOf (state_, ids::partials));

    double frequencies[dsp::ModalResonator::kMaxModes] {};

    const int audible = buildModeFrequencies (frequencies, settings, root, scale_,
                                              sampleRate_);

    (void) audible;

    const double ceiling = 0.45 * sampleRate_;

    for (int mode = 0; mode < settings.partials
                       && mode < dsp::ModalResonator::kMaxModes; ++mode)
        if (frequencies[mode] < ceiling)
            stack.frequencies.push_back (frequencies[mode]);

    return stack;
}

// ---------------------------------------------------------------------------
// Presets
// ---------------------------------------------------------------------------

int MalleusProcessor::getNumPrograms()
{
    return static_cast<int> (presets().size());
}

const juce::String MalleusProcessor::getProgramName (int index)
{
    const auto& list = presets();

    return list[static_cast<std::size_t> (
        juce::jlimit (0, static_cast<int> (list.size()) - 1, index))].name;
}

void MalleusProcessor::setCurrentProgram (int index)
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
// The tuning (mirrors Sonitus and Svarayantra)
// ---------------------------------------------------------------------------

void MalleusProcessor::publishTuning()
{
    const juce::SpinLock::ScopedLockType lock (tuningLock_);

    pendingScale_ = scale_;
    pendingMap_ = hasKeyboardMap_ ? keyboardMap_ : dsp::KeyboardMap {};
    pendingConcertHz_ = concertPitchHz_;

    tuningPending_.store (true, std::memory_order_release);

    previewTuning_.setScale (scale_);
    previewTuning_.setKeyboardMap (hasKeyboardMap_ ? keyboardMap_ : dsp::KeyboardMap {});
    previewTuning_.setConcertPitch (concertPitchHz_);
}

void MalleusProcessor::collectTuning() noexcept
{
    if (! tuningPending_.load (std::memory_order_acquire))
        return;

    const juce::SpinLock::ScopedTryLockType lock (tuningLock_);

    if (! lock.isLocked())
        return;

    // swapScale rather than setScale: a swap allocates nothing and hands
    // the old scale back for the message thread to destroy. It also retunes
    // the taraf, and every voice struck from here on locks its partials
    // onto the new lattice -- one scale, three readers.
    engine_.swapScale (pendingScale_);
    engine_.tuning().swapKeyboardMap (pendingMap_);
    engine_.tuning().setConcertPitch (pendingConcertHz_);

    tuningPending_.store (false, std::memory_order_release);
}

juce::String MalleusProcessor::loadScalaText (const juce::String& text,
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

juce::String MalleusProcessor::loadKeyboardMapText (const juce::String& text)
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

juce::String MalleusProcessor::selectBuiltInScale (const juce::String& name)
{
    for (const auto& scale : dsp::scales::all())
    {
        if (name == juce::String (scale.name))
        {
            scale_ = scale;
            scaleName_ = name;
            scalaText_.clear();

            publishTuning();
            return {};
        }
    }

    return "No built-in scale is named \"" + name + "\".";
}

void MalleusProcessor::resetTuning()
{
    scale_ = dsp::scales::twelveToneEqual();
    scaleName_ = scale_.name;
    scalaText_.clear();
    keyboardMap_ = {};
    keyboardMapText_.clear();
    hasKeyboardMap_ = false;

    publishTuning();
}

void MalleusProcessor::setConcertPitch (double hz)
{
    concertPitchHz_ = std::clamp (hz, dsp::Tuning::kMinimumConcertHz,
                                  dsp::Tuning::kMaximumConcertHz);
    publishTuning();
}

double MalleusProcessor::previewFrequencyFor (int midiNote) const
{
    return previewTuning_.frequencyFor (midiNote);
}

double MalleusProcessor::getRootHz() const noexcept
{
    return previewTuning_.frequencyFor (previewTuning_.getRootNote());
}

juce::String MalleusProcessor::describeTuning() const
{
    const int root = previewTuning_.getRootNote();
    const double hz = previewTuning_.frequencyFor (root);

    return scaleName_ + "  --  " + juce::String (scale_.ratios.size())
             + " degrees, root " + juce::MidiMessage::getMidiNoteName (root, true, true, 4)
             + " at " + juce::String (hz, 2) + " Hz";
}

// ---------------------------------------------------------------------------
// State
// ---------------------------------------------------------------------------

void MalleusProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = state_.copyState();

    state.setProperty ("schemaVersion", kStateSchemaVersion, nullptr);

    // The tuning travels with the project, as text. A .scl file lives on
    // one machine and a project does not, so storing only its path would
    // open silently detuned somewhere else -- and here it would also
    // relocate every partial of every object.
    state.setProperty (kScaleNameProperty, scaleName_, nullptr);
    state.setProperty (kScaleTextProperty, scalaText_, nullptr);
    state.setProperty (kKeyboardMapProperty, keyboardMapText_, nullptr);
    state.setProperty (kConcertPitchProperty, concertPitchHz_, nullptr);
    state.setProperty (ui::stateIds::tooltipsEnabled, tooltipsEnabled_, nullptr);

    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void MalleusProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    auto xml = getXmlFromBinary (data, sizeInBytes);

    if (xml == nullptr || ! xml->hasTagName (state_.state.getType()))
        return;

    const auto tree = juce::ValueTree::fromXml (*xml);

    state_.replaceState (tree);

    tooltipsEnabled_ = tree.getProperty (ui::stateIds::tooltipsEnabled, true);

    // Concert pitch before the scale, so every publish carries it.
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
}

juce::AudioProcessorEditor* MalleusProcessor::createEditor()
{
    return new MalleusEditor (*this);
}

} // namespace tezla::malleus

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new tezla::malleus::MalleusProcessor();
}
