#include "PluginEditor.h"

using namespace zampler;
using ui::Colours;

// ------------------------------------------------------------------------------------ NoteMap

void ui::NoteMap::setData (const Key& k, const std::array<double, 12>& usage, std::vector<Chord> chords)
{
    key = k;
    const double mx = *std::max_element (usage.begin(), usage.end());
    for (size_t i = 0; i < 12; ++i) use[i] = mx > 0 ? usage[i] / mx : 0;
    barChords = std::move (chords);
    repaint();
}

void ui::NoteMap::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    auto keysArea = r.removeFromTop (46.0f);
    const float w = std::min (54.0f, (keysArea.getWidth() - 11 * 4) / 12.0f);
    for (int pc = 0; pc < 12; ++pc)
    {
        juce::Rectangle<float> k (keysArea.getX() + (float) pc * (w + 4), keysArea.getY(), w, 42.0f);
        const bool in = key.contains (pc);
        g.setColour (in ? Colours::accent.darker (0.3f) : Colours::panel);
        g.fillRoundedRectangle (k, 5.0f);
        g.setColour (pc == key.root ? Colours::gold : Colours::edge);
        g.drawRoundedRectangle (k.reduced (0.5f), 5.0f, pc == key.root ? 2.0f : 1.0f);
        g.setColour (in ? Colours::text : Colours::dim);
        g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
        g.drawText (kNoteNames[(size_t) pc], k.withTrimmedBottom (16), juce::Justification::centred);
        juce::String deg = "x";
        if (in)
        {
            const auto& iv = key.intervals();
            deg = juce::String ((int) (std::find (iv.begin(), iv.end(), ((pc - key.root) % 12 + 12) % 12) - iv.begin()) + 1);
        }
        g.setFont (juce::FontOptions (10.0f));
        g.drawText (deg, k.withTrimmedTop (20).withTrimmedBottom (4), juce::Justification::centred);
        g.setColour (Colours::gold);
        g.fillRect (k.getX() + 2, k.getBottom() - 4, (k.getWidth() - 4) * (float) use[(size_t) pc], 3.0f);
    }
    // chords per bar
    r.removeFromTop (4);
    g.setFont (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), 12.0f, juce::Font::plain));
    float x = r.getX();
    g.setColour (Colours::dim);
    g.drawText ("Chords:", juce::Rectangle<float> (x, r.getY(), 56, 20), juce::Justification::centredLeft);
    x += 56;
    for (size_t i = 0; i < barChords.size() && x < r.getRight() - 60; ++i)
    {
        const auto txt = juce::String ((int) i + 1) + ": " + barChords[i].name();
        const float cw = 14.0f + 7.5f * (float) txt.length();
        g.setColour (Colours::panel);
        g.fillRoundedRectangle (x, r.getY(), cw, 20, 4);
        g.setColour (Colours::text);
        g.drawText (txt, juce::Rectangle<float> (x, r.getY(), cw, 20), juce::Justification::centred);
        x += cw + 4;
    }
}

void ui::DragMidiButton::mouseDrag (const juce::MouseEvent& e)
{
    if (dragging || e.getDistanceFromDragStart() < 6) return;
    const auto f = proc.tempMidiForDrag();
    if (! f.existsAsFile()) return;
    dragging = true;
    juce::DragAndDropContainer::performExternalDragDropOfFiles ({ f.getFullPathName() }, false, this,
                                                               [this] { dragging = false; });
}

// ------------------------------------------------------------------------------------ editor

