#include "PluginProcessor.h"

#include "BinaryData.h"
#include "PluginEditor.h"
#include "PreviewSynth.h"

using namespace zampler;

namespace
{
juce::StringArray toStringArray (const std::vector<std::string>& v)
{
    juce::StringArray a;
    for (auto& s : v) a.add (s);
    return a;
}
const juce::StringArray kLpbChoices { "2", "4", "8", "16" };
const juce::StringArray kPatternChoices { "16", "32", "64", "128", "256", "512" };
const juce::StringArray kSketchSteps { "1/16", "1/8", "1/4" };

int choice (const juce::AudioProcessorValueTreeState& p, const char* id)
{
    return (int) p.getRawParameterValue (id)->load();
}
bool flag (const juce::AudioProcessorValueTreeState& p, const char* id)
{
    return p.getRawParameterValue (id)->load() > 0.5f;
}
float value (const juce::AudioProcessorValueTreeState& p, const char* id)
{
    return p.getRawParameterValue (id)->load();
}

/** MIDI channel (1-based) per part: drums on 10, others in order skipping 10. */
std::vector<int> partChannels (const std::vector<Part>& parts)
{
    std::vector<int> ch;
    int melodic = 0;
    for (auto& p : parts)
    {
        if (p.drums) ch.push_back (10);
        else
        {
            int c = melodic < 9 ? melodic + 1 : melodic + 2;
            ch.push_back (((c - 1) % 16) + 1);
            ++melodic;
        }
    }
    return ch;
}
} // namespace

juce::StringArray ZamplerProcessor::rootChoices()
{
    juce::StringArray a { "Detect" };
    for (auto* n : kNoteNames) a.add (n);
    return a;
}

juce::StringArray ZamplerProcessor::scaleChoices()
{
    juce::StringArray a { "As detected" };
    for (auto& s : scales()) a.add (s.name);
    return a;
}

juce::AudioProcessorValueTreeState::ParameterLayout ZamplerProcessor::createLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout l;
    auto addChoice = [&] (const char* id, const char* name, StringArray c, int def) {
        l.add (std::make_unique<AudioParameterChoice> (ParameterID { id, 1 }, name, c, def));
    };
    auto addBool = [&] (const char* id, const char* name, bool def) {
        l.add (std::make_unique<AudioParameterBool> (ParameterID { id, 1 }, name, def));
    };
    auto addInt = [&] (const char* id, const char* name, int lo, int hi, int def) {
        l.add (std::make_unique<AudioParameterInt> (ParameterID { id, 1 }, name, lo, hi, def));
    };
    auto addFloat = [&] (const char* id, const char* name, float def) {
        l.add (std::make_unique<AudioParameterFloat> (ParameterID { id, 1 }, name, 0.0f, 1.0f, def));
    };
    addChoice ("keyRoot", "Key root", rootChoices(), 0);
    addChoice ("scale", "Scale", scaleChoices(), 0);
    addBool ("scaleLock", "Scale lock", true);
    addBool ("snapSample", "Snap sample notes", false);
    addChoice ("detectMode", "Detect", { "Every note (chords too)", "Melody line only" }, 0);
    addInt ("maxPoly", "Max notes at once", 2, 8, 6);
    addFloat ("drumSens", "Drum sensitivity", 0.5f);
    addChoice ("lpb", "Lines per beat", kLpbChoices, 1);
    addChoice ("patternLines", "Pattern length", kPatternChoices, 2);

    addBool ("sampleOn", "Sample track", true);
    addBool ("drumsOn", "Drum guide", true);
    addBool ("kickOn", "Kick", true);
    addBool ("snareOn", "Snare", true);
    addBool ("hatOn", "Hat", true);

    addBool ("bassOn", "Bass", true);
    addChoice ("bassProg", "Bass progression", toStringArray (kProgressionNames), 0);
    addChoice ("bassStyle", "Bass style", toStringArray (kBassStyles), 1);
    addInt ("bassOct", "Bass octave", 0, 6, 2);

    addBool ("chordsOn", "Chords", true);
    addChoice ("chordProg", "Chord progression", toStringArray (kProgressionNames), 0);
    addChoice ("chordStyle", "Chord rhythm", toStringArray (kChordStyles), 0);
    addInt ("chordOct", "Chord octave", 1, 7, 3);
    addBool ("sevenths", "7ths", false);

    addBool ("leadOn", "Lead", false);
    addChoice ("leadStyle", "Lead style", toStringArray (kLeadStyles), 0);
    addInt ("leadOct", "Lead octave", 2, 8, 5);
    addFloat ("leadBusy", "Lead busy", 0.5f);
    addInt ("leadSeed", "Lead variation", 0, 999, 1);

    addChoice ("sketchStep", "Sketch step", kSketchSteps, 1);
    addBool ("preview", "Preview sound", true);
    return l;
}

