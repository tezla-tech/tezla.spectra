#include <tezla/ui/ModulationView.hpp>

#include <cmath>

namespace tezla::ui
{

namespace
{
/// One colour per source, fixed rather than derived from the palette.
///
/// A ring's colour is the only thing saying which source drew it, so it has to
/// mean the same in every plugin -- and it must not collide with either accent,
/// or an armed ring would read as part of the knob. Hence a rose for the level
/// follower rather than the obvious amber, which is Halo's gold.
const juce::Colour kSourceColours[] {
    juce::Colour (0xff86837e),      // Off -- only ever drawn as a disabled state
    juce::Colour (0xff54c7c0),      // LFO 1, teal
    juce::Colour (0xff9d7bff),      // LFO 2, violet
    juce::Colour (0xff6fbf5b),      // LFO 3, green
    juce::Colour (0xffe86a92)       // ENV, rose
};

static_assert (static_cast<int> (std::size (kSourceColours)) == dsp::Modulation::kNumSources,
               "every source needs a colour, and in the same order");
} // namespace

ModulationView::ModulationView (juce::AudioProcessorValueTreeState& state,
                                dsp::Modulation& modulation,
                                const char* const* destinationParameterIds,
                                int numDestinations,
                                Palette palette)
    : state_ (state),
      modulation_ (modulation),
      destinationIds_ (destinationParameterIds),
      numDestinations_ (numDestinations),
      palette_ (palette)
{
    jassert (destinationIds_ != nullptr);
    jassert (numDestinations_ > 0 && numDestinations_ <= dsp::Modulation::kMaxDestinations);
}

void ModulationView::setArmedSource (int source)
{
    const int wanted = juce::jlimit (0, numSources - 1, source);

    if (wanted == armed_)
        return;

    armed_ = wanted;

    // Synchronous: the rings have to change what they intercept before the mouse
    // event that armed them is finished being handled, or the first click after
    // arming lands on the knob underneath.
    sendSynchronousChangeMessage();
}

int ModulationView::destinationFor (juce::StringRef parameterId) const
{
    for (int i = 0; i < numDestinations_; ++i)
        if (parameterId == juce::StringRef (destinationIds_[i]))
            return i;

    return -1;
}

juce::RangedAudioParameter* ModulationView::parameterFor (int destination) const
{
    if (destination < 0 || destination >= numDestinations_)
        return nullptr;

    return state_.getParameter (destinationIds_[destination]);
}

double ModulationView::baseProportionFor (int destination) const
{
    if (auto* parameter = parameterFor (destination))
        return static_cast<double> (parameter->getValue());

    return 0.0;
}

juce::RangedAudioParameter* ModulationView::slotParameter (const char* const* ids, int slot) const
{
    if (slot < 0 || slot >= numSlots)
        return nullptr;

    return state_.getParameter (ids[slot]);
}

void ModulationView::setParameter (juce::RangedAudioParameter* parameter, double value) const
{
    if (parameter == nullptr)
        return;

    parameter->setValueNotifyingHost (parameter->convertTo0to1 (static_cast<float> (value)));
}

void ModulationView::setParameterOnce (juce::RangedAudioParameter* parameter, double value) const
{
    if (parameter == nullptr)
        return;

    // Bracketed even though it is a single jump. A host that is recording sees a
    // discrete change rather than a value that arrived from nowhere, which is
    // the same courtesy the spectrum's Focus drag pays.
    parameter->beginChangeGesture();
    setParameter (parameter, value);
    parameter->endChangeGesture();
}

int ModulationView::getSlotSource (int slot) const
{
    if (auto* parameter = slotParameter (modIds::source, slot))
        return juce::roundToInt (parameter->convertFrom0to1 (parameter->getValue()));

    return none;
}

int ModulationView::getSlotDestination (int slot) const
{
    if (auto* parameter = slotParameter (modIds::destination, slot))
        return juce::roundToInt (parameter->convertFrom0to1 (parameter->getValue()));

    return 0;
}

double ModulationView::getSlotDepth (int slot) const
{
    if (auto* parameter = slotParameter (modIds::depth, slot))
        return static_cast<double> (parameter->convertFrom0to1 (parameter->getValue()));

    return 0.0;
}

int ModulationView::findSlot (int source, int destination) const
{
    for (int slot = 0; slot < numSlots; ++slot)
        if (getSlotSource (slot) == source && getSlotDestination (slot) == destination)
            return slot;

    return -1;
}

int ModulationView::allocateSlot (int source, int destination)
{
    if (source == none)
        return -1;

    if (const int existing = findSlot (source, destination); existing >= 0)
        return existing;

    for (int slot = 0; slot < numSlots; ++slot)
    {
        if (getSlotSource (slot) != none)
            continue;

        // Destination first. The source is what makes a slot live, so setting it
        // last means the matrix never sees a live slot pointed at whatever the
        // last user of this slot happened to choose.
        setParameterOnce (slotParameter (modIds::depth, slot), 0.0);
        setParameterOnce (slotParameter (modIds::destination, slot), destination);
        setParameterOnce (slotParameter (modIds::source, slot), source);

        sendSynchronousChangeMessage();
        return slot;
    }

    return -1;
}

void ModulationView::freeSlot (int slot)
{
    if (slot < 0 || slot >= numSlots)
        return;

    setParameterOnce (slotParameter (modIds::depth, slot), 0.0);
    setParameterOnce (slotParameter (modIds::source, slot), none);

    sendSynchronousChangeMessage();
}

int ModulationView::getSlotsUsed() const
{
    int used = 0;

    for (int slot = 0; slot < numSlots; ++slot)
        if (getSlotSource (slot) != none)
            ++used;

    return used;
}

void ModulationView::beginDepthGesture (int slot)
{
    if (auto* parameter = slotParameter (modIds::depth, slot))
        parameter->beginChangeGesture();
}

void ModulationView::setSlotDepth (int slot, double depth)
{
    setParameter (slotParameter (modIds::depth, slot), juce::jlimit (-1.0, 1.0, depth));
}

void ModulationView::endDepthGesture (int slot)
{
    if (auto* parameter = slotParameter (modIds::depth, slot))
        parameter->endChangeGesture();
}

double ModulationView::totalDepthFor (int destination) const
{
    double total = 0.0;

    for (int slot = 0; slot < numSlots; ++slot)
        if (getSlotSource (slot) != none && getSlotDestination (slot) == destination)
            total += std::abs (getSlotDepth (slot));

    return total;
}

int ModulationView::soleSourceFor (int destination) const
{
    int found = none;

    for (int slot = 0; slot < numSlots; ++slot)
    {
        const int source = getSlotSource (slot);

        if (source == none || getSlotDestination (slot) != destination || getSlotDepth (slot) == 0.0)
            continue;

        if (found != none && found != source)
            return none;

        found = source;
    }

    return found;
}

double ModulationView::liveOffsetFor (int destination) const
{
    return modulation_.offsetFor (destination);
}

double ModulationView::liveSourceValue (int source) const
{
    return modulation_.valueOf (static_cast<dsp::Modulation::Source> (
        juce::jlimit (0, numSources - 1, source)));
}

double ModulationView::liveSourcePhase (int source) const
{
    if (source < lfo1 || source > lfo3)
        return 0.0;

    return modulation_.lfo (source - lfo1).getPhase();
}

dsp::Lfo::Wave ModulationView::waveOf (int source) const
{
    if (source < lfo1 || source > lfo3)
        return dsp::Lfo::Wave::sine;

    if (auto* parameter = state_.getParameter (modIds::lfoWave[static_cast<std::size_t> (source - lfo1)]))
        return static_cast<dsp::Lfo::Wave> (juce::jlimit (
            0, dsp::Lfo::kNumWaves - 1,
            juce::roundToInt (parameter->convertFrom0to1 (parameter->getValue()))));

    return dsp::Lfo::Wave::sine;
}

juce::Colour ModulationView::colourForSource (int source)
{
    return kSourceColours[static_cast<std::size_t> (juce::jlimit (0, numSources - 1, source))];
}

juce::String ModulationView::nameForSource (int source)
{
    switch (source)
    {
        case lfo1:  return "LFO 1";
        case lfo2:  return "LFO 2";
        case lfo3:  return "LFO 3";
        case level: return "ENV";
        default:    break;
    }

    return "Off";
}

} // namespace tezla::ui