ZamplerEditor::ZamplerEditor (ZamplerProcessor& p) : AudioProcessorEditor (p), proc (p)
{
    setOpaque (true);
    auto& lf = getLookAndFeel();
    lf.setColour (juce::ResizableWindow::backgroundColourId, Colours::bg);
    lf.setColour (juce::ComboBox::backgroundColourId, Colours::panel);
    lf.setColour (juce::ComboBox::outlineColourId, Colours::edge);
    lf.setColour (juce::TextButton::buttonColourId, Colours::panel);
    lf.setColour (juce::TextEditor::backgroundColourId, Colours::panel);
    lf.setColour (juce::TextEditor::outlineColourId, Colours::edge);
    lf.setColour (juce::Label::textColourId, Colours::text);
    lf.setColour (juce::ToggleButton::tickColourId, Colours::accent);
    lf.setColour (juce::Slider::trackColourId, Colours::accent);
    lf.setColour (juce::Slider::thumbColourId, Colours::text);
    lf.setColour (juce::Slider::textBoxOutlineColourId, Colours::edge);

    addAndMakeVisible (openButton);
    openButton.onClick = [this] { openFile(); };
    for (auto* l : { &status, &info })
    {
        addAndMakeVisible (*l);
        l->setJustificationType (juce::Justification::centredLeft);
    }
    status.setFont (juce::FontOptions (15.0f, juce::Font::bold));
    info.setColour (juce::Label::textColourId, Colours::dim);

    setupCombo (keyRoot, "keyRoot", "Key");
    setupCombo (scale, "scale", "Scale");
    setupCombo (detectMode, "detectMode", "Detect");
    setupCombo (lpb, "lpb", "LPB");
    setupCombo (patternLines, "patternLines", "Pattern");
    setupToggle (scaleLock, "scaleLock", "Scale lock");
    setupToggle (snapSample, "snapSample", "Snap sample");
    setupToggle (preview, "preview", "Preview sound");
    setupSlider (maxPoly, "maxPoly", "Max notes");
    setupSlider (drumSens, "drumSens", "Drum sens.");
    scaleLock.button.setTooltip ("Every generated or typed note is snapped into the key");
    snapSample.button.setTooltip ("Also snap the sample's own notes (off = exactly as heard)");
    preview.button.setTooltip ("Play the parts with Zampler's built-in sounds while the host plays");

    addAndMakeVisible (noteMap);

    setupToggle (sampleOn, "sampleOn", "Sample");
    setupToggle (drumsOn, "drumsOn", "Drums");
    setupToggle (kickOn, "kickOn", "kick");
    setupToggle (snareOn, "snareOn", "snare");
    setupToggle (hatOn, "hatOn", "hat");
    setupToggle (bassOn, "bassOn", "Bass");
    setupCombo (bassProg, "bassProg", "Progression");
    setupCombo (bassStyle, "bassStyle", "Style");
    setupSlider (bassOct, "bassOct", "Octave");
    setupToggle (chordsOn, "chordsOn", "Chords");
    setupCombo (chordProg, "chordProg", "Progression");
    setupCombo (chordStyle, "chordStyle", "Rhythm");
    setupSlider (chordOct, "chordOct", "Octave");
    setupToggle (sevenths, "sevenths", "7ths");
    setupToggle (leadOn, "leadOn", "Lead");
    setupCombo (leadStyle, "leadStyle", "Style");
    setupSlider (leadOct, "leadOct", "Octave");
    setupSlider (leadBusy, "leadBusy", "Busy");
    setupSlider (leadSeed, "leadSeed", "Variation");
    setupCombo (sketchStep, "sketchStep", "Step");

    addAndMakeVisible (sketchLabel);
    addAndMakeVisible (sketchBox);
    sketchBox.setTextToShowWhenEmpty ("Type notes: A3 C4 E4 - G4 . F4  ('.' rest, '-' hold). Wrong notes get fixed.", Colours::dim);
    sketchBox.setText (proc.getSketchText(), false);
    sketchBox.onTextChange = [this] { proc.setSketchText (sketchBox.getText()); };
    addAndMakeVisible (sketchFixes);
    sketchFixes.setColour (juce::Label::textColourId, Colours::gold);
    sketchFixes.setFont (juce::FontOptions (12.0f));

    addAndMakeVisible (patternLabel);
    patternLabel.setFont (juce::FontOptions (15.0f, juce::Font::bold));
    addAndMakeVisible (patternPick);
    patternPick.onChange = [this] { shownVersion = -1; refresh(); };
    addAndMakeVisible (patternView);
    patternView.setMultiLine (true, false);
    patternView.setReadOnly (true);
    patternView.setScrollbarsShown (true);
    patternView.setCaretVisible (false);
    patternView.setFont (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), 13.0f, juce::Font::plain));
    patternView.setColour (juce::TextEditor::backgroundColourId, juce::Colour (0xff101216));
    patternView.setColour (juce::TextEditor::textColourId, juce::Colour (0xffc8f0d8));

    for (auto* b : { &exportRenoise, &exportMidi, &exportTool })
        addAndMakeVisible (*b);
    addAndMakeVisible (dragMidi);
    exportRenoise.onClick = [this] {
        exportFile ("Save Renoise pattern file", "*.lua", proc.getSampleFile().getFileNameWithoutExtension() + ".zampler.lua",
                    [this] (const juce::File& f) { return proc.writeRenoiseFile (f); });
    };
    exportMidi.onClick = [this] {
        exportFile ("Save MIDI file", "*.mid", proc.getSampleFile().getFileNameWithoutExtension() + ".mid",
                    [this] (const juce::File& f) { return proc.writeMidiFile (f); });
    };
    exportTool.onClick = [this] {
        exportFile ("Save the Zampler Import tool for Renoise", "*.xrnt", "com.zampler.Import.xrnt",
                    [this] (const juce::File& f) { return proc.writeRenoiseTool (f); });
    };
    exportRenoise.setTooltip ("Load in Renoise with Tools > Zampler > Import Pattern File...");
    exportTool.setTooltip ("Drag the saved .xrnt onto Renoise once to install the import tool");

    setResizable (true, true);
    setResizeLimits (900, 640, 2400, 1800);
    setSize (1080, 760);
    startTimerHz (15);
    refresh();
}

