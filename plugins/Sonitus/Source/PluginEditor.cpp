#include "PluginEditor.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

#include <tezla/dsp/Adsr.hpp>
#include <tezla/dsp/Oscillator.hpp>
#include <tezla/dsp/Scales.hpp>

namespace tezla::sonitus
{

namespace
{
// ---------------------------------------------------------------------------
// The palette: hot pink, and five siblings a golden angle apart
// ---------------------------------------------------------------------------
//
// **There is no colour that makes anyone more creative**, and it is worth
// saying plainly because the claim is everywhere. The one study people cite --
// Mehta & Zhu, *Science* 2009, blue for creative tasks and red for detail ones
// -- is a single line of work with small effects and a contested replication
// record. So this palette is not chasing that. It is chasing *fun*, which is a
// perfectly good reason, and it is built with real arithmetic underneath so the
// fun is coherent rather than random.
//
// **Hot pink is the anchor.** #FF69B4 in OKLCH is L 0.728, C 0.197, H 352 --
// and everything else is derived from that hue by the **golden angle**,
// 137.50776 degrees. That is not decoration: rotating by the golden angle is
// the one step that never repeats and never clusters, which is why sunflowers
// use it to pack seeds and why it is the right way to spread N hues round a
// wheel when you do not know N in advance. Six turns of it give six hues that
// are all as far from each other as six hues can be.
//
//     OSC   352 pink      FILTER 182 cyan     ENV    267 blue
//     MOD    45 orange    MANGLE 320 magenta  TUNING 130 lime
//
// **Every accent shares one lightness** (L 0.74) and takes its own hue's
// maximum available chroma in sRGB. Equal lightness in OKLab means equal
// *perceived* brightness, so the six read as one family rather than as some
// loud colours and some quiet ones -- and taking each one to its own gamut
// limit is what keeps them candy rather than pastel. A single shared chroma
// would have been more "correct" and would have dragged the pink down to a
// dusty rose, because cyan cannot be as chromatic as magenta at any lightness.
// Vividness wins here; it is a synthesiser, not a spreadsheet.
//
// **The dark half is brushed metal, and deliberately neutral.** An earlier pass
// tinted it with the pink's own hue, which put pink knobs on a pink panel and
// left both fighting -- a saturated accent needs somewhere *uncoloured* to sit
// against or it stops reading as an accent at all. So the background, panels and
// text are chroma 0.005 or less: silver, with the whole chroma budget spent on
// the six things that carry meaning.
//
// The metal itself is painted rather than coloured: a vertical gradient with a
// specular band across the upper third and fine horizontal striations, cached in
// an image so it costs one blit per repaint rather than a thousand lines. See
// `MetalBackground`.
//
// **The lightness is set by the accents, not by taste.** Every accent has to
// clear 4.5:1 against the panel it sits on, and they are bright colours -- so
// the panel can only go so light before they stop being legible. Measured
// across the six, the limit is L 0.34; the group panel sits at 0.335 and the
// worst accent (magenta) reads 4.72:1. A lighter, prettier silver would have
// been under it. Text is 10.8:1 and the dim labels 4.8:1.
//
// The identity is unchanged where it has to be: bypass orange is the same in
// every plugin, and "over" is the same red.
const ui::Palette kPalette {
    juce::Colour { 0xff2c2e30 },   // background   L 0.300  C 0.004  H 250
    juce::Colour { 0xff282a2c },   // panel        L 0.282  C 0.005  H 250
    juce::Colour { 0xfff1f4f6 },   // text         L 0.965  C 0.004  H 250
    juce::Colour { 0xffa2a5a8 },   // dim text     L 0.720  C 0.006  H 250
    juce::Colour { 0xffe277fc },   // accent: HOT PINK      L 0.74  H 320
    juce::Colour { 0xfff2c5fe },   // accent bright         L 0.88  H 320
    juce::Colour { 0xff2cf7df },   // secondary: modulation L 0.88  H 182
    juce::Colour { 0xffff7a18 },   // bypass glow, the same in every plugin
    juce::Colour { 0xffe95048 },   // over         L 0.640  C 0.190  H 27
    juce::Colour { 0xff2cf7df }    // hold         L 0.88   H 182
};

/// The group panels, one perceptual step above the page panel.
///
/// A literal value rather than `panel.brighter()`, because `brighter` works in
/// HSB and its steps are not perceptually even -- the same argument gives a
/// different apparent jump on a dark colour than on a light one.
const juce::Colour kGroupPanel { 0xff34373a };

/// The three stops the brushed metal is built from: the specular band, the
/// shoulder below it, and the shadow at both ends.
///
/// **The whole chassis sits below the darkest panel.** The first version was a
/// bright polished silver -- specular band at L 0.713 against panels at 0.284 --
/// and it was wrong twice over. It read as the loudest thing in the window, so
/// the panels looked like holes cut in a bright plate rather than lit surfaces
/// on a dark one; and the meter's scale labels, which are dim text at L 0.720,
/// landed on it at **1.03:1**. That is not low contrast, it is the same colour.
///
/// So: anodised rather than polished. The specular band is L 0.263, under the
/// page panel's 0.284, and the shadow at the ends reaches 0.139. The gradient
/// still has 0.12 of lightness to move through, which is more than twice the
/// step between the page panel and a group panel, so it still reads as a
/// surface with a light on it. The same scale labels now read 6.22:1.
const juce::Colour kMetalHigh { 0xff222528 };   // L 0.263  C 0.007  H 248
const juce::Colour kMetalMid  { 0xff15171a };   // L 0.204
const juce::Colour kMetalLow  { 0xff07090c };   // L 0.139

/// Each page's own accent, and its bright partner. Golden-angle hues, one
/// lightness, each at its own chroma limit -- see the comment above.
///
/// Ordered so the **brightest** of the family lands on OSC -- the page the
/// plugin opens on, and therefore the colour it is remembered as -- and the lime
/// on TUNING, which is the page nobody stares at. The header takes the same
/// magenta, so the title and the first page agree the moment the window opens.
struct PageAccent
{
    juce::Colour accent;
    juce::Colour bright;
};

const PageAccent kPageAccents[] {
    { juce::Colour { 0xffe277fc }, juce::Colour { 0xfff2c5fe } },   // OSC     magenta H 320
    { juce::Colour { 0xff20c5b1 }, juce::Colour { 0xff2cf7df } },   // FILTER  cyan    H 182
    { juce::Colour { 0xff86a7fc }, juce::Colour { 0xffc7d7fe } },   // ENV     blue    H 267
    { juce::Colour { 0xfffc854d }, juce::Colour { 0xfffecbb5 } },   // MOD     orange  H  45
    { juce::Colour { 0xfffc75b7 }, juce::Colour { 0xfffec5dc } },   // MANGLE  pink    H 352
    { juce::Colour { 0xff83c11b }, juce::Colour { 0xffa6f326 } }    // TUNING  lime    H 130
};

/// The base palette with one page's accent swapped in.
[[nodiscard]] ui::Palette paletteForPage (int index)
{
    auto palette = kPalette;

    const auto& accent = kPageAccents[static_cast<std::size_t> (
        juce::jlimit (0, static_cast<int> (std::size (kPageAccents)) - 1, index))];

    palette.accent = accent.accent;
    palette.accentBright = accent.bright;

    // The modulation colour has to stay told-apart-able from whatever the page
    // is wearing. It is the cyan by default; on the page that *is* cyan it
    // steps aside to the magenta.
    palette.secondary = index == 1 ? juce::Colour { 0xffe277fc }
                                   : juce::Colour { 0xff2cf7df };

    return palette;
}

// The cell. Deliberately small: sixty controls on six pages only fit at a size
// the stock JUCE rotary cannot be read at, which is what the arc-and-pointer
// look and feel in shared/tezla-ui is for. 62 pixels is a 12-pixel name, a
// 35-pixel knob and a 14-pixel number, and every one of those is legible.
constexpr int kCellLabelHeight = 12;
constexpr int kCellValueHeight = 14;
constexpr int kMinCellHeight = 62;
constexpr int kMaxCellHeight = 80;

/// Cells stop widening past this, and the grid centres instead. A four-column
/// group on a wide window used to stretch each cell to two hundred pixels of
/// which thirty-nine were the knob; capping the width is most of the answer to
/// "too much space used".
constexpr int kMaxCellWidth = 172;

constexpr int kHeadingHeight = 19;
constexpr int kGroupGap = 10;
constexpr int kPagePad = 5;

/// The strip along the bottom of the *editor* that carries the current page's
/// note. It used to be the last thing on the page itself, which put it below
/// the fold on exactly the two pages long enough to scroll -- and the MANGLE
/// note is the one that says what oversampling is doing right now, which is the
/// least useful thing in the plugin to have to scroll to find.
constexpr int kNoteHeight = 38;

// The envelope page's block: a heading, then a graph beside two rows of knobs.
constexpr int kEnvKnobRows = 3;
constexpr int kEnvKnobColumns = 3;
constexpr int kEnvBodyHeight = kEnvKnobRows * kMinCellHeight + 4;
constexpr int kEnvBlockHeight = kHeadingHeight + kEnvBodyHeight + 4;

/// A folded ADV row: the heading and its enable pill, nothing else.
constexpr int kAdvStripHeight = kHeadingHeight + 44;

constexpr int kMinWidth  = 880;
constexpr int kMinHeight = 560;
constexpr int kMaxWidth  = 1720;
constexpr int kMaxHeight = 1240;

constexpr int kMeterWidth = ui::LevelMeter::kMinimumWidth + ui::LevelMeter::kScaleWidth;
constexpr int kTabHeight = 26;
constexpr int kStatusHeight = 26;
constexpr int kStepStripHeight = 108;

/// Splits "NAME -- what it is" into its two halves. Everything after the dashes
/// is set in dim text, which is what stops six headings on a page reading as six
/// paragraphs.
[[nodiscard]] std::pair<juce::String, juce::String> splitHeading (const juce::String& text)
{
    const auto separator = text.indexOf (" -- ");

    if (separator < 0)
        return { text, {} };

    return { text.substring (0, separator).trim(), text.substring (separator + 4).trim() };
}

/// The panel behind a group of controls.
///
/// Two things beyond a filled rectangle, and both are what makes a dark panel
/// look lit rather than flat: a shallow vertical gradient, brighter at the top,
/// and a **one-pixel highlight along the top edge**. That highlight is how every
/// piece of real hardware catches the light in a photograph, and it costs a
/// single line.
void paintGroupPanel (juce::Graphics& g, juce::Rectangle<int> bounds)
{
    const auto area = bounds.toFloat();

    // A drop shadow, because the plate now sits on something bright and a dark
    // rectangle with no shadow reads as a hole rather than as a plate.
    g.setColour (juce::Colours::black.withAlpha (0.30f));
    g.fillRoundedRectangle (area.translated (0.0f, 1.5f).expanded (1.0f, 0.5f), 6.0f);

    g.setGradientFill (juce::ColourGradient (kGroupPanel.brighter (0.09f), area.getX(), area.getY(),
                                             kGroupPanel.darker (0.16f), area.getX(), area.getBottom(),
                                             false));
    g.fillRoundedRectangle (area, 5.0f);

    // A bright edge along the top and a dark one along the bottom: a raised
    // plate catches the light on its upper lip and casts into its lower one,
    // and two lines are the whole of it.
    g.setColour (juce::Colours::white.withAlpha (0.10f));
    g.drawLine (area.getX() + 5.0f, area.getY() + 0.5f,
                area.getRight() - 5.0f, area.getY() + 0.5f, 1.0f);

    g.setColour (juce::Colours::black.withAlpha (0.28f));
    g.drawLine (area.getX() + 5.0f, area.getBottom() - 0.5f,
                area.getRight() - 5.0f, area.getBottom() - 0.5f, 1.0f);
}

/// Draws a group's heading: its name, its explanation, and a rule running out
/// to the right edge so the group reads as one thing.
void paintHeading (juce::Graphics& g, const ui::Palette& palette, juce::Rectangle<int> row,
                   const juce::String& name, const juce::String& detail)
{
    const auto text = row.reduced (8, 0);

    g.setColour (palette.accent);
    g.setFont (juce::FontOptions (10.5f, juce::Font::bold));

    const auto nameWidth = juce::GlyphArrangement::getStringWidthInt (g.getCurrentFont(), name);
    g.drawText (name, text, juce::Justification::centredLeft, false);

    int x = text.getX() + nameWidth + 8;

    if (detail.isNotEmpty())
    {
        g.setColour (palette.dimText);
        g.setFont (juce::FontOptions (10.5f));

        const auto detailWidth = juce::GlyphArrangement::getStringWidthInt (g.getCurrentFont(), detail);

        g.drawText (detail, text.withX (x).withWidth (juce::jmax (0, text.getRight() - x)),
                    juce::Justification::centredLeft, false);

        x += juce::jmin (detailWidth, juce::jmax (0, text.getRight() - x)) + 8;
    }

    if (x < text.getRight())
    {
        g.setColour (palette.accent.withAlpha (0.16f));
        g.drawHorizontalLine (row.getCentreY(), static_cast<float> (x),
                              static_cast<float> (text.getRight()));
    }
}
} // namespace

// ---------------------------------------------------------------------------
// MetalBackground
// ---------------------------------------------------------------------------

void MetalBackground::paint (juce::Graphics& g, juce::Rectangle<int> bounds,
                         float highlightAt)
{
    if (bounds.isEmpty())
        return;

    if (image_.getWidth() != bounds.getWidth() || image_.getHeight() != bounds.getHeight()
        || std::abs (highlight_ - highlightAt) > 1.0e-6f)
    {
        render (bounds.getWidth(), bounds.getHeight(), highlightAt);
    }

    g.drawImageAt (image_, bounds.getX(), bounds.getY());
}

void MetalBackground::render (int width, int height, float highlightAt)
{
    image_ = juce::Image { juce::Image::RGB, width, height, false };
    highlight_ = highlightAt;

    juce::Graphics g { image_ };

    // The base gradient: bright near the specular band, falling away above
    // and below it. Three stops rather than two, because a linear ramp from
    // top to bottom reads as a backdrop and metal reads as a *surface*.
    juce::ColourGradient gradient { kMetalLow, 0.0f, 0.0f,
                                    kMetalLow, 0.0f, static_cast<float> (height), false };

    gradient.addColour (juce::jlimit (0.05, 0.95, static_cast<double> (highlightAt)),
                        kMetalHigh);
    gradient.addColour (juce::jlimit (0.06, 0.96, static_cast<double> (highlightAt) + 0.25),
                        kMetalMid);

    g.setGradientFill (gradient);
    g.fillRect (0, 0, width, height);

    // The brush marks. Two passes at different densities so the grain has
    // more than one scale to it -- one alone reads as television static.
    for (int pass = 0; pass < 2; ++pass)
    {
        const float thickness = pass == 0 ? 1.0f : 2.0f;
        const int step = pass == 0 ? 1 : 3;

        for (int y = 0; y < height; y += step)
        {
            const double noise = hashed (static_cast<std::uint64_t> (y)
                                           + static_cast<std::uint64_t> (pass) * 7919ull);

            // **Always subtractive.** The first version was centred on zero, so
            // the grain lightened as often as it darkened and the average
            // brightness stayed the gradient's -- tidy, but it put bright lines
            // on the backdrop, competing with the panels in front of it. A
            // brushed surface is scratched *into* the metal, so every mark is
            // a place where less light comes back. Darker than the gradient it
            // sits on, always, and by an amount that varies.
            const float alpha = static_cast<float> (std::abs (noise)) * (pass == 0 ? 0.16f : 0.085f);

            g.setColour (juce::Colours::black.withAlpha (alpha));
            g.fillRect (0.0f, static_cast<float> (y), static_cast<float> (width), thickness);
        }
    }
}

double MetalBackground::hashed (std::uint64_t index)
{
    std::uint64_t z = index + 0x9e3779b97f4a7c15ull;

    z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ull;
    z = (z ^ (z >> 27)) * 0x94d049bb133111ebull;
    z ^= z >> 31;

    return 2.0 * (static_cast<double> (z >> 11) / 9007199254740992.0) - 1.0;
}

// ---------------------------------------------------------------------------
// WrappingLabel
// ---------------------------------------------------------------------------

void WrappingLabel::paint (juce::Graphics& g)
{
    g.setColour (findColour (juce::Label::textColourId));
    g.setFont (getFont());
    g.drawFittedText (getText(), getLocalBounds(), getJustificationType(), 4, 1.0f);
}

// ---------------------------------------------------------------------------
// Cells
// ---------------------------------------------------------------------------

ParameterCell::ParameterCell (juce::String parameterId, const juce::String& name, ui::Palette palette)
    : id_ (std::move (parameterId)), palette_ (palette)
{
    // Upper case, because a name set beside a number wants to be read as a
    // label rather than as a word -- and at ten pixels the capitals are the
    // legible half of the alphabet anyway.
    label_.setText (name.toUpperCase(), juce::dontSendNotification);
    label_.setJustificationType (juce::Justification::centred);
    label_.setColour (juce::Label::textColourId, palette_.dimText);
    label_.setFont (juce::FontOptions (10.0f, juce::Font::bold));
    label_.setMinimumHorizontalScale (0.65f);
    addAndMakeVisible (label_);

    // The parameter's own id, so `tezla-render editor hit:cutoff` can ask
    // whether that control is the thing a click at its centre would reach. The
    // Transpectus lesson: a buried control is still visible and still enabled,
    // and from the outside is indistinguishable from one that was never added.
    setComponentID (id_);
}

void ParameterCell::resized()
{
    label_.setBounds (getLocalBounds().removeFromTop (kCellLabelHeight));
}

juce::Rectangle<int> ParameterCell::controlBounds() const
{
    return getLocalBounds().withTrimmedTop (kCellLabelHeight);
}

KnobCell::KnobCell (juce::AudioProcessorValueTreeState& state, const juce::String& parameterId,
                    const juce::String& name, const juce::String& tooltip, ui::Palette palette)
    : ParameterCell (parameterId, name, palette)
{
    slider_.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    slider_.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 90, kCellValueHeight);
    slider_.setColour (juce::Slider::textBoxTextColourId, palette_.text);
    slider_.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    slider_.setTooltip (tooltip);
    label_.setTooltip (tooltip);

