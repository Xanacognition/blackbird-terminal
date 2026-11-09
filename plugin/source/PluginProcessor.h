#pragma once

#include "Tracker.h"

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>

#include <atomic>
#include <memory>
#include <thread>

/** Everything the editor and the audio thread need to show / play the result. */
struct Arrangement
{
    zampler::Grid grid;
    zampler::Key key;
    std::vector<zampler::Part> parts;
    std::vector<std::string> sketchFixes;
    int totalLines = 16;
};

class ZamplerProcessor : public juce::AudioProcessor,
                         private juce::AudioProcessorValueTreeState::Listener,
                         private juce::AsyncUpdater
{
public:
    ZamplerProcessor();
    ~ZamplerProcessor() override;

    // --- AudioProcessor
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    using juce::AudioProcessor::processBlock;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "Zampler"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return true; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.5; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    // --- Zampler
    juce::AudioProcessorValueTreeState params;

    /** Load + analyse a file on a background thread. */
    void loadSample (const juce::File& file);
    bool isAnalysing() const { return analysing.load(); }
    float analysisProgress() const { return progress.load(); }
    juce::String getStatus() const;
    juce::File getSampleFile() const { return sampleFile; }

    std::shared_ptr<const zampler::Analysis> getAnalysis() const { return std::atomic_load (&analysis); }
    std::shared_ptr<const Arrangement> getArrangement() const { return std::atomic_load (&arrangement); }

    void setSketchText (const juce::String& text);
    juce::String getSketchText() const;

    /** Re-analyse if needed, then rebuild the parts (message thread). */
    void rebuild();
    /** Build + publish the parts from the analysis and current settings.
        Thread-safe: also called from the analysis thread, so playback never
        depends on the host pumping our message queue (no editor open). */
    void buildArrangement();
    /** Bumped every time a new arrangement is published (editor polls it). */
    std::atomic<int> arrangementVersion { 0 };

    // exports
    juce::File writeMidiFile (const juce::File& target) const;
    juce::File writeRenoiseFile (const juce::File& target) const;
    juce::File writeRenoiseTool (const juce::File& target) const;
    /** A temp .mid for drag-and-drop out of the plugin. */
    juce::File tempMidiForDrag() const;

    static juce::StringArray rootChoices();
    static juce::StringArray scaleChoices();

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    void parameterChanged (const juce::String&, float) override { triggerAsyncUpdate(); }
    void handleAsyncUpdate() override { rebuild(); }
    void startAnalysis();
    zampler::AnalysisOptions currentOptions() const;

    juce::AudioFormatManager formats;
    juce::File sampleFile;
    juce::String sketchText;
    mutable juce::CriticalSection sketchLock;
    juce::CriticalSection buildLock;
    juce::String lastError;

    std::shared_ptr<const zampler::Analysis> analysis;
    std::shared_ptr<const Arrangement> arrangement;
    std::thread worker;
    std::atomic<bool> analysing { false }, cancel { false };
    std::atomic<float> progress { 0.0f };
    int analysedMaxPoly = -1;
    float analysedSensitivity = -1.0f;

    // playback / preview
    juce::Synthesiser synth;
    std::array<bool, 16 * 128> noteOn {};
    double sr = 44100.0;
    bool wasPlaying = false;
    double lastPpqEnd = -1.0;
    juce::MidiBuffer scratch;

    void stopAllNotes (juce::MidiBuffer& out, int sampleOffset);

    JUCE_DECLARE_WEAK_REFERENCEABLE (ZamplerProcessor)
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ZamplerProcessor)
};
