#include "Tracker.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>
#include <random>
#include <set>
#include <sstream>

namespace zampler
{

int Grid::toLine (double seconds) const { return (int) std::lround (seconds / secondsPerLine()); }

std::vector<Step> quantize (const std::vector<Note>& notesIn, const Grid& g, const Key* key, bool scaleLock)
{
    auto notes = notesIn;
    std::stable_sort (notes.begin(), notes.end(), [] (const Note& a, const Note& b) { return a.start < b.start; });
    std::vector<Step> steps;
    std::set<int> used;
    for (auto& n : notes)
    {
        int line = g.toLine (n.start);
        while (used.count (line)) ++line;   // never drop a note
        const int end = std::max (line + 1, g.toLine (n.end));
        const int pitch = (scaleLock && key) ? key->snap (n.pitch) : n.pitch;
        steps.push_back ({ line, end - line, pitch, n.velocity });
        used.insert (line);
    }
    for (size_t i = 0; i + 1 < steps.size(); ++i)
        if (steps[i].end() > steps[i + 1].line)
            steps[i].length = std::max (1, steps[i + 1].line - steps[i].line);
    return steps;
}

std::vector<Step> quantizePoly (const std::vector<Note>& notesIn, const Grid& g, const Key* key, bool scaleLock)
{
    auto notes = notesIn;
    std::stable_sort (notes.begin(), notes.end(), [] (const Note& a, const Note& b) { return a.start < b.start; });
    std::map<std::pair<int, int>, Step> slots;
    for (auto& n : notes)
    {
        const int line = g.toLine (n.start);
        const int end = std::max (line + 1, g.toLine (n.end));
        const int pitch = (scaleLock && key) ? key->snap (n.pitch) : n.pitch;
        auto it = slots.find ({ line, pitch });
        if (it != slots.end())
        {
            it->second.length = std::max (it->second.length, end - line);
            it->second.velocity = std::max (it->second.velocity, n.velocity);
        }
        else
            slots[{ line, pitch }] = { line, end - line, pitch, n.velocity };
    }
    std::vector<Step> steps;
    for (auto& [k, s] : slots) steps.push_back (s);
    std::map<int, size_t> last;
    for (size_t i = 0; i < steps.size(); ++i)
    {
        auto it = last.find (steps[i].pitch);
        if (it != last.end() && steps[it->second].end() > steps[i].line)
            steps[it->second].length = std::max (1, steps[i].line - steps[it->second].line);
        last[steps[i].pitch] = i;
    }
    return steps;
}

int songLength (const std::vector<Part>& parts, const Grid& g)
{
    int end = g.linesPerBar();
    for (auto& p : parts)
        for (auto& s : p.steps) end = std::max (end, s.end());
    const int bar = g.linesPerBar();
    return std::max (bar, ((end + bar - 1) / bar) * bar);
}

std::vector<std::vector<Cell>> layoutColumns (const Part& part, int total)
{
    auto steps = part.steps;
    std::stable_sort (steps.begin(), steps.end(), [] (const Step& a, const Step& b) {
        return a.line != b.line ? a.line < b.line : a.pitch < b.pitch;
    });
    std::vector<int> freeAt;
    std::vector<std::pair<int, Step>> assign;
    for (auto& s : steps)
    {
        int col = -1;
        for (size_t c = 0; c < freeAt.size(); ++c)
            if (freeAt[c] <= s.line) { col = (int) c; break; }
        if (col < 0)
        {
            if ((int) freeAt.size() >= kMaxNoteColumns)
                col = (int) (std::min_element (freeAt.begin(), freeAt.end()) - freeAt.begin());
            else
            {
                col = (int) freeAt.size();
                freeAt.push_back (0);
            }
        }
        freeAt[(size_t) col] = s.end();
        assign.emplace_back (col, s);
    }
    std::vector<std::vector<Cell>> cols (std::max<size_t> (1, freeAt.size()), std::vector<Cell> ((size_t) total));
    for (auto& [c, s] : assign)
        if (s.line >= 0 && s.line < total)
            cols[(size_t) c][(size_t) s.line] = { part.drums ? s.pitch + kRenoiseDrumOffset : s.pitch, false, s.velocity, part.instrument };
    if (! part.drums)
        for (auto& [c, s] : assign)
            if (s.end() < total && cols[(size_t) c][(size_t) s.end()].pitch < 0)
                cols[(size_t) c][(size_t) s.end()] = { -1, true, -1, -1 };
    return cols;
}

static int velToVol (int v) { return std::min (0x80, (int) std::lround (v / 127.0 * 0x80)); }

static std::string fmtCell (const Cell& c, bool showVol)
{
    char buf[32];
    std::string note, ins = "..", vol = "..";
    if (c.off) note = "OFF";
    else if (c.pitch < 0) note = "---";
    else
    {
        note = (c.pitch >= 12 && c.pitch <= 131) ? noteName (c.pitch, true) : "???";
        if (c.instrument >= 0) { std::snprintf (buf, sizeof buf, "%02X", c.instrument); ins = buf; }
        if (c.velocity >= 0) { std::snprintf (buf, sizeof buf, "%02X", velToVol (c.velocity)); vol = buf; }
    }
    return showVol ? note + " " + ins + " " + vol : note + " " + ins;
}

std::string renderPattern (const std::vector<Part>& parts, const Grid& g, int start, int length, bool showVol)
{
    const int total = songLength (parts, g);
    std::vector<std::vector<std::vector<Cell>>> layouts;
    for (auto& p : parts) layouts.push_back (layoutColumns (p, total));
    const int cellW = (int) fmtCell ({}, showVol).size();
    std::ostringstream out;
    std::string header = "    |";
    for (size_t i = 0; i < parts.size(); ++i)
    {
        const int w = (int) layouts[i].size() * (cellW + 1) - 1;
        std::string name = parts[i].name.substr (0, (size_t) w);
        name.resize ((size_t) w, ' ');
        header += " " + name + " |";
    }
    out << header << "\n" << std::string (header.size(), '-') << "\n";
    char buf[16];
    for (int ln = start; ln < std::min (total, start + length); ++ln)
    {
        std::snprintf (buf, sizeof buf, "%03X |", ln - start);
        std::string row = buf;
        for (auto& cols : layouts)
        {
            row += " ";
            for (size_t c = 0; c < cols.size(); ++c)
                row += (c ? " " : "") + fmtCell (cols[c][(size_t) ln], showVol);
            row += " |";
        }
        out << row << "\n";
    }
    return out.str();
}

// ---- generators ----------------------------------------------------------------------------

static const std::vector<std::pair<std::string, std::vector<int>>>& progressionTable()
{
    static const std::vector<std::pair<std::string, std::vector<int>>> t {
        { "From sample", {} },
        { "i - VI - III - VII", { 0, 5, 2, 6 } },
        { "i - iv - v - i", { 0, 3, 4, 0 } },
        { "I - V - vi - IV", { 0, 4, 5, 3 } },
        { "vi - IV - I - V", { 5, 3, 0, 4 } },
        { "ii - V - I - I", { 1, 4, 0, 0 } },
        { "i - VII - VI - VII", { 0, 6, 5, 6 } },
        { "i - i - iv - v", { 0, 0, 3, 4 } },
    };
    return t;
}

const std::vector<std::string> kProgressionNames = [] {
    std::vector<std::string> v;
    for (auto& p : progressionTable()) v.push_back (p.first);
    return v;
}();
const std::vector<std::string> kBassStyles { "Root notes", "Eighths", "Root + fifth", "Octave bounce", "Walking", "Syncopated", "Follow the kick" };
const std::vector<std::string> kChordStyles { "Whole bar", "Half notes", "Off-beat stabs", "Quarter pulse", "Arp up", "Arp up/down" };
const std::vector<std::string> kLeadStyles { "Motif", "Harmony (3rd above sample)", "Counter-melody", "Octave double" };

std::vector<Chord> progression (const Key& key, const std::string& name, int bars, const std::vector<Chord>& sampleChords, bool sevenths)
{
    std::vector<Chord> base;
    for (auto& [n, degrees] : progressionTable())
        if (n == name && ! degrees.empty())
            for (int d : degrees) base.push_back (diatonicChord (key, d % (int) key.intervals().size(), sevenths));
    if (base.empty()) base = sampleChords;
    if (base.empty()) base.push_back (diatonicChord (key, 0, sevenths));
    std::vector<Chord> out;
    for (int i = 0; i < bars; ++i) out.push_back (base[(size_t) i % base.size()]);
    return out;
}

static int rootIn (const Chord& c, int low) { return low + (((c.root - low) % 12) + 12) % 12; }

Part bassline (const Key& key, const std::vector<Chord>& chords, const Grid& g, const std::string& style,
               int octave, int velocity, bool scaleLock, const std::vector<int>& kicks)
{
    const int low = (octave + 1) * 12, bar = g.linesPerBar(), beat = g.lpb, eighth = std::max (1, g.lpb / 2);
    Part p { "Bass", {}, 1, false };
    auto add = [&] (int line, int len, int pitch, int vel) {
        p.steps.push_back ({ line, std::max (1, len), scaleLock ? key.snap (pitch) : pitch, vel });
    };
    for (size_t b = 0; b < chords.size(); ++b)
    {
        const int t0 = (int) b * bar;
        const int root = rootIn (chords[b], low), fifth = root + 7;
        const int nxt = rootIn (chords[(b + 1) % chords.size()], low);
        if (style == "Follow the kick" && ! kicks.empty())
        {
            const int maxK = *std::max_element (kicks.begin(), kicks.end());
            const int nBars = std::max (1, (maxK + bar) / bar);
            const int src = (int) (b % (size_t) nBars) * bar;
            std::set<int> hits;
            for (int k : kicks)
                if (k >= src && k < src + bar) hits.insert (k - src);
            if (hits.empty()) add (t0, bar, root, velocity);
            std::vector<int> h (hits.begin(), hits.end());
            for (size_t i = 0; i < h.size(); ++i)
                add (t0 + h[i], (i + 1 < h.size() ? h[i + 1] : bar) - h[i], root, velocity);
        }
        else if (style == "Eighths")
            for (int i = 0; i < bar; i += eighth) add (t0 + i, eighth, root, i % beat == 0 ? velocity : velocity - 20);
        else if (style == "Root + fifth")
            for (int i = 0; i < g.beatsPerBar; ++i) add (t0 + i * beat, beat, i % 2 == 0 ? root : fifth, velocity);
        else if (style == "Octave bounce")
            for (int i = 0; i < bar; i += eighth) add (t0 + i, eighth, (i / eighth) % 2 == 0 ? root : root + 12, velocity);
        else if (style == "Walking")
        {
            const int dir = nxt >= root ? 1 : -1;
            const int line[4] = { root, key.step (root, dir), key.step (root, 2 * dir), nxt - dir };
            for (int i = 0; i < std::min (4, g.beatsPerBar); ++i) add (t0 + i * beat, beat, line[i], velocity);
        }
        else if (style == "Syncopated")
        {
            add (t0, 3 * eighth, root, velocity);
            add (t0 + 3 * eighth, 3 * eighth, root, velocity);
            add (t0 + 6 * eighth, 2 * eighth, fifth, velocity);
        }
        else
            add (t0, bar, root, velocity);
    }
    return p;
}

static std::vector<int> smoothVoicing (const std::vector<int>& v, const std::vector<int>& prev)
{
    if (prev.empty()) return v;
    double centre = 0;
    for (int x : prev) centre += x;
    centre /= (double) prev.size();
    std::vector<int> best = v, cur = v;
    double bestD = 1e9;
    for (size_t inv = 0; inv < v.size(); ++inv)
    {
        for (int shift : { -12, 0, 12 })
        {
            double c = 0;
            for (int x : cur) c += x + shift;
            c /= (double) cur.size();
            if (std::abs (c - centre) < bestD)
            {
                bestD = std::abs (c - centre);
                best = cur;
                for (auto& x : best) x += shift;
            }
        }
        std::rotate (cur.begin(), cur.begin() + 1, cur.end());
        cur.back() += 12;
    }
    return best;
}

Part chordPart (const Key& key, const std::vector<Chord>& chords, const Grid& g, const std::string& style,
                int octave, int velocity, bool scaleLock)
{
    const int base = (octave + 1) * 12, bar = g.linesPerBar(), beat = g.lpb, eighth = std::max (1, g.lpb / 2);
    Part p { "Chords", {}, 2, false };
    std::vector<int> prev;
    for (size_t b = 0; b < chords.size(); ++b)
    {
        auto v = smoothVoicing (chords[b].voicing (base), prev);
        if (scaleLock)
        {
            std::set<int> s;
            for (int x : v) s.insert (key.snap (x));
            v.assign (s.begin(), s.end());
        }
        prev = v;
        const int t0 = (int) b * bar;
        auto hit = [&] (int off, int len, const std::vector<int>& notes) {
            for (int n : notes) p.steps.push_back ({ t0 + off, std::max (1, len), n, velocity });
        };
        if (style == "Half notes") { hit (0, bar / 2, v); hit (bar / 2, bar / 2, v); }
        else if (style == "Off-beat stabs") for (int i = 0; i < g.beatsPerBar; ++i) hit (i * beat + eighth, eighth, v);
        else if (style == "Quarter pulse") for (int i = 0; i < g.beatsPerBar; ++i) hit (i * beat, beat, v);
        else if (style == "Arp up" || style == "Arp up/down")
        {
            auto seq = v;
            seq.push_back (v.front() + 12);
            if (style == "Arp up/down")
                for (int i = (int) seq.size() - 2; i > 0; --i) seq.push_back (seq[(size_t) i]);
            for (int i = 0; i < bar; i += eighth) hit (i, eighth, { seq[(size_t) (i / eighth) % seq.size()] });
        }
        else hit (0, bar, v);
    }
    return p;
}

Part lead (const Key& key, const std::vector<Chord>& chords, const Grid& g, const std::string& style, int octave,
           double density, uint32_t seed, const std::vector<Step>& sample, int velocity)
{
    std::mt19937 rng (seed);
    auto uni = [&] { return std::uniform_real_distribution<double> (0, 1) (rng); };
    auto pick = [&] (const std::vector<int>& v) { return v[std::uniform_int_distribution<size_t> (0, v.size() - 1) (rng)]; };
    const int bar = g.linesPerBar(), eighth = std::max (1, g.lpb / 2), low = (octave + 1) * 12;
    Part p { "Lead", {}, 3, false };

    if ((style == "Harmony (3rd above sample)" || style == "Octave double") && ! sample.empty())
    {
        double centre = 0;
        for (auto& s : sample) centre += s.pitch;
        centre /= (double) sample.size();
        const int shift = 12 * (int) std::lround ((low + 6 - centre) / 12.0);
        for (auto& s : sample)
        {
            int q = key.snap (s.pitch);
            q = style == "Octave double" ? q + 12 : key.step (q, 2);
            p.steps.push_back ({ s.line, s.length, key.snap (q + shift), velocity });
        }
        return p;
    }
    std::set<int> sampleLines;
    for (auto& s : sample) sampleLines.insert (s.line);
    struct M { int off, len, deg; };
    std::vector<M> motif;
    int pos = 0, cur = 0;
    while (pos < 2 * bar)
    {
        const bool onBeat = pos % g.lpb == 0;
        if (uni() < (onBeat ? 0.35 + 0.6 * density : 0.15 + 0.6 * density))
        {
            const int len = density < 0.6 ? pick ({ eighth, eighth, g.lpb, g.lpb * 2 }) : pick ({ eighth, eighth, g.lpb });
            cur = std::clamp (cur + pick ({ -2, -1, -1, 1, 1, 2, 0, 3 }), -4, 5);
            motif.push_back ({ pos, len, cur });
            pos += len;
        }
        else
            pos += eighth;
    }
    for (size_t b = 0; b < chords.size(); b += 2)
    {
        const bool variation = (b / 2) % 2 == 1;
        for (auto& m : motif)
        {
            const size_t barIdx = b + (size_t) (m.off / bar);
            if (barIdx >= chords.size()) break;
            const int line = (int) b * bar + m.off;
            if (style == "Counter-melody" && sampleLines.count (line)) continue;
            const auto& chord = chords[barIdx];
            const int root = key.snap (rootIn (chord, low - 5));
            const int d = m.deg + (variation && m.off >= bar ? pick ({ -1, 1 }) : 0);
            int pitch = key.step (root, d);
            if (m.off % g.lpb == 0 && ! chord.hasPitchClass (pitch))
            {
                const int a = key.step (pitch, -1), c = key.step (pitch, 1);
                const bool ac = chord.hasPitchClass (a), cc = chord.hasPitchClass (c);
                pitch = (ac && ! cc) ? a : (cc && ! ac) ? c : a; // prefer chord tone, then lower neighbour
            }
            p.steps.push_back ({ line, m.len, key.snap (pitch), velocity - (m.off % g.lpb == 0 ? 0 : 15) });
        }
    }
    return p;
}

Part sketch (const std::string& text, const Key& key, const Grid& g, int stepLines, bool scaleLock, std::vector<std::string>* fixes)
{
    Part p { "Sketch", {}, 4, false };
    std::string t = text;
    std::replace (t.begin(), t.end(), ',', ' ');
    std::istringstream in (t);
    std::string tok;
    int line = 0;
    stepLines = std::max (1, stepLines);
    while (in >> tok)
    {
        if (tok == ".") { line += stepLines; continue; }
        if (tok == "-") { if (! p.steps.empty()) p.steps.back().length += stepLines; line += stepLines; continue; }
        auto n = parseNote (tok);
        if (! n)
        {
            if (fixes) fixes->push_back ("skipped '" + tok + "': can't read that note");
            line += stepLines;
            continue;
        }
        int pitch = *n;
        if (scaleLock && ! key.contains (pitch))
        {
            const int q = key.snap (pitch);
            if (fixes) fixes->push_back (noteName (pitch) + " is not in " + key.name() + " -> " + noteName (q));
            pitch = q;
        }
        p.steps.push_back ({ line, stepLines, pitch, 100 });
        line += stepLines;
    }
    (void) g;
    return p;
}

Part drumPart (const std::vector<DrumHit>& hits, const Grid& g, bool kick, bool snare, bool hat)
{
    std::map<std::pair<int, int>, Step> slots;
    for (auto& h : hits)
    {
        if ((h.kind == DrumKind::kick && ! kick) || (h.kind == DrumKind::snare && ! snare) || (h.kind == DrumKind::hat && ! hat))
            continue;
        const int line = g.toLine (h.time), gm = drumGmNote (h.kind);
        auto it = slots.find ({ line, gm });
        if (it == slots.end() || it->second.velocity < h.velocity)
            slots[{ line, gm }] = { line, 1, gm, h.velocity };
    }
    Part p { "Drums", {}, 5, true };
    for (auto& [k, s] : slots) p.steps.push_back (s);
    return p;
}

std::vector<int> kickLines (const std::vector<DrumHit>& hits, const Grid& g)
{
    std::set<int> s;
    for (auto& h : hits)
        if (h.kind == DrumKind::kick) s.insert (g.toLine (h.time));
    return { s.begin(), s.end() };
}

// ---- exports --------------------------------------------------------------------------------

static void putVarLen (std::vector<uint8_t>& out, uint32_t v)
{
    uint8_t buf[4];
    int n = 0;
    buf[n++] = v & 0x7F;
    while (v >>= 7) buf[n++] = (uint8_t) ((v & 0x7F) | 0x80);
    while (n) out.push_back (buf[--n]);
}
static void put32 (std::vector<uint8_t>& o, uint32_t v) { for (int s = 24; s >= 0; s -= 8) o.push_back ((uint8_t) (v >> s)); }
static void put16 (std::vector<uint8_t>& o, uint16_t v) { o.push_back ((uint8_t) (v >> 8)); o.push_back ((uint8_t) v); }

static void putTrack (std::vector<uint8_t>& file, const std::vector<uint8_t>& body)
{
    file.insert (file.end(), { 'M', 'T', 'r', 'k' });
    put32 (file, (uint32_t) body.size());
    file.insert (file.end(), body.begin(), body.end());
}

std::vector<uint8_t> toMidiFile (const std::vector<Part>& parts, const Grid& g)
{
    const int tpb = 480, tpl = tpb / g.lpb;
    std::vector<uint8_t> file { 'M', 'T', 'h', 'd' };
    put32 (file, 6);
    put16 (file, 1);
    put16 (file, (uint16_t) (parts.size() + 1));
    put16 (file, tpb);

    std::vector<uint8_t> meta;
    const uint32_t uspq = (uint32_t) std::lround (60000000.0 / g.bpm);
    meta.insert (meta.end(), { 0x00, 0xFF, 0x51, 0x03, (uint8_t) (uspq >> 16), (uint8_t) (uspq >> 8), (uint8_t) uspq });
    meta.insert (meta.end(), { 0x00, 0xFF, 0x58, 0x04, (uint8_t) g.beatsPerBar, 0x02, 0x18, 0x08 });
    meta.insert (meta.end(), { 0x00, 0xFF, 0x2F, 0x00 });
    putTrack (file, meta);

    int melodic = 0;
    for (auto& part : parts)
    {
        int ch = part.drums ? 9 : (melodic < 9 ? melodic : melodic + 1);
        if (! part.drums) ++melodic;
        ch %= 16;
        struct Ev { int tick, order, note, vel; };
        std::vector<Ev> evs;
        for (auto& s : part.steps)
        {
            if (s.pitch < 0 || s.pitch > 127) continue;
            evs.push_back ({ s.line * tpl, 1, s.pitch, std::clamp (s.velocity, 1, 127) });
            evs.push_back ({ s.end() * tpl, 0, s.pitch, 0 });
        }
        std::stable_sort (evs.begin(), evs.end(), [] (const Ev& a, const Ev& b) {
            return a.tick != b.tick ? a.tick < b.tick : a.order < b.order;
        });
        std::vector<uint8_t> body { 0x00, 0xFF, 0x03 };
        putVarLen (body, (uint32_t) part.name.size());
        body.insert (body.end(), part.name.begin(), part.name.end());
        int now = 0;
        for (auto& e : evs)
        {
            putVarLen (body, (uint32_t) (e.tick - now));
            now = e.tick;
            body.push_back ((uint8_t) ((e.order ? 0x90 : 0x80) | ch));
            body.push_back ((uint8_t) e.note);
            body.push_back ((uint8_t) e.vel);
        }
        body.insert (body.end(), { 0x00, 0xFF, 0x2F, 0x00 });
        putTrack (file, body);
    }
    return file;
}

static std::string luaStr (const std::string& s)
{
    std::string o = "\"";
    for (char c : s)
    {
        if (c == '\\' || c == '"') o += '\\';
        o += (c == '\n' ? ' ' : c);
    }
    return o + "\"";
}

std::string toRenoise (const std::vector<Part>& parts, const Grid& g, int patternLines, const std::string& name)
{
    patternLines = std::clamp (patternLines, 1, kMaxPatternLines);
    const int total = songLength (parts, g);
    std::vector<std::vector<std::vector<Cell>>> layouts;
    for (auto& p : parts) layouts.push_back (layoutColumns (p, total));
    const int nPatterns = (total + patternLines - 1) / patternLines;
    std::ostringstream o;
    char bpmBuf[32];
    std::snprintf (bpmBuf, sizeof bpmBuf, "%g", g.bpm);
    o << "-- Zampler export. Load with Tools > Zampler > Import Pattern File...\nreturn {\n"
      << "  name = " << luaStr (name) << ",\n  bpm = " << bpmBuf << ",\n  lpb = " << g.lpb
      << ",\n  pattern_lines = " << patternLines << ",\n  tracks = {\n";
    for (size_t i = 0; i < parts.size(); ++i)
        o << "    { name = " << luaStr (parts[i].name) << ", columns = " << layouts[i].size() << " },\n";
    o << "  },\n  patterns = {\n";
    for (int pt = 0; pt < nPatterns; ++pt)
    {
        const int lo = pt * patternLines, hi = std::min (total, (pt + 1) * patternLines);
        o << "    { lines = " << hi - lo << ", notes = {\n";
        for (size_t t = 0; t < layouts.size(); ++t)
            for (size_t c = 0; c < layouts[t].size(); ++c)
                for (int ln = lo; ln < hi; ++ln)
                {
                    const auto& cell = layouts[t][c][(size_t) ln];
                    if (cell.off)
                        o << "      {" << t + 1 << "," << c + 1 << "," << ln - lo << ",120},\n";
                    else if (cell.pitch >= 12 && cell.pitch <= 131)
                        o << "      {" << t + 1 << "," << c + 1 << "," << ln - lo << "," << cell.pitch - 12 << ","
                          << std::max (0, cell.instrument) << "," << velToVol (cell.velocity < 0 ? 100 : cell.velocity) << "},\n";
                }
        o << "    } },\n";
    }
    o << "  },\n}\n";
    return o.str();
}

} // namespace zampler