    // Double-click goes back to the parameter's own default rather than to the
    // middle of its range, which on a skewed range is somewhere else entirely.
    if (auto* parameter = state.getParameter (parameterId))
        slider_.setDoubleClickReturnValue (
            true, parameter->convertFrom0to1 (parameter->getDefaultValue()));

    addAndMakeVisible (slider_);

    attachment_ = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        state, parameterId, slider_);
}

void KnobCell::setControlEnabled (bool enabled)
{
    slider_.setEnabled (enabled);

    // The number too. A greyed knob beside a bright reading says the control is
    // inert but its value still matters, which is the opposite of the truth.
    slider_.setColour (juce::Slider::textBoxTextColourId,
                       enabled ? palette_.text : palette_.dimText.withAlpha (0.4f));

    label_.setColour (juce::Label::textColourId,
                      enabled ? palette_.dimText : palette_.dimText.withAlpha (0.35f));
    slider_.repaint();
    label_.repaint();
}

void KnobCell::resized()
{
    ParameterCell::resized();
    slider_.setBounds (controlBounds().reduced (2, 0));
}

WaveCell::WaveCell (juce::AudioProcessorValueTreeState& state, const juce::String& shapeId,
                    const juce::String& widthId, const juce::String& morphId,
                    ui::Palette palette)
    : ParameterCell (shapeId, "", palette),
      state_ (state), shapeId_ (shapeId), widthId_ (widthId), morphId_ (morphId)
{
    setComponentID ("wave-" + shapeId);
    setInterceptsMouseClicks (false, false);

    state_.addParameterListener (shapeId_, this);
    state_.addParameterListener (widthId_, this);
    state_.addParameterListener (morphId_, this);
}

WaveCell::~WaveCell()
{
    state_.removeParameterListener (shapeId_, this);
    state_.removeParameterListener (widthId_, this);
    state_.removeParameterListener (morphId_, this);
}

void WaveCell::parameterChanged (const juce::String&, float)
{
    // Parameter callbacks may arrive off the message thread; painting may not.
    juce::MessageManager::callAsync ([safe = juce::Component::SafePointer (this)]
    {
        if (safe != nullptr)
            safe->repaint();
    });
}

void WaveCell::paint (juce::Graphics& g)
{
    const auto value = [this] (const juce::String& id)
    {
        const auto* raw = state_.getRawParameterValue (id);
        return raw != nullptr ? raw->load() : 0.0f;
    };

    const auto shape = static_cast<dsp::OscShape> (std::lround (value (shapeId_)));
    const auto width = static_cast<double> (value (widthId_));
    const auto morph = static_cast<double> (value (morphId_));

    auto box = getLocalBounds().reduced (2).toFloat();

    g.setColour (palette_.background.darker (0.25f));
    g.fillRoundedRectangle (box, 4.0f);

    g.setColour (palette_.dimText.withAlpha (0.25f));
    g.drawHorizontalLine (static_cast<int> (box.getCentreY()), box.getX() + 3.0f,
                          box.getRight() - 3.0f);

    // One cycle of the same function the DSP reads, so this cannot lie.
    juce::Path path;
    const auto inner = box.reduced (4.0f, 5.0f);
    constexpr int kPoints = 96;

    for (int i = 0; i <= kPoints; ++i)
    {
        const double phase = static_cast<double> (i) / kPoints;
        const double sample = dsp::Oscillator::naiveShapeSample (shape, phase, width, morph);

        const float x = inner.getX() + inner.getWidth() * static_cast<float> (phase);
        const float y = inner.getCentreY()
                      - 0.5f * inner.getHeight() * static_cast<float> (sample);

        if (i == 0)
            path.startNewSubPath (x, y);
        else
            path.lineTo (x, y);
    }

    g.setColour (palette_.accent);
    g.strokePath (path, juce::PathStrokeType (1.6f, juce::PathStrokeType::curved));
}

MorphCell::MorphCell (juce::AudioProcessorValueTreeState& state, const juce::String& parameterId,
                      const juce::String& name, const juce::String& tooltip, ui::Palette palette)
    : ParameterCell (parameterId, name, palette)
{
    slider_.setSliderStyle (juce::Slider::LinearHorizontal);
    slider_.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    slider_.setPopupDisplayEnabled (true, true, nullptr);
    slider_.setTooltip (tooltip);
    label_.setTooltip (tooltip);

    if (auto* parameter = state.getParameter (parameterId))
        slider_.setDoubleClickReturnValue (
            true, parameter->convertFrom0to1 (parameter->getDefaultValue()));

    addAndMakeVisible (slider_);

    attachment_ = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        state, parameterId, slider_);
}

void MorphCell::setControlEnabled (bool enabled)
{
    slider_.setEnabled (enabled);
    slider_.setAlpha (enabled ? 1.0f : 0.35f);
    label_.setColour (juce::Label::textColourId,
                      enabled ? palette_.dimText : palette_.dimText.withAlpha (0.4f));
}

void MorphCell::resized()
{
    ParameterCell::resized();

    // A slim strip centred in the control area, so the small slider reads as
    // deliberate rather than as a knob that failed to draw.
    auto area = controlBounds();
    slider_.setBounds (area.withSizeKeepingCentre (area.getWidth() - 6,
                                                   juce::jmin (18, area.getHeight())));
}

ChoiceCell::ChoiceCell (juce::AudioProcessorValueTreeState& state, const juce::String& parameterId,
                        const juce::String& name, const juce::String& tooltip, ui::Palette palette)
    : ParameterCell (parameterId, name, palette)
{
    // Populated from the parameter itself. A ComboBoxAttachment selects an item
    // by index and does not create one, so a box left empty here stays empty on
    // screen and cannot be operated at all.
    if (auto* parameter = dynamic_cast<juce::AudioParameterChoice*> (state.getParameter (parameterId)))
        box_.addItemList (parameter->choices, 1);
    else
        jassertfalse;   // a ChoiceCell on something that is not a choice parameter

    box_.setColour (juce::ComboBox::backgroundColourId, palette_.panel.brighter (0.10f));
    box_.setColour (juce::ComboBox::textColourId, palette_.text);
    box_.setTooltip (tooltip);
    label_.setTooltip (tooltip);
    addAndMakeVisible (box_);

    attachment_ = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (
        state, parameterId, box_);
}

void ChoiceCell::setControlEnabled (bool enabled)
{
    box_.setEnabled (enabled);
    label_.setColour (juce::Label::textColourId,
                      enabled ? palette_.dimText : palette_.dimText.withAlpha (0.35f));
    label_.repaint();
}

void ChoiceCell::resized()
{
    ParameterCell::resized();
    auto area = controlBounds();
    box_.setBounds (area.withSizeKeepingCentre (juce::jmin (area.getWidth() - 6, 118), 22)
                        .withY (area.getY() + 4));
}

ToggleCell::ToggleCell (juce::AudioProcessorValueTreeState& state, const juce::String& parameterId,
                        const juce::String& name, const juce::String& tooltip, ui::Palette palette)
    : ParameterCell (parameterId, name, palette)
{
    button_.setButtonText ("");
    button_.setTooltip (tooltip);
    label_.setTooltip (tooltip);
    addAndMakeVisible (button_);

    attachment_ = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
        state, parameterId, button_);
}

void ToggleCell::setControlEnabled (bool enabled)
{
    button_.setEnabled (enabled);
    label_.setColour (juce::Label::textColourId,
                      enabled ? palette_.dimText : palette_.dimText.withAlpha (0.35f));
    label_.repaint();
}

void ToggleCell::resized()
{
    ParameterCell::resized();
    auto area = controlBounds();
    button_.setBounds (area.withSizeKeepingCentre (40, 20).withY (area.getY() + 6));
}

// ---------------------------------------------------------------------------
// ControlPage
// ---------------------------------------------------------------------------

void ControlPage::addHeading (const juce::String& text, int columns, bool sameRow)
{
    const auto parts = splitHeading (text);

    // The first group on a page has nothing to sit beside, whatever it asks
    // for -- so the flag is cleared rather than trusted.
    groups_.push_back ({ parts.first, parts.second, juce::jmax (1, columns),
                         sameRow && ! groups_.empty(), {}, {} });
}

ControlPage::Group& ControlPage::currentGroup()
{
    if (groups_.empty())
        groups_.push_back ({ {}, {}, 5, false, {}, {} });

    return groups_.back();
}

void ControlPage::add (std::unique_ptr<ParameterCell> cell)
{
    auto* raw = cell.get();

    addAndMakeVisible (*raw);
    owned_.push_back (std::move (cell));
    currentGroup().cells.push_back (raw);
}

void ControlPage::addKnob (const juce::String& parameterId, const juce::String& name,
                           const juce::String& tooltip)
{
    add (std::make_unique<KnobCell> (state_, parameterId, name, tooltip, palette_));
}

void ControlPage::addChoice (const juce::String& parameterId, const juce::String& name,
                             const juce::String& tooltip)
{
    add (std::make_unique<ChoiceCell> (state_, parameterId, name, tooltip, palette_));
}

void ControlPage::addToggle (const juce::String& parameterId, const juce::String& name,
                             const juce::String& tooltip)
{
    add (std::make_unique<ToggleCell> (state_, parameterId, name, tooltip, palette_));
}

void ControlPage::addWave (const juce::String& shapeId, const juce::String& widthId,
                           const juce::String& morphId)
{
    add (std::make_unique<WaveCell> (state_, shapeId, widthId, morphId, palette_));
}

void ControlPage::addMorph (const juce::String& parameterId, const juce::String& name,
                            const juce::String& tooltip)
{
    add (std::make_unique<MorphCell> (state_, parameterId, name, tooltip, palette_));
}

void ControlPage::addGap()
{
    currentGroup().cells.push_back (nullptr);
}

void ControlPage::setControlEnabled (const juce::String& parameterId, bool enabled)
{
    for (auto& cell : owned_)
        if (cell->parameterId() == parameterId)
            cell->setControlEnabled (enabled);
}

int ControlPage::rowsIn (const Group& group) const
{
    return (static_cast<int> (group.cells.size()) + group.columns - 1) / group.columns;
}

int ControlPage::totalRows() const
{
    // A band is one group, or several sharing a row -- and a band is as tall as
    // its tallest member. Summing the groups instead would reserve height for
    // rows that are drawn side by side and leave a page of air below them.
    int rows = 0;
    int band = 0;

    for (const auto& group : groups_)
    {
        if (! group.sameRow)
        {
            rows += band;
            band = 0;
        }

        band = juce::jmax (band, rowsIn (group));
    }

    return rows + band;
}

int ControlPage::bandCount() const
{
    int bands = 0;

    for (const auto& group : groups_)
        if (! group.sameRow)
            ++bands;

    return juce::jmax (1, bands);
}

int ControlPage::getPreferredHeight() const
{
    return 2 * kPagePad
         + bandCount() * (kHeadingHeight + 4 + kGroupGap)
         + totalRows() * kMinCellHeight;
}

void ControlPage::paint (juce::Graphics& g)
{
    // Only as far down as the content goes. A page with two groups in a tall
    // window used to paint a panel over the whole of it and leave two thirds of
    // that panel empty, which reads as a layout that has gone wrong rather than
    // as a page that is simply short.
    // **Nothing.** The brushed chassis showing between the plates is the whole
    // look: a bright silver frame with dark control plates bolted to it, which
    // is how the hardware this is imitating is actually built -- and it is what
    // lets the panel be silver while the accents keep the dark ground they need
    // to be legible against. A page-wide fill here would cover it up and put
    // the plates on a rectangle instead of on metal.

    for (const auto& group : groups_)
    {
        if (group.bounds.isEmpty())
            continue;

        paintGroupPanel (g, group.bounds);

        if (group.heading.isNotEmpty())
            paintHeading (g, palette_, group.bounds.withHeight (kHeadingHeight),
                          group.heading, group.detail);
    }
}

void ControlPage::resized()
{
    if (groups_.empty())
        return;

    auto bounds = getLocalBounds().reduced (6, kPagePad);

    const int rows = totalRows();
    const int bands = bandCount();
    const int available = bounds.getHeight() - bands * (kHeadingHeight + 4 + kGroupGap);

    const int cellHeight = rows > 0
        ? juce::jlimit (kMinCellHeight, kMaxCellHeight, available / rows)
        : kMinCellHeight;

    int y = bounds.getY();

    for (std::size_t first = 0; first < groups_.size(); )
    {
        // Everything from here up to the next group that wants its own row.
        std::size_t last = first + 1;

        while (last < groups_.size() && groups_[last].sameRow)
            ++last;

        // The band is as tall as its tallest member, and its width is shared in
        // proportion to what each member asked for -- so a two-column group
        // beside a five-column one gets two sevenths, and the cells across the
        // whole band come out the same size.
        int tallest = 1;
        int totalColumns = 0;

        for (std::size_t i = first; i < last; ++i)
        {
            tallest = juce::jmax (tallest, rowsIn (groups_[i]));
            totalColumns += groups_[i].columns;
        }

        const int bandHeight = kHeadingHeight + tallest * cellHeight + 4;

        int x = bounds.getX();

        for (std::size_t i = first; i < last; ++i)
        {
            auto& group = groups_[i];

            const bool lastInBand = i + 1 == last;

            const int width = lastInBand
                ? bounds.getRight() - x
                : bounds.getWidth() * group.columns / totalColumns;

            group.bounds = { x, y, width, bandHeight };

            auto inner = group.bounds.reduced (4, 2).withTrimmedTop (kHeadingHeight);

            // A group with fewer controls than columns centres on what it has
            // rather than on what it was allowed.
            const int used = juce::jmax (1, juce::jmin (group.columns,
                                                        static_cast<int> (group.cells.size())));

            const int cellWidth = juce::jmin (kMaxCellWidth, inner.getWidth() / group.columns);
            const int left = inner.getX() + (inner.getWidth() - cellWidth * used) / 2;

            for (std::size_t cell = 0; cell < group.cells.size(); ++cell)
            {
                if (group.cells[cell] == nullptr)
                    continue;

                const int column = static_cast<int> (cell) % group.columns;
                const int row = static_cast<int> (cell) / group.columns;

                group.cells[cell]->setBounds ({ left + column * cellWidth,
                                                inner.getY() + row * cellHeight,
                                                cellWidth, cellHeight });
            }

            x += width;
        }

        y += bandHeight + kGroupGap;
        first = last;
    }

    contentHeight_ = y - bounds.getY() - kGroupGap + 2 * kPagePad;
}

// ---------------------------------------------------------------------------
// EnvelopeEditor
// ---------------------------------------------------------------------------

namespace
{
// How much of the graph's width each segment is allotted. The sustain's slice
// is fixed and the other four are filled in proportion to their parameters, so
// the five together are exactly the full width when all four are at maximum.
constexpr float kAttackShare  = 0.22f;
constexpr float kHoldShare    = 0.16f;
constexpr float kDecayShare   = 0.22f;
constexpr float kSustainShare = 0.12f;
constexpr float kReleaseShare = 0.28f;

constexpr float kHandleRadius = 4.5f;
constexpr float kGrabRadius = 11.0f;

/// The width the live-level column takes on the right.
constexpr float kLevelColumn = 7.0f;

/// The strip along the bottom that carries the A / H / D / S / R marks.
constexpr float kAxisHeight = 11.0f;
} // namespace

EnvelopeEditor::EnvelopeEditor (juce::AudioProcessorValueTreeState& state, ui::Palette palette,
                                Ids ids)
    : state_ (state), palette_ (palette), ids_ (std::move (ids))
{
    setTooltip ("Drag the four corners: the first sets the attack, the second the hold, the third "
                "sets the decay across and the sustain up and down, and the last sets the release. "
                "Double-click a corner to put it back to its default. The knobs do the same job to "
                "the sample -- this is for finding the shape, they are for pinning it down.\n\n"
                "The curve drawn is the curve that plays, tension included -- each segment bends "
                "the way its own tension knob says. The bar on the right is this envelope's live "
                "output on the most recent note.");
}

double EnvelopeEditor::segment (double u, double from, double to, double tension)
{
    // The library's arithmetic, not an approximation of it. A segment aims past
    // its destination and approaches it (positive tension), or aims past its
    // *origin* and recedes from it (negative), and the second case is exactly
    // the first with the coefficient inverted. See shared/tezla-dsp Adsr.hpp.
    const double distance = to - from;

    if (std::abs (distance) < 1.0e-12)
        return to;

    const double overshoot = dsp::Adsr::overshootFor (tension);
    const double ratio = (overshoot - 1.0) / overshoot;

    const double target = tension < 0.0 ? from - distance * (overshoot - 1.0)
                                        : to + distance * (overshoot - 1.0);

    const double travelled = std::pow (ratio, tension < 0.0 ? -u : u);

    return target + (from - target) * travelled;
}

float EnvelopeEditor::normalised (const juce::String& id) const
{
    if (auto* parameter = state_.getParameter (id))
        return parameter->getValue();

    return 0.0f;
}

float EnvelopeEditor::plain (const juce::String& id) const
{
    if (auto* parameter = state_.getParameter (id))
        return parameter->convertFrom0to1 (parameter->getValue());

    return 0.0f;
}

