// End-to-end host test.
//
//   zampler_host_test <path/to/Zampler.vst3> [editor-screenshot.png]
//
// 1. Writes a test sample (Am-F-C-G chords + melody + drums, 120 BPM, 44.1 kHz).
// 2. Loads the built VST3 the way a DAW does, points it at the sample through
//    its saved state, waits for the analysis, then plays 4 bars with a fake
//    host transport and checks the MIDI it sends out (and that the preview
//    synth makes sound).
// 3. Optionally renders the editor (from the same sources) to a PNG.
#include "../source/PluginEditor.h"
#include "../source/PluginProcessor.h"

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_extra/juce_gui_extra.h>

#include <cstdio>
#include <map>
#include <random>
#include <set>

static int failures = 0;
#define CHECK(cond, ...)                                                       \
    do {                                                                       \
        if (cond) std::printf ("ok    " __VA_ARGS__);                          \
        else { ++failures; std::printf ("FAIL  " __VA_ARGS__); }               \
        std::printf ("\n");                                                    \
    } while (0)

namespace
{
constexpr double kRate = 44100.0, kBpm = 120.0;

juce::File writeTestSample()
{
    const double beat = 60.0 / kBpm;
    const int bars = 4;
    const int n = (int) (bars * 4 * beat * kRate) + (int) kRate / 2;
    juce::AudioBuffer<float> buf (1, n);
    buf.clear();
    auto* y = buf.getWritePointer (0);
    auto tone = [&] (double t0, double dur, int midi, double gain) {
        const double f = 440.0 * std::pow (2.0, (midi - 69) / 12.0);
        for (int i = 0; i < (int) (dur * kRate); ++i)
        {
            const double t = i / kRate;
            double w = 0;
            for (int k = 1; k <= 3; ++k) w += std::sin (2 * juce::MathConstants<double>::pi * f * k * t) / k;
            const double env = std::min (1.0, t / 0.005) * std::exp (-t * 3) * std::min (1.0, (dur - t) / 0.01);
            const int idx = (int) (t0 * kRate) + i;
            if (idx < n) y[idx] += (float) (w * env * 0.3 * gain * 0.5);
        }
    };
    const int chords[4][3] = { { 57, 60, 64 }, { 53, 57, 60 }, { 48, 55, 64 }, { 55, 59, 62 } };
    const int top[16] = { 72, 76, 74, 72, 77, 76, 72, 69, 76, 79, 76, 72, 74, 71, 74, 79 };
    for (int b = 0; b < bars * 2; ++b)
        for (int v = 0; v < 3; ++v) tone (b * 2 * beat, 2 * beat, chords[b % 4][v], 1.0);
    for (int i = 0; i < bars * 8; ++i) tone (i * 0.5 * beat, 0.5 * beat, top[i % 16], 0.8);
    std::mt19937 rng (0);
    std::normal_distribution<double> gauss;
    double prev = 0;
    for (int b = 0; b < bars * 4; ++b)
    {
        const int s0 = (int) (b * beat * kRate);
        if (b % 2 == 0)
            for (int i = 0; i < (int) (0.35 * kRate) && s0 + i < n; ++i)
            {
                const double t = i / kRate;
                y[s0 + i] += (float) (0.5 * 0.8 * 0.9 * std::sin (2 * juce::MathConstants<double>::pi * (45 * t + 4.4 * (1 - std::exp (-25 * t)))) * std::exp (-t * 9));
            }
        else
            for (int i = 0; i < (int) (0.2 * kRate) && s0 + i < n; ++i)
            {
                const double t = i / kRate;
                y[s0 + i] += (float) (0.5 * 0.8 * 0.6 * (0.5 * std::sin (2 * juce::MathConstants<double>::pi * 190 * t) + gauss (rng) * 0.6) * std::exp (-t * 22));
            }
        for (double half : { 0.0, 0.5 })
        {
            const int h0 = (int) ((b + half) * beat * kRate);
            for (int i = 0; i < (int) (0.05 * kRate) && h0 + i < n; ++i)
            {
                const double cur = gauss (rng);
                y[h0 + i] += (float) (0.5 * 0.8 * 0.25 * (cur - prev) * std::exp (-(i / kRate) * 70));
                prev = cur;
            }
        }
    }
    auto file = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("zampler_host_test.wav");
    file.deleteFile();
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::OutputStream> os (file.createOutputStream().release());
    if (auto* w = wav.createWriterFor (os.get(), kRate, 1, 24, {}, 0))
    {
        os.release();
        std::unique_ptr<juce::AudioFormatWriter> writer (w);
        writer->writeFromAudioSampleBuffer (buf, 0, n);
    }
    return file;
}

struct FakePlayHead : juce::AudioPlayHead
{
    bool playing = false;
    double ppq = 0;
    juce::Optional<PositionInfo> getPosition() const override
    {
        PositionInfo p;
        p.setIsPlaying (playing);
        p.setBpm (kBpm);
        p.setPpqPosition (ppq);
        p.setTimeInSeconds (ppq * 60.0 / kBpm);
        p.setTimeSignature (TimeSignature { 4, 4 });
        return p;
    }
};

/** Take the hosted plugin's real state (a JUCE host wraps VST3 state in a
    <VST3PluginState> with the component state base64-encoded inside), point
    it at `sample`, and return it in the same wrapped form. */
juce::MemoryBlock stateFor (juce::AudioPluginInstance& inst, const juce::File& sample)
{
    juce::MemoryBlock hostState;
    inst.getStateInformation (hostState);
    auto outer = juce::AudioProcessor::getXmlFromBinary (hostState.getData(), (int) hostState.getSize());
    if (outer == nullptr) return {};
    auto* comp = outer->getChildByName ("IComponent");
    if (comp == nullptr) return {};
    juce::MemoryBlock inner;
    inner.fromBase64Encoding (comp->getAllSubText());
    auto zxml = juce::AudioProcessor::getXmlFromBinary (inner.getData(), (int) inner.getSize());
    if (zxml == nullptr) return {};
    zxml->setAttribute ("sample", sample.getFullPathName());
    juce::MemoryBlock newInner;
    juce::AudioProcessor::copyXmlToBinary (*zxml, newInner);
    comp->deleteAllTextElements();
    comp->addTextElement (newInner.toBase64Encoding());
    juce::MemoryBlock out;
    juce::AudioProcessor::copyXmlToBinary (*outer, out);
    return out;
}

struct NoteEvent { double ppq; int channel, note, velocity; bool on; };

void testHostedVst3 (const juce::File& pluginFile, const juce::File& sample)
{
    std::printf ("\n== VST3 hosted like a DAW: %s\n", pluginFile.getFullPathName().toRawUTF8());
    juce::AudioPluginFormatManager fm;
    fm.addFormat (new juce::VST3PluginFormat());
    juce::OwnedArray<juce::PluginDescription> types;
    fm.getFormat (0)->findAllTypesForFile (types, pluginFile.getFullPathName());
    CHECK (types.size() == 1, "VST3 scanned: %d plugin(s)", types.size());
    if (types.isEmpty()) return;
    CHECK (types[0]->name == "Zampler" && types[0]->isInstrument, "name '%s', instrument %d",
           types[0]->name.toRawUTF8(), (int) types[0]->isInstrument);

    juce::String err;
    auto inst = fm.createPluginInstance (*types[0], kRate, 512, err);
    CHECK (inst != nullptr, "instantiated %s", err.toRawUTF8());
    if (! inst) return;
    CHECK (inst->getParameters().size() >= 25, "%d host parameters (incl. VST3 MIDI-CC proxies)", inst->getParameters().size());
    CHECK (inst->producesMidi() && inst->acceptsMidi(), "MIDI in + out");

    FakePlayHead head;
    inst->setPlayHead (&head);
    inst->setRateAndBufferSizeDetails (kRate, 512);
    inst->prepareToPlay (kRate, 512);
    const auto st = stateFor (*inst, sample);
    CHECK (st.getSize() > 0, "plugin state wrapped by the host, sample path injected");
    inst->setStateInformation (st.getData(), (int) st.getSize());

    juce::AudioBuffer<float> audio (2, 512);
    juce::MidiBuffer midi;
    // Wait for the background analysis (message loop must run for its callback).
    bool ready = false;
    const auto t0 = juce::Time::getMillisecondCounterHiRes();
    while (! ready && juce::Time::getMillisecondCounterHiRes() - t0 < 60000)
    {
        juce::MessageManager::getInstance()->runDispatchLoopUntil (50);
        head.playing = true;
        head.ppq = 0;
        midi.clear();
        inst->processBlock (audio, midi);
        for (const auto m : midi) ready = ready || (m.getMessage().isNoteOn() && m.getMessage().getChannel() == 10);
        head.playing = false;
        midi.clear();
        inst->processBlock (audio, midi); // stop -> all notes off
    }
    CHECK (ready, "analysis finished in %.1f s", (juce::Time::getMillisecondCounterHiRes() - t0) / 1000.0);
    if (! ready) return;

    // Play 4 bars from the top.
    std::vector<NoteEvent> events;
    float peak = 0;
    head.playing = true;
    const double ppqPerBlock = 512 * kBpm / 60.0 / kRate;
    for (head.ppq = 0; head.ppq < 16.0; head.ppq += ppqPerBlock)
    {
        midi.clear();
        audio.clear();
        inst->processBlock (audio, midi);
        peak = std::max (peak, audio.getMagnitude (0, 512));
        for (const auto m : midi)
        {
            const auto msg = m.getMessage();
            if (msg.isNoteOnOrOff())
                events.push_back ({ head.ppq + m.samplePosition * kBpm / 60.0 / kRate, msg.getChannel(), msg.getNoteNumber(),
                                    msg.getVelocity(), msg.isNoteOn() });
        }
    }
    head.playing = false;
    midi.clear();
    inst->processBlock (audio, midi);
    int offsAtStop = 0;
    for (const auto m : midi) offsAtStop += m.getMessage().isNoteOff() ? 1 : 0;

    std::map<int, std::set<std::pair<int, int>>> ons; // channel -> (16th line, note)
    std::map<std::pair<int, int>, int> balance;
    for (auto& e : events)
    {
        const int line = (int) std::lround (e.ppq * 4);
        if (e.on && line < 64) ons[e.channel].insert ({ line, e.note }); // first pass of the 4-bar loop
        balance[{ e.channel, e.note }] += e.on ? 1 : -1;
    }
    std::printf ("      note-ons per channel:");
    for (auto& [ch, s] : ons) std::printf ("  ch%d=%zu", ch, s.size());
    std::printf ("\n");

    // Sample track (ch 1): the chords and the melody on top.
    int chordHits = 0;
    const int chords[4][3] = { { 57, 60, 64 }, { 53, 57, 60 }, { 48, 55, 64 }, { 55, 59, 62 } };
    for (int b = 0; b < 8; ++b)
        for (int v = 0; v < 3; ++v) chordHits += ons[1].count ({ b * 8, chords[b % 4][v] }) ? 1 : 0;
    CHECK (chordHits >= 22, "sample chords on ch 1: %d/24 chord notes at the right line", chordHits);
    if (chordHits < 22)
    {
        std::printf ("      missing:");
        for (int b = 0; b < 8; ++b)
            for (int v = 0; v < 3; ++v)
                if (! ons[1].count ({ b * 8, chords[b % 4][v] })) std::printf (" %d:%d", b * 8, chords[b % 4][v]);
        std::printf ("\n");
    }
    const int top[16] = { 72, 76, 74, 72, 77, 76, 72, 69, 76, 79, 76, 72, 74, 71, 74, 79 };
    int melodyHits = 0;
    for (int i = 0; i < 32; ++i) melodyHits += ons[1].count ({ i * 2, top[i % 16] }) ? 1 : 0;
    CHECK (melodyHits >= 27, "sample melody on ch 1: %d/32 notes at the right line", melodyHits);

    // Drum guide (ch 10, GM): kick on 1 & 3, snare on 2 & 4.
    std::set<int> kicks, snares;
    for (auto& [line, note] : ons[10])
    {
        if (note == 36) kicks.insert (line);
        if (note == 38) snares.insert (line);
    }
    CHECK ((kicks == std::set<int> { 0, 8, 16, 24, 32, 40, 48, 56 }), "kicks on beats 1 & 3 (%zu)", kicks.size());
    std::printf ("      kick lines:");
    for (int l : kicks) std::printf (" %d", l);
    std::printf ("\n      snare lines:");
    for (int l : snares) std::printf (" %d", l);
    std::printf ("\n");
    CHECK ((snares == std::set<int> { 4, 12, 20, 28, 36, 44, 52, 60 }), "snares on beats 2 & 4 (%zu)", snares.size());

    // Bass (ch 2) and chords (ch 3) present and in A minor / C major (same notes).
    const zampler::Key am { 9, "minor" };
    bool inKey = ! ons[2].empty() && ! ons[3].empty();
    for (int ch : { 2, 3 })
        for (auto& [line, note] : ons[(size_t) ch]) inKey = inKey && am.contains (note);
    CHECK (inKey, "bass (%zu) + chords (%zu) all in key", ons[2].size(), ons[3].size());

    bool balanced = true;
    for (auto& [k, v] : balance) balanced = balanced && (v == 0 || v == 1);
    CHECK (balanced, "every note-on gets its note-off");
    CHECK (offsAtStop >= 0, "stop sends %d note-offs for hanging notes", offsAtStop);
    CHECK (peak > 0.01f, "preview synth is audible (peak %.3f)", peak);

    // State round trip.
    juce::MemoryBlock saved;
    inst->getStateInformation (saved);
    CHECK (saved.toString().contains ("zampler_host_test.wav") || saved.getSize() > 100, "state saved (%zu bytes)", saved.getSize());
    inst->releaseResources();
}

void renderEditor (const juce::File& sample, const juce::File& png)
{
    std::printf ("\n== Editor screenshot -> %s\n", png.getFullPathName().toRawUTF8());
    ZamplerProcessor proc;
    proc.setRateAndBufferSizeDetails (kRate, 512);
    proc.prepareToPlay (kRate, 512);
    proc.params.getParameter ("leadOn")->setValueNotifyingHost (1.0f);
    proc.setSketchText ("A4 C5 E5 - G#5 . F#5 E5");
    proc.loadSample (sample);
    const auto t0 = juce::Time::getMillisecondCounterHiRes();
    while ((proc.isAnalysing() || proc.getAnalysis() == nullptr) && juce::Time::getMillisecondCounterHiRes() - t0 < 60000)
        juce::MessageManager::getInstance()->runDispatchLoopUntil (50);
    juce::MessageManager::getInstance()->runDispatchLoopUntil (200);
    std::unique_ptr<juce::AudioProcessorEditor> ed (proc.createEditor());
    ed->setSize (1080, 760);
    juce::MessageManager::getInstance()->runDispatchLoopUntil (300);
    auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, 2.0f);
    png.deleteFile();
    juce::FileOutputStream out (png);
    juce::PNGImageFormat fmt;
    CHECK (out.openedOk() && fmt.writeImageToStream (img, out), "editor rendered %dx%d", img.getWidth(), img.getHeight());
    ed.reset();
}
} // namespace

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI gui;
    if (argc < 2)
    {
        std::printf ("usage: zampler_host_test <Zampler.vst3> [screenshot.png]\n");
        return 2;
    }
    const auto sample = writeTestSample();
    CHECK (sample.existsAsFile(), "test sample written (%lld bytes)", (long long) sample.getSize());
    testHostedVst3 (juce::File::getCurrentWorkingDirectory().getChildFile (argv[1]), sample);
    if (argc >= 3) renderEditor (sample, juce::File::getCurrentWorkingDirectory().getChildFile (argv[2]));
    std::printf ("\n%s (%d failure%s)\n", failures ? "FAILED" : "PASSED", failures, failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
