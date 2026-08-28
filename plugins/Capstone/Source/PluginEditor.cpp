#include "PluginEditor.h"

#include <algorithm>
#include <cmath>

namespace tezla::capstone
{

namespace
{
// Capstone's own accent: cool steel, against Emberdrive's ember and Halo's
// gold. Gain reduction gets a warm colour deliberately -- it is the one reading
// here that is not a level, and the two must not be confusable at a glance.
const ui::Palette kPalette {
    juce::Colour { 0xff141416 },   // background
    juce::Colour { 0xff1d1d20 },   // panel
    juce::Colour { 0xffd8d5cf },   // text
    juce::Colour { 0xff86837e },   // dim text
    juce::Colour { 0xff5b8dd9 },   // accent
    juce::Colour { 0xff8fb6ee },   // accent bright
    juce::Colour { 0xffe0a33c },   // secondary: gain reduction
    juce::Colour { 0xffff7a18 }    // bypass glow, the same in every plugin
};

constexpr int kLabelHeight = 16;
constexpr int kValueHeight = 18;
constexpr int kMaxCellHeight = 130;
constexpr int kNoteHeight = 58;

/// How much reduction the meter shows end to end. Twelve, because that is where
/// a limiter stops being a limiter -- past it you are looking at a different
/// problem, and a scale that went to 40 would make 3 dB unreadable.
constexpr float kReductionRangeDb = 12.0f;

constexpr int kMinWidth  = 720;
constexpr int kMinHeight = 560;
constexpr int kMaxWidth  = 1440;
constexpr int kMaxHeight = 1120;

constexpr int kMeterWidth = ui::LevelMeter::kMinimumWidth;
constexpr int kTabHeight  = 28;
constexpr int kStatusHeight = 24;
constexpr int kReductionHeight = 62;
} // namespace

// ---------------------------------------------------------------------------
// ReductionMeter
// ---------------------------------------------------------------------------

void ReductionMeter::paint (juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat().reduced (1.0f);

    g.setColour (palette_.panel.brighter (0.10f));
    g.fillRoundedRectangle (bounds, 3.0f);

    // A gutter for the row labels, taken before anything else. Drawing them
    // over the bars put "LIMIT" inside the reading it was labelling.
    auto gutter = bounds.removeFromLeft (46.0f);
    auto scale  = bounds.removeFromBottom (12.0f);
    auto bars   = bounds.reduced (2.0f, 2.0f);

    const float half = bars.getHeight() * 0.5f;

    g.setColour (palette_.dimText);
    g.setFont (juce::FontOptions (10.0f));
    g.drawText ("LIMIT", gutter.withHeight (half).withY (bars.getY()),
                juce::Justification::centredLeft);
    g.drawText ("CLIP", gutter.withHeight (half).withY (bars.getY() + half),
                juce::Justification::centredLeft);

    // Graduations every 3 dB, growing leftwards from the right-hand edge, which
    // is the direction gain reduction moves.
    g.setColour (palette_.dimText.withAlpha (0.30f));
    g.setFont (juce::FontOptions (9.0f));

    // Up to but not including full scale: a label centred on the left-hand edge
    // is half outside the meter, which reads as a rendering fault rather than as
    // the end of the scale.
    for (int db = 3; db < static_cast<int> (kReductionRangeDb); db += 3)
    {
        const float x = bars.getRight() - bars.getWidth() * (db / kReductionRangeDb);

        g.drawVerticalLine (juce::roundToInt (x), bars.getY(), bars.getBottom());
        g.drawText ("-" + juce::String (db),
                    juce::Rectangle<float> { x - 13.0f, scale.getY(), 26.0f, scale.getHeight() },
                    juce::Justification::centred);
    }

    const auto drawBar = [&] (float db, juce::Colour colour, juce::Rectangle<float> row)
    {
        const float amount = juce::jlimit (0.0f, 1.0f, -db / kReductionRangeDb);

        if (amount <= 0.001f)
            return;

        g.setColour (colour);
        g.fillRoundedRectangle (row.withLeft (row.getRight() - row.getWidth() * amount), 2.0f);
    };

    drawBar (limiterDb_, palette_.secondary, bars.withHeight (half).reduced (0.0f, 1.5f));
    drawBar (clipDb_, palette_.bypassGlow,
             bars.withY (bars.getY() + half).withHeight (half).reduced (0.0f, 1.5f));
}

// ---------------------------------------------------------------------------
// WrappingLabel
// ---------------------------------------------------------------------------

void WrappingLabel::paint (juce::Graphics& g)
{
    g.setColour (findColour (juce::Label::textColourId));
    g.setFont (getFont());
    g.drawFittedText (getText(), getLocalBounds(), getJustificationType(), 3, 1.0f);
}

// ---------------------------------------------------------------------------
// ControlPage
// ---------------------------------------------------------------------------

void ControlPage::addKnob (const char* parameterId, const juce::String& name,
                           const juce::String& tooltip)
{
    auto knob = std::make_unique<Knob>();

    knob->slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    knob->slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 96, kValueHeight);
    knob->slider.setColour (juce::Slider::rotarySliderFillColourId, palette_.accent);
    knob->slider.setColour (juce::Slider::rotarySliderOutlineColourId, palette_.panel.brighter (0.25f));
    knob->slider.setColour (juce::Slider::thumbColourId, palette_.accentBright);
    knob->slider.setColour (juce::Slider::textBoxTextColourId, palette_.text);
    knob->slider.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    knob->slider.setTooltip (tooltip);
    addAndMakeVisible (knob->slider);