void EnvelopeEditor::setNormalised (const juce::String& id, float value, bool gesture)
{
    auto* parameter = state_.getParameter (id);

    if (parameter == nullptr)
        return;

    const float clamped = juce::jlimit (0.0f, 1.0f, value);

    if (gesture)
        parameter->beginChangeGesture();

    parameter->setValueNotifyingHost (clamped);

    if (gesture)
        parameter->endChangeGesture();
}

EnvelopeEditor::Geometry EnvelopeEditor::geometry() const
{
    Geometry g;

    g.plot = getLocalBounds().toFloat().reduced (9.0f, 8.0f);
    g.plot.removeFromBottom (kAxisHeight);
    g.plot.removeFromRight (kLevelColumn + 5.0f);

    const float width = g.plot.getWidth();
    const float x0 = g.plot.getX();

    g.attackX = x0 + width * kAttackShare * normalised (ids_.attack);
    g.holdX = g.attackX + width * kHoldShare * normalised (ids_.hold);
    g.decayX = g.holdX + width * kDecayShare * normalised (ids_.decay);
    g.sustainEndX = g.decayX + width * kSustainShare;
    g.releaseX = g.sustainEndX + width * kReleaseShare * normalised (ids_.release);

    g.sustainY = g.plot.getBottom() - normalised (ids_.sustain) * g.plot.getHeight();

    return g;
}

juce::Point<float> EnvelopeEditor::handlePosition (Handle handle, const Geometry& g) const
{
    switch (handle)
    {
        case Handle::attack:       return { g.attackX, g.plot.getY() };
        case Handle::hold:         return { g.holdX, g.plot.getY() };
        case Handle::decaySustain: return { g.decayX, g.sustainY };
        case Handle::release:      return { g.releaseX, g.plot.getBottom() };
        case Handle::none:         break;
    }

    return {};
}

EnvelopeEditor::Handle EnvelopeEditor::handleAt (juce::Point<float> position) const
{
    const auto g = geometry();

    Handle best = Handle::none;
    float bestDistance = kGrabRadius;

    // Hold before attack, so that with the hold at zero -- where the two sit on
    // top of each other -- the one that grows is the one you get.
    for (auto handle : { Handle::hold, Handle::attack, Handle::decaySustain, Handle::release })
    {
        const float distance = position.getDistanceFrom (handlePosition (handle, g));

        if (distance < bestDistance)
        {
            bestDistance = distance;
            best = handle;
        }
    }

    return best;
}

void EnvelopeEditor::refresh (double level)
{
    const float values[8] { normalised (ids_.attack), normalised (ids_.hold),
                            normalised (ids_.decay), normalised (ids_.sustain),
                            normalised (ids_.release), normalised (ids_.attackTension),
                            normalised (ids_.decayTension), normalised (ids_.releaseTension) };

    bool changed = std::abs (static_cast<float> (level) - shownLevel_) > 1.0e-3f;

    for (int i = 0; i < 8; ++i)
        if (std::abs (values[i] - shown_[i]) > 1.0e-6f)
            changed = true;

    if (! changed)
        return;

    for (int i = 0; i < 8; ++i)
        shown_[i] = values[i];

    shownLevel_ = static_cast<float> (level);
    level_ = static_cast<float> (juce::jlimit (0.0, 1.0, level));

    repaint();
}

void EnvelopeEditor::appendSegment (juce::Path& path, float x0, float x1,
                                    double from, double to, double tension) const
{
    constexpr int kSteps = 28;

    const auto g = geometry();

    const auto yFor = [&g] (double level)
    {
        return g.plot.getBottom() - static_cast<float> (level) * g.plot.getHeight();
    };

    // A segment with no width is a vertical edge -- a zero attack is a click,
    // and drawing it as one is honest.
    if (x1 - x0 < 0.5f)
    {
        path.lineTo (x1, yFor (to));
        return;
    }

    for (int step = 1; step <= kSteps; ++step)
    {
        const double u = static_cast<double> (step) / kSteps;

        path.lineTo (x0 + (x1 - x0) * static_cast<float> (u),
                     yFor (segment (u, from, to, tension)));
    }
}

void EnvelopeEditor::paint (juce::Graphics& graphics)
{
    const auto g = geometry();
    const auto full = getLocalBounds().toFloat();

    graphics.setColour (palette_.background.brighter (0.06f));
    graphics.fillRoundedRectangle (full, 4.0f);

    // The grid: quarters, with the top and bottom rails brighter because those
    // two are the ones a value is read against.
    for (int line = 0; line <= 4; ++line)
    {
        const float y = g.plot.getBottom() - g.plot.getHeight() * static_cast<float> (line) / 4.0f;

        graphics.setColour (palette_.dimText.withAlpha (line == 0 || line == 4 ? 0.22f : 0.09f));
        graphics.drawHorizontalLine (juce::roundToInt (y), g.plot.getX(), g.plot.getRight());
    }

    const double sustain = normalised (ids_.sustain);

    const double attackTension = plain (ids_.attackTension);
    const double decayTension = plain (ids_.decayTension);
    const double releaseTension = plain (ids_.releaseTension);

    juce::Path curve;
    curve.startNewSubPath (g.plot.getX(), g.plot.getBottom());
    appendSegment (curve, g.plot.getX(), g.attackX, 0.0, 1.0, attackTension);
    curve.lineTo (g.holdX, g.plot.getY());
    appendSegment (curve, g.holdX, g.decayX, 1.0, sustain, decayTension);
    curve.lineTo (g.sustainEndX, g.sustainY);
    appendSegment (curve, g.sustainEndX, g.releaseX, sustain, 0.0, releaseTension);

    // Under the curve, so the shape reads as an amount rather than as a line.
    {
        juce::Path filled (curve);
        filled.lineTo (g.releaseX, g.plot.getBottom());
        filled.closeSubPath();

        graphics.setColour (palette_.accent.withAlpha (0.16f));
        graphics.fillPath (filled);
    }

    graphics.setColour (palette_.accent);
    graphics.strokePath (curve, juce::PathStrokeType (1.8f, juce::PathStrokeType::curved,
                                                      juce::PathStrokeType::rounded));

    // The sustain, overdrawn dashed: it lasts as long as the key is down, which
    // is the one part of the picture that is not a duration. The **hold** is
    // solid, because it is one.
    {
        const float dashes[] { 3.0f, 3.0f };

        graphics.setColour (palette_.background.brighter (0.06f));
        graphics.drawLine (g.decayX, g.sustainY, g.sustainEndX, g.sustainY, 2.6f);

        graphics.setColour (palette_.accent.withAlpha (0.8f));
        graphics.drawDashedLine ({ { g.decayX, g.sustainY }, { g.sustainEndX, g.sustainY } },
                                 dashes, 2, 1.8f);
    }

    // The handles, with the same halo the knobs get.
    for (auto handle : { Handle::attack, Handle::hold, Handle::decaySustain, Handle::release })
    {
        const auto centre = handlePosition (handle, g);
        const bool lit = handle == dragging_ || handle == hovered_;
        const float radius = lit ? kHandleRadius + 1.0f : kHandleRadius;

        graphics.setColour (palette_.accent.withAlpha (0.22f));
        graphics.fillEllipse (juce::Rectangle<float> { radius * 3.4f, radius * 3.4f }
                                .withCentre (centre));

        graphics.setColour (palette_.background.darker (0.4f));
        graphics.fillEllipse (juce::Rectangle<float> { radius * 2.0f + 2.0f, radius * 2.0f + 2.0f }
                                .withCentre (centre));

        graphics.setColour (lit ? palette_.accentBright : palette_.accent);
        graphics.fillEllipse (juce::Rectangle<float> { radius * 2.0f, radius * 2.0f }
                                .withCentre (centre));
    }

    // The axis marks, under the segments they name rather than under the slices
    // those segments were allotted. Under the allotments they stay put while a
    // handle is dragged, which is tidier and wrong: with a 5 ms attack the "A"
    // sat a quarter of the way across a graph whose attack was three pixels
    // wide. A mark that does not point at the thing it names is decoration.
    {
        const auto axis = juce::Rectangle<float> { g.plot.getX(), g.plot.getBottom() + 1.0f,
                                                   g.plot.getWidth(), kAxisHeight };

        graphics.setFont (juce::FontOptions (9.0f, juce::Font::bold));

        const float edges[6] { g.plot.getX(), g.attackX, g.holdX, g.decayX,
                               g.sustainEndX, g.releaseX };
        static const char* names[5] { "A", "H", "D", "S", "R" };

        for (int i = 0; i < 5; ++i)
        {
            const float width = edges[i + 1] - edges[i];

            // A segment too narrow to hold its own letter is left unlabelled --
            // a zero hold has nothing to point at, and a letter squeezed
            // between two others reads as belonging to neither.
            if (width < 11.0f)
                continue;

            graphics.setColour (palette_.dimText.withAlpha (0.65f));
            graphics.drawText (names[i], axis.withX (edges[i]).withWidth (width),
                               juce::Justification::centred, false);
        }
    }

    // The live level, in the modulation colour rather than the accent: it is a
    // reading, and everything else on this panel is a control.
    {
        const auto column = juce::Rectangle<float> { full.getRight() - kLevelColumn - 4.0f,
                                                     g.plot.getY(), kLevelColumn,
                                                     g.plot.getHeight() };

        graphics.setColour (palette_.panel.darker (0.25f));
        graphics.fillRoundedRectangle (column, 2.0f);

        if (level_ > 1.0e-4f)
        {
            const auto filled = column.withTop (column.getBottom() - level_ * column.getHeight());

            graphics.setColour (palette_.secondary.withAlpha (0.30f));
            graphics.fillRoundedRectangle (filled.expanded (2.0f, 0.0f), 3.0f);

            graphics.setColour (palette_.secondary);
            graphics.fillRoundedRectangle (filled, 2.0f);
        }
    }
}

void EnvelopeEditor::mouseDown (const juce::MouseEvent& event)
{
    dragging_ = handleAt (event.position);

    if (dragging_ == Handle::none)
        return;

    const auto begin = [this] (const juce::String& id)
    {
        if (auto* parameter = state_.getParameter (id))
            parameter->beginChangeGesture();
    };

    switch (dragging_)
    {
        case Handle::attack:       begin (ids_.attack); break;
        case Handle::hold:         begin (ids_.hold); break;
        case Handle::decaySustain: begin (ids_.decay); begin (ids_.sustain); break;
        case Handle::release:      begin (ids_.release); break;
        case Handle::none:         break;
    }
}

void EnvelopeEditor::mouseDrag (const juce::MouseEvent& event)
{
    if (dragging_ == Handle::none)
        return;

    const auto g = geometry();
    const float width = g.plot.getWidth();

    if (width <= 0.0f)
        return;

    // Each handle measures from the start of its own segment, which moves as the
    // ones before it do -- so dragging the release does not shift when the decay
    // is lengthened, and the number under the cursor is the one being set.
    switch (dragging_)
    {
        case Handle::attack:
            setNormalised (ids_.attack,
                           (event.position.x - g.plot.getX()) / (width * kAttackShare), false);
            break;

        case Handle::hold:
            setNormalised (ids_.hold,
                           (event.position.x - g.attackX) / (width * kHoldShare), false);
            break;

        case Handle::decaySustain:
            setNormalised (ids_.decay,
                           (event.position.x - g.holdX) / (width * kDecayShare), false);
            setNormalised (ids_.sustain,
                           (g.plot.getBottom() - event.position.y) / g.plot.getHeight(), false);
            break;

        case Handle::release:
            setNormalised (ids_.release,
                           (event.position.x - g.sustainEndX) / (width * kReleaseShare), false);
            break;

        case Handle::none:
            break;
    }
}

void EnvelopeEditor::mouseUp (const juce::MouseEvent&)
{
    const auto end = [this] (const juce::String& id)
    {
        if (auto* parameter = state_.getParameter (id))
            parameter->endChangeGesture();
    };

    switch (dragging_)
    {
        case Handle::attack:       end (ids_.attack); break;
        case Handle::hold:         end (ids_.hold); break;
        case Handle::decaySustain: end (ids_.decay); end (ids_.sustain); break;
        case Handle::release:      end (ids_.release); break;
        case Handle::none:         break;
    }

    dragging_ = Handle::none;
}

void EnvelopeEditor::mouseMove (const juce::MouseEvent& event)
{
    const auto hovered = handleAt (event.position);

    if (hovered == hovered_)
        return;

    hovered_ = hovered;
    setMouseCursor (hovered_ == Handle::none ? juce::MouseCursor::NormalCursor
                                             : juce::MouseCursor::DraggingHandCursor);
    repaint();
}

void EnvelopeEditor::mouseExit (const juce::MouseEvent&)
{
    if (hovered_ == Handle::none)
        return;

    hovered_ = Handle::none;
    repaint();
}

void EnvelopeEditor::mouseDoubleClick (const juce::MouseEvent& event)
{
    const auto handle = handleAt (event.position);

    const auto reset = [this] (const juce::String& id)
    {
        if (auto* parameter = state_.getParameter (id))
            setNormalised (id, parameter->getDefaultValue(), true);
    };

    switch (handle)
    {
        case Handle::attack:       reset (ids_.attack); break;
        case Handle::hold:         reset (ids_.hold); break;
        case Handle::decaySustain: reset (ids_.decay); reset (ids_.sustain); break;
        case Handle::release:      reset (ids_.release); break;
        case Handle::none:         break;
    }
}

// ---------------------------------------------------------------------------
// MultiEnvelopeEditor
// ---------------------------------------------------------------------------

MultiEnvelopeEditor::MultiEnvelopeEditor (juce::AudioProcessorValueTreeState& state,
                                          int envelopeIndex, ui::Palette palette)
    : state_ (state), envelope_ (envelopeIndex), palette_ (palette)
{
    setComponentID ("adv" + juce::String (envelopeIndex + 1) + "-graph");
}

float MultiEnvelopeEditor::plain (const juce::String& field) const
{
    if (auto* parameter = state_.getParameter (ids::adv (envelope_, field)))
        return parameter->convertFrom0to1 (parameter->getValue());

    return 0.0f;
}

void MultiEnvelopeEditor::setPlain (const juce::String& field, float value, bool gesture)
{
    auto* parameter = state_.getParameter (ids::adv (envelope_, field));

    if (parameter == nullptr)
        return;

    const float normalised = parameter->convertTo0to1 (value);

    if (gesture)
        parameter->beginChangeGesture();

    parameter->setValueNotifyingHost (juce::jlimit (0.0f, 1.0f, normalised));

    if (gesture)
        parameter->endChangeGesture();
}

juce::Rectangle<float> MultiEnvelopeEditor::plotArea() const
{
    return getLocalBounds().toFloat().reduced (8.0f, 7.0f);
}

MultiEnvelopeEditor::Layout MultiEnvelopeEditor::layoutNow() const
{
    Layout layout;

    layout.points = juce::jlimit (2, dsp::MultiEnvelope::kMaxPoints,
                                  static_cast<int> (std::lround (plain ("Points"))));
    layout.sustain = juce::jlimit (0, layout.points - 1,
                                   static_cast<int> (std::lround (plain ("Sustain"))) - 1);
    layout.loopStart = juce::jlimit (0, layout.sustain,
                                     static_cast<int> (std::lround (plain ("LoopStart"))) - 1);
    layout.loop = state_.getRawParameterValue (ids::adv (envelope_, "Loop"))->load() > 0.5f;

    layout.total = 0.0;
    for (int i = 0; i < layout.points; ++i)
        layout.total += plain ("T" + juce::String (i + 1));
    layout.total = std::max (layout.total, 1.0e-3);

    const auto area = plotArea();

    layout.x[0] = area.getX();
    layout.y[0] = area.getBottom();   // the gate opens from level 0

    double elapsed = 0.0;
    for (int i = 0; i < layout.points; ++i)
    {
        elapsed += plain ("T" + juce::String (i + 1));

        layout.x[static_cast<std::size_t> (i + 1)] =
            area.getX() + area.getWidth() * static_cast<float> (elapsed / layout.total);
        layout.y[static_cast<std::size_t> (i + 1)] =
            area.getBottom() - area.getHeight() * plain ("L" + juce::String (i + 1));
    }

    return layout;
}

void MultiEnvelopeEditor::paint (juce::Graphics& g)
{
    const auto area = plotArea();
    const auto layout = layoutNow();

    g.setColour (palette_.background.darker (0.2f));
    g.fillRoundedRectangle (getLocalBounds().toFloat(), 5.0f);

    // The loop region, shaded, while Loop is on.
    if (layout.loop)
    {
        const float from = layout.x[static_cast<std::size_t> (layout.loopStart + 1)];
        const float to = layout.x[static_cast<std::size_t> (layout.sustain + 1)];

        g.setColour (palette_.secondary.withAlpha (0.10f));
        g.fillRect (juce::Rectangle<float> (from, area.getY(),
                                            std::max (2.0f, to - from), area.getHeight()));
    }

    // The curve, in the DSP's own arithmetic -- EnvelopeEditor::segment.
    juce::Path path;
    path.startNewSubPath (layout.x[0], layout.y[0]);

    for (int i = 0; i < layout.points; ++i)
    {
        const float x0 = layout.x[static_cast<std::size_t> (i)];
        const float x1 = layout.x[static_cast<std::size_t> (i + 1)];
        const double from = (area.getBottom() - layout.y[static_cast<std::size_t> (i)]) / area.getHeight();
        const double to = (area.getBottom() - layout.y[static_cast<std::size_t> (i + 1)]) / area.getHeight();
        const double tension = plain ("C" + juce::String (i + 1));

        constexpr int kSteps = 20;
        for (int step = 1; step <= kSteps; ++step)
        {
            const double u = static_cast<double> (step) / kSteps;
            const double level = EnvelopeEditor::segment (u, from, to, tension);

            path.lineTo (x0 + (x1 - x0) * static_cast<float> (u),
                         area.getBottom() - area.getHeight() * static_cast<float> (level));
        }
    }

    g.setColour (palette_.accent);
    g.strokePath (path, juce::PathStrokeType (1.8f, juce::PathStrokeType::curved));

    // Handles. The sustain point wears a ring; the release chain draws dim.
    for (int i = 0; i < layout.points; ++i)
    {
        const auto centre = juce::Point<float> (layout.x[static_cast<std::size_t> (i + 1)],
                                                layout.y[static_cast<std::size_t> (i + 1)]);

        const bool isSustain = i == layout.sustain;
        const bool isRelease = i > layout.sustain;

        g.setColour (isRelease ? palette_.dimText : palette_.accentBright);
        g.fillEllipse (juce::Rectangle<float> (7.0f, 7.0f).withCentre (centre));

        if (isSustain)
        {
            g.setColour (palette_.text);
            g.drawEllipse (juce::Rectangle<float> (12.0f, 12.0f).withCentre (centre), 1.4f);
        }
    }
}

