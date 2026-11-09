// Zampler engine: audio -> notes, tempo, key, chords, drum hits.
#pragma once

#include "Dsp.h"
#include "Theory.h"

#include <array>
#include <atomic>
#include <string>
#include <vector>

namespace zampler
{

struct Note
{
    double start = 0, end = 0; // seconds
    int pitch = 60;            // MIDI
    int velocity = 100;
    double duration() const { return end - start; }
};

enum class DrumKind { kick, snare, hat };
const char* drumKindName (DrumKind k);
int drumGmNote (DrumKind k); // 36 / 38 / 42

struct DrumHit
{
    double time = 0;
    DrumKind kind = DrumKind::kick;
    int velocity = 100;
};

struct AnalysisOptions
{
    double bpmHint = 0;           // 0 = detect
    double maxSeconds = 90;
    int midiLow = 24;             // C1
    int midiHigh = 96;            // C7
    bool poly = true;
    int maxPoly = 6;
    bool drums = true;
    double drumSensitivity = 0.5;
};

struct Analysis
{
    double duration = 0;
    double bpm = 120;
    Key key;
    double keyConfidence = 0;
    std::vector<Note> notes;          // melody line
    std::vector<Note> polyNotes;      // every note
    std::vector<DrumHit> drumHits;
    std::array<double, 12> pitchProfile {};
    std::vector<std::pair<double, Chord>> barChords;
};

/** Full analysis of mono audio at any sample rate. `progress` (0..1) and
    `cancel` are optional hooks for a background thread. */
Analysis analyze (const dsp::Signal& mono, double sampleRate, const AnalysisOptions& opts = {},
                  std::atomic<float>* progress = nullptr, const std::atomic<bool>* cancel = nullptr);

// Individual stages (all expect audio at dsp::kSampleRate).
std::vector<Note> transcribeMono (const dsp::Signal& y, int midiLow = 24, int midiHigh = 96, double minNote = 0.06);
std::vector<Note> transcribePoly (const dsp::Signal& y, int midiLow = 24, int midiHigh = 96, int maxPoly = 6,
                                  double rel = 0.18, double minNote = 0.06);
std::vector<DrumHit> detectDrums (const dsp::Signal& y, double sensitivity = 0.5, double minPercussive = 0.06);
std::vector<Note> dropDrumTones (const std::vector<Note>& notes, const std::vector<DrumHit>& hits);
double estimateTempo (const dsp::Signal& y);
double refineTempo (double bpm, std::vector<double> onsets, double search = 0.06);

} // namespace zampler