    knob->label.setText (name, juce::dontSendNotification);
    knob->label.setJustificationType (juce::Justification::centred);
    knob->label.setColour (juce::Label::textColourId, palette_.dimText);
    knob->label.setFont (juce::FontOptions (12.0f));
    knob->label.setTooltip (tooltip);
    addAndMakeVisible (knob->label);

    knob->id = parameterId;
    knob->attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        state_, parameterId, knob->slider);

    cells_.push_back ({ Cell::Kind::knob, static_cast<int> (knobs_.size()) });
    knobs_.push_back (std::move (knob));
}

void ControlPage::addChoice (const char* parameterId, const juce::String& name,
                             const juce::String& tooltip)
{
    auto choice = std::make_unique<Choice>();

    // Populated from the parameter itself. A ComboBoxAttachment selects an item
    // by index and does not create one, so a box left empty here stays empty on
    // screen and cannot be operated at all.
    if (auto* parameter = dynamic_cast<juce::AudioParameterChoice*> (state_.getParameter (parameterId)))
        choice->box.addItemList (parameter->choices, 1);
    else
        jassertfalse;   // addChoice used on something that is not a choice parameter

    choice->box.setColour (juce::ComboBox::backgroundColourId, palette_.panel.brighter (0.15f));
    choice->box.setColour (juce::ComboBox::textColourId, palette_.text);
    choice->box.setColour (juce::ComboBox::outlineColourId, palette_.panel.brighter (0.3f));
    choice->box.setTooltip (tooltip);
    addAndMakeVisible (choice->box);

    choice->label.setText (name, juce::dontSendNotification);
    choice->label.setJustificationType (juce::Justification::centred);
    choice->label.setColour (juce::Label::textColourId, palette_.dimText);
    choice->label.setFont (juce::FontOptions (12.0f));
    choice->label.setTooltip (tooltip);
    addAndMakeVisible (choice->label);

    choice->id = parameterId;
    choice->attachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (
        state_, parameterId, choice->box);

    cells_.push_back ({ Cell::Kind::choice, static_cast<int> (choices_.size()) });
    choices_.push_back (std::move (choice));
}

void ControlPage::addToggle (const char* parameterId, const juce::String& name,
                             const juce::String& tooltip)
{
    auto toggle = std::make_unique<Toggle>();

    toggle->button.setButtonText (name);
    toggle->button.setColour (juce::ToggleButton::textColourId, palette_.text);
    toggle->button.setColour (juce::ToggleButton::tickColourId, palette_.accent);
    toggle->button.setTooltip (tooltip);
    addAndMakeVisible (toggle->button);

    toggle->id = parameterId;
    toggle->attachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
        state_, parameterId, toggle->button);

    cells_.push_back ({ Cell::Kind::toggle, static_cast<int> (toggles_.size()) });
    toggles_.push_back (std::move (toggle));
}