void MultiEnvelopeEditor::mouseDown (const juce::MouseEvent& event)
{
    const auto layout = layoutNow();
    const auto position = event.position;

    dragPoint_ = -1;
    dragSegment_ = -1;

    // A point first: the nearest within reach.
    float best = 12.0f * 12.0f;
    for (int i = 0; i < layout.points; ++i)
    {
        const auto centre = juce::Point<float> (layout.x[static_cast<std::size_t> (i + 1)],
                                                layout.y[static_cast<std::size_t> (i + 1)]);
        const float d2 = centre.getDistanceSquaredFrom (position);

        if (d2 < best)
        {
            best = d2;
            dragPoint_ = i;
        }
    }

    if (dragPoint_ >= 0)
        return;

    // Otherwise a segment, for its tension: nearest midpoint, the FL gesture.
    best = 18.0f * 18.0f;
    for (int i = 0; i < layout.points; ++i)
    {
        const auto mid = juce::Point<float> (
            0.5f * (layout.x[static_cast<std::size_t> (i)] + layout.x[static_cast<std::size_t> (i + 1)]),
            0.5f * (layout.y[static_cast<std::size_t> (i)] + layout.y[static_cast<std::size_t> (i + 1)]));

        const float d2 = mid.getDistanceSquaredFrom (position);

        if (d2 < best)
        {
            best = d2;
            dragSegment_ = i;
        }
    }

    if (dragSegment_ >= 0)
    {
        dragStartTension_ = plain ("C" + juce::String (dragSegment_ + 1));
        dragStartY_ = position.y;
    }
}

void MultiEnvelopeEditor::mouseDrag (const juce::MouseEvent& event)
{
    const auto area = plotArea();

    if (dragPoint_ >= 0)
    {
        const auto layout = layoutNow();
        const auto n = juce::String (dragPoint_ + 1);

        // y is the point's level, plainly.
        const float level = juce::jlimit (0.0f, 1.0f,
            (area.getBottom() - event.position.y) / area.getHeight());
        setPlain ("L" + n, level, true);

        // x is this segment's own time: the distance from the previous point,
        // in the current total's scale. The total shifts as the time does,
        // which reads as the graph breathing -- correct, if surprising once.
        const float previousX = layout.x[static_cast<std::size_t> (dragPoint_)];
        const double fraction = juce::jlimit (0.001f, 1.0f,
            (event.position.x - previousX) / area.getWidth());
        setPlain ("T" + n, static_cast<float> (fraction * layout.total), true);

        repaint();
        return;
    }

    if (dragSegment_ >= 0)
    {
        // Up bends towards the analogue curve, down towards its mirror.
        const float delta = (dragStartY_ - event.position.y) / 60.0f;
        setPlain ("C" + juce::String (dragSegment_ + 1),
                  juce::jlimit (-1.0f, 1.0f, dragStartTension_ + delta), true);
        repaint();
    }
}

void MultiEnvelopeEditor::mouseUp (const juce::MouseEvent&)
{
    dragPoint_ = -1;
    dragSegment_ = -1;
}

// ---------------------------------------------------------------------------
// EnvelopePage
// ---------------------------------------------------------------------------

EnvelopePage::EnvelopePage (juce::AudioProcessorValueTreeState& state, ui::Palette palette)
    : palette_ (palette)
{
    addBlock (state, "AMPLITUDE", "the voice's own level",
              { ids::ampAttack, ids::ampHold, ids::ampDecay, ids::ampSustain, ids::ampRelease,
                ids::ampAttackT, ids::ampDecayT, ids::ampReleaseT },
              ids::ampVelocity, "Velocity",
              "How much of the level comes from how hard the note was played.", ids::ampSnap);

    addBlock (state, "MOD ENVELOPE 1", "point it at something in MOD",
              { ids::env1Attack, ids::env1Hold, ids::env1Decay, ids::env1Sustain, ids::env1Release,
                ids::env1AttackT, ids::env1DecayT, ids::env1ReleaseT },
              nullptr, {}, {}, ids::env1Snap);

    addBlock (state, "MOD ENVELOPE 2", "and the mangle takes these too",
              { ids::env2Attack, ids::env2Hold, ids::env2Decay, ids::env2Sustain, ids::env2Release,
                ids::env2AttackT, ids::env2DecayT, ids::env2ReleaseT },
              nullptr, {}, {}, ids::env2Snap);

    for (int index = 0; index < 3; ++index)
        addAdvRow (state, index);
}

void EnvelopePage::addAdvRow (juce::AudioProcessorValueTreeState& state, int index)
{
    auto& row = advRows_[static_cast<std::size_t> (index)];

    row.index = index;
    row.heading = "ADV ENVELOPE " + juce::String (index + 1);

    row.graph = std::make_unique<MultiEnvelopeEditor> (state, index, palette_);
    addChildComponent (*row.graph);   // hidden until enabled

    const auto cell = [&] (std::unique_ptr<ParameterCell> made, bool visibleWhenEnabled)
    {
        if (visibleWhenEnabled)
            addChildComponent (*made);
        else
            addAndMakeVisible (*made);

        row.cells.push_back (std::move (made));
    };

    // The enable pill is the strip's whole content while disabled; everything
    // else appears when it lights.
    cell (std::make_unique<ToggleCell> (state, ids::adv (index, "Enable"), "Enable",
        "Switches this envelope on. Off costs nothing -- a disabled slot is never even "
        "ticked -- and the row stays folded. On, it appears as a source in both matrices "
        "as ADV " + juce::String (index + 1) + ": drag the points, ring is the sustain "
        "point, drag a segment's middle for its curve.",
        palette_), false);

    cell (std::make_unique<KnobCell> (state, ids::adv (index, "Points"), "Points",
        "How many points the envelope has, 2 to 8. The rest keep their values for when "
        "you lengthen it again.", palette_), true);

    cell (std::make_unique<KnobCell> (state, ids::adv (index, "Sustain"), "Sustain pt",
        "Which point the envelope parks at while the key is held -- the ringed one. The "
        "points after it are the release.", palette_), true);

    cell (std::make_unique<KnobCell> (state, ids::adv (index, "LoopStart"), "Loop from",
        "Where the loop returns to. Arriving at the sustain point travels back here -- "
        "the return leg uses this point's own time and curve -- then forward again.",
        palette_), true);

    cell (std::make_unique<ToggleCell> (state, ids::adv (index, "Loop"), "Loop",
        "Cycles between the loop point and the sustain point while the key is held. With "
        "Snap on, the loop is a tempo-locked rhythm.", palette_), true);

    cell (std::make_unique<ToggleCell> (state, ids::adv (index, "Snap"), "Snap",
        "Quantises every point's time to note lengths at the host tempo, exactly as the "
        "AHDSR Snap does.", palette_), true);
}

void EnvelopePage::addBlock (juce::AudioProcessorValueTreeState& state, const juce::String& heading,
                             const juce::String& detail, const EnvelopeEditor::Ids& ids_,
                             const char* extraId, const juce::String& extraName,
                             const juce::String& extraTooltip, const char* snapId)
{
    Block block;

    block.heading = heading;
    block.detail = detail;

    block.graph = std::make_unique<EnvelopeEditor> (state, palette_, ids_);

    // Named after its attack parameter, which is unique per envelope and needs
    // no second list to keep in step.
    block.graph->setComponentID ("graph-" + ids_.attack);
    addAndMakeVisible (*block.graph);

    const auto knob = [&] (const juce::String& id, const juce::String& name,
                           const juce::String& tooltip)
    {
        auto cell = std::make_unique<KnobCell> (state, id, name, tooltip, palette_);
        addAndMakeVisible (*cell);
        block.knobs.push_back (std::move (cell));
    };

    static const juce::String kTension =
        "How the segment bends. **Bipolar**: zero is a straight line, positive is the analogue "
        "shape -- fast at first and decelerating, which is what a capacitor does and what the ear "
        "expects -- and negative is that curve reflected, slow at first and accelerating.\n\n"
        "The old single Shape control could only do the positive half, because aiming a segment "
        "past its destination bends it one way and no value bends it the other. The mirror is a "
        "separate branch of the same arithmetic.\n\n"
        "It changes the shape and **not** the duration: the time constant is corrected for the "
        "tension, so this is a tone control rather than a second time control.";

    knob (ids_.attack, "Attack",
          "How long from nothing to full. Skewed so the short end has room: a tenth of the travel "
          "is 5 ms and half of it is 120 ms.");
    knob (ids_.attackTension, "A tension", kTension);
    knob (ids_.hold, "Hold",
          "How long the envelope sits at **full level** before the decay begins -- the H in "
          "AHDSR.\n\n"
          "It is what makes a plucked or gated sound possible without setting the sustain to 1 and "
          "shortening the note, which is not the same thing: the release would then start from "
          "wherever the key was let go rather than from the top. At 0 the stage is skipped "
          "entirely.");
    knob (ids_.decay, "Decay", "How long from full down to the sustain level.");
    knob (ids_.decayTension, "D tension", kTension);
    knob (ids_.sustain, "Sustain", "Where it holds while the key is down.");
    knob (ids_.release, "Release", "How long it takes to fall away after the key is up.");
    knob (ids_.releaseTension, "R tension", kTension);

    if (extraId != nullptr)
        knob (extraId, extraName, extraTooltip);

    // The tempo grid. A toggle rather than a knob, in the same cell grid.
    if (snapId != nullptr)
    {
        auto cell = std::make_unique<ToggleCell> (state, snapId, "Snap",
            "Quantises Attack, Hold, Decay and Release to note lengths at the host tempo -- "
            "nearest in musical distance, from 1/32 up to 8 bars. Times under half a 1/32 pass "
            "through untouched, so an instant attack stays instant. The knobs keep their "
            "positions; the sound follows the grid, live, when the tempo changes.",
            palette_);
        addAndMakeVisible (*cell);
        block.knobs.push_back (std::move (cell));
    }

    blocks_.push_back (std::move (block));
}

void EnvelopePage::refresh (const SonitusProcessor& processor)
{
    for (std::size_t i = 0; i < blocks_.size(); ++i)
        blocks_[i].graph->refresh (processor.getEnvelopeLevel (static_cast<int> (i)));

    bool heightChanged = false;

    for (auto& row : advRows_)
    {
        auto& state = const_cast<SonitusProcessor&> (processor).getState();
        const bool enabled = state
            .getRawParameterValue (ids::adv (row.index, "Enable"))->load() > 0.5f;

        if (enabled != row.shownEnabled)
        {
            row.shownEnabled = enabled;
            heightChanged = true;

            row.graph->setVisible (enabled);

            for (std::size_t i = 1; i < row.cells.size(); ++i)
                row.cells[i]->setVisible (enabled);
        }

        if (enabled)
            row.graph->repaint();   // external parameter changes redraw
    }

    if (heightChanged)
    {
        resized();

        if (onHeightChanged)
            onHeightChanged();
    }
}

int EnvelopePage::getPreferredHeight() const
{
    int height = 2 * kPagePad
               + static_cast<int> (blocks_.size()) * (kEnvBlockHeight + kGroupGap);

    for (const auto& row : advRows_)
        height += (row.shownEnabled ? kEnvBlockHeight : kAdvStripHeight) + kGroupGap;

    return height;
}

void EnvelopePage::paint (juce::Graphics& g)
{
    for (const auto& block : blocks_)
    {
        if (block.bounds.isEmpty())
            continue;

        paintGroupPanel (g, block.bounds);

        paintHeading (g, palette_, block.bounds.withHeight (kHeadingHeight),
                      block.heading, block.detail);
    }

    for (const auto& row : advRows_)
    {
        if (row.bounds.isEmpty())
            continue;

        paintGroupPanel (g, row.bounds);

        paintHeading (g, palette_, row.bounds.withHeight (kHeadingHeight),
                      row.heading,
                      row.shownEnabled ? "a source in both matrices"
                                       : "off -- costs nothing folded");
    }
}

void EnvelopePage::resized()
{
    auto bounds = getLocalBounds().reduced (6, kPagePad);

    // The blocks share whatever is going spare equally, so the page fills a
    // tall window rather than stacking against the top -- but only after the
    // ADV rows' claim is reserved, or the three AHDSRs eat the whole window
    // and push the rows off the bottom of it, which is exactly what the first
    // tall-window screenshot showed.
    int advTotal = 0;
    for (const auto& row : advRows_)
        advTotal += (row.shownEnabled ? kEnvBlockHeight : kAdvStripHeight) + kGroupGap;

    const int blocks = juce::jmax (1, static_cast<int> (blocks_.size()));
    const int height = juce::jmax (kEnvBlockHeight,
                                   (bounds.getHeight() - advTotal - blocks * kGroupGap) / blocks);

    for (auto& block : blocks_)
    {
        block.bounds = bounds.removeFromTop (height);
        bounds.removeFromTop (kGroupGap);

        auto inner = block.bounds.reduced (5, 3).withTrimmedTop (kHeadingHeight);

        const int columns = kEnvKnobColumns;
        const int rows = kEnvKnobRows;

        const int cellWidth = juce::jlimit (76, kMaxCellWidth, inner.getWidth() / (2 * columns));
        const int cellHeight = juce::jlimit (kMinCellHeight, kMaxCellHeight, inner.getHeight() / rows);

        auto knobArea = inner.removeFromRight (cellWidth * columns);

        for (std::size_t i = 0; i < block.knobs.size(); ++i)
        {
            const int column = static_cast<int> (i) % columns;
            const int row = static_cast<int> (i) / columns;

            block.knobs[i]->setBounds ({ knobArea.getX() + column * cellWidth,
                                         knobArea.getY() + row * cellHeight,
                                         cellWidth, cellHeight });
        }

        block.graph->setBounds (inner.withTrimmedRight (8).reduced (0, 1));
    }

    for (auto& row : advRows_)
    {
        const int height = row.shownEnabled ? kEnvBlockHeight : kAdvStripHeight;

        row.bounds = bounds.removeFromTop (height);
        bounds.removeFromTop (kGroupGap);

        auto inner = row.bounds.reduced (5, 3).withTrimmedTop (kHeadingHeight);

        if (! row.shownEnabled)
        {
            // The strip: the enable pill alone, left, compact.
            if (! row.cells.empty())
                row.cells[0]->setBounds (inner.removeFromLeft (150));
            continue;
        }

        // Enabled: enable + the five cells in two columns on the right, the
        // graph taking the rest.
        const int cellWidth = juce::jlimit (76, kMaxCellWidth, inner.getWidth() / 6);
        const int cellHeight = juce::jlimit (kMinCellHeight, kMaxCellHeight, inner.getHeight() / 3);

        auto cellArea = inner.removeFromRight (2 * cellWidth);

        for (std::size_t i = 0; i < row.cells.size(); ++i)
        {
            const int column = static_cast<int> (i) % 2;
            const int cellRow = static_cast<int> (i) / 2;

            row.cells[i]->setBounds ({ cellArea.getX() + column * cellWidth,
                                       cellArea.getY() + cellRow * cellHeight,
                                       cellWidth, cellHeight });
        }

        row.graph->setBounds (inner.withTrimmedRight (8).reduced (0, 1));
    }
}

// ---------------------------------------------------------------------------
// StepStrip
// ---------------------------------------------------------------------------

namespace
{
/// The row of step numbers along the bottom.
constexpr int kStepNumberHeight = 11;
} // namespace

StepStrip::StepStrip (juce::AudioProcessorValueTreeState& state, ui::Palette palette)
    : palette_ (palette)
{
    for (int step = 0; step < dsp::StepSequencer::kMaxSteps; ++step)
    {
        auto& slider = sliders_[static_cast<std::size_t> (step)];

        slider.setSliderStyle (juce::Slider::LinearBarVertical);
        slider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        slider.setColour (juce::Slider::trackColourId, palette_.accent.withAlpha (0.85f));
        slider.setColour (juce::Slider::backgroundColourId, palette_.panel.brighter (0.04f));
        slider.setTooltip ("Step " + juce::String (step + 1)
                             + ". Bipolar: the centre is no modulation, and either end is full "
                               "depth in that direction. Drag; double-click to centre.");

        if (auto* parameter = state.getParameter (ids::step (step)))
            slider.setDoubleClickReturnValue (
                true, parameter->convertFrom0to1 (parameter->getDefaultValue()));

        addAndMakeVisible (slider);

        attachments_[static_cast<std::size_t> (step)]
          = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
                state, ids::step (step), slider);
    }

    setTooltip ("The sixteen steps, drawn as a pattern rather than as sixteen knobs -- the shape "
                "is the thing being edited. Point a global slot at Comb time and this draws the "
                "comb sweep in time with the track, which is the thing an automation lane cannot "
                "do. The playing step is lit.");
}

