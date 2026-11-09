#include "Analysis.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <map>
#include <numeric>
#include <set>

namespace zampler
{

using namespace dsp;

const char* drumKindName (DrumKind k)
{
    switch (k)
    {
        case DrumKind::kick: return "kick";
        case DrumKind::snare: return "snare";
        case DrumKind::hat: return "hat";
    }
    return "?";
}

int drumGmNote (DrumKind k)
{
    switch (k)
    {
        case DrumKind::kick: return 36;
        case DrumKind::snare: return 38;
        case DrumKind::hat: return 42;
    }
    return 36;
}

static double midiToHz (double m) { return 440.0 * std::pow (2.0, (m - 69.0) / 12.0); }
static double hzToMidi (double f) { return 69.0 + 12.0 * std::log2 (f / 440.0); }

// --------------------------------------------------------------------------------------------
// Monophonic transcription: YIN (difference function via FFT) -> semitones -> notes.

static constexpr int kMonoHop = 256;

std::vector<Note> transcribeMono (const Signal& y, int midiLow, int midiHigh, double minNote)
{
    const double sr = kSampleRate;
    if (y.size() < (size_t) (sr * 0.05))
        return {};
    const int W = 1024;                                   // integration window
    const int tauMax = std::min (W, (int) std::ceil (sr / midiToHz (midiLow)) + 2);
    const int tauMin = std::max (2, (int) std::floor (sr / midiToHz (midiHigh)));
    const int N = W + tauMax;
    int nfft = 1;
    while (nfft < 2 * N) nfft <<= 1;
    const int frames = 1 + (int) (y.size() / (size_t) kMonoHop);

    std::vector<int> midi ((size_t) frames, -1);
    std::vector<float> rms ((size_t) frames, 0.0f);
    std::vector<float> x ((size_t) N);
    std::vector<std::complex<float>> A ((size_t) nfft), B ((size_t) nfft);
    std::vector<double> d ((size_t) tauMax + 1), cm ((size_t) tauMax + 1), prefix ((size_t) N + 1);

    for (int f = 0; f < frames; ++f)
    {
        const long start = (long) f * kMonoHop - W / 2;
        for (int i = 0; i < N; ++i)
        {
            const long idx = start + i;
            x[(size_t) i] = (idx >= 0 && idx < (long) y.size()) ? y[(size_t) idx] : 0.0f;
        }
        double e = 0;
        for (int i = 0; i < W; ++i) e += (double) x[(size_t) i] * x[(size_t) i];
        rms[(size_t) f] = (float) std::sqrt (e / W);
        if (e <= 1e-10)
            continue;
        // r(tau) = sum_{j<W} x[j] x[j+tau]  via FFT cross-correlation
        std::fill (A.begin(), A.end(), std::complex<float> (0, 0));
        std::fill (B.begin(), B.end(), std::complex<float> (0, 0));
        for (int i = 0; i < W; ++i) A[(size_t) i] = x[(size_t) i];
        for (int i = 0; i < N; ++i) B[(size_t) i] = x[(size_t) i];
        fft (A);
        fft (B);
        for (int i = 0; i < nfft; ++i) A[(size_t) i] = std::conj (std::conj (A[(size_t) i]) * B[(size_t) i]);
        fft (A); // inverse via conj trick: result = conj(fft(conj(X))) / n
        prefix[0] = 0;
        for (int i = 0; i < N; ++i) prefix[(size_t) i + 1] = prefix[(size_t) i] + (double) x[(size_t) i] * x[(size_t) i];
        for (int tau = 0; tau <= tauMax; ++tau)
        {
            const double r = std::conj (A[(size_t) tau]).real() / nfft;
            const double eTau = prefix[(size_t) (tau + W)] - prefix[(size_t) tau];
            d[(size_t) tau] = std::max (0.0, e + eTau - 2 * r);
        }
        // cumulative mean normalised difference
        cm[0] = 1;
        double run = 0;
        for (int tau = 1; tau <= tauMax; ++tau)
        {
            run += d[(size_t) tau];
            cm[(size_t) tau] = run > 0 ? d[(size_t) tau] * tau / run : 1;
        }
        const double thr = 0.15;
        int best = -1;
        for (int tau = tauMin; tau < tauMax; ++tau)
        {
            if (cm[(size_t) tau] < thr)
            {
                while (tau + 1 < tauMax && cm[(size_t) tau + 1] < cm[(size_t) tau]) ++tau;
                best = tau;
                break;
            }
        }
        if (best < 0)
            continue;
        // parabolic interpolation
        double t = best;
        if (best > 1 && best < tauMax)
        {
            const double a = cm[(size_t) best - 1], b = cm[(size_t) best], c = cm[(size_t) best + 1];
            const double den = a - 2 * b + c;
            if (std::abs (den) > 1e-12) t = best + 0.5 * (a - c) / den;
        }
        const double m = hzToMidi (sr / t);
        if (m >= midiLow - 0.5 && m <= midiHigh + 0.5)
            midi[(size_t) f] = (int) std::lround (m);
    }
    const float peakRms = *std::max_element (rms.begin(), rms.end());
    for (int f = 0; f < frames; ++f)
        if (rms[(size_t) f] < 0.03f * peakRms)
            midi[(size_t) f] = -1;

    auto smooth = medianFilter (midi, 5);
    for (int f = 0; f < frames; ++f)
        if (midi[(size_t) f] < 0) smooth[(size_t) f] = -1;

    // Onsets (for splitting repeated notes)
    const auto mag = stftMagnitude (y, 2048, kMonoHop);
    const auto env = onsetStrength (mag);
    std::vector<double> rough;
    for (int o : pickOnsets (env, sr, kMonoHop)) rough.push_back (frameToTime (o, sr, kMonoHop));
    const auto onsetTimes = refineOnsetTimes (y, sr, rough);

    auto t = [&] (int f) { return frameToTime (f, sr, kMonoHop); };
    std::vector<Note> notes;
    int start = -1, cur = -1;
    auto close = [&] (int endIdx) {
        if (start < 0 || cur < 0) return;
        const double t0 = t (start), t1 = t (endIdx);
        if (t1 - t0 < minNote) return;
        double s = 0;
        for (int i = start; i < endIdx; ++i) s += (double) rms[(size_t) i] * rms[(size_t) i];
        const double level = std::sqrt (s / std::max (1, endIdx - start)) / std::max (1e-9f, peakRms);
        notes.push_back ({ t0, t1, cur, (int) std::clamp (40 + 87 * level, 1.0, 127.0) });
    };
    for (int f = 0; f < frames; ++f)
    {
        const int p = smooth[(size_t) f];
        if (p != cur)
        {
            close (f);
            start = p >= 0 ? f : -1;
            cur = p;
        }
    }
    close (frames);

    // merge tiny gaps
    std::vector<Note> merged;
    for (auto& n : notes)
    {
        if (! merged.empty() && merged.back().pitch == n.pitch && n.start - merged.back().end <= 0.03)
        {
            merged.back().end = n.end;
            merged.back().velocity = std::max (merged.back().velocity, n.velocity);
        }
        else
            merged.push_back (n);
    }
    // split at re-attacks
    std::vector<Note> out;
    for (auto& n : merged)
    {
        std::vector<double> edges { n.start };
        for (double ot : onsetTimes)
        {
            if (ot >= n.start + minNote && ot <= n.end - minNote)
                edges.push_back (ot);
        }
        edges.push_back (n.end);
        for (size_t i = 0; i + 1 < edges.size(); ++i)
            out.push_back ({ edges[i], edges[i + 1], n.pitch, n.velocity });
    }
    // Pitch frames are blurred by the analysis window; snap each note start to
    // the true attack nearby (and pull the previous note's end along).
    for (size_t i = 0; i < out.size(); ++i)
    {
        double best = -1, bestD = 0.06;
        for (double ot : onsetTimes)
            if (std::abs (ot - out[i].start) < bestD) { bestD = std::abs (ot - out[i].start); best = ot; }
        if (best < 0 || best >= out[i].end - 0.02) continue;
        if (i > 0 && std::abs (out[i - 1].end - out[i].start) < 0.07 && best > out[i - 1].start + 0.02)
            out[i - 1].end = best;
        out[i].start = best;
    }
    return out;
}

// --------------------------------------------------------------------------------------------
// Tempo

double estimateTempo (const Signal& y)
{
    const int hop = 512;
    const double sr = kSampleRate;
    const auto env = onsetStrength (stftMagnitude (y, 2048, hop));
    const double fps = sr / hop;
    double best = 120, bestScore = -1;
    const double mean = std::accumulate (env.begin(), env.end(), 0.0) / std::max<size_t> (1, env.size());
    for (double bpm = 60; bpm <= 200; bpm += 0.5)
    {
        const double lag = 60.0 * fps / bpm;
        double s = 0;
        int cnt = 0;
        for (size_t i = 0; i < env.size(); ++i)
        {
            const double j = (double) i + lag;
            const size_t j0 = (size_t) j;
            if (j0 + 1 >= env.size()) break;
            const double v = env[j0] + (j - (double) j0) * (env[j0 + 1] - env[j0]);
            s += (env[i] - mean) * (v - mean);
            ++cnt;
        }
        if (cnt == 0) continue;
        s /= cnt;
        const double prior = std::exp (-0.5 * std::pow (std::log2 (bpm / 120.0) / 1.0, 2)); // librosa-like
        s *= prior;
        if (s > bestScore) { bestScore = s; best = bpm; }
    }
    while (best < 70) best *= 2;
    while (best > 180) best /= 2;
    return best;
}

static double gridError (const std::vector<double>& on, double bpm)
{
    const double step = 60.0 / bpm / 4;
    std::complex<double> acc (0, 0);
    for (double o : on) acc += std::polar (1.0, 2 * M_PI * o / step);
    const double offset = std::arg (acc) / (2 * M_PI);
    double err = 0;
    for (double o : on)
    {
        const double d = o / step - offset;
        err += std::abs (d - std::round (d));
    }
    return err / (double) on.size();
}

double refineTempo (double bpm, std::vector<double> onsets, double search)
{
    if (onsets.size() < 4)
        return std::round (bpm * 10) / 10;
    std::sort (onsets.begin(), onsets.end());
    const double first = onsets[0];
    for (auto& o : onsets) o -= first;
    double best = bpm, bestErr = 1e9;
    for (double c = bpm * (1 - search); c < bpm * (1 + search); c += 0.1)
    {
        const double cand = std::round (c * 10) / 10;
        const double e = gridError (onsets, cand);
        if (e < bestErr - 1e-9) { bestErr = e; best = cand; }
    }
    double lo = std::floor (best), hi = std::ceil (best);
    for (double c : (std::abs (lo - best) <= std::abs (hi - best)) ? std::array<double, 2> { lo, hi } : std::array<double, 2> { hi, lo })
        if (gridError (onsets, c) <= bestErr + 0.01)
            return c;
    return best;
}

// --------------------------------------------------------------------------------------------

std::vector<Note> dropDrumTones (const std::vector<Note>& notes, const std::vector<DrumHit>& hits)
{
    // A kick's fundamental sits around 40-100 Hz, a snare's body 150-260 Hz:
    // only learn a drum's tone inside its own range, so chord notes that happen
    // to change on every kick are never mistaken for it.
    const double maxLen = 0.8, window = 0.04, decay = 0.35, minShare = 0.75;
    std::set<size_t> drop;
    for (auto kind : { DrumKind::kick, DrumKind::snare })
    {
        const int lo = kind == DrumKind::kick ? 0 : 48, hi = kind == DrumKind::kick ? 45 : 60;
        std::set<double> ts;
        for (auto& h : hits)
            if (h.kind == kind) ts.insert (h.time);
        if (ts.size() < 2) continue;
        std::map<int, int> counts;
        for (double t : ts)
        {
            // A drum's tone can wobble across a semitone boundary between hits
            // (a 190 Hz snare reads as F#3 or G3), so count +-1 semitone. Kick
            // tones can be picked up a little late, so look across its decay.
            std::set<int> near;
            for (auto& n : notes)
                if (n.start - t >= -window && n.start - t <= (kind == DrumKind::kick ? decay : window) && n.pitch >= lo && n.pitch <= hi)
                    for (int d = -1; d <= 1; ++d) near.insert (n.pitch + d);
            for (int p : near) counts[p]++;
        }
        std::set<int> tones;
        for (auto& [p, c] : counts)
            if (c >= minShare * (double) ts.size() && p >= lo && p <= hi)
                for (int d = -1; d <= 1; ++d) tones.insert (p + d);
        for (size_t i = 0; i < notes.size(); ++i)
        {
            const auto& n = notes[i];
            if (! tones.count (n.pitch) || n.duration() > maxLen) continue;
            for (double t : ts)
            {
                const double d = n.start - t;
                if (d >= -window && d <= decay) { drop.insert (i); break; }
            }
        }
    }
    std::vector<Note> out;
    for (size_t i = 0; i < notes.size(); ++i)
        if (! drop.count (i)) out.push_back (notes[i]);
    return out;
}

// --------------------------------------------------------------------------------------------

Analysis analyze (const Signal& mono, double sampleRate, const AnalysisOptions& opts,
                  std::atomic<float>* progress, const std::atomic<bool>* cancel)
{
    auto report = [&] (float p) { if (progress) progress->store (p); };
    auto cancelled = [&] { return cancel && cancel->load(); };
    Analysis a;
    Signal y = mono;
    if (opts.maxSeconds > 0 && (double) y.size() > opts.maxSeconds * sampleRate)
        y.resize ((size_t) (opts.maxSeconds * sampleRate));
    y = resample (y, sampleRate, kSampleRate);
    y = trimSilence (y, 50.0);
    const double sr = kSampleRate;
    a.duration = (double) y.size() / sr;
    report (0.05f);
    if (y.size() < (size_t) (0.05 * sr) || cancelled()) return a;

    double bpm = opts.bpmHint > 0 ? opts.bpmHint : estimateTempo (y);
    report (0.15f);
    a.notes = transcribeMono (y, opts.midiLow, opts.midiHigh);
    report (0.35f);
    if (cancelled()) return a;

    // Key: harmonic chroma + melody durations
    const int hop = 512;
    const auto S = semitoneSpectrogram (y, sr, 24, 84, hop);
    report (0.5f);
    std::array<double, 12> harm {}, mel {};
    for (int r = 0; r < S.rows; ++r)
        for (int c = 0; c < S.cols; ++c)
            harm[(size_t) ((24 + r) % 12)] += S.at (r, c);
    for (auto& n : a.notes) mel[(size_t) (n.pitch % 12)] += n.duration();
    const double hs = std::accumulate (harm.begin(), harm.end(), 0.0);
    const double ms = std::accumulate (mel.begin(), mel.end(), 0.0);
    for (size_t i = 0; i < 12; ++i)
        a.pitchProfile[i] = (hs > 0 ? 0.5 * harm[i] / hs : 0) + (ms > 0 ? 0.5 * mel[i] / ms : 0);
    a.key = detectKey (a.pitchProfile, &a.keyConfidence);

    if (opts.poly && ! cancelled())
        a.polyNotes = transcribePoly (y, opts.midiLow, opts.midiHigh, opts.maxPoly);
    report (0.7f);
    if (opts.drums && ! cancelled())
        a.drumHits = detectDrums (y, opts.drumSensitivity);
    report (0.9f);
    if (! a.drumHits.empty() && ! a.polyNotes.empty())
        a.polyNotes = dropDrumTones (a.polyNotes, a.drumHits);

    if (opts.bpmHint <= 0)
    {
        std::vector<double> ref;
        if (a.drumHits.size() >= 4)
        {
            std::set<double> ts;
            for (auto& h : a.drumHits) ts.insert (std::round (h.time * 1000) / 1000);
            ref.assign (ts.begin(), ts.end());
        }
        else
            for (auto& n : a.notes) ref.push_back (n.start);
        bpm = refineTempo (bpm, ref);
    }
    a.bpm = bpm;

    // Chord per bar (4/4)
    const double barLen = 60.0 / bpm * 4;
    for (double t = 0; t < a.duration - barLen * 0.25; t += barLen)
    {
        std::array<double, 12> chroma {};
        const int f0 = timeToFrame (t, sr, hop), f1 = std::min (S.cols, timeToFrame (t + barLen, sr, hop));
        for (int c = std::max (0, f0); c < f1; ++c)
            for (int r = 0; r < S.rows; ++r)
                chroma[(size_t) ((24 + r) % 12)] += S.at (r, c);
        a.barChords.emplace_back (t, matchChord (chroma, &a.key));
    }
    report (1.0f);
    return a;
}

} // namespace zampler
