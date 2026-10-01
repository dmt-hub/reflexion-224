// The plugin's skeletal editor: plain JUCE widgets generated from the
// catalog model, laid out like the web page (web/index.html): "Machine and
// firmware", "Audio" (Input, Output), "Program" with the program's pages as
// labelled columns of horizontal sliders.
//
// It never touches the machine: a timer copies the published UiSnapshot
// (ui_interface.hpp) through `read`, and every user action is a UiCommands
// call. The integrator wraps an EditorView in its AudioProcessorEditor:
//
//   view = std::make_unique<lexui::EditorView>(catalogs, help,
//       [&](lexui::UiSnapshot &s) { processor.readUiSnapshot(s); }, processor.uiCommands());
//   addAndMakeVisible(*view); setResizable(true, true); setSize(1040, 820);
#pragma once
#include "ui_interface.hpp"
#include "../source/catalog/catalog.hpp"
#include "../source/catalog/help.hpp"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace lexui {

class TooltipLnf : public juce::LookAndFeel_V4 {
public:
    static juce::TextLayout layoutTip (const juce::String &text, juce::Colour colour) {
        const float fontSize = 22.0f;
        const int maxWidth = 500;

        juce::AttributedString as;
        as.setJustification (juce::Justification::centred);
        as.append (text, juce::Font (juce::FontOptions (fontSize)));
        as.setColour (colour);

        juce::TextLayout tl;
        tl.createLayoutWithBalancedLineLengths (as, (float) maxWidth);
        return tl;
    }

    juce::Rectangle<int> getTooltipBounds (const juce::String &tipText,
                                            juce::Point<int> screenPos,
                                            juce::Rectangle<int> parentArea) override {
        const auto tl = layoutTip (tipText, juce::Colours::black);
        const int w = (int) (tl.getWidth() + 20.0f);
        const int h = (int) (tl.getHeight() + 10.0f);
        return juce::Rectangle<int> (screenPos.x > parentArea.getCentreX() ? screenPos.x - (w + 12) : screenPos.x + 24,
                                     screenPos.y > parentArea.getCentreY() ? screenPos.y - (h + 6)  : screenPos.y + 6,
                                     w, h)
                   .constrainedWithin (parentArea);
    }

    void drawTooltip (juce::Graphics &g, const juce::String &text, int width, int height) override {
        g.fillAll (findColour (juce::TooltipWindow::backgroundColourId));

       #if ! JUCE_MAC
        g.setColour (findColour (juce::TooltipWindow::outlineColourId));
        g.drawRect (0, 0, width, height, 1);
       #endif

        layoutTip (text, findColour (juce::TooltipWindow::textColourId))
            .draw (g, juce::Rectangle<float> ((float) width, (float) height));
    }
};

class EditorView : public juce::Component, public juce::FileDragAndDropTarget, private juce::Timer {
public:
    using SnapshotReader = std::function<void(UiSnapshot &)>;

    // `catalogs`, `help` and `commands` must outlive the view.
    EditorView(const lexcat::CatalogLibrary &catalogs, const lexcat::ParamHelp &help, SnapshotReader read,
               UiCommands &commands);
    ~EditorView() override;

    // Read the snapshot now and update every widget (the timer does this at 20 Hz).
    void refresh();

    // The height the content wants at the current width (for snapshots/tests).
    int contentHeight() const;
    // The scrolled content, e.g. for createComponentSnapshot of everything.
    juce::Component &content();

    void resized() override;
    void paint(juce::Graphics &g) override;

    bool isInterestedInFileDrag(const juce::StringArray &files) override;
    void filesDropped(const juce::StringArray &files, int x, int y) override;

private:
    void timerCallback() override;

    class Content;
    const lexcat::CatalogLibrary &catalogs_;
    const lexcat::ParamHelp &help_;
    SnapshotReader read_;
    UiCommands &commands_;
    TooltipLnf tooltipLnf_;
    juce::TooltipWindow tooltips_{this, 600};
    juce::Viewport viewport_;
    std::unique_ptr<Content> content_;
};

}  // namespace lexui
