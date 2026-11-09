// Zampler engine: music theory (port of zampler/theory.py).
// Pure C++17, no JUCE, so it can be unit tested on its own.
#pragma once

#include <array>
#include <optional>
#include <string>
#include <vector>

namespace zampler
{

extern const std::array<const char*, 12> kNoteNames;

struct Scale
{
    const char* name;
    std::vector<int> intervals;
};

const std::vector<Scale>& scales();
const Scale& scaleByName (const std::string& name);

/** "C4" (or "C-4" with renoise = true). MIDI 60 = C4 = Renoise C-4. */
std::string noteName (int midi, bool renoise = false);
/** Parses "C4", "c#3", "Eb2", "C-4" or a MIDI number. Returns nullopt if unreadable. */
std::optional<int> parseNote (const std::string& text);

struct Key
{
    int root = 9;                 // pitch class 0..11
    std::string scale = "minor";

    const std::vector<int>& intervals() const { return scaleByName (scale).intervals; }
    std::string name() const;
    bool contains (int midi) const;
    /** Closest in-key note. Ties go down unless preferUp. */
    int snap (int midi, bool preferUp = false) const;
    /** Note for an absolute scale degree (may be negative / exceed one octave) above octaveMidi. */
    int degreeNote (int degree, int octaveMidi) const;
    /** Absolute degree of an in-key note (inverse of degreeNote with octaveMidi = 0). */
    std::optional<int> degreeOf (int midi) const;
    /** Move by scale steps (snaps first). */
    int step (int midi, int steps) const;
    std::vector<int> triad (int degree, int octaveMidi = 48, int size = 3) const;
};

/** Krumhansl-Schmuckler key finding. confidence is the winning correlation (0..1). */
Key detectKey (const std::array<double, 12>& profile, double* confidence = nullptr);

struct ChordQuality
{
    const char* suffix;
    std::vector<int> intervals;
};
const std::vector<ChordQuality>& chordQualities();

struct Chord
{
    int root = 0;
    std::string quality;      // "", "m", "dim", "sus2", "sus4", "7", "maj7", "m7"

    std::string name() const;
    std::vector<int> intervals() const;
    std::vector<int> pitchClasses() const;
    bool hasPitchClass (int pc) const;
    /** Close voicing with the root at or above baseMidi. */
    std::vector<int> voicing (int baseMidi = 48) const;
    bool operator== (const Chord& o) const { return root == o.root && quality == o.quality; }
};

Chord matchChord (const std::array<double, 12>& chroma, const Key* key = nullptr, double* score = nullptr);
Chord diatonicChord (const Key& key, int degree, bool sevenths = false);

} // namespace zampler
