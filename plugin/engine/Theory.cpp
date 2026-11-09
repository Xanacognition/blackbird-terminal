#include "Theory.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <numeric>
#include <set>

namespace zampler
{

const std::array<const char*, 12> kNoteNames { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };

const std::vector<Scale>& scales()
{
    static const std::vector<Scale> s {
        { "major", { 0, 2, 4, 5, 7, 9, 11 } },
        { "minor", { 0, 2, 3, 5, 7, 8, 10 } },
        { "harmonic minor", { 0, 2, 3, 5, 7, 8, 11 } },
        { "melodic minor", { 0, 2, 3, 5, 7, 9, 11 } },
        { "dorian", { 0, 2, 3, 5, 7, 9, 10 } },
        { "phrygian", { 0, 1, 3, 5, 7, 8, 10 } },
        { "lydian", { 0, 2, 4, 6, 7, 9, 11 } },
        { "mixolydian", { 0, 2, 4, 5, 7, 9, 10 } },
        { "locrian", { 0, 1, 3, 5, 6, 8, 10 } },
        { "major pentatonic", { 0, 2, 4, 7, 9 } },
        { "minor pentatonic", { 0, 3, 5, 7, 10 } },
        { "blues", { 0, 3, 5, 6, 7, 10 } },
        { "chromatic", { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11 } },
    };
    return s;
}

const Scale& scaleByName (const std::string& name)
{
    for (auto& s : scales())
        if (name == s.name)
            return s;
    return scales()[1];
}

static int floorDiv (int a, int b) { return (a >= 0) ? a / b : -((-a + b - 1) / b); }
static int posMod (int a, int b) { return ((a % b) + b) % b; }

std::string noteName (int midi, bool renoise)
{
    std::string n = kNoteNames[(size_t) posMod (midi, 12)];
    const int octave = floorDiv (midi, 12) - 1;
    if (renoise && n.size() == 1)
        n += "-";
    return n + std::to_string (octave);
}

std::optional<int> parseNote (const std::string& text)
{
    std::string t;
    for (char c : text)
        if (! std::isspace ((unsigned char) c))
            t += (char) std::toupper ((unsigned char) c);
    if (t.empty())
        return std::nullopt;
    if (std::all_of (t.begin(), t.end(), [] (char c) { return std::isdigit ((unsigned char) c); }))
        return std::stoi (t);
    if (t[0] < 'A' || t[0] > 'G')
        return std::nullopt;
    static const int base[] = { 9, 11, 0, 2, 4, 5, 7 }; // A B C D E F G
    int pc = base[t[0] - 'A'];
    size_t i = 1;
    if (i < t.size() && (t[i] == '#' || t[i] == 'B'))
    {
        // 'B' is a flat only if an octave number follows.
        if (t[i] == '#') { pc += 1; ++i; }
        else if (i + 1 < t.size()) { pc -= 1; ++i; }
    }
    if (i < t.size() && t[i] == '-' && i + 1 < t.size() && std::isdigit ((unsigned char) t[i + 1]))
        ++i; // Renoise style "C-4"
    if (i >= t.size())
        return std::nullopt;
    bool neg = false;
    if (t[i] == '-') { neg = true; ++i; }
    if (i >= t.size() || ! std::all_of (t.begin() + (long) i, t.end(), [] (char c) { return std::isdigit ((unsigned char) c); }))
        return std::nullopt;
    int octave = std::stoi (t.substr (i));
    if (neg) octave = -octave;
    return (octave + 1) * 12 + pc;
}

std::string Key::name() const { return std::string (kNoteNames[(size_t) posMod (root, 12)]) + " " + scale; }

bool Key::contains (int midi) const
{
    const int rel = posMod (midi - root, 12);
    for (int i : intervals())
        if (i == rel)
            return true;
    return false;
}

int Key::snap (int midi, bool preferUp) const
{
    if (contains (midi))
        return midi;
    for (int d = 1; d < 12; ++d)
    {
        const int a = preferUp ? midi + d : midi - d;
        const int b = preferUp ? midi - d : midi + d;
        if (contains (a)) return a;
        if (contains (b)) return b;
    }
    return midi;
}

int Key::degreeNote (int degree, int octaveMidi) const
{
    const auto& iv = intervals();
    const int n = (int) iv.size();
    const int octs = floorDiv (degree, n);
    const int idx = posMod (degree, n);
    return octaveMidi + root + iv[(size_t) idx] + 12 * octs;
}

std::optional<int> Key::degreeOf (int midi) const
{
    if (! contains (midi))
        return std::nullopt;
    const int rel = midi - root;
    const int octs = floorDiv (rel, 12);
    const int pc = posMod (rel, 12);
    const auto& iv = intervals();
    const int idx = (int) (std::find (iv.begin(), iv.end(), pc) - iv.begin());
    return octs * (int) iv.size() + idx;
}

int Key::step (int midi, int steps) const
{
    return degreeNote (*degreeOf (snap (midi)) + steps, 0);
}

std::vector<int> Key::triad (int degree, int octaveMidi, int size) const
{
    std::vector<int> out;
    for (int i = 0; i < size; ++i)
        out.push_back (degreeNote (degree + 2 * i, octaveMidi));
    return out;
}

static double correlation (const std::array<double, 12>& a, const std::array<double, 12>& b)
{
    const double ma = std::accumulate (a.begin(), a.end(), 0.0) / 12.0;
    const double mb = std::accumulate (b.begin(), b.end(), 0.0) / 12.0;
    double num = 0, da = 0, db = 0;
    for (size_t i = 0; i < 12; ++i)
    {
        num += (a[i] - ma) * (b[i] - mb);
        da += (a[i] - ma) * (a[i] - ma);
        db += (b[i] - mb) * (b[i] - mb);
    }
    return (da > 0 && db > 0) ? num / std::sqrt (da * db) : 0.0;
}

Key detectKey (const std::array<double, 12>& profile, double* confidence)
{
    static const std::array<double, 12> major { 6.35, 2.23, 3.48, 2.33, 4.38, 4.09, 2.52, 5.19, 2.39, 3.66, 2.29, 2.88 };
    static const std::array<double, 12> minor { 6.33, 2.68, 3.52, 5.38, 2.60, 3.53, 2.54, 4.75, 3.98, 2.69, 3.34, 3.17 };
    double best = -2.0;
    Key bestKey { 0, "minor" };
    if (std::all_of (profile.begin(), profile.end(), [] (double v) { return v <= 0; }))
    {
        if (confidence) *confidence = 0;
        return bestKey;
    }
    for (int root = 0; root < 12; ++root)
    {
        for (int m = 0; m < 2; ++m)
        {
            std::array<double, 12> ref {};
            const auto& src = m == 0 ? major : minor;
            for (int i = 0; i < 12; ++i)
                ref[(size_t) ((i + root) % 12)] = src[(size_t) i];
            const double r = correlation (profile, ref);
            if (r > best)
            {
                best = r;
                bestKey = Key { root, m == 0 ? "major" : "minor" };
            }
        }
    }
    if (confidence) *confidence = std::max (0.0, best);
    return bestKey;
}

const std::vector<ChordQuality>& chordQualities()
{
    static const std::vector<ChordQuality> q {
        { "", { 0, 4, 7 } }, { "m", { 0, 3, 7 } }, { "dim", { 0, 3, 6 } }, { "sus2", { 0, 2, 7 } },
        { "sus4", { 0, 5, 7 } }, { "7", { 0, 4, 7, 10 } }, { "maj7", { 0, 4, 7, 11 } }, { "m7", { 0, 3, 7, 10 } },
    };
    return q;
}

std::string Chord::name() const { return std::string (kNoteNames[(size_t) posMod (root, 12)]) + quality; }

std::vector<int> Chord::intervals() const
{
    for (auto& q : chordQualities())
        if (quality == q.suffix)
            return q.intervals;
    return { 0, 4, 7 };
}

std::vector<int> Chord::pitchClasses() const
{
    std::vector<int> out;
    for (int i : intervals())
        out.push_back ((root + i) % 12);
    return out;
}

bool Chord::hasPitchClass (int pc) const
{
    for (int p : pitchClasses())
        if (p == posMod (pc, 12))
            return true;
    return false;
}

std::vector<int> Chord::voicing (int baseMidi) const
{
    const int r = baseMidi + posMod (root - baseMidi, 12);
    std::vector<int> out;
    for (int i : intervals())
        out.push_back (r + i);
    return out;
}

Chord matchChord (const std::array<double, 12>& chroma, const Key* key, double* scoreOut)
{
    double norm = 0;
    for (double v : chroma) norm += v * v;
    norm = std::sqrt (norm);
    if (norm == 0)
    {
        if (scoreOut) *scoreOut = 0;
        if (key) return Chord { key->root, key->scale.find ("minor") != std::string::npos ? "m" : "" };
        return Chord {};
    }
    double best = -1;
    Chord bestChord;
    for (int root = 0; root < 12; ++root)
    {
        for (auto& q : chordQualities())
        {
            const double tn = std::sqrt ((double) q.intervals.size());
            double score = 0;
            for (int i : q.intervals)
                score += chroma[(size_t) ((root + i) % 12)] / norm / tn;
            if (q.intervals.size() == 4)
                score -= 0.03;
            Chord c { root, q.suffix };
            if (key)
            {
                bool all = true;
                for (int pc : c.pitchClasses())
                    all = all && key->contains (pc);
                if (all) score += 0.05;
            }
            if (score > best)
            {
                best = score;
                bestChord = c;
            }
        }
    }
    if (scoreOut) *scoreOut = best;
    return bestChord;
}

Chord diatonicChord (const Key& key, int degree, bool sevenths)
{
    const auto notes = key.triad (degree, 0, sevenths ? 4 : 3);
    const int root = posMod (notes[0], 12);
    std::set<int> ivs;
    for (int n : notes) ivs.insert (posMod (n - notes[0], 12));
    for (auto& q : chordQualities())
    {
        std::set<int> t (q.intervals.begin(), q.intervals.end());
        if (t == ivs)
            return Chord { root, q.suffix };
    }
    const int third = posMod (notes[1] - notes[0], 12);
    return Chord { root, third == 3 ? "m" : "" };
}

} // namespace zampler