void StepStrip::setPlaying (int step, int length)
{
    if (step == playing_ && length == length_)
        return;

    playing_ = step;
    length_ = length;

    repaint();
}

void StepStrip::paint (juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();

    g.setColour (palette_.panel);
    g.fillRoundedRectangle (bounds, 5.0f);

    paintHeading (g, palette_, getLocalBounds().withHeight (kHeadingHeight),
                  "PATTERN", "sixteen steps, bipolar; the centre is no modulation");

    bounds.removeFromTop (static_cast<float> (kHeadingHeight));

    auto numbers = bounds.removeFromBottom (static_cast<float> (kStepNumberHeight));
    bounds = bounds.reduced (3.0f, 2.0f);

    const float slotWidth = bounds.getWidth() / dsp::StepSequencer::kMaxSteps;

    g.setFont (juce::FontOptions (9.0f));

    for (int step = 0; step < dsp::StepSequencer::kMaxSteps; ++step)
    {
        const auto slot = bounds.withWidth (slotWidth)
                                .withX (bounds.getX() + slotWidth * static_cast<float> (step));

        // Steps past the pattern's length are dimmed rather than hidden: the
        // values are still there and shortening the pattern is reversible, so
        // hiding them would lose an edit the player expects to get back.
        if (step >= length_)
        {
            g.setColour (palette_.background.withAlpha (0.55f));
            g.fillRect (slot.reduced (1.0f, 0.0f));
        }

        if (step == playing_)
        {
            g.setColour (palette_.secondary.withAlpha (0.26f));
            g.fillRect (slot.reduced (1.0f, 0.0f));

            g.setColour (palette_.secondary);
            g.drawRect (slot.reduced (1.0f, 0.0f), 1.2f);
        }

        // Every fourth step numbered, and the beats brighter -- sixteen numbers
        // in a row is noise, four is a bar.
        const bool onBeat = step % 4 == 0;

        g.setColour (step == playing_ ? palette_.secondary
                                      : palette_.dimText.withAlpha (onBeat ? 0.75f : 0.30f));

        g.drawText (juce::String (step + 1),
                    numbers.withWidth (slotWidth)
                           .withX (bounds.getX() + slotWidth * static_cast<float> (step)),
                    juce::Justification::centred, false);
    }
}

void StepStrip::resized()
{
    auto bounds = getLocalBounds().withTrimmedTop (kHeadingHeight)
                                  .withTrimmedBottom (kStepNumberHeight)
                                  .reduced (3, 2);

    const int slotWidth = bounds.getWidth() / dsp::StepSequencer::kMaxSteps;

    for (int step = 0; step < dsp::StepSequencer::kMaxSteps; ++step)
        sliders_[static_cast<std::size_t> (step)].setBounds (
            juce::Rectangle<int> { bounds.getX() + slotWidth * step, bounds.getY(),
                                   slotWidth, bounds.getHeight() }.reduced (1, 0));
}

// ---------------------------------------------------------------------------
// TuningPage
// ---------------------------------------------------------------------------

void DegreeTable::setScale (const dsp::Scale& scale, double rootHz)
{
    rows_.clear();
    rows_.reserve (static_cast<std::size_t> (scale.size()) + 1);

    const auto formatHz = [] (double hz)
    {
        return juce::String (hz, hz < 1000.0 ? 2 : 1);
    };

    for (int degree = 0; degree < scale.size(); ++degree)
    {
        const double ratio = scale.ratios[static_cast<std::size_t> (degree)];
        const double next = degree + 1 < scale.size()
                              ? scale.ratios[static_cast<std::size_t> (degree + 1)]
                              : scale.repeat;

        Row row;
        row.degree = juce::String (degree);

        // A fraction only when the degree is exactly one -- a tempered degree
        // shows a dash here and speaks through its cents instead.
        const auto fraction = dsp::nearestFraction (ratio);

        row.ratio = fraction.found
                      ? juce::String (fraction.numerator) + "/" + juce::String (fraction.denominator)
                      : juce::String ("-");

        row.cents = juce::String (scale.cents (degree), 1);
        row.step = juce::String (1200.0 * std::log2 (next / ratio), 1);
        row.hz = rootHz > 0.0 ? formatHz (rootHz * ratio) : juce::String ("-");

        rows_.push_back (std::move (row));
    }

    // The repeat interval as its own row: 2/1 for the octave scales, 3/1 for
    // Bohlen-Pierce, and the golden section's 1.618 has no fraction at all.
    Row repeat;
    repeat.degree = "R";

    const auto fraction = dsp::nearestFraction (scale.repeat);

    repeat.ratio = fraction.found
                     ? juce::String (fraction.numerator) + "/" + juce::String (fraction.denominator)
                     : juce::String (scale.repeat, 5);

    repeat.cents = juce::String (scale.repeatCents(), 1);
    repeat.step = "";
    repeat.hz = rootHz > 0.0 ? formatHz (rootHz * scale.repeat) : juce::String ("-");
    repeat.isRepeat = true;

    rows_.push_back (std::move (repeat));

    setSize (juce::jmax (getWidth(), 1), preferredHeight());
    repaint();
}

void DegreeTable::paint (juce::Graphics& g)
{
    const auto mono = juce::FontOptions()
                        .withName (juce::Font::getDefaultMonospacedFontName())
                        .withHeight (11.0f);

    const int width = getWidth();

    // Five columns: degree, ratio, cents, step, and the sounding frequency.
    // The ratio column gets the most room because 177147/131072 is a real
    // resident; the Hz column is what moves when the A4 control does.
    const int degreeRight = 26;
    const int ratioRight = degreeRight + 100;
    const int centsRight = ratioRight + 58;
    const int stepRight = centsRight + 50;
    const int hzRight = juce::jmin (stepRight + 74, width - 4);

    g.setFont (juce::FontOptions (10.0f, juce::Font::bold));
    g.setColour (palette_.dimText);
    g.drawText ("#", 0, 0, degreeRight, kHeaderHeight, juce::Justification::centredRight);
    g.drawText ("RATIO", degreeRight, 0, ratioRight - degreeRight, kHeaderHeight,
                juce::Justification::centredRight);
    g.drawText ("CENTS", ratioRight, 0, centsRight - ratioRight, kHeaderHeight,
                juce::Justification::centredRight);
    g.drawText ("STEP", centsRight, 0, stepRight - centsRight, kHeaderHeight,
                juce::Justification::centredRight);
    g.drawText ("HZ", stepRight, 0, hzRight - stepRight, kHeaderHeight,
                juce::Justification::centredRight);

    g.setFont (mono);

    for (std::size_t index = 0; index < rows_.size(); ++index)
    {
        const auto& row = rows_[index];
        const int y = kHeaderHeight + static_cast<int> (index) * kRowHeight;

        if (index % 2 == 0)
        {
            g.setColour (palette_.panel.brighter (0.06f));
            g.fillRect (0, y, width, kRowHeight);
        }

        g.setColour (row.isRepeat ? palette_.accent : palette_.dimText);
        g.drawText (row.degree, 0, y, degreeRight, kRowHeight, juce::Justification::centredRight);

        g.setColour (row.isRepeat ? palette_.accent : palette_.text);
        g.drawText (row.ratio, degreeRight, y, ratioRight - degreeRight, kRowHeight,
                    juce::Justification::centredRight);
        g.drawText (row.cents, ratioRight, y, centsRight - ratioRight, kRowHeight,
                    juce::Justification::centredRight);

        g.setColour (palette_.dimText);
        g.drawText (row.step, centsRight, y, stepRight - centsRight, kRowHeight,
                    juce::Justification::centredRight);

        g.setColour (row.isRepeat ? palette_.accent : palette_.text);
        g.drawText (row.hz, stepRight, y, hzRight - stepRight, kRowHeight,
                    juce::Justification::centredRight);
    }
}

TuningPage::TuningPage (SonitusProcessor& processorToUse, ui::Palette palette)
    : sonitus_ (processorToUse), palette_ (palette), degreeTable_ (palette)
{
    headingLabel_.setText ("TUNING", juce::dontSendNotification);
    headingLabel_.setColour (juce::Label::textColourId, palette_.accent);
    headingLabel_.setFont (juce::FontOptions (11.0f, juce::Font::bold));
    addAndMakeVisible (headingLabel_);

    // The built-in scales, listed by **name**. The name is what gets stored,
    // not the index -- which is the more robust choice for a list that will
    // grow, and the reason this is a menu rather than a choice parameter.
    scaleBox_.setColour (juce::ComboBox::backgroundColourId, palette_.panel.brighter (0.15f));
    scaleBox_.setColour (juce::ComboBox::textColourId, palette_.text);
    scaleBox_.setColour (juce::ComboBox::outlineColourId, palette_.panel.brighter (0.3f));
    scaleBox_.setTextWhenNothingSelected ("Loaded from a file");
    scaleBox_.setTooltip (
        "The built-in scales, each generated from its own definition rather than shipped as "
        "somebody's data file. The strange ones are the point: Bohlen-Pierce repeats at 3/1 "
        "rather than the octave, and the Carlos scales do not repeat at an octave at all. "
        "With the comb key-tracked, a harmonic scale makes the instrument agree with itself -- "
        "intervals beat at the rate the comb is combing at, instead of against it.");

    {
        int id = 1;

        for (const auto& scale : dsp::scales::all())
            scaleBox_.addItem (juce::String (scale.name), id++);
    }

    scaleBox_.onChange = [this]
    {
        if (updating_ || scaleBox_.getSelectedId() <= 0)
            return;

        const auto reason = sonitus_.selectBuiltInScale (scaleBox_.getText());

        if (reason.isNotEmpty())
            reportFailure ("scale", reason);
        else
            errorLabel_.setText ({}, juce::dontSendNotification);

        refresh();
    };

    addAndMakeVisible (scaleBox_);

    loadScaleButton_.setTooltip (
        "Load a Scala .scl scale file. The parser refuses a file it cannot fully read and says "
        "which line stopped it -- a tuning that half-loads is worse than one that will not load, "
        "because it plays.");
    loadScaleButton_.onClick = [this] { loadScaleFile(); };
    addAndMakeVisible (loadScaleButton_);

    loadMapButton_.setTooltip (
        "Load a Scala .kbm keyboard map: which MIDI note is the reference, what frequency it is, "
        "and which keys the scale's degrees land on. Optional -- without one the scale starts at "
        "middle C and every key is used.");
    loadMapButton_.onClick = [this] { loadKeyboardMapFile(); };
    addAndMakeVisible (loadMapButton_);

    resetButton_.setTooltip ("Back to twelve-tone equal temperament, with no keyboard map.");
    resetButton_.onClick = [this]
    {
        sonitus_.resetTuning();
        errorLabel_.setText ({}, juce::dontSendNotification);
        refresh();
    };
    addAndMakeVisible (resetButton_);

    for (auto* button : { &loadScaleButton_, &loadMapButton_, &resetButton_ })
    {
        button->setColour (juce::TextButton::buttonColourId, palette_.panel.brighter (0.12f));
        button->setColour (juce::TextButton::textColourOffId, palette_.text);
    }

    descriptionLabel_.setColour (juce::Label::textColourId, palette_.text);
    descriptionLabel_.setFont (juce::FontOptions (13.0f));
    descriptionLabel_.setJustificationType (juce::Justification::topLeft);
    addAndMakeVisible (descriptionLabel_);

    explanationLabel_.setColour (juce::Label::textColourId, palette_.dimText);
    explanationLabel_.setFont (juce::FontOptions (11.0f));
    explanationLabel_.setJustificationType (juce::Justification::topLeft);
    explanationLabel_.setText (
        "Microtuning is built in because the comb key-tracks onto harmonics of the played note: "
        "a just interval locks against it where a tempered one churns. The scale travels with "
        "the project -- .scl text is saved into the plugin's state. Detune and glide stay in "
        "cents; they are a spread around a pitch, not a scale degree.",
        juce::dontSendNotification);
    addAndMakeVisible (explanationLabel_);

    errorLabel_.setColour (juce::Label::textColourId, palette_.over);
    errorLabel_.setFont (juce::FontOptions (12.0f));
    errorLabel_.setJustificationType (juce::Justification::topLeft);
    addAndMakeVisible (errorLabel_);

    // What the selected scale is: theorem, story, and every degree.
    constructionLabel_.setColour (juce::Label::textColourId, palette_.accent);
    constructionLabel_.setFont (juce::FontOptions (12.0f, juce::Font::bold));
    constructionLabel_.setJustificationType (juce::Justification::topLeft);
    addAndMakeVisible (constructionLabel_);

    storyLabel_.setColour (juce::Label::textColourId, palette_.text);
    storyLabel_.setFont (juce::FontOptions (12.0f));
    storyLabel_.setJustificationType (juce::Justification::topLeft);
    addAndMakeVisible (storyLabel_);

    tableViewport_.setViewedComponent (&degreeTable_, false);
    tableViewport_.setScrollBarsShown (true, false);
    tableViewport_.setScrollBarThickness (14);
    addAndMakeVisible (tableViewport_);

    // The pitch standard: the tradition's own tuning practice, bold, with a
    // button when it names a number the A4 control can be set to.
    pitchLoreLabel_.setColour (juce::Label::textColourId, palette_.text);
    pitchLoreLabel_.setFont (juce::FontOptions (12.0f, juce::Font::bold));
    pitchLoreLabel_.setJustificationType (juce::Justification::topLeft);
    addAndMakeVisible (pitchLoreLabel_);

    applyPitchButton_.setColour (juce::TextButton::buttonColourId, palette_.panel.brighter (0.12f));
    applyPitchButton_.setColour (juce::TextButton::textColourOffId, palette_.accent);
    applyPitchButton_.setTooltip (
        "Sets the A4 control to the pitch standard this scale's tradition names -- A415 for "
        "the baroque temperaments, ISO A440 for 12-TET. Scales whose tradition left no "
        "number (Babylon, Greece, Persia...) have no button, because inventing one would "
        "be a lie.");
    applyPitchButton_.onClick = [this]
    {
        const double suggested = sonitus_.getScale().suggestedConcertHz;

        if (suggested > 0.0)
        {
            sonitus_.setConcertPitch (suggested);
            refresh();
        }
    };
    applyPitchButton_.setComponentID ("apply-pitch");
    addAndMakeVisible (applyPitchButton_);

    // A4: the whole tuning scaled by one ratio against 440. The table's Hz
    // column follows the drag live.
    concertLabel_.setText ("A4", juce::dontSendNotification);
    concertLabel_.setColour (juce::Label::textColourId, palette_.dimText);
    concertLabel_.setFont (juce::FontOptions (11.0f, juce::Font::bold));
    concertLabel_.setJustificationType (juce::Justification::centredRight);
    addAndMakeVisible (concertLabel_);

    concertSlider_.setSliderStyle (juce::Slider::LinearBar);
    concertSlider_.setRange (dsp::Tuning::kMinimumConcertHz, dsp::Tuning::kMaximumConcertHz, 0.1);
    concertSlider_.setValue (440.0, juce::dontSendNotification);
    concertSlider_.setDoubleClickReturnValue (true, 440.0);
    concertSlider_.setTextValueSuffix (" Hz");
    concertSlider_.setNumDecimalPlacesToDisplay (1);
    concertSlider_.setColour (juce::Slider::trackColourId, palette_.panel.brighter (0.18f));
    concertSlider_.setColour (juce::Slider::textBoxTextColourId, palette_.text);
    concertSlider_.setTooltip (
        "The pitch standard, as what A440 is moved to: the whole tuning -- keyboard map "
        "reference included -- scales by this against 440, so it means something even in a "
        "scale with no A in it. A440 has only been the standard since 1939 (ISO 16, 1955); "
        "the 19th-century French diapason normal was 435, scientific pitch C-256 gives "
        "430.5, and 432 is a modern preference with no historical orchestra behind it -- "
        "all one drag away. Double-click returns to 440. Saved with the project, and "
        "presets do not touch it.");
    concertSlider_.onValueChange = [this]
    {
        if (updating_)
            return;

        sonitus_.setConcertPitch (concertSlider_.getValue());
        refresh();
    };
    concertSlider_.setComponentID ("concert-pitch");
    addAndMakeVisible (concertSlider_);

    refresh();
}

void TuningPage::refresh()
{
    descriptionLabel_.setText (sonitus_.describeTuning(), juce::dontSendNotification);

    // The info panel follows whatever is actually loaded -- built-in or file.
    // A built-in carries its construction and story; a file-loaded scale has
    // neither, so the panel says where it came from and lets the computed
    // table speak for the numbers.
    const auto& scale = sonitus_.getScale();

    degreeTable_.setScale (scale, sonitus_.getRootHz());

    constructionLabel_.setText (
        scale.construction.empty()
            ? juce::String ("Loaded from a Scala file: the degrees are the file's own.")
            : juce::String (scale.construction),
        juce::dontSendNotification);

    storyLabel_.setText (
        scale.story.empty()
            ? juce::String ("The table shows every degree as the instrument will play it -- "
                            "an exact fraction where the file gave a ratio, cents where it "
                            "gave cents.")
            : juce::String (scale.story),
        juce::dontSendNotification);

    // The pitch standard, bold -- and honestly generic when the scale is an
    // interval system with no frequency of its own.
    pitchLoreLabel_.setText (
        scale.pitchStandard.empty()
            ? juce::String ("No inherent pitch standard: this scale fixes intervals, not "
                            "frequencies. A440 is the modern default; the A4 control moves "
                            "the whole tuning together.")
            : juce::String (scale.pitchStandard),
        juce::dontSendNotification);

    // The Apply button exists only when the tradition names a number.
    const double suggested = scale.suggestedConcertHz;

    applyPitchButton_.setVisible (suggested > 0.0);

    if (suggested > 0.0)
        applyPitchButton_.setButtonText ("Apply A" + juce::String (suggested, 0));

    {
        const juce::ScopedValueSetter<bool> sliderGuard (updating_, true);
        concertSlider_.setValue (sonitus_.getConcertPitch(), juce::dontSendNotification);
    }

    const auto name = sonitus_.getScaleName();

    // The box follows the processor rather than the other way round, so a state
    // load from the host shows the scale that is actually playing. Guarded, or
    // restoring the selection would fire `onChange` and re-select it.
    const juce::ScopedValueSetter<bool> guard (updating_, true);

    for (int item = 0; item < scaleBox_.getNumItems(); ++item)
        if (scaleBox_.getItemText (item) == name)
        {
            scaleBox_.setSelectedItemIndex (item, juce::dontSendNotification);
            return;
        }

    // A scale loaded from a file is not in the list, and showing the last
    // built-in that happened to be selected would be a lie about what is
    // playing.
    scaleBox_.setSelectedId (0, juce::dontSendNotification);
}