ZamplerEditor::~ZamplerEditor() { stopTimer(); }

void ZamplerEditor::setupCombo (LabelledCombo& c, const juce::String& id, const juce::String& text)
{
    c.label.setText (text, juce::dontSendNotification);
    c.label.setColour (juce::Label::textColourId, Colours::dim);
    c.label.setFont (juce::FontOptions (12.0f));
    if (auto* p = dynamic_cast<juce::AudioParameterChoice*> (proc.params.getParameter (id)))
        c.box.addItemList (p->choices, 1);
    c.att = std::make_unique<ComboAtt> (proc.params, id, c.box);
    addAndMakeVisible (c.label);
    addAndMakeVisible (c.box);
}

void ZamplerEditor::setupSlider (LabelledSlider& s, const juce::String& id, const juce::String& text)
{
    s.label.setText (text, juce::dontSendNotification);
    s.label.setColour (juce::Label::textColourId, Colours::dim);
    s.label.setFont (juce::FontOptions (12.0f));
    s.slider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 44, 20);
    s.att = std::make_unique<SliderAtt> (proc.params, id, s.slider);
    addAndMakeVisible (s.label);
    addAndMakeVisible (s.slider);
}

void ZamplerEditor::setupToggle (Toggle& t, const juce::String& id, const juce::String& text)
{
    t.button.setButtonText (text);
    t.att = std::make_unique<ButtonAtt> (proc.params, id, t.button);
    addAndMakeVisible (t.button);
}

void ZamplerEditor::paint (juce::Graphics& g)
{
    g.fillAll (Colours::bg);
    auto top = getLocalBounds().removeFromTop (64).reduced (12, 8);
    g.setColour (Colours::text);
    g.setFont (juce::FontOptions (24.0f, juce::Font::bold));
    g.drawText ("ZAMPLER", top.removeFromLeft (150), juce::Justification::centredLeft);
    // drop zone outline
    auto drop = status.getBounds().getUnion (info.getBounds()).expanded (8, 4).toFloat();
    g.setColour (dragOver ? Colours::accent : Colours::edge);
    const float dashes[] = { 6.0f, 4.0f };
    juce::Path path;
    path.addRoundedRectangle (drop, 6.0f);
    juce::PathStrokeType (1.5f).createDashedStroke (path, path, dashes, 2);
    g.fillPath (path);
}

void ZamplerEditor::paintOverChildren (juce::Graphics& g)
{
    if (proc.isAnalysing())
    {
        auto drop = status.getBounds().getUnion (info.getBounds()).expanded (8, 4).toFloat();
        g.setColour (Colours::accent);
        g.fillRect (drop.getX(), drop.getBottom() - 3, drop.getWidth() * proc.analysisProgress(), 3.0f);
    }
}

