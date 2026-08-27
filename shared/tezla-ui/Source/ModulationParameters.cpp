#include <tezla/ui/ModulationParameters.hpp>

namespace tezla::ui::modulation
{

namespace
{
using Parameter  = juce::AudioParameterFloat;
using Choice     = juce::AudioParameterChoice;
using Boolean    = juce::AudioParameterBool;
using Attributes = juce::AudioParameterFloatAttributes;

/// A range whose mid-travel lands on `centre`. Musical ranges are logarithmic
/// and a linear one spends most of itself where nobody sets the control.
juce::NormalisableRange<float> skewedRange (float minimum, float maximum, float centre)
{
    juce::NormalisableRange<float> range { minimum, maximum };
    range.setSkewForCentre (centre);
    return range;
}

Attributes formatted (const juce::String& unit, int decimals)
{
    return Attributes()
        .withLabel (unit)
        .withStringFromValueFunction ([unit, decimals] (float value, int)
        {
            return juce::String (value, decimals) + " " + unit;
        })
        .withValueFromStringFunction ([] (const juce::String& text) { return text.getFloatValue(); });
}

Attributes percentAttributes()
{
    return Attributes()
        .withLabel ("%")
        .withStringFromValueFunction ([] (float value, int)
        {
            return juce::String (juce::roundToInt (value * 100.0f)) + " %";
        })
        .withValueFromStringFunction ([] (const juce::String& text)
        {
            return text.getFloatValue() * 0.01f;
        });
}
} // namespace

juce::StringArray sourceNames()
{
    return { "Off", "LFO 1", "LFO 2", "LFO 3", "Env" };
}

juce::StringArray waveNames()
{
    return { "Sine", "Triangle", "Saw up", "Saw down", "Square",
             "Sample & hold", "Smooth random" };
}

juce::StringArray divisionNames()
{
    juce::StringArray names;

    for (const auto& entry : divisions)
        names.add (entry.name);

    return names;
}

void addParameters (juce::AudioProcessorValueTreeState::ParameterLayout& layout,
                    int schemaVersion,
                    const juce::StringArray& destinationNames)
{
    // Eight slots rather than a depth per source per knob. Halo has twenty
    // continuous controls and Emberdrive thirty; a full matrix would be eighty
    // and a hundred and twenty depth parameters, several hundred entries in a
    // host's list, and that many IDs frozen before the UI had been used in
    // anger.
    for (int slot = 0; slot < dsp::Modulation::kNumSlots; ++slot)
    {
        const auto i = static_cast<std::size_t> (slot);
        const auto number = juce::String (slot + 1);

        layout.add (std::make_unique<Choice> (
            juce::ParameterID { modIds::source[i], schemaVersion },
            "Mod " + number + " Source", sourceNames(), 0));

        layout.add (std::make_unique<Choice> (
            juce::ParameterID { modIds::destination[i], schemaVersion },
            "Mod " + number + " Target", destinationNames, 0));

        layout.add (std::make_unique<Parameter> (
            juce::ParameterID { modIds::depth[i], schemaVersion },
            "Mod " + number + " Depth",
            juce::NormalisableRange<float> { -1.0f, 1.0f }, 0.0f,
            Attributes().withStringFromValueFunction ([] (float value, int)
            {
                // Decided on the number that is about to be shown, not on the
                // raw value. A depth of 0.004 used to read "0 %", which claims
                // to be nothing while the slot was still spent -- and it is the
                // percentage, not the float, that the user is reading.
                const int percent = juce::roundToInt (value * 100.0f);

                return percent == 0 ? juce::String ("Off")
                                    : juce::String (percent) + " %";
            })
            .withValueFromStringFunction ([] (const juce::String& text)
            {
                const auto trimmed = text.trim();
                if (trimmed.startsWithIgnoreCase ("off")) return 0.0f;
                return juce::jlimit (-1.0f, 1.0f, trimmed.getFloatValue() * 0.01f);
            })));
    }

    for (int index = 0; index < dsp::Modulation::kNumLfos; ++index)
    {
        const auto i = static_cast<std::size_t> (index);
        const auto number = juce::String (index + 1);

        layout.add (std::make_unique<Choice> (
            juce::ParameterID { modIds::lfoWave[i], schemaVersion },
            "LFO " + number + " Wave", waveNames(), 0));

        layout.add (std::make_unique<Parameter> (
            juce::ParameterID { modIds::lfoRate[i], schemaVersion },
            "LFO " + number + " Rate",
            // Skewed so 1 Hz sits mid-travel: nearly everything musical is
            // under 8 Hz, and a linear 0.01-20 range spends most of itself
            // above where anyone sets it.
            skewedRange (0.01f, 20.0f, 1.0f), 1.0f,
            formatted ("Hz", 2)));

        layout.add (std::make_unique<Boolean> (
            juce::ParameterID { modIds::lfoSync[i], schemaVersion },
            "LFO " + number + " Sync", false));

        layout.add (std::make_unique<Choice> (
            juce::ParameterID { modIds::lfoDivision[i], schemaVersion },
            "LFO " + number + " Division", divisionNames(), defaultDivision));

        layout.add (std::make_unique<Parameter> (
            juce::ParameterID { modIds::lfoPhase[i], schemaVersion },
            "LFO " + number + " Phase",
            juce::NormalisableRange<float> { 0.0f, 1.0f }, 0.0f,
            Attributes().withStringFromValueFunction ([] (float value, int)
            {
                return juce::String (juce::roundToInt (value * 360.0f))
                     + juce::String::fromUTF8 (" \xc2\xb0");
            })
            .withValueFromStringFunction ([] (const juce::String& text)
            {
                return juce::jlimit (0.0f, 1.0f, text.getFloatValue() / 360.0f);
            })));

        layout.add (std::make_unique<Parameter> (
            juce::ParameterID { modIds::lfoSmooth[i], schemaVersion },
            "LFO " + number + " Smooth",
            juce::NormalisableRange<float> { 0.0f, 1.0f }, 0.0f,
            percentAttributes()));
    }

    layout.add (std::make_unique<Parameter> (
        juce::ParameterID { modIds::envAttack, schemaVersion }, "Env Attack",
        skewedRange (0.5f, 200.0f, 15.0f), 10.0f, formatted ("ms", 1)));

    layout.add (std::make_unique<Parameter> (
        juce::ParameterID { modIds::envRelease, schemaVersion }, "Env Release",
        skewedRange (5.0f, 2000.0f, 200.0f), 150.0f, formatted ("ms", 0)));

    layout.add (std::make_unique<Parameter> (
        juce::ParameterID { modIds::envSensitivity, schemaVersion }, "Env Sensitivity",
        juce::NormalisableRange<float> { -48.0f, 6.0f }, -12.0f, formatted ("dB", 1)));
}

void applyPreset (juce::AudioProcessorValueTreeState& state, const Settings& settings)
{
    const auto set = [&state] (const char* id, double value)
    {
        if (auto* parameter = state.getParameter (id))
            parameter->setValueNotifyingHost (
                parameter->convertTo0to1 (static_cast<float> (value)));
    };

    for (int index = 0; index < dsp::Modulation::kNumLfos; ++index)
    {
        const auto i = static_cast<std::size_t> (index);
        const auto& lfo = settings.lfos[i];

        set (modIds::lfoWave[i],     lfo.wave);
        set (modIds::lfoRate[i],     lfo.rateHz);
        set (modIds::lfoSync[i],     lfo.sync ? 1.0 : 0.0);
        set (modIds::lfoDivision[i], lfo.division);
        set (modIds::lfoPhase[i],    lfo.phase);
        set (modIds::lfoSmooth[i],   lfo.smooth);
    }

    set (modIds::envAttack,      settings.envAttackMs);
    set (modIds::envRelease,     settings.envReleaseMs);
    set (modIds::envSensitivity, settings.envSensitivityDb);

    for (int slot = 0; slot < dsp::Modulation::kNumSlots; ++slot)
    {
        const auto i = static_cast<std::size_t> (slot);
        const auto& assignment = settings.slots[i];

        // Depth first and source last, so the matrix never sees a live slot
        // pointed at whatever the previous preset happened to choose.
        set (modIds::depth[i],       assignment.depth);
        set (modIds::destination[i], assignment.destination);
        set (modIds::source[i],      assignment.source);
    }
}

void pushSettings (juce::AudioProcessorValueTreeState& state,
                   dsp::Modulation& modulation,
                   int numDestinations)
{
    const auto value = [&state] (const char* id)
    {
        return state.getRawParameterValue (id)->load();
    };

    for (int index = 0; index < dsp::Modulation::kNumLfos; ++index)
    {
        const auto i = static_cast<std::size_t> (index);
        auto& lfo = modulation.lfo (index);

        lfo.setWave (static_cast<dsp::Lfo::Wave> (
            juce::jlimit (0, dsp::Lfo::kNumWaves - 1,
                          static_cast<int> (value (modIds::lfoWave[i])))));
        lfo.setRateHz (static_cast<double> (value (modIds::lfoRate[i])));
        lfo.setPhaseOffset (static_cast<double> (value (modIds::lfoPhase[i])));
        lfo.setSmooth (static_cast<double> (value (modIds::lfoSmooth[i])));

        const int chosen = juce::jlimit (0, numDivisions - 1,
                                         static_cast<int> (value (modIds::lfoDivision[i])));

        modulation.setLfoSync (index, value (modIds::lfoSync[i]) > 0.5f,
                               divisions[static_cast<std::size_t> (chosen)].cyclesPerBeat);
    }

    auto& follower = modulation.levelFollower();
    follower.setAttackMs (static_cast<double> (value (modIds::envAttack)));
    follower.setReleaseMs (static_cast<double> (value (modIds::envRelease)));
    follower.setSensitivityDb (static_cast<double> (value (modIds::envSensitivity)));

    for (int slot = 0; slot < dsp::Modulation::kNumSlots; ++slot)
    {
        const auto i = static_cast<std::size_t> (slot);

        dsp::Modulation::Slot settings;
        settings.source = static_cast<dsp::Modulation::Source> (
            juce::jlimit (0, dsp::Modulation::kNumSources - 1,
                          static_cast<int> (value (modIds::source[i]))));
        settings.destination = juce::jlimit (0, juce::jmax (1, numDestinations) - 1,
                                             static_cast<int> (value (modIds::destination[i])));
        settings.depth = static_cast<double> (value (modIds::depth[i]));

        modulation.setSlot (slot, settings);
    }
}

} // namespace tezla::ui::modulation