void TuningPage::reportFailure (const juce::String& what, const juce::String& reason)
{
    errorLabel_.setText ("Could not load that " + what + ". " + reason
                           + "\nThe tuning is unchanged.",
                         juce::dontSendNotification);
}

void TuningPage::loadScaleFile()
{
    chooser_ = std::make_unique<juce::FileChooser> ("Load a Scala scale", juce::File {}, "*.scl");

    chooser_->launchAsync (juce::FileBrowserComponent::openMode
                             | juce::FileBrowserComponent::canSelectFiles,
                           [this] (const juce::FileChooser& chooser)
    {
        const auto file = chooser.getResult();

        if (! file.existsAsFile())
            return;

        const auto reason = sonitus_.loadScalaText (file.loadFileAsString(),
                                                    file.getFileNameWithoutExtension());

        if (reason.isNotEmpty())
            reportFailure ("scale", reason);
        else
            errorLabel_.setText ({}, juce::dontSendNotification);

        refresh();
    });
}

void TuningPage::loadKeyboardMapFile()
{
    chooser_ = std::make_unique<juce::FileChooser> ("Load a Scala keyboard map",
                                                    juce::File {}, "*.kbm");

    chooser_->launchAsync (juce::FileBrowserComponent::openMode
                             | juce::FileBrowserComponent::canSelectFiles,
                           [this] (const juce::FileChooser& chooser)
    {
        const auto file = chooser.getResult();

        if (! file.existsAsFile())
            return;

        const auto reason = sonitus_.loadKeyboardMapText (file.loadFileAsString());

        if (reason.isNotEmpty())
            reportFailure ("keyboard map", reason);
        else
            errorLabel_.setText ({}, juce::dontSendNotification);

        refresh();
    });
}

void TuningPage::paint (juce::Graphics& g)
{
    g.setColour (palette_.panel);
    g.fillRoundedRectangle (getLocalBounds().toFloat(), 6.0f);
}

void TuningPage::resized()
{
    auto bounds = getLocalBounds().reduced (12, 8);

    headingLabel_.setBounds (bounds.removeFromTop (20));

    auto row = bounds.removeFromTop (30);

    scaleBox_.setBounds (row.removeFromLeft (juce::jmin (280, row.getWidth() / 2)).reduced (0, 2));
    row.removeFromLeft (10);
    loadScaleButton_.setBounds (row.removeFromLeft (110).reduced (0, 2));
    row.removeFromLeft (6);
    loadMapButton_.setBounds (row.removeFromLeft (110).reduced (0, 2));
    row.removeFromLeft (6);
    resetButton_.setBounds (row.removeFromLeft (90).reduced (0, 2));

    // The pitch standard control lives on the same row: A4, then the value.
    row.removeFromLeft (14);
    concertLabel_.setBounds (row.removeFromLeft (24));
    row.removeFromLeft (4);
    concertSlider_.setBounds (row.removeFromLeft (juce::jmin (130, row.getWidth())).reduced (0, 3));

    bounds.removeFromTop (8);
    descriptionLabel_.setBounds (bounds.removeFromTop (18));

    bounds.removeFromTop (4);
    errorLabel_.setBounds (bounds.removeFromTop (18));

    bounds.removeFromTop (6);

    // The info panel: the degree table on the left; the theorem, the pitch
    // standard (with its Apply button when the tradition names a number) and
    // the story on the right. The table scrolls -- Partch has 43 rows and
    // 53-TET has 53 -- and the prose wraps in the room that remains.
    auto info = bounds;
    auto tableArea = info.removeFromLeft (juce::jmin (350, info.getWidth() * 2 / 5));

    tableViewport_.setBounds (tableArea);
    degreeTable_.setSize (tableArea.getWidth() - tableViewport_.getScrollBarThickness(),
                          degreeTable_.preferredHeight());

    info.removeFromLeft (12);

    constructionLabel_.setBounds (info.removeFromTop (44));
    info.removeFromTop (4);

    pitchLoreLabel_.setBounds (info.removeFromTop (46));

    auto applyRow = info.removeFromTop (22);
    applyPitchButton_.setBounds (applyRow.removeFromLeft (110).reduced (0, 1));
    info.removeFromTop (4);

    const int explanation = 46;
    storyLabel_.setBounds (info.removeFromTop (
        juce::jmax (40, info.getHeight() - explanation - 6)));

    info.removeFromTop (6);
    explanationLabel_.setBounds (info);
}


// ---------------------------------------------------------------------------
// SonitusEditor
// ---------------------------------------------------------------------------

SonitusEditor::SonitusEditor (SonitusProcessor& processorToUse)
    : juce::AudioProcessorEditor (&processorToUse),
      sonitus_ (processorToUse),
      palette_ (kPalette),
      lookAndFeel_ (kPalette),
      outputMeter_ (std::make_unique<ui::LevelMeter> (kPalette))
{
    // Set before anything is constructed under it, so every child inherits it
    // rather than half of them being built against the default.
    setLookAndFeel (&lookAndFeel_);

    // No bypass parameter: an instrument that is bypassed is an instrument that
    // is silent, which is what muting the track already does. The header takes
    // a null id and simply leaves the button out.
    header_ = std::make_unique<ui::HeaderBar> (
        sonitus_.getState(), "SONITUS",
        "Growl and reese instrument", nullptr, palette_);

    header_->onSwapRequested = [this]
    {
        sonitus_.getAbCompare().swapSlots();
        header_->setActiveSlot (sonitus_.getAbCompare().isSlotB());
        header_->setOtherSlotFilled (sonitus_.getAbCompare().otherSlotFilled());
    };

    header_->onCopyRequested = [this]
    {
        sonitus_.getAbCompare().copyToOtherSlot();
        header_->setOtherSlotFilled (sonitus_.getAbCompare().otherSlotFilled());
    };

    header_->onTooltipsToggled = [this] (bool enabled)
    {
        sonitus_.setTooltipsEnabled (enabled);
        tooltips_.setEnabled (enabled);
    };

    header_->setActiveSlot (sonitus_.getAbCompare().isSlotB());
    header_->setOtherSlotFilled (sonitus_.getAbCompare().otherSlotFilled());
    header_->setTooltipsEnabled (sonitus_.getTooltipsEnabled());
    header_->attachSuiteControls (sonitus_.getState(), nullptr, ids::output, ids::oversampling);
    addAndMakeVisible (*header_);

    tooltips_.setEnabled (sonitus_.getTooltipsEnabled());

    buildPages();

    viewport_.setComponentID ("pages");
    viewport_.setScrollBarsShown (true, false);
    // 14 px, up from 9: the user could not tell the pages scrolled at all.
    // The width pairs with the rail-and-thumb drawing in KnobLookAndFeel.
    viewport_.setScrollBarThickness (14);
    addAndMakeVisible (viewport_);

    // The MOD page's palette, because that is the only page it appears on --
    // it is a child of the editor rather than of the page, so it does not
    // inherit the accent the way the controls above it do.
    steps_ = std::make_unique<StepStrip> (sonitus_.getState(), paletteForPage (kModPage));

    // **Added as a child, which it was not.** `setVisible` on a component with
    // no parent does nothing at all -- it does not throw, it does not warn, and
    // the component simply never paints. The MOD page showed a black gap where
    // the sequencer should be and the TUNING tab was blank, and both were this
    // one missing line. Nothing headless could have caught it: the editor is the
    // one part of this plugin no test can run. The tuning page is no longer in
    // this position to be forgotten -- it is a `Page` and the viewport owns its
    // visibility now.
    steps_->setComponentID ("steps");

    // After buildPages, which is what creates them.
    steps_->setLookAndFeel (pageLookAndFeels_[kModPage].get());

    addAndMakeVisible (*steps_);

    static const char* tabNames[kNumPages] { "OSC", "FILTER", "ENV", "MOD", "MANGLE", "TUNING" };

    for (int i = 0; i < kNumPages; ++i)
    {
        auto& tab = tabs_[static_cast<std::size_t> (i)];

        tab.setButtonText (tabNames[i]);
        tab.setClickingTogglesState (false);
        tab.onClick = [this, i] { showPage (i); };

        // Named so `tezla-render editor` can drive the panel: `click:tab-mod`
        // changes page, `hit:tab-mod` asks whether a click there would actually
        // reach it. Both matter here -- this editor has already shipped a page
        // that existed, was told to be visible, and was not on screen.
        tab.setComponentID ("tab-" + juce::String (tabNames[i]).toLowerCase());

        addAndMakeVisible (tab);
    }

    // An instrument's output has no ceiling parameter to measure against, so
    // the reference is 0 dBFS and "over" means over full scale.
    outputMeter_->setComponentID ("meter");
    outputMeter_->setReferenceDb (0.0f);
    outputMeter_->setScaleVisible (true);
    outputMeter_->setTooltip (
        "Output level. The number is the worst peak since it was last cleared -- click to clear. "
        "The bar is VU-ballistic and the number is peak, and on a reese those differ by ten "
        "decibels or more: the bar says how loud it sounds and the number says whether it is "
        "clipping the converter.");
    addAndMakeVisible (*outputMeter_);

    outputMeterLabel_.setJustificationType (juce::Justification::centred);
    outputMeterLabel_.setColour (juce::Label::textColourId, palette_.panel.darker (0.3f));
    outputMeterLabel_.setFont (juce::FontOptions (9.0f, juce::Font::bold));
    addAndMakeVisible (outputMeterLabel_);

    noteLabel_.setJustificationType (juce::Justification::centred);
    noteLabel_.setColour (juce::Label::textColourId, palette_.panel.darker (0.3f));
    noteLabel_.setFont (juce::FontOptions (11.5f));
    addAndMakeVisible (noteLabel_);

    statusLabel_.setJustificationType (juce::Justification::centred);
    // Dark, because this line sits on the bright chassis rather than on a
    // plate -- the dim grey that reads on a dark plate vanishes on silver.
    statusLabel_.setColour (juce::Label::textColourId, palette_.panel.darker (0.3f));
    statusLabel_.setFont (juce::FontOptions (11.0f));
    addAndMakeVisible (statusLabel_);

    showPage (0);

    setResizable (true, true);
    setResizeLimits (kMinWidth, kMinHeight, kMaxWidth, kMaxHeight);
    setSize (1000, 660);

    startTimerHz (30);
}

SonitusEditor::~SonitusEditor()
{
    // Before any child is destroyed, and before the look and feel is. A
    // component holding a dangling pointer to one is a use-after-free with no
    // symptom until the host next repaints -- and the pages are held by this
    // class rather than by the viewport, so they outlive it unless told
    // otherwise here.
    for (auto& page : pages_)
        if (page != nullptr)
            page->setLookAndFeel (nullptr);

    if (steps_ != nullptr)
        steps_->setLookAndFeel (nullptr);

    setLookAndFeel (nullptr);
}