void ZamplerEditor::resized()
{
    auto r = getLocalBounds().reduced (12);
    // header
    auto header = r.removeFromTop (48);
    header.removeFromLeft (150);
    openButton.setBounds (header.removeFromRight (130).reduced (0, 8));
    header.removeFromRight (12);
    auto dropArea = header.reduced (10, 2);
    status.setBounds (dropArea.removeFromTop (22));
    info.setBounds (dropArea);
    r.removeFromTop (10);

    // settings row
    auto row = r.removeFromTop (44);
    auto combo = [] (LabelledCombo& c, juce::Rectangle<int> a) {
        c.label.setBounds (a.removeFromTop (16));
        c.box.setBounds (a.removeFromTop (24));
    };
    auto slider = [] (LabelledSlider& s, juce::Rectangle<int> a) {
        s.label.setBounds (a.removeFromTop (16));
        s.slider.setBounds (a.removeFromTop (24));
    };
    combo (keyRoot, row.removeFromLeft (80));
    row.removeFromLeft (6);
    combo (scale, row.removeFromLeft (140));
    row.removeFromLeft (6);
    combo (detectMode, row.removeFromLeft (170));
    row.removeFromLeft (6);
    slider (maxPoly, row.removeFromLeft (120));
    row.removeFromLeft (6);
    slider (drumSens, row.removeFromLeft (120));
    row.removeFromLeft (6);
    combo (lpb, row.removeFromLeft (60));
    row.removeFromLeft (6);
    combo (patternLines, row.removeFromLeft (76));
    row.removeFromLeft (8);
    auto toggles = row;
    scaleLock.button.setBounds (toggles.removeFromTop (22).removeFromLeft (110));
    auto t2 = toggles;
    snapSample.button.setBounds (t2.removeFromLeft (110));
    preview.button.setBounds (t2.removeFromLeft (120));
    r.removeFromTop (8);

    noteMap.setBounds (r.removeFromTop (72));
    r.removeFromTop (6);

    // builders: 4 columns
    auto builders = r.removeFromTop (150);
    const int colW = (builders.getWidth() - 3 * 10) / 4;
    auto col = [&] { auto c = builders.removeFromLeft (colW); builders.removeFromLeft (10); return c; };
    {
        auto c = col();
        sampleOn.button.setBounds (c.removeFromTop (24));
        drumsOn.button.setBounds (c.removeFromTop (24));
        auto kinds = c.removeFromTop (24);
        kickOn.button.setBounds (kinds.removeFromLeft (colW / 3));
        snareOn.button.setBounds (kinds.removeFromLeft (colW / 3));
        hatOn.button.setBounds (kinds);
        c.removeFromTop (6);
        combo (sketchStep, c.removeFromTop (42));
    }
    {
        auto c = col();
        bassOn.button.setBounds (c.removeFromTop (24));
        combo (bassProg, c.removeFromTop (42));
        combo (bassStyle, c.removeFromTop (42));
        slider (bassOct, c.removeFromTop (42));
    }
    {
        auto c = col();
        auto top = c.removeFromTop (24);
        chordsOn.button.setBounds (top.removeFromLeft (100));
        sevenths.button.setBounds (top);
        combo (chordProg, c.removeFromTop (42));
        combo (chordStyle, c.removeFromTop (42));
        slider (chordOct, c.removeFromTop (42));
    }
    {
        auto c = col();
        leadOn.button.setBounds (c.removeFromTop (24));
        combo (leadStyle, c.removeFromTop (42));
        auto two = c.removeFromTop (42);
        slider (leadOct, two.removeFromLeft (two.getWidth() / 2));
        slider (leadBusy, two);
        slider (leadSeed, c.removeFromTop (42));
    }
    r.removeFromTop (8);
    auto sk = r.removeFromTop (26);
    sketchLabel.setBounds (sk.removeFromLeft (56));
    sketchBox.setBounds (sk);
    sketchFixes.setBounds (r.removeFromTop (20));
    r.removeFromTop (4);

    // exports at the bottom
    auto bottom = r.removeFromBottom (32);
    const int bw = (bottom.getWidth() - 30) / 4;
    for (auto* b : std::initializer_list<juce::Component*> { &exportRenoise, &exportMidi, &exportTool, &dragMidi })
    {
        b->setBounds (bottom.removeFromLeft (bw));
        bottom.removeFromLeft (10);
    }
    r.removeFromBottom (8);

    auto ph = r.removeFromTop (26);
    patternLabel.setBounds (ph.removeFromLeft (80));
    patternPick.setBounds (ph.removeFromLeft (140));
    r.removeFromTop (4);
    patternView.setBounds (r);
}

bool ZamplerEditor::isInterestedInFileDrag (const juce::StringArray& files)
{
    for (auto& f : files)
        if (juce::File (f).hasFileExtension ("wav;aif;aiff;flac;ogg;mp3"))
            return true;
    return false;
}

void ZamplerEditor::filesDropped (const juce::StringArray& files, int, int)
{
    dragOver = false;
    for (auto& f : files)
        if (juce::File (f).hasFileExtension ("wav;aif;aiff;flac;ogg;mp3"))
        {
            proc.loadSample (juce::File (f));
            break;
        }
    repaint();
}

