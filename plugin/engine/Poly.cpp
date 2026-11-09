// Polyphonic transcription (port of zampler/poly.py): harmonic salience on a
// semitone spectrum, spectral-smoothness overtone removal, peak gating, and
// stitching of held notes across onsets.
#include "Analysis.h"

#include <algorithm>
#include <cmath>
#include <map>

namespace zampler
{

using namespace dsp;

namespace
{
constexpr int kHop = 512;
constexpr int kHarmonics = 6;
const int kHarmOffs[kHarmonics] = { 0, 12, 19, 24, 28, 31 };
const double kHarmW[kHarmonics] = { 1.0, 0.9, 0.81, 0.729, 0.6561, 0.59049 };

bool isPeak (const std::vector<double>& s, int i)
{
    const double l = i > 0 ? s[(size_t) i - 1] : 0.0;
    const double r = i + 1 < (int) s.size() ? s[(size_t) i + 1] : 0.0;
    return s[(size_t) i] >= l && s[(size_t) i] >= r && s[(size_t) i] > 1.3 * std::min (l, r) + 1e-12;
}

std::vector<std::pair<int, double>> pickNotes (std::vector<double> s, double floor, int maxPoly, double rel)
{
    const int n = (int) s.size();
    std::vector<std::pair<int, double>> picked;
    double loudest = 0;
    for (int iter = 0; iter < maxPoly; ++iter)
    {
        int best = -1;
        double bestSal = 0;
        for (int p = 0; p < n; ++p)
        {
            if (s[(size_t) p] < floor || ! isPeak (s, p))
                continue;
            double sal = 0;
            for (int h = 0; h < kHarmonics; ++h)
                if (p + kHarmOffs[h] < n) sal += kHarmW[h] * s[(size_t) (p + kHarmOffs[h])];
            if (sal > bestSal) { bestSal = sal; best = p; }
        }
        if (best < 0)
            break;
        const double amp = s[(size_t) best];
        if (amp < rel * loudest)
            break;
        loudest = std::max (loudest, amp);
        picked.emplace_back (best, amp);
        // spectral smoothness: remove only what a smooth harmonic series explains
        std::vector<int> idx;
        for (int h = 0; h < kHarmonics; ++h)
            if (best + kHarmOffs[h] < n) idx.push_back (best + kHarmOffs[h]);
        std::vector<double> obs;
        for (int i : idx) obs.push_back (s[(size_t) i]);
        for (size_t h = 1; h < idx.size(); ++h)
        {
            double nb = obs[h - 1];
            int cnt = 1;
            if (h + 1 < obs.size()) { nb += obs[h + 1]; ++cnt; }
            s[(size_t) idx[h]] = std::max (0.0, s[(size_t) idx[h]] - std::min (obs[h], nb / cnt));
        }
        s[(size_t) best] = 0;
        for (int nbIdx : { best - 1, best + 1 })
            if (nbIdx >= 0 && nbIdx < n) s[(size_t) nbIdx] = std::max (0.0, s[(size_t) nbIdx] - 0.5 * amp);
    }
    return picked;
}
} // namespace

std::vector<Note> transcribePoly (const Signal& y, int midiLow, int midiHigh, int maxPoly, double rel, double minNote)
{
    const double sr = kSampleRate;
    if (y.size() < (size_t) (0.05 * sr))
        return {};
    const int nSemis = 12 * 7;
    auto S = semitoneSpectrogram (y, sr, midiLow, nSemis, kHop);
    // Gentle tilt (like librosa's length-normalised CQT): overtones of a note
    // weigh less than fundamentals, so they aren't mistaken for extra notes.
    for (int r = 0; r < nSemis; ++r)
    {
        const float tilt = (float) std::min (1.0, std::pow (2.0, -(midiLow + r - 60) / 24.0)); // above C4 only
        for (int c = 0; c < S.cols; ++c) S.at (r, c) *= tilt;
    }
    const int nFrames = S.cols;
    double peak = 0;
    for (float v : S.data) peak = std::max (peak, (double) v);
    if (peak <= 0) return {};
    const double floor = 0.04 * peak;

    const auto env = onsetStrength (stftMagnitude (y, 2048, kHop));
    std::vector<int> bounds { 0 };
    std::vector<double> rough;
    for (int b : pickOnsets (env, sr, kHop)) rough.push_back (frameToTime (b, sr, kHop));
    for (double t : refineOnsetTimes (y, sr, rough))
    {
        const int b = (int) std::lround (t * sr / kHop);
        if (b > 0 && b < nFrames) bounds.push_back (b);
    }
    bounds.push_back (nFrames);
    std::sort (bounds.begin(), bounds.end());
    bounds.erase (std::unique (bounds.begin(), bounds.end()), bounds.end());
    const int minFrames = std::max (1, (int) std::lround (minNote * sr / kHop));
    auto T = [&] (int f) { return frameToTime (f, sr, kHop); };

    std::vector<Note> notes;
    std::map<int, size_t> active;     // pitch -> index into notes
    std::map<int, double> prevLevel;
    for (size_t bi = 0; bi + 1 < bounds.size(); ++bi)
    {
        const int a = bounds[bi], b = bounds[bi + 1];
        if (b - a < minFrames)
            continue;
        const int w0 = std::min (b - 1, a + 1);
        const int w1 = std::max (w0 + 1, std::min (b, a + std::max (2, (b - a) / 2)));
        std::vector<double> seg ((size_t) nSemis, 0.0);
        for (int r = 0; r < nSemis; ++r)
        {
            double s = 0;
            for (int c = w0; c < w1; ++c) s += S.at (r, c);
            seg[(size_t) r] = s / (w1 - w0);
        }
        std::map<int, size_t> current;
        for (auto [row, amp] : pickNotes (seg, floor, maxPoly, rel))
        {
            const int pitch = midiLow + row;
            if (pitch > midiHigh) continue;
            float emax = 0;
            for (int c = a; c < b; ++c) emax = std::max (emax, S.at (row, c));
            int lastAlive = -1;
            for (int c = a; c < b; ++c)
                if (S.at (row, c) >= 0.15f * emax) lastAlive = c - a;
            const int endF = a + (lastAlive >= 0 ? lastAlive + 1 : b - a);
            float startLevel = 0;
            for (int c = a; c < std::min (b, a + 3); ++c) startLevel = std::max (startLevel, S.at (row, c));
            auto it = active.find (pitch);
            const bool held = it != active.end() && notes[it->second].end >= T (a) - 1e-6
                              && startLevel <= 1.25 * prevLevel[pitch];
            size_t idx;
            if (held)
            {
                idx = it->second;
                notes[idx].end = T (endF);
            }
            else
            {
                // A new note needs an attack: if the pitch was already this loud
                // just before the boundary, it's the tail of an older note.
                float pre = 0;
                for (int c = std::max (0, a - 3); c < a; ++c) pre = std::max (pre, S.at (row, c));
                if (a > 0 && startLevel <= 1.1f * pre)
                    continue;
                const int vel = (int) std::clamp (40 + 87 * std::sqrt (amp / peak), 1.0, 127.0);
                notes.push_back ({ T (a), T (endF), pitch, vel });
                idx = notes.size() - 1;
            }
            current[pitch] = idx;
            double lv = 0;
            int cnt = 0;
            for (int c = std::max (a, b - 3); c < b; ++c, ++cnt) lv += S.at (row, c);
            prevLevel[pitch] = cnt ? lv / cnt : 0;
        }
        active.clear();
        for (auto& [p, idx] : current)
            if (notes[idx].end >= T (b) - 2.0 * kHop / sr) active[p] = idx;
    }
    std::vector<Note> out;
    for (auto& n : notes)
        if (n.duration() >= minNote) out.push_back (n);
    std::sort (out.begin(), out.end(), [] (const Note& x, const Note& y2) {
        return x.start != y2.start ? x.start < y2.start : x.pitch < y2.pitch;
    });
    return out;
}

} // namespace zampler