ControlPage* SonitusEditor::controlPage (int index) const
{
    if (index < 0 || index >= kNumPages)
        return nullptr;

    return dynamic_cast<ControlPage*> (pages_[static_cast<std::size_t> (index)].get());
}
void SonitusEditor::buildPages()
{
    auto& state = sonitus_.getState();

    // ---- OSC -----------------------------------------------------------------

    auto osc = std::make_unique<ControlPage> (state, paletteForPage (kOscPage));

    const auto addOscillator = [] (ControlPage& page, const char* shapeId, const char* octaveId,
                                   const char* semitoneId, const char* centsId, const char* widthId,
                                   const char* morphId,
                                   const char* levelId, const char* unisonId, const char* detuneId,
                                   const char* spreadId, const char* driftId, const juce::String& which)
    {
        page.addChoice (shapeId, "Shape",
            "Saw is the dense one and where a reese starts -- every harmonic present, which is "
            "what gives a comb something to cut. Pulse is hollow and its Width knob sweeps which "
            "harmonics survive. Triangle is soft, and its Width is a *skew* -- push it off centre "
            "and it leans towards a saw. Sine has nothing above the fundamental and is for sub "
            "and for driving PM.\n\n"
            "The rest read the Morph slider, and 0 is always the classic form. Vintage is an "
            "analogue saw core -- an RC curve instead of a straight ramp, Morph deepening the "
            "sag. Dome is a pressed sine with **zero aliasing by construction**, Morph pressing "
            "it from a sine towards a rounded pulse. Double saw is two ramps, Morph sliding the "
            "second -- a one-oscillator flanger, and a great Morph target for an LFO. Harmonic "
            "is sixteen partials with Morph setting the roll-off, bright to dark. Noise is "
            "noise: pitch, sync and PM do nothing to it, unison spread makes it wide, and Morph "
            "darkens it.");

        // The picture, live: one cycle of exactly what the DSP reads, so
        // shape, Width and Morph changes redraw it as they land.
        page.addWave (shapeId, widthId, morphId);

        page.addKnob (octaveId, "Octave", "Whole octaves, -3 to +3.");
        page.addKnob (semitoneId, "Semis", "Semitones, -24 to +24. Snapped, so an interval stays an interval.");
        page.addKnob (centsId, "Fine",
            "Cents. Two oscillators a few cents apart beat, and the beating *is* a comb whose "
            "notches sweep at the difference frequency -- which is the whole reese, before the "
            "flanger is even switched on.");

        page.addKnob (widthId, "Width",
            "Pulse width, and the triangle's skew. At 50% a pulse is a square and has only odd "
            "harmonics; away from it the even ones come in. Modulate it for the classic PWM "
            "shimmer -- it is in the voice matrix as Width " + which + ".");

        page.addMorph (morphId, "Morph",
            "The shape's own tweak -- what it does depends on the shape, and 0 is always the "
            "classic form. Vintage: how hard the ramp sags. Dome: how hard the sine is pressed "
            "(more harmonics, still zero aliasing). Double saw: the second ramp's offset -- "
            "sweep it for a one-oscillator flanger. Harmonic: the roll-off, bright to dark. "
            "Noise: the colour, white to dark. The four classic shapes ignore it, and it greys "
            "out to say so. In the matrix as Morph " + which + ".");

        page.addKnob (levelId, "Level", "How much of this oscillator reaches the mix.");

        page.addKnob (unisonId, "Unison",
            "How many copies, 1 to 7. Seven each on both oscillators with eight voices down is "
            "112 oscillators, which is the CPU number to keep an eye on: three is usually thicker "
            "than it sounds and costs less than half.");

        page.addKnob (detuneId, "Detune",
            "How far the copies spread, in cents. This is the comb that costs nothing -- the "
            "notches move at the beat rate and there is no delay line involved.");

        page.addKnob (spreadId, "Spread",
            "How far the copies spread across the stereo field. The centre copy stays centred, "
            "so the mono sum keeps its fundamental.");

        page.addKnob (driftId, "Drift",
            "Slow random wander on each copy's pitch, in cents. What an analogue oscillator bank "
            "does because its components are warm and imperfect. A little is life; a lot is a "
            "broken machine, which is occasionally what you want.");
    };

    osc->addHeading ("OSCILLATOR A -- the sync master", 6);
    addOscillator (*osc, ids::shapeA, ids::octaveA, ids::semitonesA, ids::centsA, ids::widthA,
                   ids::morphA, ids::levelA, ids::unisonA, ids::detuneA, ids::spreadA,
                   ids::driftA, "A");

    osc->addHeading ("OSCILLATOR B -- the sync slave and the PM target", 6);
    addOscillator (*osc, ids::shapeB, ids::octaveB, ids::semitonesB, ids::centsB, ids::widthB,
                   ids::morphB, ids::levelB, ids::unisonB, ids::detuneB, ids::spreadB,
                   ids::driftB, "B");

    osc->addHeading ("SUB, RING AND FOLD", 5);

    osc->addChoice (ids::subShape, "Sub shape",
        "Sine is pure weight and disappears on a laptop; square has odd harmonics that carry it "
        "through a small speaker.");

    osc->addKnob (ids::subOctave, "Sub oct",
        "Which octave the sub sits in, from two below the note to two above. Zero doubles the "
        "note rather than underpinning it, which is a thickener rather than a sub -- and above "
        "the note it stops being a sub at all and becomes a fixed-interval second voice that no "
        "amount of filtering can detune.");
    osc->addKnob (ids::subLevel, "Sub",
        "The sub oscillator's level. It is generated in the voice and then taken *out* of the "
        "mangle by the split, so nothing downstream can smear it.");

    osc->addKnob (ids::ringAmount, "Ring",
        "Ring modulation: A times B, which produces the sum and difference of every pair of "
        "their harmonics and almost nothing at either original pitch. Inharmonic on purpose -- "
        "put B a fifth up for metal, an octave up for something still tuned.");

    osc->addKnob (ids::foldAmount, "Fold",
        "A sine wave folder. Past full scale the transfer curve turns round and comes back, so "
        "the harder you push the more harmonics appear -- the opposite of a clipper, which runs "
        "out. Antialiased, and at full fold it is the widest-band thing in the instrument: this "
        "is the one control that genuinely wants x8 oversampling.");

    osc->addHeading ("SYNC AND PM", 2, true);

    osc->addToggle (ids::syncB, "Sync B",
        "Hard sync: B's phase is reset every time the played note's period comes round, so B's "
        "own pitch stops being a pitch and becomes a formant -- a peak in the spectrum that "
        "sweeps when you sweep B. This is the Pro-53 sound, and it is worth nothing standing "
        "still: put an envelope on Pitch B in the matrix and sweep it.");

    osc->addKnob (ids::pmIndex, "PM",
        "Phase modulation of B by A. Frequency modulation's better-behaved sibling -- the same "
        "sidebands with no DC drift, which is why every FM synth since the DX7 has actually been "
        "a PM synth. At small amounts it thickens; past about 2 it is a different instrument.");

    osc->addHeading ("KARGYRAA -- period doubling", 3, true);

    osc->addKnob (ids::kargyraa, "Kargyraa",
        "**Period doubling, the way a Tuvan throat singer gets one.** The false vocal folds sit "
        "above the true ones and vibrate at exactly half their rate, so every second glottal pulse "
        "is the damped one and the voice gains a real subharmonic -- while the pitch being sung, "
        "and the formants shaping it, stay where they were.\n\n"
        "This is not an octave divider and not the Sub knob. A sub adds a separate tone an octave "
        "down; this damps alternate cycles of the waveform that is already there, so what appears "
        "is the half-integer series -- f/2, 3f/2, 5f/2 -- around every harmonic. The same voice "
        "with a doubled period, which is why it growls rather than sounding like two notes.\n\n"
        "Locked to oscillator A's own cycle counter, so it cannot drift against the note however "
        "long you hold it, and a glide takes it along. It gets quieter as you turn it up, because "
        "the effect is a periodic absence -- that is the sound, not a fault. At 0 it is bit-exactly "
        "out of the path.");

    osc->addKnob (ids::kargyraaRasp, "Rasp",
        "How sharp the damped part of the cycle is. Low is a smooth subharmonic with little more "
        "than f/2; high is a narrow rasp with much more of the series present and a harder edge.\n\n"
        "The shape is a raised cosine taken to a power, which expands into a **finite** Fourier "
        "series -- so the modulator is band-limited by construction and this knob names its "
        "bandwidth rather than trading it for aliasing. Measured at the top of the range: nothing "
        "at all above where the maths says it stops, against -24 dB of hash from the obvious "
        "implementation, a hard gate on the alternate cycle.");

    osc->addChoice (ids::kargyraaDivisor, "Divisor",
        "How many cycles one modulator cycle spans. **/2 is kargyraa** -- it is what the throat "
        "does. /3 and /4 are not anything anatomical; the machinery is the same and a third-order "
        "subdivision is a sound this instrument should be able to make.");

    pages_[kOscPage] = std::move (osc);

    // ---- FILTER --------------------------------------------------------------

    auto filter = std::make_unique<ControlPage> (state, paletteForPage (kFilterPage));

    filter->addHeading ("FILTER -- zero-delay state variable, drive inside the loop", 4);

    filter->addChoice (ids::filterMode, "Mode",
        "Lowpass is the reese's shape. Bandpass throws the fundamental away and leaves the growl. "
        "Highpass and notch are there because the filter is a state-variable and they cost "
        "nothing.");

    filter->addKnob (ids::cutoff, "Cutoff",
        "Where the filter sits. The corner lands where theory puts it at every sample rate -- "
        "the prewarp guarantees that much. What it does not guarantee is the whole curve: a "
        "discrete filter's response is symmetric about Nyquist and Nyquist moves, so two octaves "
        "above a 4 kHz corner the 48 kHz and 192 kHz curves differ by 10.6 dB. Inside the "
        "oversampled section, which this is, that difference is pushed out of the audible band.");

    filter->addKnob (ids::resonance, "Resonance",
        "Q, on a geometric law: 0.5 at nothing and 500 at full, which is 15 dB of peak per quarter "
        "turn all the way up. A linear law would put Q at 1.0 halfway and cram twenty decibels "
        "into the last one percent of the travel.");

    filter->addKnob (ids::filterDrive, "Drive",
        "Overdrives the filter's own integrators, which is what a ladder does when you push it. "
        "The rail is fixed rather than falling with drive, so this adds harmonics instead of "
        "acting as a volume control -- with a matching trim behind it, the level barely moves.");

    filter->addKnob (ids::filterTrack, "Key track",
        "How far the cutoff follows the played note. At 100% a note two octaves up gets a cutoff "
        "two octaves up, so the timbre is constant across the keyboard. At 0 the filter is a fixed "
        "formant and the low notes are darker, which is usually what a bass wants.");

    filter->addKnob (ids::filterFm,  "FM",
        "Oscillator A modulating the cutoff at audio rate. Not a wobble: at these speeds the "
        "modulation makes sidebands of its own and the filter becomes part of the oscillator. "
        "Small amounts add a metallic edge, large amounts are chaos.");

    filter->addKnob (ids::filterVel, "Velocity",
        "How far velocity opens the filter. The standard expressive link, and the reason a "
        "programmed bassline can breathe.");

    filter->addHeading ("KEYBOARD", 4, true);

    filter->addChoice (ids::keyMode, "Mode",
        "Poly is many notes. Mono retriggers the envelopes on every note; Legato does not, so a "
        "phrase played without gaps glides through one envelope -- which is what makes a bassline "
        "sound played rather than typed. A reese is one voice: mono costs a fourteenth of poly.");

    filter->addKnob (ids::polyphony, "Voices",
        "How many notes at once, up to 32, and this is the control that decides the CPU bill: "
        "a sounding voice costs about 4.5% of one core, so sixteen ringing at once is roughly "
        "three quarters of it. The slots you do not use are free -- an idle voice returns "
        "immediately -- so set this for the widest chord or the longest overlapping release "
        "you actually play. Stealing takes a free voice first, then the quietest released one, "
        "then the oldest held one, so a held chord survives a passing melody.");

    filter->addKnob (ids::glide, "Glide",
        "How long a slide from one note to the next takes. In Legato it only happens between "
        "overlapping notes, which is how a glide becomes a performance control.");

    filter->addKnob (ids::bendRange, "Bend",
        "How far the pitch wheel reaches, in semitones.");

    pages_[kFilterPage] = std::move (filter);

    // ---- ENV -----------------------------------------------------------------

    // Its own page rather than a grid of fifteen knobs: an envelope's shape is
    // the thing being edited and five numbers do not show it. See EnvelopePage.
    {
        auto envPage = std::make_unique<EnvelopePage> (state, paletteForPage (kEnvPage));

        // An enable flip changes the page's preferred height; the editor's
        // resized() is what re-sizes the viewport's content.
        envPage->onHeightChanged = [this] { resized(); };

        pages_[kEnvPage] = std::move (envPage);
    }

    // ---- MOD -----------------------------------------------------------------

    auto mod = std::make_unique<ControlPage> (state, paletteForPage (kModPage));

    mod->addHeading ("LFO 1 -- the one the sequencer can drive the rate of", 8);

    mod->addChoice (ids::lfo1Wave, "Wave", "Its shape. Sample & hold steps; smooth random glides.");
    mod->addKnob (ids::lfo1Rate, "Rate",
        "How fast, in hertz. **Zero is a legitimate setting and is the brief's original trick** "
        "-- the rate pinned at nothing so the depth is drawn from somewhere else entirely. Here "
        "that somewhere else is the sequencer below, or the host's automation on the depth.");
    mod->addToggle (ids::lfo1Sync, "Sync",
        "Locks the rate to the host tempo: the Rate knob stands aside and the division beside this picks the speed. With Retrig off and the transport running, the *phase* locks to the song position too -- rewind and the same bar is the same wobble, which is the whole reason to sync. With Retrig on, the phase belongs to the note and only the rate is synced.");
    mod->addChoice (ids::lfo1Div, "Division",
        "The note length one LFO cycle spans when Sync is on. Key track and Seq-to-rate still multiply on top in retrigger mode; phase-locked (Sync on, Retrig off, transport running) the position comes straight from the bar and they stand aside.");
    mod->addKnob (ids::lfo1Smooth, "Smooth",
        "Rounds the corners off a square or a sample-and-hold, so a step becomes a slide.");
    mod->addToggle (ids::lfo1Retrig, "Retrig",
        "Restarts the LFO from the top of its cycle every time a note is pressed. Free-running is right for a wobble that should keep its place across a phrase; retriggered is right for anything that has to line up with the note -- a sweep, a stab, a phase that has to start in the same place every time. On a reese the difference is the whole sound: with this on, every note gets the same phase relationship and the growl is repeatable.");
    mod->addKnob (ids::lfo1Att, "Attack",
        "Fades the LFO's **depth** in from nothing over this long, restarting on every note. Delayed vibrato is the classic use -- the note arrives steady and the movement creeps in after it -- and on a filter it is a sweep that opens up rather than one that is already going.\n\nIt restarts whether or not Retrig is on: the two are separate ideas, one about the waveform's phase and the other about its depth. Eased rather than ramped, so it arrives without the corner a straight fade leaves at the top. At 0 there is no fade.");
    mod->addKnob (ids::lfo1Key, "Key track",
        "How far the LFO's rate follows the played note, referenced to middle C. At 100% an octave up doubles the rate, so the modulation stays in the same relationship to the pitch all the way up the keyboard -- which is how you get a phase or a wobble that reads as part of the tone rather than as an effect laid over it. At 0 the rate is the same at every pitch.");

    mod->addHeading ("LFO 2", 8);

    mod->addChoice (ids::lfo2Wave, "Wave", "Its shape.");
    mod->addKnob (ids::lfo2Rate, "Rate", "How fast, in hertz.");
    mod->addToggle (ids::lfo2Sync, "Sync",
        "Locks the rate to the host tempo: the Rate knob stands aside and the division beside this picks the speed. With Retrig off and the transport running, the *phase* locks to the song position too -- rewind and the same bar is the same wobble, which is the whole reason to sync. With Retrig on, the phase belongs to the note and only the rate is synced.");
    mod->addChoice (ids::lfo2Div, "Division",
        "The note length one LFO cycle spans when Sync is on. Key track and Seq-to-rate still multiply on top in retrigger mode; phase-locked (Sync on, Retrig off, transport running) the position comes straight from the bar and they stand aside.");
    mod->addKnob (ids::lfo2Smooth, "Smooth", "Rounds its corners off.");
    mod->addToggle (ids::lfo2Retrig, "Retrig",
        "Restarts the LFO from the top of its cycle every time a note is pressed. Free-running is right for a wobble that should keep its place across a phrase; retriggered is right for anything that has to line up with the note -- a sweep, a stab, a phase that has to start in the same place every time. On a reese the difference is the whole sound: with this on, every note gets the same phase relationship and the growl is repeatable.");
    mod->addKnob (ids::lfo2Att, "Attack",
        "Fades the LFO's **depth** in from nothing over this long, restarting on every note. Delayed vibrato is the classic use -- the note arrives steady and the movement creeps in after it -- and on a filter it is a sweep that opens up rather than one that is already going.\n\nIt restarts whether or not Retrig is on: the two are separate ideas, one about the waveform's phase and the other about its depth. Eased rather than ramped, so it arrives without the corner a straight fade leaves at the top. At 0 there is no fade.");
    mod->addKnob (ids::lfo2Key, "Key track",
        "How far the LFO's rate follows the played note, referenced to middle C. At 100% an octave up doubles the rate, so the modulation stays in the same relationship to the pitch all the way up the keyboard -- which is how you get a phase or a wobble that reads as part of the tone rather than as an effect laid over it. At 0 the rate is the same at every pitch.");

    mod->addHeading ("SEQUENCER", 4);

    mod->addKnob (ids::seqRate, "Seq rate",
        "Steps per beat. With the transport running the pattern locks to it, so a sixteen-step "
        "figure at 4 per beat is exactly one bar. Stopped, it free-runs at the same speed.");

    mod->addKnob (ids::seqLength, "Steps",
        "How many of the sixteen are in the pattern. The rest are dimmed rather than hidden -- "
        "their values are still there when you lengthen it again.");

    mod->addKnob (ids::seqGlide, "Seq glide",
        "How much of each step is spent sliding to the next. At 0 it steps; at 1 it never holds "
        "still and the pattern is a curve rather than a staircase.");

    mod->addKnob (ids::seqToLfoRate, "Seq to rate",
        "The old automation trick, wired in: the sequencer steps LFO 1's *rate* through a pattern "
        "of speeds, in octaves. With LFO 1 on the cutoff this is a wobble that changes tempo on "
        "the step, which is the thing that used to take an automation lane and a steady hand.");

    // The global matrix first: it is the one the user reaches for -- the comb,
    // the formant and the tube live there -- and the first thing on a page
    // should be the thing the page is opened for.
    mod->addHeading ("GLOBAL MATRIX -- one chain, shared by every note", 6);

    for (int slot = 0; slot < EngineParameters::kGlobalSlots; ++slot)
    {
        const auto number = juce::String (slot + 1);

        mod->addChoice (ids::globalSource (slot), "Src " + number,
            "What drives global slot " + number + ". Only the sources that exist once rather than "
            "once per note: an amp envelope has no single value when eight notes are down.");

        mod->addChoice (ids::globalDest (slot), "To " + number,
            "What global slot " + number + " drives. **Comb time is the one this instrument was "
            "built for** -- the brief's flanger-at-rate-zero with a sequencer or an LFO behind it "
            "instead of a hand on a fader.");

        mod->addKnob (ids::globalDepth (slot), "Depth " + number,
            "How much, and which way, on the same square law as the voice matrix. Comb time and "
            "Phase centre move by six octaves at full depth -- a delay and a filter centre are "
            "pitches in disguise -- which is deliberately further than either control's own range, "
            "so a full-depth sweep drives into the ends and stays there. Tube moves 36 dB, "
            "Harmonic walks 23 partials. Output is the one held back at 24 dB: it is a level "
            "rather than a character, and a level swinging under an envelope is a hazard.");
    }

    mod->addHeading ("VOICE MATRIX -- one set of these per sounding note", 6);

    for (int slot = 0; slot < VoiceParameters::kSlots; ++slot)
    {
        const auto number = juce::String (slot + 1);

        mod->addChoice (ids::modSource (slot), "Src " + number,
            "What drives slot " + number + ". These are the per-note sources: each voice has its "
            "own envelopes, its own velocity and its own note-on random, so eight notes modulate "
            "eight different ways.");

        mod->addChoice (ids::modDest (slot), "To " + number,
            "What slot " + number + " drives. Continuous controls only -- a switch reconfigures "
            "rather than adjusts, and modulating one would mean rebuilding a filter every chunk.");

        mod->addKnob (ids::modDepth (slot), "Depth " + number,
            "How much, and which way. **The law is square, so this knob is fine at the bottom and "
            "enormous at the top**: a tenth of the travel on Pitch is 72 cents and the end of it "
            "is six octaves. Ten octaves on Cutoff, an octave on Detune, sixteen on PM. "
            "Full depth is meant to be too much -- that is what it is for.");
    }

    pages_[kModPage] = std::move (mod);

    // ---- MANGLE --------------------------------------------------------------

    auto mangle = std::make_unique<ControlPage> (state, paletteForPage (kManglePage));

    mangle->addHeading ("THE SPLIT -- the sub bypasses everything below", 5);

    mangle->addToggle (ids::subSplit, "Split on",
        "The whole crossover, on or off. Off is the pure path: no split, no sub mono, the "
        "complete signal through the mangle and one gentle DC blocker on the way out -- for "
        "when you want to band-split on a DAW mixer bus yourself instead of inside the "
        "instrument. Costs nothing either way; the toggle crossfades over 30 ms.");

    mangle->addKnob (ids::splitHz, "Split",
        "Where the sub is taken out of the mangle. Below this the signal gets a DC blocker and "
        "nothing else -- no tube, no comb, no formant. This is inside the instrument rather than "
        "three plugins later because it is what makes the rest of the page usable on a real "
        "track: you can destroy the body without touching the weight.");

    mangle->addToggle (ids::subMono, "Sub mono",
        "Sums the sub band to mono. On by default: a wide sub is the single most common way to "
        "lose a bass on a club system, where the two sides are summed in the amplifier and "
        "anything out of phase cancels.");

    mangle->addChoice (ids::order, "Order",
        "Where the tube sits relative to the comb, and it is two different instruments. Comb "
        "first: the tube generates harmonics of a signal that already has holes in it, and the "
        "holes stay holes -- tuned and hollow. Tube first: the tube fills the notches with "
        "harmonics it made itself and the comb then cuts those too, which is denser and less "
        "tuned. The same distinction as a tone stack in front of a distortion or behind it.");

    mangle->addKnob (ids::tubeDrive, "Tube",
        "A triode stage, straight from Anvil. Its grid conducts on the positive half and blocks, "
        "so the operating point drifts under load -- which is why the hundredth bar sounds unlike "
        "the first. At 0 dB it is bit-exactly out of the path.");

    mangle->addKnob (ids::tilt, "Tilt",
        "One knob of tone: two shelves moving in opposite directions about 700 Hz. A balance "
        "rather than a boost -- it moves where the energy sits rather than how much there is.");

    mangle->addHeading ("THE COMB -- what this instrument is for", 5);

    mangle->addChoice (ids::combMode, "Comb",
        "Flange is a delay: every frequency is shifted by the same *time*, so the notches are "
        "evenly spaced and it rings and sounds metallic. Phase is an allpass cascade: every "
        "frequency is shifted by a different *phase*, so the notches bunch and it sounds vocal "
        "and does not ring. Two topologies, one control surface.");

    mangle->addKnob (ids::combTime, "Time",
        "The delay, and the first notch sits at 1/(2 x time). Point a global slot at this and it "
        "sweeps -- that is the whole instrument. Three octaves at full depth.");

    mangle->addKnob (ids::combTrack, "Key track",
        "Pulls the delay onto the played note's period, so the notches land on that note's own "
        "harmonics. The growl comes out *tuned* rather than random, which is most of why a "
        "well-made growl sits in a mix instead of fighting it. Anywhere between free and locked.");

    mangle->addKnob (ids::combFeed, "Feedback",
        "How much comes back round, and **negative is the invert-feedback switch** as a "
        "continuous control -- it moves the whole notch pattern by half a spacing, which is a "
        "different sound rather than a smaller one. Capped below unity, so it cannot run away.");

    mangle->addKnob (ids::combDamp, "Damp",
        "A lowpass inside the feedback loop, so each pass round is darker than the last. Takes "
        "the glassiness off a high-feedback setting.");

    mangle->addKnob (ids::combSpread, "Spread",
        "Offsets the two channels' delays. What makes a flanger wide -- and what makes it "
        "collapse in mono, so watch the correlation if the track is going to a club.");

    mangle->addKnob (ids::combMix, "Mix",
        "Dry against combed. At 0 the comb is bit-exactly out of the path, not merely quiet.");

    mangle->addToggle (ids::combInvert, "Invert wet",
        "Flips the combed signal's polarity, which turns the notches into peaks and the peaks "
        "into notches. On a feedforward comb it also halves the spacing -- the first notch moves "
        "from 1/(2T) to 1/T.");

    mangle->addKnob (ids::phaseFreq, "Phase centre",
        "Where the allpass cascade's notches are bunched. Only in Phase mode.");

    mangle->addKnob (ids::phaseStages, "Stages",
        "How many allpass sections, 2 to 16. Each pair makes one notch, so 8 stages is 4 notches. "
        "Only in Phase mode.");

    mangle->addHeading ("VOWEL -- the comb, shaped like a mouth", 3);

    mangle->addKnob (ids::formantMorph, "Vowel",
        "Morphs across ee - eh - ah - oh - oo. Three resonant peaks at the frequencies a human "
        "tract actually puts them, from Peterson and Barney's measurements of adult male vowels. "
        "The same idea as the comb, shaped like a mouth.");

    mangle->addKnob (ids::formantSharp, "Sharpness",
        "How narrow the three peaks are. The gain is divided by the Q, so this sharpens the "
        "vowel rather than turning it up.");

    mangle->addKnob (ids::formantMix, "Vowel mix",
        "Dry against vowelled. At 0 the formant filter is bit-exactly out of the path.");

    mangle->addHeading ("OVERTONE -- key tracking, on the vowel", 4, true);

    mangle->addKnob (ids::formantLock, "Harmonic lock",
        "Pulls the three resonances off the vowel and onto **harmonics of the played note**. "
        "This is what overtone singing is: not a second voice, but one source with a resonance "
        "sharp enough to pick a single partial out of the drone and make it a melody. Because it "
        "can only land on a harmonic, it is always in tune with the bass under it.\n\n"
        "The lock sharpens as it engages -- selecting one partial takes a bandwidth of about "
        "1.6 Hz where a spoken vowel has eighty. At 0 the vowel is bit-exactly untouched.");

    mangle->addKnob (ids::formantHarmonic, "Harmonic",
        "Which partial the lock selects, counting the fundamental as 1. Continuous, because it is "
        "a modulation destination -- point the sequencer at Harmonic in the global matrix and the "
        "overtone line walks the series in time with the track. Partials 6 to 12 are where sygyt "
        "actually sings.");

    mangle->addKnob (ids::formantNotch, "Notch",
        "The anti-formant. A nasal is not a vowel with different peaks -- it is a vowel with a "
        "**zero**: the nasal cavity is a side branch, and a side branch cancels rather than "
        "resonates. That is what a vowel filter with only peaks cannot make, and why none of them "
        "can say \"m\" or the ending of a chanted \"AUM\".\n\n"
        "Set aside from the vocal reading, it is simply a hole you can put anywhere in the growl.");

    mangle->addKnob (ids::formantNotchDepth, "Notch depth",
        "How deep the hole goes -- 26.6 dB at the centre when full, and localised: two octaves "
        "away it is within 3 dB of untouched. At 0 it is bit-exactly out of the path.");

    pages_[kManglePage] = std::move (mangle);

    // ---- TUNING --------------------------------------------------------------

    // A page like any other, and hosted by the viewport like any other. It was
    // neither before: it was a component the editor parented by hand, and the
    // hand-parenting is what got forgotten.
    pages_[kTuningPage] = std::make_unique<TuningPage> (sonitus_, paletteForPage (kTuningPage));

    // Each page wears its own accent, and the look and feel is how that reaches
    // the knobs: JUCE resolves one by walking up the tree, so a page holding
    // its own colours every control inside it with nothing passed down by hand.
    for (int page = 0; page < kNumPages; ++page)
    {
        auto& lookAndFeel = pageLookAndFeels_[static_cast<std::size_t> (page)];

        lookAndFeel = std::make_unique<ui::KnobLookAndFeel> (paletteForPage (page));

        if (auto* component = pages_[static_cast<std::size_t> (page)].get())
            component->setLookAndFeel (lookAndFeel.get());
    }
}