void ZamplerEditor::openFile()
{
    chooser = std::make_unique<juce::FileChooser> ("Choose a sample", juce::File(), "*.wav;*.aif;*.aiff;*.flac;*.ogg;*.mp3");
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [this] (const juce::FileChooser& fc) {
                              if (fc.getResult().existsAsFile()) proc.loadSample (fc.getResult());
                          });
}

void ZamplerEditor::exportFile (const juce::String& title, const juce::String& pattern, const juce::String& defaultName,
                                std::function<juce::File (const juce::File&)> writer)
{
    auto start = proc.getSampleFile().existsAsFile() ? proc.getSampleFile().getParentDirectory()
                                                     : juce::File::getSpecialLocation (juce::File::userDocumentsDirectory);
    chooser = std::make_unique<juce::FileChooser> (title, start.getChildFile (defaultName), pattern);
    chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting,
                          [this, writer] (const juce::FileChooser& fc) {
                              const auto f = fc.getResult();
                              if (f == juce::File()) return;
                              const auto written = writer (f);
                              info.setText (written.existsAsFile() ? "Saved " + written.getFullPathName() : "Couldn't save " + f.getFullPathName(),
                                            juce::dontSendNotification);
                          });
}

void ZamplerEditor::timerCallback()
{
    const auto s = proc.getStatus();
    if (s != lastStatus)
    {
        lastStatus = s;
        status.setText (s, juce::dontSendNotification);
    }
    if (proc.isAnalysing()) repaint();
    if (proc.arrangementVersion.load() != shownVersion) refresh();
}

void ZamplerEditor::refresh()
{
    shownVersion = proc.arrangementVersion.load();
    const auto arr = proc.getArrangement();
    const auto a = proc.getAnalysis();
    if (! arr) return;

    if (a)
    {
        int kicks = 0, snares = 0, hats = 0;
        for (auto& h : a->drumHits) (h.kind == DrumKind::kick ? kicks : h.kind == DrumKind::snare ? snares : hats)++;
        int notes = 0, outOfKey = 0;
        for (auto& p : arr->parts)
            if (p.name == "Sample")
                for (auto& st : p.steps) { ++notes; outOfKey += arr->key.contains (st.pitch) ? 0 : 1; }
        juce::String txt;
        txt << juce::String (arr->grid.bpm, 1) << " BPM   |   " << arr->key.name() << " ("
            << juce::roundToInt (a->keyConfidence * 100) << "% sure)   |   " << notes << " notes"
            << (outOfKey ? " (" + juce::String (outOfKey) + " out of key)" : juce::String()) << "   |   "
            << (a->drumHits.empty() ? juce::String ("no drums")
                                    : juce::String (kicks) + " kick / " + juce::String (snares) + " snare / " + juce::String (hats) + " hat")
            << "   |   " << juce::String (arr->totalLines / arr->grid.linesPerBar()) << " bars";
        info.setText (txt, juce::dontSendNotification);
        std::vector<Chord> chords;
        for (auto& [t, c] : a->barChords) chords.push_back (c);
        noteMap.setData (arr->key, a->pitchProfile, chords);
    }
    else
    {
        info.setText ("Zampler maps it to tracker notes, then you build bass, chords and leads that can't hit a wrong note.",
                      juce::dontSendNotification);
        noteMap.setData (arr->key, {}, {});
    }

    juce::String fixes;
    for (auto& f : arr->sketchFixes) fixes << f << "    ";
    sketchFixes.setText (fixes, juce::dontSendNotification);

    // pattern picker
    auto* pl = dynamic_cast<juce::AudioParameterChoice*> (proc.params.getParameter ("patternLines"));
    const int lines = pl ? pl->getCurrentChoiceName().getIntValue() : 64;
    const int nPatterns = std::max (1, (arr->totalLines + lines - 1) / lines);
    const int sel = juce::jlimit (1, nPatterns, std::max (1, patternPick.getSelectedId()));
    if (patternPick.getNumItems() != nPatterns)
    {
        patternPick.clear (juce::dontSendNotification);
        for (int i = 1; i <= nPatterns; ++i) patternPick.addItem ("Pattern " + juce::String (i) + " / " + juce::String (nPatterns), i);
    }
    patternPick.setSelectedId (sel, juce::dontSendNotification);
    const auto text = arr->parts.empty() ? std::string ("(nothing yet: drop a sample)")
                                         : renderPattern (arr->parts, arr->grid, (sel - 1) * lines, lines);
    patternView.setText (text, false);
}
