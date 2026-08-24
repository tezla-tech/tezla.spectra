#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "PluginProcessor.h"

namespace tezla::emberdrive
{

/// A meter with honest ballistics: a VU bar for how loud it is, a peak line for
/// whether it is about to clip, and a separate gain-reduction bar. Those two
/// readings disagree by 10 dB or more on drums, and the disagreement is the
/// information an engineer is actually reading.
class LevelMeter final : public juce::Component
{
public:
    enum class Style { level, gainReduction };

    explicit LevelMeter (Style style) : style_ (style) {}

    void setValues (float vuDb, float peakDb) noexcept
    {
        vuDb_ = vuDb;
        peakDb_ = peakDb;
    }

    void paint (juce::Graphics&) override;

private:
    [[nodiscard]] float positionFor (float db) const noexcept;

    Style style_;
    float vuDb_   { -100.0f };
    float peakDb_ { -100.0f };
};

class EmberdriveEditor final : public juce::AudioProcessorEditor,
                               private juce::Timer
{
public:
    explicit EmberdriveEditor (EmberdriveProcessor& processorToUse);
    ~EmberdriveEditor() override = default;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;

    struct Knob
    {
        juce::Slider slider;
        juce::Label  label;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
    };

    void addKnob (Knob&, const char* parameterId, const juce::String& name, const juce::String& tooltip);

    EmberdriveProcessor& processor_;

    juce::TooltipWindow tooltips_ { this, 500 };

    Knob drive_, character_, tone_, ceiling_, knee_, speed_, release_, mix_, output_;

    juce::ComboBox oversampling_;
    juce::Label    oversamplingLabel_;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> oversamplingAttachment_;

    juce::ToggleButton autoTrim_    { "Auto trim" };
    juce::ToggleButton autoRelease_ { "Auto release" };
    juce::ToggleButton bypass_      { "Bypass" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> autoTrimAttachment_;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> autoReleaseAttachment_;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> bypassAttachment_;

    LevelMeter inputMeter_  { LevelMeter::Style::level };
    LevelMeter outputMeter_ { LevelMeter::Style::level };
    LevelMeter reductionMeter_ { LevelMeter::Style::gainReduction };

    juce::Label inputMeterLabel_     { {}, "IN" };
    juce::Label outputMeterLabel_    { {}, "OUT" };
    juce::Label reductionMeterLabel_ { {}, "GR" };

    juce::Label statusLabel_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EmberdriveEditor)
};

} // namespace tezla::emberdrive