void SonitusEditor::showPage (int index)
{
    currentPage_ = juce::jlimit (0, kNumPages - 1, index);

    for (int i = 0; i < kNumPages; ++i)
    {
        auto& tab = tabs_[static_cast<std::size_t> (i)];

        const bool active = i == currentPage_;
        const auto accent = kPageAccents[static_cast<std::size_t> (i)].accent;

        // The inactive tabs are dark plates with their own page's colour on
        // them, so the row is a key to where everything is rather than six
        // identical buttons. Plates rather than a wash of the accent, because a
        // translucent tint over the bright chassis comes out pale and all six
        // end up looking the same shade of nothing.
        tab.setColour (juce::TextButton::buttonColourId,
                       active ? accent : kGroupPanel.darker (0.10f));
        tab.setColour (juce::TextButton::textColourOffId,
                       active ? kGroupPanel.darker (0.4f) : accent);
    }

    // `false`: the viewport must not take ownership -- the pages outlive the
    // page changes and are owned by the array.
    viewport_.setViewedComponent (pages_[static_cast<std::size_t> (currentPage_)].get(), false);

    // The step strip belongs to the MOD page, so it follows the tab rather than
    // being always on screen.
    if (steps_ != nullptr)
        steps_->setVisible (currentPage_ == kModPage);

    if (auto* tuning = dynamic_cast<TuningPage*> (pages_[static_cast<std::size_t> (currentPage_)].get()))
        tuning->refresh();

    noteLabel_.setText (notes_[static_cast<std::size_t> (currentPage_)], juce::dontSendNotification);

    resized();
}

void SonitusEditor::updateForSwitches()
{
    auto& state = sonitus_.getState();

    const auto index = [&state] (const char* id)
    {
        return static_cast<int> (std::lround (state.getRawParameterValue (id)->load()));
    };

    const int combMode = index (ids::combMode);
    const int keyMode = index (ids::keyMode);
    const int oversample = index (ids::oversampling);
    const int syncB = index (ids::syncB);
    const int shapeA = index (ids::shapeA);
    const int shapeB = index (ids::shapeB);
    const int lfoSync = index (ids::lfo1Sync) + 2 * index (ids::lfo2Sync);
    const int latency = sonitus_.isPrepared() ? sonitus_.getLatencySamples() : -1;

    // The notch moves continuously, so it is rounded before being compared --
    // otherwise the note is rebuilt thirty times a second while anything sweeps
    // and the page repaints for nothing.
    const int notch = juce::roundToInt (sonitus_.getCombNotchHz());

    const auto scale = sonitus_.getScaleName();

    if (combMode == shownCombMode_ && keyMode == shownKeyMode_ && oversample == shownOversample_
        && latency == shownLatency_ && syncB == shownSyncB_ && shapeA == shownShapeA_
        && shapeB == shownShapeB_ && notch == shownNotch_ && scale == shownScale_
        && lfoSync == shownLfoSync_)
        return;

    const bool combChanged = combMode != shownCombMode_;
    const bool shapesChanged = shapeA != shownShapeA_ || shapeB != shownShapeB_
                            || syncB != shownSyncB_;
    const bool scaleChanged = scale != shownScale_;

    shownCombMode_ = combMode;
    shownKeyMode_ = keyMode;
    shownOversample_ = oversample;
    shownLatency_ = latency;
    shownSyncB_ = syncB;
    shownShapeA_ = shapeA;
    shownShapeB_ = shapeB;
    shownNotch_ = notch;
    shownScale_ = scale;
    shownLfoSync_ = lfoSync;

    // A synced LFO's Rate knob is inert and its Division is live; free, the
    // other way round. Greyed rather than hidden, because a control that
    // vanishes reads as a bug and one that moves without doing anything reads
    // as a worse one.
    if (auto* mod = controlPage (kModPage))
    {
        mod->setControlEnabled (ids::lfo1Rate, (lfoSync & 1) == 0);
        mod->setControlEnabled (ids::lfo1Div, (lfoSync & 1) != 0);
        mod->setControlEnabled (ids::lfo2Rate, (lfoSync & 2) == 0);
        mod->setControlEnabled (ids::lfo2Div, (lfoSync & 2) != 0);
    }

    auto* osc = controlPage (kOscPage);
    auto* filter = controlPage (kFilterPage);
    auto* mangle = controlPage (kManglePage);

    if (combChanged && mangle != nullptr)
    {
        // Greyed rather than hidden: a knob that moves and does nothing reads
        // as a broken plugin rather than as a mode.
        const bool isFlange = combMode == static_cast<int> (CombMode::flange);
        const bool isPhase = combMode == static_cast<int> (CombMode::phase);
        const bool anyComb = isFlange || isPhase;

        for (const char* id : { ids::combTime, ids::combTrack, ids::combDamp })
            mangle->setControlEnabled (id, isFlange);

        for (const char* id : { ids::phaseFreq, ids::phaseStages })
            mangle->setControlEnabled (id, isPhase);

        for (const char* id : { ids::combFeed, ids::combSpread, ids::combMix, ids::combInvert })
            mangle->setControlEnabled (id, anyComb);
    }

    if (shapesChanged && osc != nullptr)
    {
        // Width does nothing to a saw or a sine: both are fully described
        // without it.
        const auto hasWidth = [] (int shape)
        {
            return shape == static_cast<int> (dsp::OscShape::pulse)
                || shape == static_cast<int> (dsp::OscShape::triangle);
        };

        osc->setControlEnabled (ids::widthA, hasWidth (shapeA));
        osc->setControlEnabled (ids::widthB, hasWidth (shapeB));

        // Morph belongs to the phase-3 shapes; the classic four ignore it and
        // the slider says so by greying.
        const auto hasMorph = [] (int shape)
        {
            return shape >= static_cast<int> (dsp::OscShape::vintage);
        };

        osc->setControlEnabled (ids::morphA, hasMorph (shapeA));
        osc->setControlEnabled (ids::morphB, hasMorph (shapeB));
    }

    if (scaleChanged)
        if (auto* tuning = dynamic_cast<TuningPage*> (pages_[kTuningPage].get()))
            tuning->refresh();

    juce::ignoreUnused (osc, filter);

    notes_[kManglePage] = sonitus_.describeComb() + "  " + sonitus_.describeOversampling();

    notes_[kOscPage] = syncB != 0
        ? juce::String ("Sync is on: B restarts every time the note's period comes round, so its "
                        "Semis and Fine knobs are sweeping a formant rather than setting a pitch. "
                        "Put a mod envelope on Pitch B in the matrix -- standing still it is just "
                        "a bright waveform.")
        : juce::String ("Two dense sources, and the denser the better: a comb can only cut "
                        "harmonics that are there. Saw plus saw a few cents apart is already a "
                        "moving comb before anything on the MANGLE page is switched on.");

    notes_[kFilterPage] = keyMode == static_cast<int> (KeyboardMode::poly)
        ? juce::String ("Poly. Eight voices with seven-way unison on both oscillators is 112 "
                        "oscillators -- the number to watch. A reese does not need any of it: "
                        "switch to Mono and spend the CPU on oversampling instead.")
        : juce::String ("Mono, so the whole instrument is one voice and the CPU is free. Legato "
                        "differs in one thing and it matters: it does not retrigger the envelopes, "
                        "so a phrase played without gaps runs through a single envelope and glides "
                        "between its notes.");

    noteLabel_.setText (notes_[static_cast<std::size_t> (currentPage_)], juce::dontSendNotification);
}

void SonitusEditor::timerCallback()
{
    auto& meters = sonitus_.getMeterValues();

    outputMeter_->setValues (meters.outputVuDb.load (std::memory_order_relaxed),
                             meters.outputPeakDb.load (std::memory_order_relaxed));
    outputMeter_->repaint();

    if (steps_ != nullptr && steps_->isVisible())
    {
        const int length = static_cast<int> (std::lround (
            sonitus_.getState().getRawParameterValue (ids::seqLength)->load()));

        steps_->setPlaying (sonitus_.getSequencerStep(), length);
    }

    // Only the page on screen. Three envelope graphs each comparing five
    // parameters is cheap, but repainting a page nobody is looking at is not
    // cheap enough to do thirty times a second.
    if (auto* envelopes = dynamic_cast<EnvelopePage*> (pages_[kEnvPage].get()))
        if (envelopes->isVisible())
            envelopes->refresh (sonitus_);

    const double rate = sonitus_.getSampleRate() > 0.0 ? sonitus_.getSampleRate() : 48000.0;

    juce::String status;
    status << "VOICES " << sonitus_.getActiveVoiceCount()
           << "   \xe2\x80\xa2   " << sonitus_.getScaleName()
           << "   \xe2\x80\xa2   OUT " << juce::String (
                  meters.outputPeakDb.load (std::memory_order_relaxed), 1) << " dB peak"
           << "   \xe2\x80\xa2   LATENCY ";

    // Until the host has started audio there is no latency figure, only an
    // uninitialised one -- and printing "0 sm" for it says the plugin has none,
    // which is a different claim entirely.
    if (sonitus_.isPrepared())
    {
        const int latency = sonitus_.getLatencySamples();

        status << latency << " sm (" << juce::String (1000.0 * latency / rate, 2) << " ms)";
    }
    else
    {
        status << "--";
    }

    statusLabel_.setText (status, juce::dontSendNotification);

    header_->setActiveSlot (sonitus_.getAbCompare().isSlotB());
    header_->setOtherSlotFilled (sonitus_.getAbCompare().otherSlotFilled());

    updateForSwitches();
}

void SonitusEditor::paint (juce::Graphics& g)
{
    // Brushed metal, cached -- see MetalBackground. The specular band sits high
    // so it catches the header rather than the middle of the controls, which is
    // where a real panel's light would fall and where it is least in the way of
    // reading a knob.
    metal_.paint (g, getLocalBounds(), 0.16f);
}

void SonitusEditor::resized()
{
    auto bounds = getLocalBounds();

    header_->setBounds (bounds.removeFromTop (ui::HeaderBar::getPreferredHeight()));

    statusLabel_.setBounds (bounds.removeFromBottom (kStatusHeight).reduced (12, 3));

    // Reserved before anything else takes the space, and only when the page on
    // screen has something to say -- the ENV, MOD and TUNING pages do not, and
    // an empty strip is 38 pixels of nothing.
    noteLabel_.setVisible (notes_[static_cast<std::size_t> (currentPage_)].isNotEmpty());

    if (noteLabel_.isVisible())
        noteLabel_.setBounds (bounds.removeFromBottom (kNoteHeight).reduced (16, 2));

    auto right = bounds.removeFromRight (kMeterWidth + 8).reduced (3, 5);
    outputMeterLabel_.setBounds (right.removeFromBottom (11));
    outputMeter_->setBounds (right);

    auto tabRow = bounds.removeFromTop (kTabHeight).reduced (4, 1);
    const int tabWidth = tabRow.getWidth() / kNumPages;

    for (int i = 0; i < kNumPages; ++i)
        tabs_[static_cast<std::size_t> (i)].setBounds (
            tabRow.removeFromLeft (i == kNumPages - 1 ? tabRow.getWidth() : tabWidth).reduced (2, 0));

    auto body = bounds.reduced (4, 2);

    if (steps_ != nullptr && steps_->isVisible())
        steps_->setBounds (body.removeFromBottom (kStepStripHeight).withTrimmedTop (4));

    viewport_.setBounds (body);

    if (auto* page = pages_[static_cast<std::size_t> (currentPage_)].get())
    {
        // Sized before it is asked how tall it wants to be: the note's height
        // depends on nothing, but the row count does not fit until the width
        // is known, and a page that fits gets the viewport's full height so it
        // fills the window rather than sitting against the top.
        // Twice, because the two are circular: whether the scroll bar is shown
        // depends on the page's height, and the width the page gets depends on
        // whether the scroll bar is shown. One pass settles it, and the second
        // is what makes the result the same whichever page was on screen
        // before.
        for (int pass = 0; pass < 2; ++pass)
            page->setSize (viewport_.getMaximumVisibleWidth(),
                           juce::jmax (viewport_.getMaximumVisibleHeight(),
                                       page->getPreferredHeight()));
    }
}

} // namespace tezla::sonitus
