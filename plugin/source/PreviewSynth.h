// A tiny built-in synth so the generated parts are audible straight away in
// the host (the real sound design happens on your own instruments / samples).
#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

#include <cmath>
#include <random>

namespace preview
{

/** One sound per MIDI channel, so a voice knows which part it's playing. */
struct ChannelSound : juce::SynthesiserSound
{
    explicit ChannelSound (int ch) : channel (ch) {}
    bool appliesToNote (int) override { return true; }
    bool appliesToChannel (int midiChannel) override { return midiChannel == channel; }
    const int channel; // 1-based
};

class Voice : public juce::SynthesiserVoice
{
public:
    bool canPlaySound (juce::SynthesiserSound* s) override { return dynamic_cast<ChannelSound*> (s) != nullptr; }

    void startNote (int note, float velocity, juce::SynthesiserSound* s, int) override
    {
        channel = static_cast<ChannelSound*> (s)->channel;
        level = velocity * 0.18f;
        freq = juce::MidiMessage::getMidiNoteInHertz (note);
        drumNote = note;
        phase = 0;
        t = 0;
        lp = 0;
        juce::ADSR::Parameters p;
        if (channel == 10)       p = { 0.001f, 0.25f, 0.0f, 0.05f };
        else if (channel == 2)   p = { 0.003f, 0.25f, 0.6f, 0.08f };   // bass
        else if (channel == 3)   p = { 0.02f, 0.4f, 0.5f, 0.25f };     // chords
        else                     p = { 0.005f, 0.2f, 0.55f, 0.12f };
        adsr.setSampleRate (getSampleRate());
        adsr.setParameters (p);
        adsr.noteOn();
    }

    void stopNote (float, bool allowTailOff) override
    {
        if (allowTailOff) adsr.noteOff();
        else { adsr.reset(); clearCurrentNote(); }
    }

    void pitchWheelMoved (int) override {}
    void controllerMoved (int, int) override {}

    void renderNextBlock (juce::AudioBuffer<float>& out, int start, int num) override
    {
        if (! isVoiceActive()) return;
        const double srate = getSampleRate();
        for (int i = 0; i < num; ++i)
        {
            float s = 0;
            if (channel == 10) s = drum (srate);
            else
            {
                phase += freq / srate;
                phase -= std::floor (phase);
                const float saw = (float) (2 * phase - 1);
                const float tri = (float) (1 - 4 * std::abs (phase - 0.5));
                const float sq = phase < 0.5 ? 0.6f : -0.6f;
                float raw = channel == 2 ? saw : channel == 3 ? sq : channel == 4 ? saw : tri;
                const float cutoff = channel == 2 ? 0.08f : channel == 3 ? 0.05f : 0.15f;
                lp += cutoff * (raw - lp);
                s = lp;
            }
            const float v = s * level * adsr.getNextSample();
            for (int c = 0; c < out.getNumChannels(); ++c) out.addSample (c, start + i, v);
            t += 1.0 / srate;
            if (! adsr.isActive()) { clearCurrentNote(); break; }
        }
    }

private:
    float drum (double srate)
    {
        std::uniform_real_distribution<float> u (-1.0f, 1.0f);
        if (drumNote == 36 || drumNote == 35) // kick: falling sine
        {
            const double f = 45 + 110 * std::exp (-t * 25);
            phase += f / srate;
            return (float) (std::sin (2 * juce::MathConstants<double>::pi * phase) * std::exp (-t * 9) * 3.0);
        }
        if (drumNote == 38 || drumNote == 40) // snare: tone + noise
        {
            phase += 190 / srate;
            return (float) ((0.5 * std::sin (2 * juce::MathConstants<double>::pi * phase) + 0.7 * u (rng)) * std::exp (-t * 20) * 2.0);
        }
        // hat: differentiated noise
        const float n = u (rng), hp = n - lastNoise;
        lastNoise = n;
        return (float) (hp * std::exp (-t * 60) * 0.8);
    }

    juce::ADSR adsr;
    double phase = 0, freq = 440, t = 0;
    float level = 0, lp = 0, lastNoise = 0;
    int channel = 1, drumNote = 36;
    std::minstd_rand rng { 1234 };
};

} // namespace preview