void ControlPage::addGap()
{
    cells_.push_back ({ Cell::Kind::gap, 0 });
}

void ControlPage::setNote (const juce::String& note)
{
    if (note_ == note)
        return;

    note_ = note;
    resized();
    repaint();
}

void ControlPage::setControlEnabled (const char* parameterId, bool enabled)
{
    const juce::String id { parameterId };

    for (auto& knob : knobs_)
        if (knob->id == id)
        {
            knob->slider.setEnabled (enabled);
            knob->label.setColour (juce::Label::textColourId,
                                   enabled ? palette_.dimText : palette_.dimText.withAlpha (0.35f));
            knob->label.repaint();
        }

    for (auto& choice : choices_)
        if (choice->id == id)
        {
            choice->box.setEnabled (enabled);
            choice->label.setColour (juce::Label::textColourId,
                                     enabled ? palette_.dimText : palette_.dimText.withAlpha (0.35f));
            choice->label.repaint();
        }

    for (auto& toggle : toggles_)
        if (toggle->id == id)
            toggle->button.setEnabled (enabled);
}

void ControlPage::paint (juce::Graphics& g)
{
    g.setColour (kPalette.panel);
    g.fillRoundedRectangle (getLocalBounds().toFloat(), 6.0f);

    if (note_.isEmpty() || noteArea_.isEmpty())
        return;

    g.setColour (palette_.dimText);
    g.setFont (juce::FontOptions (12.0f));

    // Four lines, and the box is sized for four. These notes carry the measured
    // numbers behind each control -- a note that is silently cut in half is
    // worse than no note, because the half that survives still reads as a
    // complete sentence.
    g.drawFittedText (note_, noteArea_, juce::Justification::centredTop, 4, 1.0f);
}

void ControlPage::resized()
{
    if (cells_.empty())
        return;

    auto bounds = getLocalBounds().reduced (4, 2);

    // The note gets its space reserved before the grid takes any, rather than
    // living on whatever is left over -- a note that vanishes when the window is
    // a little short is worse than no note, because it is there when you write
    // it and gone when someone uses it.
    if (! note_.isEmpty())
        noteArea_ = bounds.removeFromBottom (kNoteHeight).reduced (6, 0);
    else
        noteArea_ = {};

    const int rows = (static_cast<int> (cells_.size()) + columns_ - 1) / columns_;
    const int cellWidth  = bounds.getWidth() / columns_;
    const int cellHeight = juce::jmin (bounds.getHeight() / juce::jmax (1, rows), kMaxCellHeight);

    // Centred vertically rather than pinned to the top. The pages hold four,
    // five and eleven controls, and top-aligning them left the short ones
    // huddled under the tabs with half the panel empty below.
    const int top = bounds.getY()
                  + juce::jmax (0, (bounds.getHeight() - rows * cellHeight) / 2);

    for (std::size_t i = 0; i < cells_.size(); ++i)
    {
        const int column = static_cast<int> (i) % columns_;
        const int row    = static_cast<int> (i) / columns_;

        juce::Rectangle<int> cell { bounds.getX() + column * cellWidth,
                                    top + row * cellHeight,
                                    cellWidth, cellHeight };

        switch (cells_[i].kind)
        {
            case Cell::Kind::knob:
            {
                auto& knob = *knobs_[static_cast<std::size_t> (cells_[i].index)];
                knob.label.setBounds (cell.removeFromTop (kLabelHeight));
                knob.slider.setBounds (cell.reduced (4, 0));
                break;
            }
            case Cell::Kind::choice:
            {
                auto& choice = *choices_[static_cast<std::size_t> (cells_[i].index)];
                choice.label.setBounds (cell.removeFromTop (kLabelHeight));
                choice.box.setBounds (cell.withSizeKeepingCentre (
                    juce::jmin (cell.getWidth() - 12, 132), 26));
                break;
            }
            case Cell::Kind::toggle:
            {
                auto& toggle = *toggles_[static_cast<std::size_t> (cells_[i].index)];
                toggle.button.setBounds (cell.withSizeKeepingCentre (
                    juce::jmin (cell.getWidth() - 8, 130), 26));
                break;
            }
            case Cell::Kind::gap:
                break;
        }
    }
}

