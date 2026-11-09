#pragma once

#include "PluginProcessor.h"

#include <juce_gui_extra/juce_gui_extra.h>

namespace ui
{
struct Colours
{
    static inline const juce::Colour bg { 0xff15171c }, panel { 0xff1e2128 }, edge { 0xff2c3038 },
        text { 0xffe6e8ec }, dim { 0xff8a919e }, accent { 0xff2fa36d }, gold { 0xffffd166 }, warn { 0xffe0705c };
};

/** The 12-key note map: in-key notes, scale degrees, how much the sample uses each one. */
class NoteMap : public juce::Component
{
public:
    void setData (const zampler::Key& k, const std::array<double, 12>& usage, std::vector<zampler::Chord> chords);
    void paint (juce::Graphics&) override;

private:
    zampler::Key key;
    std::array<double, 12> use {};
    std::vector<zampler::Chord> barChords;
};

/** Drag this to the host / desktop to drop the arrangement as a .mid file. */
class DragMidiButton : public juce::TextButton
{
public:
    explicit DragMidiButton (ZamplerProcessor& p) : juce::TextButton ("Drag MIDI out"), proc (p)
    {
        setTooltip ("Drag onto a track, Renoise's instrument/pattern area, or the desktop");
    }
    void mouseDrag (const juce::MouseEvent&) override;

private:
    ZamplerProcessor& proc;
    bool dragging = false;
};
} // namespace ui

class ZamplerEditor : public juce::AudioProcessorEditor,
                      public juce::FileDragAndDropTarget,
                      public juce::DragAndDropContainer,
                      private juce::Timer
{
public:
    explicit ZamplerEditor (ZamplerProcessor&);
    ~ZamplerEditor() override;

    void paint (juce::Graphics&) override;
    void paintOverChildren (juce::Graphics&) override;
    void resized() override;

    bool isInterestedInFileDrag (const juce::StringArray& files) override;
    void fileDragEnter (const juce::StringArray&, int, int) override { dragOver = true; repaint(); }
    void fileDragExit (const juce::StringArray&) override { dragOver = false; repaint(); }
    void filesDropped (const juce::StringArray& files, int, int) override;

private:
    void timerCallback() override;
    void refresh();
    void openFile();
    void exportFile (const juce::String& title, const juce::String& pattern, const juce::String& defaultName,
                     std::function<juce::File (const juce::File&)> writer);

    using ComboAtt = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
    using ButtonAtt = juce::AudioProcessorValueTreeState::ButtonAttachment;
    using SliderAtt = juce::AudioProcessorValueTreeState::SliderAttachment;

    struct LabelledCombo
    {
        juce::Label label;
        juce::ComboBox box;
        std::unique_ptr<ComboAtt> att;
    };
    struct LabelledSlider
    {
        juce::Label label;
        juce::Slider slider { juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight };
        std::unique_ptr<SliderAtt> att;
    };
    struct Toggle
    {
        juce::ToggleButton button;
        std::unique_ptr<ButtonAtt> att;
    };

    void setupCombo (LabelledCombo&, const juce::String& paramId, const juce::String& text);
    void setupSlider (LabelledSlider&, const juce::String& paramId, const juce::String& text);
    void setupToggle (Toggle&, const juce::String& paramId, const juce::String& text);

    ZamplerProcessor& proc;
    juce::TooltipWindow tooltips { this };

    // header
    juce::TextButton openButton { "Open sample..." };
    juce::Label status, info;
    bool dragOver = false;

    // settings row
    LabelledCombo keyRoot, scale, detectMode, lpb, patternLines;
    Toggle scaleLock, snapSample, preview;
    LabelledSlider maxPoly, drumSens;

    ui::NoteMap noteMap;

    // builders
    Toggle sampleOn, drumsOn, kickOn, snareOn, hatOn;
    Toggle bassOn, chordsOn, leadOn, sevenths;
    LabelledCombo bassProg, bassStyle, chordProg, chordStyle, leadStyle, sketchStep;
    LabelledSlider bassOct, chordOct, leadOct, leadBusy, leadSeed;
    juce::Label sketchLabel { {}, "Sketch" };
    juce::TextEditor sketchBox;
    juce::Label sketchFixes;

    // pattern view
    juce::Label patternLabel { {}, "Pattern" };
    juce::ComboBox patternPick;
    juce::TextEditor patternView;

    // exports
    juce::TextButton exportRenoise { "Save Renoise pattern..." }, exportMidi { "Save MIDI..." },
        exportTool { "Save Renoise tool (.xrnt)..." };
    ui::DragMidiButton dragMidi { proc };

    std::unique_ptr<juce::FileChooser> chooser;
    int shownVersion = -1;
    juce::String lastStatus;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ZamplerEditor)
};