ZamplerProcessor::ZamplerProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      params (*this, nullptr, "ZAMPLER", createLayout())
{
    formats.registerBasicFormats();
    for (int ch = 1; ch <= 16; ++ch) synth.addSound (new preview::ChannelSound (ch));
    for (int i = 0; i < 24; ++i) synth.addVoice (new preview::Voice());
    for (auto* p : getParameters())
        if (auto* wp = dynamic_cast<juce::AudioProcessorParameterWithID*> (p))
            params.addParameterListener (wp->paramID, this);
    rebuild();
}

ZamplerProcessor::~ZamplerProcessor()
{
    masterReference.clear();
    cancelPendingUpdate();
    cancel = true;
    if (worker.joinable()) worker.join();
}

bool ZamplerProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    return out == juce::AudioChannelSet::stereo() || out == juce::AudioChannelSet::mono();
}

void ZamplerProcessor::prepareToPlay (double sampleRate, int)
{
    sr = sampleRate;
    synth.setCurrentPlaybackSampleRate (sampleRate);
}

// ------------------------------------------------------------------------------------ analysis

AnalysisOptions ZamplerProcessor::currentOptions() const
{
    AnalysisOptions o;
    o.maxPoly = (int) value (params, "maxPoly");
    o.drumSensitivity = value (params, "drumSens");
    return o;
}

void ZamplerProcessor::loadSample (const juce::File& file)
{
    sampleFile = file;
    startAnalysis();
}

void ZamplerProcessor::startAnalysis()
{
    if (! sampleFile.existsAsFile()) return;
    cancel = true;
    if (worker.joinable()) worker.join();
    cancel = false;
    analysing = true;
    progress = 0.0f;
    lastError.clear();
    const auto opts = currentOptions();
    analysedMaxPoly = opts.maxPoly;
    analysedSensitivity = (float) opts.drumSensitivity;
    const auto file = sampleFile;
    juce::WeakReference<ZamplerProcessor> weak (this);
    worker = std::thread ([this, weak, file, opts] {
        std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));
        if (reader == nullptr)
        {
            juce::MessageManager::callAsync ([weak, name = file.getFileName()] {
                if (auto* self = weak.get())
                {
                    self->lastError = "Couldn't read " + name;
                    self->analysing = false;
                }
            });
            return;
        }
        const auto maxLen = (juce::int64) (opts.maxSeconds * reader->sampleRate);
        const int len = (int) std::min (reader->lengthInSamples, maxLen);
        juce::AudioBuffer<float> buf ((int) reader->numChannels, len);
        reader->read (&buf, 0, len, 0, true, true);
        dsp::Signal mono ((size_t) len, 0.0f);
        for (int c = 0; c < buf.getNumChannels(); ++c)
            for (int i = 0; i < len; ++i) mono[(size_t) i] += buf.getSample (c, i) / (float) buf.getNumChannels();
        auto result = std::make_shared<const Analysis> (zampler::analyze (mono, reader->sampleRate, opts, &progress, &cancel));
        if (cancel) return;
        // Publish right here (not via the message thread): hosts don't always
        // pump a plugin's message queue while its editor is closed.
        std::atomic_store (&analysis, std::shared_ptr<const Analysis> (result));
        analysing = false;
        buildArrangement();
        juce::MessageManager::callAsync ([weak, result] {
            if (auto* self = weak.get())
                self->rebuild(); // re-analyse if settings changed meanwhile
        });
    });
}

juce::String ZamplerProcessor::getStatus() const
{
    if (analysing) return "Listening... " + juce::String (juce::roundToInt (progress * 100)) + "%";
    if (lastError.isNotEmpty()) return lastError;
    if (! sampleFile.existsAsFile()) return "Drop a sample here (wav, aiff, flac, ogg, mp3)";
    return sampleFile.getFileName();
}

void ZamplerProcessor::setSketchText (const juce::String& text)
{
    {
        const juce::ScopedLock sl (sketchLock);
        sketchText = text;
    }
    rebuild();
}

juce::String ZamplerProcessor::getSketchText() const
{
    const juce::ScopedLock sl (sketchLock);
    return sketchText;
}

void ZamplerProcessor::rebuild()
{
    // Settings that change the analysis itself trigger a re-analysis.
    if (! analysing && sampleFile.existsAsFile() && getAnalysis() != nullptr
        && ((int) value (params, "maxPoly") != analysedMaxPoly || std::abs (value (params, "drumSens") - analysedSensitivity) > 1e-4f))
    {
        startAnalysis();
        return;
    }
    buildArrangement();
}