// ---------------------------------------------------------------------------
// CapstoneEditor
// ---------------------------------------------------------------------------

CapstoneEditor::CapstoneEditor (CapstoneProcessor& processorToUse)
    : juce::AudioProcessorEditor (&processorToUse),
      capstone_ (processorToUse),
      palette_ (kPalette),
      inputMeter_ (std::make_unique<ui::LevelMeter> (kPalette)),
      outputMeter_ (std::make_unique<ui::LevelMeter> (kPalette)),
      reductionMeter_ (std::make_unique<ReductionMeter> (kPalette))
{
    header_ = std::make_unique<ui::HeaderBar> (
        capstone_.getState(), "CAPSTONE",
        "True-peak brickwall limiter and clipper", ids::bypass, palette_);

    header_->onSwapRequested = [this]
    {
        capstone_.getAbCompare().swapSlots();
        header_->setActiveSlot (capstone_.getAbCompare().isSlotB());
        header_->setOtherSlotFilled (capstone_.getAbCompare().otherSlotFilled());
    };

    header_->onCopyRequested = [this]
    {
        capstone_.getAbCompare().copyToOtherSlot();
        header_->setOtherSlotFilled (capstone_.getAbCompare().otherSlotFilled());
    };

    // The TIPS button. The flag lives on the processor so it survives the
    // window being closed, and the header is told the current value rather
    // than assuming its own default -- reopening a panel whose tips were off
    // must not turn them back on.
    header_->onTooltipsToggled = [this] (bool enabled)
    {
        capstone_.setTooltipsEnabled (enabled);
        tooltips_.setEnabled (enabled);
    };

    header_->setTooltipsEnabled (capstone_.getTooltipsEnabled());
    tooltips_.setEnabled (capstone_.getTooltipsEnabled());

    header_->setActiveSlot (capstone_.getAbCompare().isSlotB());
    header_->setOtherSlotFilled (capstone_.getAbCompare().otherSlotFilled());

    // The suite-wide pair, in the header rather than at the bottom of a page.
    header_->attachSuiteControls (capstone_.getState(), nullptr, ids::output, nullptr);
    addAndMakeVisible (*header_);

    buildPages();

    static const char* tabNames[kNumPages] { "LIMIT", "CLIP", "OUTPUT" };

    for (int i = 0; i < kNumPages; ++i)
    {
        tabs_[static_cast<std::size_t> (i)].setButtonText (tabNames[i]);
        tabs_[static_cast<std::size_t> (i)].setClickingTogglesState (false);
        tabs_[static_cast<std::size_t> (i)].onClick = [this, i] { showPage (i); };
        addAndMakeVisible (tabs_[static_cast<std::size_t> (i)]);
    }

    // The input is measured against full scale; the output against whatever
    // Ceiling is set to, because that is the number this plugin promised.
    inputMeter_->setReferenceDb (0.0f);

    // One scale between the two meters would be ideal, but they sit on opposite
    // edges of the window -- so the output gets it, since that is the one a
    // ceiling is read off.
    outputMeter_->setScaleVisible (true);

    addAndMakeVisible (*inputMeter_);
    addAndMakeVisible (*outputMeter_);
    addAndMakeVisible (*reductionMeter_);

    for (auto* meter : { inputMeter_.get(), outputMeter_.get() })
        meter->setTooltip ("Peak level. The number is the worst peak since it was last "
                           "cleared -- click the meter to clear it. It turns red when the "
                           "peak went over: full scale on the input, Ceiling on the output. "
                           "Hover to read the live level instead of the held one.");

    for (auto* label : { &inputMeterLabel_, &outputMeterLabel_, &reductionLabel_ })
    {
        label->setJustificationType (juce::Justification::centred);
        label->setColour (juce::Label::textColourId, palette_.dimText);
        label->setFont (juce::FontOptions (10.0f));
        addAndMakeVisible (label);
    }

    statusLabel_.setJustificationType (juce::Justification::centred);
    statusLabel_.setColour (juce::Label::textColourId, palette_.dimText);
    statusLabel_.setFont (juce::FontOptions (12.0f));
    addAndMakeVisible (statusLabel_);

    showPage (0);

    setResizable (true, true);
    setResizeLimits (kMinWidth, kMinHeight, kMaxWidth, kMaxHeight);
    setSize (860, 620);

    startTimerHz (30);
}

