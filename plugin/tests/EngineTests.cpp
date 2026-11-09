// Engine tests: the same synthetic signals as the Python test-suite, so both
// engines are held to the same standard. No framework needed: run the binary,
// it exits non-zero on the first failure batch.
#include "../engine/Analysis.h"
#include "../engine/Tracker.h"

#include <cmath>
#include <cstdio>
#include <functional>
#include <map>
#include <random>
#include <set>
#include <string>
#include <vector>

using namespace zampler;

static int failures = 0, checks = 0;
#define CHECK(cond)                                                                  \
    do {                                                                             \
        ++checks;                                                                    \
        if (! (cond)) { ++failures; std::printf ("  FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); } \
    } while (0)

static const double SR = dsp::kSampleRate;

static dsp::Signal synth (const std::vector<int>& midis, double bpm = 120, double noteBeats = 0.5, double gain = 1.0)
{
    const double dur = 60.0 / bpm * noteBeats;
    dsp::Signal out;
    for (int m : midis)
    {
        const int n = (int) (dur * SR);
        for (int i = 0; i < n; ++i)
        {
            const double t = i / SR;
            if (m < 0) { out.push_back (0); continue; }
            const double f = 440.0 * std::pow (2.0, (m - 69) / 12.0);
            double w = 0;
            for (int k = 1; k <= 3; ++k) w += std::sin (2 * M_PI * f * k * t) / k;
            const double env = std::min (1.0, t / 0.005) * std::exp (-t * 3) * std::min (1.0, (dur - t) / 0.01);
            out.push_back ((float) (w * env * 0.3 * gain));
        }
    }
    return out;
}

static dsp::Signal mix (std::vector<dsp::Signal> parts, double gain = 1.0)
{
    size_t n = SIZE_MAX;
    for (auto& p : parts) n = std::min (n, p.size());
    dsp::Signal out (n, 0.0f);
    for (auto& p : parts)
        for (size_t i = 0; i < n; ++i) out[i] += (float) (p[i] * gain);
    return out;
}

static dsp::Signal drumLoop (std::map<std::string, std::vector<double>>& truth, double bpm = 120, int bars = 2)
{
    std::mt19937 rng (0);
    std::normal_distribution<double> gauss;
    const double beat = 60.0 / bpm;
    const size_t n = (size_t) (bars * 4 * beat * SR) + (size_t) SR / 2;
    std::vector<double> y (n, 0.0);
    auto put = [&] (double t, const std::vector<double>& x) {
        const size_t i0 = (size_t) (t * SR);
        for (size_t i = 0; i < x.size() && i0 + i < n; ++i) y[i0 + i] += x[i];
    };
    std::vector<double> kick, snare, hat;
    for (int i = 0; i < (int) (0.35 * SR); ++i)
    {
        const double t = i / SR;
        kick.push_back (std::sin (2 * M_PI * (45 * t + (110.0 / 25) * (1 - std::exp (-25 * t)))) * std::exp (-t * 9) * 0.9);
    }
    for (int i = 0; i < (int) (0.2 * SR); ++i)
    {
        const double t = i / SR;
        snare.push_back ((0.5 * std::sin (2 * M_PI * 190 * t) + gauss (rng) * 0.6) * std::exp (-t * 22) * 0.6);
    }
    double prev = gauss (rng);
    for (int i = 0; i < (int) (0.05 * SR); ++i)
    {
        const double t = i / SR, cur = gauss (rng);
        hat.push_back ((cur - prev) * std::exp (-t * 70) * 0.25);
        prev = cur;
    }
    for (int b = 0; b < bars * 4; ++b)
    {
        const double t = b * beat;
        if (b % 2 == 0) { put (t, kick); truth["kick"].push_back (t); }
        else { put (t, snare); truth["snare"].push_back (t); }
        for (double half : { 0.0, 0.5 })
        {
            put (t + half * beat, hat);
            truth["hat"].push_back (t + half * beat);
        }
    }
    dsp::Signal out (n);
    for (size_t i = 0; i < n; ++i) out[i] = (float) (y[i] * 0.8);
    return out;
}

static const std::vector<int> MELODY { 57, 60, 64, 62, 60, 60, 59, 57, 52, 55, 57, 60, 64, 67, 65, 64 };
static const std::vector<std::vector<int>> CHORDS { { 57, 60, 64 }, { 53, 57, 60 }, { 48, 55, 64 }, { 55, 59, 62 } };
static const std::vector<int> TOP { 72, 76, 74, 72, 77, 76, 72, 69, 76, 79, 76, 72, 74, 71, 74, 79 };

static dsp::Signal chordsAudio (int repeats = 1)
{
    std::vector<dsp::Signal> voices;
    for (int v = 0; v < 3; ++v)
    {
        std::vector<int> line;
        for (int r = 0; r < repeats; ++r)
            for (auto& c : CHORDS) line.push_back (c[(size_t) v]);
        voices.push_back (synth (line, 120, 2));
    }
    return mix (voices);
}

static std::set<std::pair<int, int>> lineSet (const std::vector<Step>& steps)
{
    std::set<std::pair<int, int>> s;
    for (auto& st : steps) s.insert ({ st.line, st.pitch });
    return s;
}

static void dumpDiff (const std::set<std::pair<int, int>>& got, const std::set<std::pair<int, int>>& want)
{
    std::printf ("    extra:");
    for (auto& g : got) if (! want.count (g)) std::printf (" %d:%s", g.first, noteName (g.second).c_str());
    std::printf ("\n    missed:");
    for (auto& w : want) if (! got.count (w)) std::printf (" %d:%s", w.first, noteName (w.second).c_str());
    std::printf ("\n");
}

static void test (const char* name, const std::function<void()>& fn)
{
    const int before = failures;
    fn();
    std::printf ("%s %s\n", failures == before ? "ok  " : "FAIL", name);
}

int main()
{
    test ("theory: parse/print notes", [] {
        CHECK (*parseNote ("C4") == 60);
        CHECK (*parseNote ("c#3") == 49);
        CHECK (*parseNote ("Eb2") == 39);
        CHECK (*parseNote ("C-4") == 60);
        CHECK (*parseNote ("A-1") == 33);
        CHECK (*parseNote ("Cb4") == 59);
        CHECK (*parseNote ("B#3") == 60);
        CHECK (! parseNote ("H2"));
        CHECK (noteName (60, true) == "C-4");
        CHECK (noteName (61, true) == "C#4");
    });
    test ("theory: key, snap, chords", [] {
        Key am { 9, "minor" };
        CHECK (am.snap (68) == 67);
        CHECK (am.snap (66) == 65);
        CHECK (am.step (57, 2) == 60);
        CHECK (diatonicChord (am, 0).name() == "Am");
        CHECK (diatonicChord (am, 4).name() == "Em");
        CHECK (diatonicChord (Key { 0, "major" }, 6).name() == "Bdim");
        std::array<double, 12> prof {};
        for (int pc : { 2, 4, 6, 7, 9, 11, 1 }) prof[(size_t) pc] = 1;
        prof[2] = 3; prof[9] = 2;
        double conf = 0;
        CHECK (detectKey (prof, &conf).name() == "D major");
    });
    test ("mono: every note incl. repeats, tempo, key", [] {
        std::vector<int> seq = MELODY;
        seq.insert (seq.end(), MELODY.begin(), MELODY.end());
        auto a = analyze (synth (seq), SR);
        std::vector<int> got;
        for (auto& n : a.notes) got.push_back (n.pitch);
        CHECK (got == seq);
        if (got != seq)
        {
            std::printf ("    got:");
            for (int p : got) std::printf (" %d", p);
            std::printf ("\n");
        }
        CHECK (a.bpm == 120);
        CHECK (a.key.name() == "A minor");
        CHECK (a.drumHits.empty());
        auto steps = quantize (a.notes, Grid { a.bpm });
        bool lines = steps.size() == seq.size();
        for (size_t i = 0; lines && i < steps.size(); ++i) lines = steps[i].line == (int) i * 2;
        CHECK (lines);
    });
    test ("mono: notes after rests", [] {
        std::vector<int> seq { 45, -1, 45, 48, -1, 43, 45, -1, 45, -1, 45, 48, -1, 43, 45, -1 };
        auto notes = transcribeMono (synth (seq, 100));
        std::vector<int> got, want;
        for (auto& n : notes) got.push_back (n.pitch);
        for (int m : seq) if (m >= 0) want.push_back (m);
        CHECK (got == want);
        if (got != want)
            for (auto& n : notes) std::printf ("    %.3f-%.3f %d\n", n.start, n.end, n.pitch);
    });
    test ("mono: resampled 44.1k input", [] {
        auto y = synth (MELODY);
        auto up = dsp::resample (y, SR, 44100);
        auto a = analyze (up, 44100);
        std::vector<int> got;
        for (auto& n : a.notes) got.push_back (n.pitch);
        CHECK (got == MELODY);
    });
    test ("poly: every chord note", [] {
        auto steps = quantizePoly (transcribePoly (chordsAudio()), Grid { 120 });
        std::set<std::pair<int, int>> want;
        for (size_t i = 0; i < CHORDS.size(); ++i)
            for (int p : CHORDS[i]) want.insert ({ (int) i * 8, p });
        CHECK (lineSet (steps) == want);
    });
    test ("poly: chords + melody on top", [] {
        auto y = mix ({ chordsAudio(), synth (TOP, 120, 0.5, 0.8) });
        auto got = lineSet (quantizePoly (transcribePoly (y), Grid { 120 }));
        std::set<std::pair<int, int>> want;
        for (size_t i = 0; i < CHORDS.size(); ++i)
            for (int p : CHORDS[i]) want.insert ({ (int) i * 8, p });
        for (size_t i = 0; i < TOP.size(); ++i) want.insert ({ (int) i * 2, TOP[i] });
        int hit = 0;
        for (auto& w : want) hit += got.count (w) ? 1 : 0;
        const int falsePos = (int) got.size() - hit;
        std::printf ("    %d/%zu found, %d extra\n", hit, want.size(), falsePos);
        if (falsePos > 1) dumpDiff (got, want);
        CHECK (hit >= (int) want.size() - 1);
        CHECK (falsePos <= 1);
    });
    test ("drums: kick / snare / hat", [] {
        std::map<std::string, std::vector<double>> truth;
        auto hits = detectDrums (drumLoop (truth));
        Grid g { 120 };
        std::map<std::string, std::set<int>> got, want;
        for (auto& h : hits) got[drumKindName (h.kind)].insert (g.toLine (h.time));
        for (auto& [k, v] : truth) for (double t : v) want[k].insert (g.toLine (t));
        CHECK (got["kick"] == want["kick"]);
        for (auto& [k, v] : got)
        {
            std::printf ("    %s:", k.c_str());
            for (int l : v) std::printf (" %d", l);
            std::printf ("\n");
        }
        CHECK (got["snare"] == want["snare"]);
        bool hatsOk = true;
        for (int l : want["hat"]) if (! want["snare"].count (l) && ! got["hat"].count (l)) hatsOk = false;
        for (int l : got["hat"]) if (! want["hat"].count (l)) hatsOk = false;
        CHECK (hatsOk);
    });
    test ("drums: none in a melody", [] {
        CHECK (detectDrums (synth (MELODY)).empty());
        CHECK (detectDrums (chordsAudio()).empty());
    });
    test ("full mix: chords + melody + drums", [] {
        std::map<std::string, std::vector<double>> truth;
        std::vector<int> top2 = TOP;
        top2.insert (top2.end(), TOP.begin(), TOP.end());
        auto y = mix ({ chordsAudio (2), synth (top2, 120, 0.5, 0.8), drumLoop (truth, 120, 4) }, 0.5);
        auto a = analyze (y, SR);
        std::set<std::pair<int, int>> want;
        for (int r = 0; r < 2; ++r)
            for (size_t i = 0; i < CHORDS.size(); ++i)
                for (int p : CHORDS[i]) want.insert ({ (int) (r * 4 + i) * 8, p });
        for (size_t i = 0; i < top2.size(); ++i) want.insert ({ (int) i * 2, top2[i] });
        auto got = lineSet (quantizePoly (a.polyNotes, Grid { a.bpm }));
        int hit = 0, lowFalse = 0;
        for (auto& w : want) hit += got.count (w) ? 1 : 0;
        for (auto& gnote : got) if (! want.count (gnote) && gnote.second < 40) ++lowFalse;
        const int falsePos = (int) got.size() - hit;
        std::printf ("    bpm %.1f, %d/%zu found, %d extra (%d sub)\n", a.bpm, hit, want.size(), falsePos, lowFalse);
        if (falsePos > 4) dumpDiff (got, want);
        CHECK (a.bpm == 120);
        CHECK (hit >= (int) (0.85 * (double) want.size()));
        CHECK (falsePos <= 4);
        CHECK (lowFalse == 0);
    });
    test ("generators stay in key", [] {
        Key am { 9, "minor" };
        Grid g { 120 };
        auto chords = progression (am, "i - VI - III - VII", 4, {}, true);
        bool ok = true;
        for (auto& s : kBassStyles)
            for (auto& st : bassline (am, chords, g, s, 2, 110, true, { 0, 6, 10, 16, 22, 26 }).steps) ok = ok && am.contains (st.pitch);
        for (auto& s : kChordStyles)
            for (auto& st : chordPart (am, chords, g, s).steps) ok = ok && am.contains (st.pitch);
        std::vector<Step> sample;
        for (int i = 0; i < 8; ++i) sample.push_back ({ i * 2, 2, MELODY[(size_t) i], 100 });
        for (auto& s : kLeadStyles)
            for (uint32_t seed = 0; seed < 5; ++seed)
            {
                auto p = lead (am, chords, g, s, 5, 0.5, seed, sample);
                ok = ok && ! p.steps.empty();
                for (auto& st : p.steps) ok = ok && am.contains (st.pitch);
            }
        CHECK (ok);
    });
    test ("bass follows the kick", [] {
        Key am { 9, "minor" };
        auto p = bassline (am, progression (am, "i - VI - III - VII", 4), Grid { 120 }, "Follow the kick", 2, 110, true,
                           { 0, 6, 10, 16, 22, 26 });
        std::vector<int> lines;
        for (auto& s : p.steps) lines.push_back (s.line);
        CHECK ((lines == std::vector<int> { 0, 6, 10, 16, 22, 26, 32, 38, 42, 48, 54, 58 }));
        CHECK (p.steps[0].length == 6);
    });
    test ("sketch fixes wrong notes", [] {
        std::vector<std::string> fixes;
        auto p = sketch ("A3 G#3 - . C4", Key { 9, "minor" }, Grid { 120 }, 2, true, &fixes);
        CHECK (p.steps.size() == 3);
        CHECK (p.steps[1].pitch == 55 && p.steps[1].length == 4 && p.steps[2].line == 8);
        CHECK (fixes.size() == 1 && fixes[0] == "G#3 is not in A minor -> G3");
    });
    test ("tracker: columns, OFFs, drums, render", [] {
        Part chords { "Chords", { { 0, 4, 57, 100 }, { 0, 4, 60, 100 }, { 0, 4, 64, 100 }, { 4, 4, 59, 100 } }, 0, false };
        auto cols = layoutColumns (chords, 16);
        CHECK (cols.size() == 3);
        CHECK (cols[0][4].pitch == 59 && cols[1][4].off && cols[2][4].off);
        CHECK (renderPattern ({ chords }, Grid { 120 }, 0, 16).find ("A-3 00 65 C-4 00 65 E-4 00 65") != std::string::npos);
        Part drums { "Drums", { { 0, 1, 36, 100 }, { 0, 1, 42, 90 }, { 4, 1, 38, 100 } }, 5, true };
        auto dc = layoutColumns (drums, 16);
        CHECK (dc[0][0].pitch == 60);
        bool anyOff = false;
        for (auto& c : dc) for (auto& cell : c) anyOff = anyOff || cell.off;
        CHECK (! anyOff);
    });
    test ("export: MIDI file + Renoise pattern", [] {
        Grid g { 140 };
        std::vector<Part> parts { { "Lead", { { 0, 2, 60, 100 }, { 2, 2, 62, 90 } }, 3, false },
                                  { "Drums", { { 0, 1, 36, 100 } }, 5, true } };
        auto mid = toMidiFile (parts, g);
        CHECK (mid.size() > 40 && mid[0] == 'M' && mid[1] == 'T' && mid[2] == 'h' && mid[3] == 'd');
        CHECK (mid[10] == 0 && mid[11] == 3); // 3 tracks
        bool drumCh = false;
        for (size_t i = 0; i + 1 < mid.size(); ++i) drumCh = drumCh || (mid[i] == 0x99 && mid[i + 1] == 36);
        CHECK (drumCh);
        auto lua = toRenoise (parts, g, 64, "test");
        CHECK (lua.find ("bpm = 140") != std::string::npos);
        CHECK (lua.find ("{1,1,0,48,3,") != std::string::npos);  // C-4 on instrument 03
        CHECK (lua.find ("{2,1,0,48,5,") != std::string::npos);  // kick -> C-4 on instrument 05
    });

    std::printf ("\n%d checks, %d failed\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