void ZamplerProcessor::buildArrangement()
{
    const juce::ScopedLock building (buildLock);
    const auto sketchNow = getSketchText();
    auto arr = std::make_shared<Arrangement>();
    const auto a = getAnalysis();
    const int lpb = kLpbChoices[choice (params, "lpb")].getIntValue();
    arr->grid = Grid { a ? a->bpm : 120.0, lpb, 4 };
    Key key = a ? a->key : Key {};
    if (const int r = choice (params, "keyRoot"); r > 0) key.root = r - 1;
    if (const int s = choice (params, "scale"); s > 0) key.scale = scales()[(size_t) s - 1].name;
    arr->key = key;
    const bool lock = flag (params, "scaleLock"), snapSample = flag (params, "snapSample");
    const auto& g = arr->grid;

    std::vector<Step> melody;
    Part sample { "Sample", {}, 0, false }, drums { "Drums", {}, 5, true };
    std::vector<Chord> sampleChords;
    std::vector<int> kicks;
    if (a)
    {
        melody = quantize (a->notes, g, &key, snapSample);
        const bool poly = choice (params, "detectMode") == 0 && ! a->polyNotes.empty();
        sample.steps = poly ? quantizePoly (a->polyNotes, g, &key, snapSample) : melody;
        drums = drumPart (a->drumHits, g, flag (params, "kickOn"), flag (params, "snareOn"), flag (params, "hatOn"));
        for (auto& [t, c] : a->barChords) sampleChords.push_back (c);
        kicks = kickLines (a->drumHits, g);
    }
    const int bars = std::max (1, songLength ({ sample, drumPart (a ? a->drumHits : std::vector<DrumHit> {}, g) }, g) / g.linesPerBar());

    if (flag (params, "sampleOn") && ! sample.steps.empty()) arr->parts.push_back (sample);
    if (flag (params, "drumsOn") && ! drums.steps.empty()) arr->parts.push_back (drums);
    auto prog = [&] (const char* id, bool sev) {
        return progression (key, kProgressionNames[(size_t) choice (params, id)], bars, sampleChords, sev);
    };
    if (a && flag (params, "bassOn"))
        arr->parts.push_back (bassline (key, prog ("bassProg", false), g, kBassStyles[(size_t) choice (params, "bassStyle")],
                                        (int) value (params, "bassOct"), 110, lock, kicks));
    if (a && flag (params, "chordsOn"))
        arr->parts.push_back (chordPart (key, prog ("chordProg", flag (params, "sevenths")), g,
                                         kChordStyles[(size_t) choice (params, "chordStyle")], (int) value (params, "chordOct"), 90, lock));
    if (a && flag (params, "leadOn"))
        arr->parts.push_back (lead (key, prog ("chordProg", false), g, kLeadStyles[(size_t) choice (params, "leadStyle")],
                                    (int) value (params, "leadOct"), value (params, "leadBusy"),
                                    (uint32_t) value (params, "leadSeed"), melody));
    if (sketchNow.trim().isNotEmpty()) // works even before a sample is loaded
    {
        const int step = std::max (1, std::array<int, 3> { lpb / 4, lpb / 2, lpb }[(size_t) choice (params, "sketchStep")]);
        arr->parts.push_back (sketch (sketchNow.toStdString(), key, g, step, lock, &arr->sketchFixes));
    }
    arr->totalLines = songLength (arr->parts, g);
    std::atomic_store (&arrangement, std::shared_ptr<const Arrangement> (arr));
    ++arrangementVersion;
}

// ------------------------------------------------------------------------------------ playback

void ZamplerProcessor::stopAllNotes (juce::MidiBuffer& out, int offset)
{
    for (int ch = 0; ch < 16; ++ch)
        for (int n = 0; n < 128; ++n)
            if (noteOn[(size_t) (ch * 128 + n)])
            {
                out.addEvent (juce::MidiMessage::noteOff (ch + 1, n), offset);
                noteOn[(size_t) (ch * 128 + n)] = false;
            }
}

void ZamplerProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    buffer.clear();
    const int n = buffer.getNumSamples();
    scratch.clear();
    scratch.addEvents (midi, 0, n, 0); // pass incoming MIDI through (and let you audition)

    bool playing = false;
    double ppq = 0, bpm = 120;
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
        {
            playing = pos->getIsPlaying();
            if (auto p = pos->getPpqPosition()) ppq = *p;
            if (auto b = pos->getBpm()) bpm = *b;
        }

    if (! playing && wasPlaying) stopAllNotes (scratch, 0);
    const auto arr = getArrangement();
    if (playing && arr && ! arr->parts.empty())
    {
        const double ppqPerSample = bpm / 60.0 / sr;
        const double ppqEnd = ppq + n * ppqPerSample;
        if (lastPpqEnd >= 0 && std::abs (ppq - lastPpqEnd) > 1e-3) stopAllNotes (scratch, 0); // jumped
        const double lpb = arr->grid.lpb;
        const double loopLen = arr->totalLines / lpb;
        const auto channels = partChannels (arr->parts);
        auto emit = [&] (double eventPpq, int ch, int note, int vel) {
            // all loop repetitions of eventPpq that fall in [ppq, ppqEnd)
            const long k0 = (long) std::floor ((ppq - eventPpq) / loopLen);
            for (long k = std::max (0L, k0); ; ++k)
            {
                const double t = eventPpq + (double) k * loopLen;
                if (t >= ppqEnd) break;
                if (t < ppq) continue;
                const int off = juce::jlimit (0, n - 1, (int) ((t - ppq) / ppqPerSample));
                const size_t idx = (size_t) ((ch - 1) * 128 + note);
                if (vel > 0)
                {
                    if (noteOn[idx]) scratch.addEvent (juce::MidiMessage::noteOff (ch, note), off);
                    scratch.addEvent (juce::MidiMessage::noteOn (ch, note, (juce::uint8) vel), off);
                    noteOn[idx] = true;
                }
                else if (noteOn[idx])
                {
                    scratch.addEvent (juce::MidiMessage::noteOff (ch, note), off);
                    noteOn[idx] = false;
                }
            }
        };
        for (size_t i = 0; i < arr->parts.size(); ++i)
            for (auto& s : arr->parts[i].steps)
            {
                if (s.pitch < 0 || s.pitch > 127) continue;
                const double end = arr->parts[i].drums ? s.line / lpb + 0.5 / lpb : s.end() / lpb;
                emit (end >= loopLen ? end - loopLen : end, channels[i], s.pitch, 0);   // offs first
                emit (s.line / lpb, channels[i], s.pitch, juce::jlimit (1, 127, s.velocity));
            }
        lastPpqEnd = ppqEnd;
    }
    else
        lastPpqEnd = -1;
    wasPlaying = playing;

    if (flag (params, "preview"))
        synth.renderNextBlock (buffer, scratch, 0, n);
    midi.swapWith (scratch);
}

// ------------------------------------------------------------------------------------ exports

juce::File ZamplerProcessor::writeMidiFile (const juce::File& target) const
{
    auto arr = getArrangement();
    if (! arr) return {};
    const auto bytes = toMidiFile (arr->parts, arr->grid);
    target.replaceWithData (bytes.data(), bytes.size());
    return target;
}

juce::File ZamplerProcessor::writeRenoiseFile (const juce::File& target) const
{
    auto arr = getArrangement();
    if (! arr) return {};
    const int lines = kPatternChoices[choice (params, "patternLines")].getIntValue();
    target.replaceWithText (toRenoise (arr->parts, arr->grid, lines, sampleFile.getFileNameWithoutExtension().toStdString()));
    return target;
}

juce::File ZamplerProcessor::writeRenoiseTool (const juce::File& target) const
{
    juce::ZipFile::Builder zip;
    zip.addEntry (new juce::MemoryInputStream (BinaryData::manifest_xml, (size_t) BinaryData::manifest_xmlSize, false), 9,
                  "manifest.xml", juce::Time::getCurrentTime());
    zip.addEntry (new juce::MemoryInputStream (BinaryData::main_lua, (size_t) BinaryData::main_luaSize, false), 9,
                  "main.lua", juce::Time::getCurrentTime());
    target.deleteFile();
    juce::FileOutputStream out (target);
    if (! out.openedOk() || ! zip.writeToStream (out, nullptr)) return {};
    return target;
}

juce::File ZamplerProcessor::tempMidiForDrag() const
{
    auto name = sampleFile.existsAsFile() ? sampleFile.getFileNameWithoutExtension() : juce::String ("zampler");
    auto f = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("Zampler").getChildFile (name + ".mid");
    f.getParentDirectory().createDirectory();
    return writeMidiFile (f);
}

// ------------------------------------------------------------------------------------ state

void ZamplerProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    auto state = params.copyState();
    state.setProperty ("sample", sampleFile.getFullPathName(), nullptr);
    state.setProperty ("sketch", getSketchText(), nullptr);
    if (auto xml = state.createXml()) copyXmlToBinary (*xml, dest);
}

void ZamplerProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
    {
        auto state = juce::ValueTree::fromXml (*xml);
        if (! state.isValid()) return;
        params.replaceState (state);
        {
            const juce::ScopedLock sl (sketchLock);
            sketchText = state.getProperty ("sketch").toString();
        }
        const juce::File f (state.getProperty ("sample").toString());
        if (f.existsAsFile()) loadSample (f);
        else rebuild();
    }
}

juce::AudioProcessorEditor* ZamplerProcessor::createEditor() { return new ZamplerEditor (*this); }

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new ZamplerProcessor(); }