void CapstoneEditor::buildPages()
{
    // ---- LIMIT ---------------------------------------------------------------

    auto& limit = pages_[0];
    limit = std::make_unique<ControlPage> (capstone_.getState(), palette_, 4);

    limit->addKnob (ids::threshold, "Threshold",
        "How hard the signal is pushed into the ceiling. The makeup is automatic, so "
        "lowering this raises the level going in without raising the level coming out -- "
        "turn it down until it is as loud as you want, then stop. At 0 with both stages "
        "off the plugin is bit-exact.");

    limit->addKnob (ids::ceiling, "Ceiling",
        "The level the output is not allowed past, and it is a guarantee rather than a "
        "target: the gain arriving at every sample is already below what that sample "
        "needs. Above 0 dBFS is deliberate -- nothing in a floating-point chain has to "
        "stop at full scale, and catching only the extremes is a real use.");

    limit->addKnob (ids::attack, "Attack",
        "The look-ahead, the attack, and the reported latency -- all the same number. "
        "Longer is more transparent and costs delay: on a 60 Hz tone limited 12 dB, "
        "distortion measures -31 dB at 0 ms, -60 dB at 5 ms and -166 dB at 20 ms. "
        "Off means the gain cannot come down before the peak, so the peak is cut instead; "
        "that is what the Clip page is for.");

    limit->addKnob (ids::hold, "Hold",
        "How long the gain stays down after a peak, before the release starts. Free: it "
        "widens the window backwards, so it needs no future samples and adds no latency at "
        "all. Use it to stop the gain fluttering on dense peaks.");

    limit->addKnob (ids::release, "Release",
        "How fast the gain comes back. Too fast on sub bass is distortion rather than "
        "limiting -- a 40 Hz cycle lasts 25 ms, and the gain must not move inside one.");

    limit->addKnob (ids::knee, "Knee",
        "How far below the ceiling the curve starts bending. Hard is a corner exactly at "
        "the ceiling; wider starts easing the signal down earlier, which is gentler and "
        "starts working sooner.");

    limit->addKnob (ids::stereoLink, "Stereo Link",
        "100% gives both channels the same gain, which keeps the centre image still. Lower "
        "lets each channel follow its own peaks -- wider and looser, and it will move the "
        "image. Sub bass wants 100%.");

    limit->addGap();

    limit->addToggle (ids::limitOn, "Limit",
        "The look-ahead limiter. Off leaves the clipper as the only thing holding a "
        "ceiling.");

    limit->addToggle (ids::lookaheadOn, "Lookahead",
        "Off pins the attack at zero, which is the only way the reported latency reaches "
        "exactly 0. The limiter then behaves like a very fast clipper -- and the Clip page "
        "does that better, band-limited and with a shape control.");

    limit->addToggle (ids::autoRelease, "Auto Release",
        "Program-dependent release: a second, slower release runs alongside the first, so "
        "short peaks recover quickly and sustained loudness does not pump. Costs nothing.");

    // ---- CLIP ----------------------------------------------------------------

    auto& clip = pages_[1];
    clip = std::make_unique<ControlPage> (capstone_.getState(), palette_, 4);

    clip->addKnob (ids::clipThreshold, "Clip Threshold",
        "Where the clipper starts cutting -- absolute, not relative to Ceiling. Set it "
        "above Ceiling and the clipper takes the transient tips while the limiter handles "
        "the body, which is how a drum bus gets loud without pumping. Set it at or below "
        "Ceiling and the clipper does all the work, which is what 0 ms limiting means.");

    clip->addKnob (ids::clipShape, "Shape",
        "Hard is a corner: maximum level for the least gain reduction, and the sound of a "
        "clipped drum bus. Soft bends into it from further down -- less level, more "
        "harmonics, closer to saturation. Below the corner the curve is the identity at "
        "either setting, so a quiet signal is passed through untouched.");

    clip->addChoice (ids::clipOversampling, "Oversampling", "");

    clip->addGap();

    clip->addToggle (ids::clipOn, "Clip",
        "Off is bit-exact -- the stage leaves the signal alone entirely, including the "
        "oversampler.");

    // ---- OUTPUT --------------------------------------------------------------

    auto& out = pages_[2];
    out = std::make_unique<ControlPage> (capstone_.getState(), palette_, 4);

    out->addChoice (ids::truePeak, "True Peak", "");

    out->addKnob (ids::output, "Output",
        "Applied after the ceiling, so this is the control that can go past it -- and past "
        "0 dBFS. Use it to match levels when comparing, or to feed something later in the "
        "chain that wants more than the ceiling allows.");

    out->addGap();
    out->addGap();

    out->addToggle (ids::listen, "Listen",
        "Solo what the clipper and the limiter removed. The fastest way to hear whether "
        "the drive is doing what you think: on a good setting it should sound like "
        "transients, not like the track.");

    for (auto& page : pages_)
        addChildComponent (*page);
}

