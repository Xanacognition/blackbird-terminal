// Zampler engine: tracker grid, parts, note columns, generators and exports
// (port of zampler/tracker.py, generators.py and export.py).
#pragma once

#include "Analysis.h"

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace zampler
{

struct Step
{
    int line = 0, length = 1, pitch = 60, velocity = 100;
    int end() const { return line + length; }
};

struct Part
{
    std::string name;
    std::vector<Step> steps;
    int instrument = 0;  // Renoise instrument index
    bool drums = false;  // GM drum notes, never scale-locked
};

struct Grid
{
    double bpm = 120;
    int lpb = 4;
    int beatsPerBar = 4;
    int linesPerBar() const { return lpb * beatsPerBar; }
    double secondsPerLine() const { return 60.0 / bpm / lpb; }
    int toLine (double seconds) const;
    double toSeconds (double line) const { return line * secondsPerLine(); }
};

constexpr int kMaxPatternLines = 512;
constexpr int kMaxNoteColumns = 12;
constexpr int kRenoiseDrumOffset = 24; // GM 36 -> C-4

std::vector<Step> quantize (const std::vector<Note>& notes, const Grid& g, const Key* key = nullptr, bool scaleLock = false);
std::vector<Step> quantizePoly (const std::vector<Note>& notes, const Grid& g, const Key* key = nullptr, bool scaleLock = false);
int songLength (const std::vector<Part>& parts, const Grid& g);

struct Cell
{
    int pitch = -1;      // MIDI (Renoise-shifted for drums); -1 empty
    bool off = false;
    int velocity = -1;
    int instrument = -1;
};
/** columns[col][line] */
std::vector<std::vector<Cell>> layoutColumns (const Part& part, int totalLines);
std::string renderPattern (const std::vector<Part>& parts, const Grid& g, int start, int length, bool showVol = true);

// ---- generators -------------------------------------------------------------------------
extern const std::vector<std::string> kProgressionNames;
extern const std::vector<std::string> kBassStyles;
extern const std::vector<std::string> kChordStyles;
extern const std::vector<std::string> kLeadStyles;

std::vector<Chord> progression (const Key& key, const std::string& name, int bars,
                                const std::vector<Chord>& sampleChords = {}, bool sevenths = false);
Part bassline (const Key& key, const std::vector<Chord>& chords, const Grid& g, const std::string& style,
               int octave = 2, int velocity = 110, bool scaleLock = true, const std::vector<int>& kickLines = {});
Part chordPart (const Key& key, const std::vector<Chord>& chords, const Grid& g, const std::string& style,
                int octave = 3, int velocity = 90, bool scaleLock = true);
Part lead (const Key& key, const std::vector<Chord>& chords, const Grid& g, const std::string& style, int octave = 5,
           double density = 0.5, uint32_t seed = 0, const std::vector<Step>& sampleSteps = {}, int velocity = 100);
/** Typed notes ("A3 C4 - . E4"). fixes receives one message per corrected note. */
Part sketch (const std::string& text, const Key& key, const Grid& g, int stepLines, bool scaleLock,
             std::vector<std::string>* fixes = nullptr);
Part drumPart (const std::vector<DrumHit>& hits, const Grid& g, bool kick = true, bool snare = true, bool hat = true);
std::vector<int> kickLines (const std::vector<DrumHit>& hits, const Grid& g);

// ---- exports ------------------------------------------------------------------------------
std::vector<uint8_t> toMidiFile (const std::vector<Part>& parts, const Grid& g);
std::string toRenoise (const std::vector<Part>& parts, const Grid& g, int patternLines = 64, const std::string& name = "Zampler");

} // namespace zampler
