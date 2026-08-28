#pragma once

#include <functional>
#include <optional>

#include <juce_audio_processors/juce_audio_processors.h>

#include <tezla/dsp/SpectrumAnalyser.hpp>
#include <tezla/ui/TooltipHost.hpp>
#include <tezla/ui/Goniometer.hpp>
#include <tezla/ui/HeaderBar.hpp>
#include <tezla/ui/LevelMeter.hpp>
#include <tezla/ui/Palette.hpp>

#include "PluginProcessor.h"

namespace tezla::transpectus
{

/// One number with its name above it, and a band of interpretation under it.
///
/// The interpretation is what makes a metering plugin worth having over a
/// number in a status bar: -8 LUFS means nothing on its own, and "6 dB louder
/// than Spotify plays things" means something immediately.
class Readout final : public juce::Component,
                      public juce::SettableTooltipClient
{
public:
    Readout (ui::Palette palette, juce::String caption, juce::String unit);

    /// `note` is the line under the number. Empty hides it.
    void setValue (juce::String text, juce::String note = {});

    /// Draws the number in the warning colour. For a reading that has gone
    /// somewhere it should not.
    void setWarning (bool shouldWarn);

    void paint (juce::Graphics&) override;

private:
    ui::Palette palette_;
    juce::String caption_, unit_, value_ { "--" }, note_;
    bool warning_ { false };
};

/// A correlation meter: -1 to +1, with the mono-safe region marked.
class CorrelationBar final : public juce::Component,
                             public juce::SettableTooltipClient
{
public:
    explicit CorrelationBar (ui::Palette palette) : palette_ (palette) {}

    void setValue (float correlation, bool warn) noexcept
    {
        correlation_ = correlation;
        warning_ = warn;
    }

    void setCaption (juce::String caption) { caption_ = std::move (caption); }

    void paint (juce::Graphics&) override;

private:
    ui::Palette palette_;
    juce::String caption_;
    float correlation_ { 1.0f };
    bool warning_ { false };
};

/// The spectrum, with the two honest references: a pink-noise slope, and a
/// curve captured from a track you already like.
///
/// No target curve ships with it. Genre curves are folklore -- they vary by
/// track, by era and by who drew them -- and baking one into a tool somebody
/// trusts is worse than shipping nothing. Pink noise is physics; the other
/// reference is whatever you point it at.
class SpectrumView final : public juce::Component,
                           public juce::SettableTooltipClient
{
public:
    SpectrumView (ui::Palette palette, dsp::ReferenceCurve& reference,
                  std::vector<float>& peakHold);

    /// Folds the latest capture onto the display bins. Returns true if there
    /// was anything new to draw.
    bool update (const dsp::SpectrumCapture& capture);

    void setShowPinkSlope (bool shouldShow);
    void setShowDifference (bool shouldShow);
    void setShowPeakHold (bool shouldShow);

    /// Throws the permanent maximum away and starts collecting again. The
    /// point of the feature: change an EQ move, clear, and watch what the new
    /// worst case turns out to be.
    void resetPeakHold();

    /// @see ui::Goniometer::setTopRightInset
    void setTopRightInset (int pixels) noexcept { topRightInset_ = juce::jmax (0, pixels); }

    /// Feeds the capture in progress, if there is one.
    void pushToCapture();

    [[nodiscard]] dsp::SpectrumAnalyser& getAnalyser() noexcept { return analyser_; }

    void paint (juce::Graphics&) override;

    // A crosshair that reads out where the pointer is. Tracked on drag as well
    // as move, so it keeps reading while a button is held rather than vanishing
    // at the moment somebody is pointing at something.
    void mouseMove (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;

private:
    /// Where a frequency sits across the width, 0 to 1. Log, so an octave takes
    /// the same room wherever it is.
    [[nodiscard]] float positionFor (double hz) const noexcept;

    /// The inverse: which frequency sits at a fraction of the width.
    [[nodiscard]] static double frequencyAt (float fraction) noexcept;

    /// The plotted rectangle, inside the panel padding and below the caption.
    /// Shared by the painting and the crosshair, because a readout computed
    /// against a slightly different rectangle than the one drawn is a readout
    /// that lies by a few pixels' worth of dB.
    [[nodiscard]] juce::Rectangle<float> plotArea() const noexcept;

    /// The live curve's level at a fraction of the width, interpolated between
    /// the two display bins either side.
    [[nodiscard]] float levelAt (float fraction) const noexcept;

    void paintCrosshair (juce::Graphics&, juce::Rectangle<float>) const;

    void paintGrid (juce::Graphics&, juce::Rectangle<float>) const;
    void paintCurve (juce::Graphics&, juce::Rectangle<float>,
                     const std::vector<float>& db, juce::Colour, float thickness, bool fill) const;

    ui::Palette palette_;
    dsp::ReferenceCurve& reference_;

    /// Owned by the processor, so it outlives this view.
    std::vector<float>& peakHold_;

    dsp::SpectrumAnalyser analyser_;

    std::vector<double> difference_;

    bool showPinkSlope_ { true };
    bool showDifference_ { false };
    bool showPeakHold_ { true };
    int  topRightInset_ { 0 };

    /// Empty when the pointer is not over the plot.
    std::optional<juce::Point<float>> cursor_;
};

/// A panel lifted out of the editor into a window of its own.
///
/// Holds its content **non-owned**: the editor still owns the component and
/// puts it back when the window closes, so there is exactly one owner whether
/// the panel is docked or floating. It carries its own TooltipWindow because a
/// tooltip belongs to a component hierarchy, and a detached panel is no longer
/// in the editor's.
class PanelWindow final : public juce::DocumentWindow
{
public:
    PanelWindow (const juce::String& name, juce::Colour background,
                 juce::Component& content, std::function<void()> onClose);

    void closeButtonPressed() override;

    /// Opens next to the editor rather than on top of it.
    void placeBeside (juce::Rectangle<int> editorArea);

private:
    std::function<void()> onClose_;
    ui::TooltipHost tooltips_ { *this };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PanelWindow)
};

class TranspectusEditor final : public juce::AudioProcessorEditor,
                                private juce::Timer
{
public:
    explicit TranspectusEditor (TranspectusProcessor& processorToUse);
    ~TranspectusEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    /// Which of the two large panels a command refers to.
    enum class Panel { spectrum, goniometer };

    void timerCallback() override;
    void buildControls();
    void buildPanelChrome();

    /// Gives one panel the whole body area, or gives the body back. Maximise
    /// means *this and nothing else*: if you want a big spectrum and the
    /// numbers at the same time, that is what detaching is for.
    void setMaximised (Panel panel, bool shouldMaximise);

    /// Lifts a panel into its own window, or puts it back.
    void setDetached (Panel panel, bool shouldDetach);

    [[nodiscard]] bool isDetached (Panel panel) const noexcept;
    [[nodiscard]] juce::Component& componentFor (Panel panel) noexcept;

    void updatePanelChrome();
    void layOutSpectrumControls (juce::Rectangle<int> row);
    [[nodiscard]] static int spectrumControlsHeight (int width) noexcept;

    /// Formats a loudness for display, with a real "silent" rather than a large
    /// negative number that looks like a reading.
    [[nodiscard]] static juce::String formatLufs (double lufs);

    void saveReference();
    void loadReference();

    /// Puts a message on the status line and holds it there. Without the hold
    /// it would be replaced 50 ms later by the next timer tick, which is to say
    /// never seen at all.
    void showNotice (juce::String text);

    /// Where the browser opens. The user's documents folder, and after that
    /// wherever they last put one.
    [[nodiscard]] static juce::File referenceFolder();

    TranspectusProcessor& transpectus_;

    ui::TooltipHost tooltips_ { *this };

    ui::Palette palette_;
    std::unique_ptr<ui::HeaderBar> header_;

    // ---- the numbers ---------------------------------------------------------

    std::unique_ptr<Readout> integrated_, shortTerm_, momentary_;
    std::unique_ptr<Readout> truePeak_, plr_, psr_;
    std::unique_ptr<Readout> delta_;

    std::unique_ptr<ui::LevelMeter> inputMeter_;

    std::unique_ptr<CorrelationBar> fullCorrelation_;
    std::unique_ptr<CorrelationBar> lowCorrelation_;
    std::unique_ptr<ui::Goniometer> goniometer_;

    std::unique_ptr<SpectrumView> spectrum_;

    // ---- maximise and detach -------------------------------------------------

    /// Empty when the body is shared between the panels.
    std::optional<Panel> maximised_;

    juce::TextButton spectrumMaxButton_, spectrumPopButton_;
    juce::TextButton goniometerMaxButton_, goniometerPopButton_;

    /// Declared after the components they hold, so ordinary member destruction
    /// tears the windows down first -- tidy rather than required; see the
    /// destructor for what was actually measured.
    std::unique_ptr<PanelWindow> spectrumWindow_, goniometerWindow_;

    juce::TextButton captureButton_ { "CAPTURE REFERENCE" };
    juce::TextButton clearReferenceButton_ { "CLEAR" };
    juce::TextButton saveReferenceButton_ { "SAVE" };
    juce::TextButton loadReferenceButton_ { "LOAD" };

    /// Held rather than local: a FileChooser launched asynchronously must
    /// outlive the call that launched it, and a stack one is destroyed the
    /// moment the browser opens.
    std::unique_ptr<juce::FileChooser> chooser_;
    juce::ToggleButton pinkButton_ { "Pink slope" };
    juce::ToggleButton differenceButton_ { "Difference" };
    juce::ToggleButton peakHoldButton_ { "Peak hold" };
    juce::TextButton resetPeaksButton_ { "RESET PEAKS" };

    // ---- the controls --------------------------------------------------------

    juce::ComboBox targetBox_, truePeakBox_;
    juce::Label targetLabel_ { {}, "TARGET" };
    juce::Label truePeakLabel_ { {}, "TRUE PEAK" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> targetAttachment_;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> truePeakAttachment_;

    juce::TextButton resetButton_ { "RESET MEASUREMENT" };

    juce::Label statusLabel_;

    /// When the status line goes back to reporting what the plugin is doing.
    juce::uint32 noticeUntilMs_ { 0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TranspectusEditor)
};

} // namespace tezla::transpectus