void CapstoneEditor::showPage (int index)
{
    currentPage_ = juce::jlimit (0, kNumPages - 1, index);

    for (int i = 0; i < kNumPages; ++i)
    {
        const bool active = i == currentPage_;

        pages_[static_cast<std::size_t> (i)]->setVisible (active);

        auto& tab = tabs_[static_cast<std::size_t> (i)];
        tab.setColour (juce::TextButton::buttonColourId,
                       active ? palette_.panel.brighter (0.25f) : palette_.background.brighter (0.05f));
        tab.setColour (juce::TextButton::textColourOffId,
                       active ? palette_.text : palette_.dimText);
    }

    resized();
}

void CapstoneEditor::updateForSwitches()
{
    auto& state = capstone_.getState();

    const auto flag = [&state] (const char* id)
    {
        return state.getRawParameterValue (id)->load() > 0.5f;
    };

    const int limitOn    = flag (ids::limitOn) ? 1 : 0;
    const int clipOn     = flag (ids::clipOn) ? 1 : 0;
    const int lookahead  = flag (ids::lookaheadOn) ? 1 : 0;
    const int truePeak   = static_cast<int> (state.getRawParameterValue (ids::truePeak)->load());
    const int oversample = static_cast<int> (state.getRawParameterValue (ids::clipOversampling)->load());
    const int latency    = capstone_.getLatencySamples();

    if (limitOn == shownLimitOn_ && clipOn == shownClipOn_ && lookahead == shownLookahead_
        && truePeak == shownTruePeak_ && oversample == shownOversample_ && latency == shownLatency_)
        return;

    shownLimitOn_ = limitOn;
    shownClipOn_ = clipOn;
    shownLookahead_ = lookahead;
    shownTruePeak_ = truePeak;
    shownOversample_ = oversample;
    shownLatency_ = latency;

    // Everything the limiter's timing controls do is inert when it is off, and
    // the attack knob is inert when look-ahead is off. Greyed rather than
    // hidden: a knob that moves and does nothing reads as a broken plugin.
    auto& limit = *pages_[0];

    for (const char* id : { ids::attack, ids::hold, ids::release, ids::knee,
                            ids::stereoLink, ids::autoRelease, ids::lookaheadOn })
        limit.setControlEnabled (id, limitOn != 0);

    limit.setControlEnabled (ids::attack, limitOn != 0 && lookahead != 0);

    limit.setNote (limitOn != 0
        ? capstone_.describeLatency()
        : "Limit is off. Nothing here is holding the ceiling except the clipper, if that "
          "is on -- the Ceiling knob still sets where the clipper's own output lands.");

    auto& clip = *pages_[1];

    for (const char* id : { ids::clipThreshold, ids::clipShape, ids::clipOversampling })
        clip.setControlEnabled (id, clipOn != 0);

    clip.setNote (clipOn != 0
        ? capstone_.describeClipOversampling()
        : "Clip is off, and off is bit-exact: the stage is skipped entirely, oversampler "
          "included, so nothing here can colour the signal.");

    pages_[2]->setNote (capstone_.describeTruePeak());
}

