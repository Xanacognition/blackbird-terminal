// Drum / rhythm guide (port of zampler/drums.py): kick/snare/hat hits from
// per-band energy jumps on the percussive part of the signal.
#include "Analysis.h"

#include <algorithm>
#include <cmath>
#include <numeric>

namespace zampler
{

using namespace dsp;

std::vector<DrumHit> detectDrums (const Signal& y, double sensitivity, double minPercussive)
{
    const double sr = kSampleRate;
    const int nfft = 1024, hop = 256;
    if (y.size() < (size_t) (0.05 * sr))
        return {};
    const auto mag = stftMagnitude (y, nfft, hop);

    // HPSS with a margin of 2 (percussive must clearly beat harmonic).
    Matrix hMask, pMask;
    {
        hpssMasks (mag, hMask, pMask, 31, 2.0f);
        // margin: P^2 / (P^2 + (2H)^2)  ==  pMask / (pMask + 4 hMask)
        for (size_t i = 0; i < pMask.data.size(); ++i)
        {
            const float p = pMask.data[i], h = hMask.data[i];
            pMask.data[i] = (p + 4 * h) > 0 ? p / (p + 4 * h) : 0.0f;
        }
    }
    double totalE = 0, percE = 0;
    for (size_t i = 0; i < mag.data.size(); ++i)
    {
        const double e = (double) mag.data[i] * mag.data[i];
        totalE += e;
        percE += e * pMask.data[i] * pMask.data[i];
    }
    if (totalE <= 0 || percE / totalE < minPercussive)
        return {};

    const int bins = mag.rows, frames = mag.cols;
    auto binHz = [&] (int b) { return (double) b * sr / nfft; };
    std::vector<double> E[3];
    for (auto& e : E) e.assign ((size_t) frames, 0.0);
    Matrix perc (bins, frames);
    for (int b = 0; b < bins; ++b)
    {
        const double hz = binHz (b);
        const int band = (hz >= 30 && hz < 150) ? 0 : (hz >= 250 && hz < 4000) ? 1 : (hz >= 6000) ? 2 : -1;
        for (int f = 0; f < frames; ++f)
        {
            const float v = mag.at (b, f) * pMask.at (b, f);
            perc.at (b, f) = v;
            if (band >= 0) E[band][(size_t) f] += (double) v * v;
        }
    }
    const auto env = onsetStrength (perc);
    const float delta = (float) (0.02 + 0.13 * (1 - sensitivity));
    std::vector<double> rough;
    for (int f : pickOnsets (env, sr, hop, delta)) rough.push_back (frameToTime (f, sr, hop));
    std::vector<int> frames0;
    for (double t : refineOnsetTimes (y, sr, rough))
    {
        const int f = (int) std::lround (t * sr / hop);
        if (f >= 0 && f < frames && (frames0.empty() || f > frames0.back())) frames0.push_back (f);
    }
    std::vector<double> total ((size_t) frames, 0.0);
    double totalMax = 0;
    for (int f = 0; f < frames; ++f)
    {
        total[(size_t) f] = E[0][(size_t) f] + E[1][(size_t) f] + E[2][(size_t) f];
        totalMax = std::max (totalMax, total[(size_t) f]);
    }
    double first4 = 0;
    for (int f = 0; f < std::min (4, frames); ++f) first4 = std::max (first4, total[(size_t) f]);
    if (first4 > 0.1 * totalMax && (frames0.empty() || frames0.front() > 4))
        frames0.insert (frames0.begin(), 0);
    if (frames0.empty())
        return {};

    const int afterN = std::max (1, (int) (0.05 * sr / hop));
    const int beforeN = std::max (1, (int) (0.03 * sr / hop));
    const size_t nh = frames0.size();
    std::vector<double> jumps[3];
    std::vector<double> strength (nh, 0.0);
    for (int k = 0; k < 3; ++k) jumps[k].assign (nh, 0.0);
    for (size_t i = 0; i < nh; ++i)
    {
        const int f = frames0[i];
        for (int j = f; j < std::min (frames, f + 4); ++j) strength[i] = std::max (strength[i], (double) env[(size_t) j]);
        for (int k = 0; k < 3; ++k)
        {
            double a = 0;
            for (int j = std::max (0, f - 2); j < std::min (frames, f + afterN); ++j) a = std::max (a, E[k][(size_t) j]);
            double b = 0;
            if (f > 2)
            {
                b = 1e300;
                for (int j = std::max (0, f - beforeN - 3); j < f - 2; ++j) b = std::min (b, E[k][(size_t) j]);
                if (b == 1e300) b = 0;
            }
            jumps[k][i] = std::max (0.0, a - b);
        }
    }
    double peakJ[3];
    for (int k = 0; k < 3; ++k)
    {
        peakJ[k] = *std::max_element (jumps[k].begin(), jumps[k].end());
        if (peakJ[k] <= 0) peakJ[k] = 1;
    }
    const double strengthMax = std::max (1e-12, *std::max_element (strength.begin(), strength.end()));
    const double floor = 0.15 + 0.2 * (1 - sensitivity);

    std::vector<DrumHit> hits;
    for (size_t i = 0; i < nh; ++i)
    {
        const double kick = jumps[0][i] / peakJ[0], snare = jumps[1][i] / peakJ[1], hat = jumps[2][i] / peakJ[2];
        bool isKick = kick >= floor + 0.1;
        // With a kick on the same hit, only call it a snare too if the mids jump
        // at least as much (a kick + chord attack also lights up the mids).
        bool isSnare = snare >= floor + 0.1 && (! isKick || snare > std::max (0.85, kick));
        bool isHat = hat >= floor * 0.4 && hat >= 1.5 * snare;
        if (std::max ({ kick, snare, hat }) < 0.4 * floor)
            continue; // no band really jumped: not a drum hit
        if (! isKick && ! isSnare && ! isHat)
        {
            if (kick >= snare && kick >= hat) isKick = true;
            else if (snare >= hat) isSnare = true;
            else isHat = true;
        }
        const double t = frameToTime (frames0[i], sr, hop);
        const int vel = (int) std::clamp (50 + 77 * strength[i] / strengthMax, 1.0, 127.0);
        if (isKick) hits.push_back ({ t, DrumKind::kick, vel });
        if (isSnare) hits.push_back ({ t, DrumKind::snare, vel });
        if (isHat) hits.push_back ({ t, DrumKind::hat, vel });
    }
    return hits;
}

} // namespace zampler