void CapstoneEditor::timerCallback()
{
    auto& meters = capstone_.getMeterValues();

    inputMeter_->setValues (meters.inputVuDb.load (std::memory_order_relaxed),
                            meters.inputPeakDb.load (std::memory_order_relaxed));
    outputMeter_->setValues (meters.outputVuDb.load (std::memory_order_relaxed),
                             meters.outputPeakDb.load (std::memory_order_relaxed));

    // The output meter's "over" line follows Ceiling rather than full scale.
    const float ceilingDb = capstone_.getState().getRawParameterValue (ids::ceiling)->load();

    if (std::abs (ceilingDb - shownCeilingDb_) > 0.001f)
    {
        shownCeilingDb_ = ceilingDb;
        outputMeter_->setReferenceDb (ceilingDb);
    }

    const float limiterDb = meters.limiterReductionDb.load (std::memory_order_relaxed);
    const float clipDb    = meters.clipReductionDb.load (std::memory_order_relaxed);

    reductionMeter_->setValues (limiterDb, clipDb);

    inputMeter_->repaint();
    outputMeter_->repaint();
    reductionMeter_->repaint();

    // The numbers behind the meter. A bar says "about 3 dB"; a delivery spec
    // wants the figure, and so does anyone comparing two settings.
    const auto readout = [] (float db)
    {
        return db <= -0.05f ? juce::String (db, 1) + " dB" : juce::String ("--");
    };

    const double rate = capstone_.getSampleRate() > 0.0 ? capstone_.getSampleRate() : 48000.0;
    const int latency = capstone_.getLatencySamples();

    juce::String status;
    status << "LIMIT " << readout (limiterDb)
           << "   \xe2\x80\xa2   CLIP " << readout (clipDb)
           << "   \xe2\x80\xa2   OUT " << juce::String (
                  meters.outputPeakDb.load (std::memory_order_relaxed), 1) << " dB peak"
           << "   \xe2\x80\xa2   LATENCY ";

    // Until the host has started audio there is no latency figure, only an
    // uninitialised one -- and printing "0 sm" for it says the plugin has none,
    // which is a different claim entirely.
    if (capstone_.isPrepared())
        status << latency << " sm (" << juce::String (1000.0 * latency / rate, 2) << " ms)";
    else
        status << "--";

    statusLabel_.setText (status, juce::dontSendNotification);

    header_->setActiveSlot (capstone_.getAbCompare().isSlotB());
    header_->setOtherSlotFilled (capstone_.getAbCompare().otherSlotFilled());

    updateForSwitches();
}

void CapstoneEditor::paint (juce::Graphics& g)
{
    g.fillAll (palette_.background);
}

void CapstoneEditor::resized()
{
    auto bounds = getLocalBounds();

    header_->setBounds (bounds.removeFromTop (ui::HeaderBar::getPreferredHeight()));

    statusLabel_.setBounds (bounds.removeFromBottom (kStatusHeight).reduced (12, 4));

    // The meters flank the panel, so the reading and the control that changes it
    // are never on different pages.
    auto left = bounds.removeFromLeft (kMeterWidth + 8).reduced (4, 6);
    inputMeterLabel_.setBounds (left.removeFromBottom (12));
    inputMeter_->setBounds (left);

    // Wider on the right: the output meter carries the labelled scale.
    auto right = bounds.removeFromRight (kMeterWidth + ui::LevelMeter::kScaleWidth + 8)
                     .reduced (4, 6);
    outputMeterLabel_.setBounds (right.removeFromBottom (12));
    outputMeter_->setBounds (right);

    auto reduction = bounds.removeFromTop (kReductionHeight + 12).reduced (4, 4);
    reductionLabel_.setBounds (reduction.removeFromTop (12));
    reductionMeter_->setBounds (reduction);

    auto tabRow = bounds.removeFromTop (kTabHeight).reduced (4, 2);
    const int tabWidth = tabRow.getWidth() / kNumPages;

    for (int i = 0; i < kNumPages; ++i)
        tabs_[static_cast<std::size_t> (i)].setBounds (
            tabRow.removeFromLeft (i == kNumPages - 1 ? tabRow.getWidth() : tabWidth).reduced (2, 0));

    for (auto& page : pages_)
        page->setBounds (bounds.reduced (4, 2));
}

} // namespace tezla::capstone
