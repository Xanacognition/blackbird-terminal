#include "Dsp.h"

#include <algorithm>
#include <cmath>
#include <numeric>

namespace zampler::dsp
{

static constexpr double kPi = 3.14159265358979323846;

Signal resample (const Signal& in, double inRate, double outRate)
{
    if (std::abs (inRate - outRate) < 1e-6 || in.empty())
        return in;
    const double ratio = outRate / inRate;
    const size_t outLen = (size_t) std::floor ((double) in.size() * ratio);
    Signal out (outLen);
    const double cutoff = std::min (1.0, ratio); // low-pass when downsampling
    const int taps = 16;
    const double width = taps / cutoff;
    for (size_t i = 0; i < outLen; ++i)
    {
        const double centre = (double) i / ratio;
        const long lo = (long) std::ceil (centre - width), hi = (long) std::floor (centre + width);
        double acc = 0, norm = 0;
        for (long j = lo; j <= hi; ++j)
        {
            if (j < 0 || j >= (long) in.size())
                continue;
            const double x = (centre - (double) j) * cutoff;
            const double sinc = std::abs (x) < 1e-9 ? 1.0 : std::sin (kPi * x) / (kPi * x);
            const double w = 0.5 + 0.5 * std::cos (kPi * (centre - (double) j) / width); // Hann
            acc += in[(size_t) j] * sinc * w;
            norm += sinc * w;
        }
        out[i] = (float) (norm != 0 ? acc / norm : 0.0);
    }
    return out;
}

void fft (std::vector<std::complex<float>>& x)
{
    const size_t n = x.size();
    for (size_t i = 1, j = 0; i < n; ++i)
    {
        size_t bit = n >> 1;
        for (; j & bit; bit >>= 1)
            j ^= bit;
        j ^= bit;
        if (i < j)
            std::swap (x[i], x[j]);
    }
    for (size_t len = 2; len <= n; len <<= 1)
    {
        const double ang = -2 * kPi / (double) len;
        const std::complex<float> wl ((float) std::cos (ang), (float) std::sin (ang));
        for (size_t i = 0; i < n; i += len)
        {
            std::complex<float> w (1.0f, 0.0f);
            for (size_t k = 0; k < len / 2; ++k)
            {
                const auto u = x[i + k];
                const auto v = x[i + k + len / 2] * w;
                x[i + k] = u + v;
                x[i + k + len / 2] = u - v;
                w *= wl;
            }
        }
    }
}

static std::vector<float> hann (int n)
{
    std::vector<float> w ((size_t) n);
    for (int i = 0; i < n; ++i)
        w[(size_t) i] = (float) (0.5 - 0.5 * std::cos (2 * kPi * i / n)); // periodic
    return w;
}

static int numFrames (size_t len, int hop) { return 1 + (int) (len / (size_t) hop); }

Matrix stftMagnitude (const Signal& y, int nfft, int hop)
{
    const int frames = numFrames (y.size(), hop);
    const int bins = nfft / 2 + 1;
    Matrix m (bins, frames);
    const auto w = hann (nfft);
    std::vector<std::complex<float>> buf ((size_t) nfft);
    for (int f = 0; f < frames; ++f)
    {
        const long start = (long) f * hop - nfft / 2;
        for (int i = 0; i < nfft; ++i)
        {
            const long idx = start + i;
            const float s = (idx >= 0 && idx < (long) y.size()) ? y[(size_t) idx] : 0.0f;
            buf[(size_t) i] = { s * w[(size_t) i], 0.0f };
        }
        fft (buf);
        for (int b = 0; b < bins; ++b)
            m.at (b, f) = std::abs (buf[(size_t) b]);
    }
    return m;
}

Matrix semitoneSpectrogram (const Signal& y, double sr, int midiLow, int nSemitones, int hop)
{
    const int frames = numFrames (y.size(), hop);
    Matrix m (nSemitones, frames);
    // Q of a 1/3-semitone constant-Q transform (like librosa with 36 bins per
    // octave): neighbouring semitones don't leak into each other.
    const double Q = 1.0 / (std::pow (2.0, 1.0 / 36.0) - 1.0);
    // Decimation pyramid so low notes use short kernels on a slower signal.
    std::vector<Signal> pyramid { y };
    std::vector<int> factors { 1 };
    while (factors.back() * 2 <= hop)
    {
        pyramid.push_back (resample (pyramid.back(), 2.0, 1.0));
        factors.push_back (factors.back() * 2);
    }
    for (int k = 0; k < nSemitones; ++k)
    {
        const double f = 440.0 * std::pow (2.0, (midiLow + k - 69) / 12.0);
        if (f >= sr / 2)
            break;
        size_t level = 0;
        while (level + 1 < factors.size() && sr / factors[level + 1] >= 4.0 * f)
            ++level;
        const Signal& x = pyramid[level];
        const double srk = sr / factors[level];
        const double hopk = (double) hop / factors[level];
        const int len = std::max (16, (int) std::ceil (Q * srk / f));
        std::vector<std::complex<float>> kern ((size_t) len);
        double wsum = 0;
        for (int i = 0; i < len; ++i)
        {
            const double w = 0.5 - 0.5 * std::cos (2 * kPi * i / len);
            const double ph = -2 * kPi * f * (i - len / 2) / srk;
            kern[(size_t) i] = { (float) (w * std::cos (ph)), (float) (w * std::sin (ph)) };
            wsum += w;
        }
        const float scale = (float) (2.0 / wsum);
        for (int fr = 0; fr < frames; ++fr)
        {
            const long start = (long) std::lround (fr * hopk) - len / 2;
            std::complex<float> acc (0, 0);
            const long i0 = std::max (0L, -start), i1 = std::min ((long) len, (long) x.size() - start);
            for (long i = i0; i < i1; ++i)
                acc += kern[(size_t) i] * x[(size_t) (start + i)];
            m.at (k, fr) = std::abs (acc) * scale;
        }
    }
    return m;
}

std::vector<float> onsetStrength (const Matrix& mag)
{
    std::vector<float> env ((size_t) mag.cols, 0.0f);
    for (int f = 1; f < mag.cols; ++f)
    {
        double s = 0;
        for (int b = 0; b < mag.rows; ++b)
        {
            const double cur = std::log1p (1000.0 * mag.at (b, f));
            const double prev = std::log1p (1000.0 * mag.at (b, f - 1));
            s += std::max (0.0, cur - prev);
        }
        env[(size_t) f] = (float) (s / mag.rows);
    }
    return env;
}

std::vector<int> pickOnsets (const std::vector<float>& envIn, double sr, int hop, float delta, double waitSeconds)
{
    std::vector<int> out;
    if (envIn.empty())
        return out;
    // Normalise to 0..1 like librosa.
    const float mn = *std::min_element (envIn.begin(), envIn.end());
    const float mx = *std::max_element (envIn.begin(), envIn.end());
    if (mx - mn <= 0)
        return out;
    std::vector<float> env (envIn.size());
    for (size_t i = 0; i < env.size(); ++i)
        env[i] = (envIn[i] - mn) / (mx - mn);
    const int preMax = std::max (1, (int) (0.03 * sr / hop)), postMax = 1;
    const int preAvg = std::max (1, (int) (0.10 * sr / hop)), postAvg = std::max (1, (int) (0.10 * sr / hop));
    const int wait = std::max (1, (int) (waitSeconds * sr / hop));
    int last = -wait - 1;
    const int n = (int) env.size();
    for (int i = 0; i < n; ++i)
    {
        float lmax = 0;
        for (int j = std::max (0, i - preMax); j <= std::min (n - 1, i + postMax); ++j)
            lmax = std::max (lmax, env[(size_t) j]);
        if (env[(size_t) i] < lmax)
            continue;
        double avg = 0;
        int cnt = 0;
        for (int j = std::max (0, i - preAvg); j <= std::min (n - 1, i + postAvg); ++j, ++cnt)
            avg += env[(size_t) j];
        avg /= std::max (1, cnt);
        if (env[(size_t) i] < avg + delta)
            continue;
        if (i - last <= wait)
            continue;
        out.push_back (i);
        last = i;
    }
    return out;
}

std::vector<double> refineOnsetTimes (const Signal& y, double sr, const std::vector<double>& times,
                                      double before, double after)
{
    // Energy in two back-to-back windows (each spans 2+ periods of low notes,
    // so there is no waveform ripple). The split point with the biggest log
    // energy rise is the attack; the size of that rise says whether it's real.
    const int hop = 32, win = 512;
    // Hann-weighted energy: no ripple from partial cycles of low notes.
    std::vector<double> hw ((size_t) win);
    for (int i = 0; i < win; ++i) hw[(size_t) i] = 0.5 - 0.5 * std::cos (2 * kPi * (i + 0.5) / win);
    auto energy = [&] (long a, long b) {
        double e = 0;
        for (long i = a; i < b; ++i)
            if (i >= 0 && i < (long) y.size()) e += hw[(size_t) (i - a)] * y[(size_t) i] * y[(size_t) i];
        return e / (double) win;
    };
    double peak = 1e-12;
    for (size_t i = 0; i + (size_t) win < y.size(); i += (size_t) win) peak = std::max (peak, energy ((long) i, (long) i + win));
    const double floorE = peak * 1e-5;
    std::vector<double> out;
    for (double t : times)
    {
        const long a = (long) ((t - before) * sr), b = (long) ((t + after) * sr);
        long bestPos = -1;
        double bestRise = 0;
        for (long split = std::max (0L, a); split <= b; split += hop)
        {
            const double pre = energy (split - win, split), post = energy (split, split + win);
            const double rise = std::log ((post + floorE) / (pre + floorE));
            if (rise > bestRise) { bestRise = rise; bestPos = split; }
        }
        // No real energy rise: a false onset (decay wobble, fade-out) -> drop it.
        if (bestPos >= 0 && bestRise > 0.12)
            out.push_back ((double) bestPos / sr);
    }
    return out;
}

static float median (std::vector<float>& v)
{
    const size_t mid = v.size() / 2;
    std::nth_element (v.begin(), v.begin() + (long) mid, v.end());
    return v[mid];
}

void hpssMasks (const Matrix& mag, Matrix& hMask, Matrix& pMask, int kernel, float power)
{
    const int half = kernel / 2;
    Matrix H (mag.rows, mag.cols), P (mag.rows, mag.cols);
    std::vector<float> buf;
    buf.reserve ((size_t) kernel);
    for (int r = 0; r < mag.rows; ++r)
        for (int c = 0; c < mag.cols; ++c)
        {
            buf.clear();
            for (int j = c - half; j <= c + half; ++j)
                buf.push_back (j >= 0 && j < mag.cols ? mag.at (r, j) : 0.0f);
            H.at (r, c) = median (buf);
            buf.clear();
            for (int j = r - half; j <= r + half; ++j)
                buf.push_back (j >= 0 && j < mag.rows ? mag.at (j, c) : 0.0f);
            P.at (r, c) = median (buf);
        }
    hMask = Matrix (mag.rows, mag.cols);
    pMask = Matrix (mag.rows, mag.cols);
    for (size_t i = 0; i < mag.data.size(); ++i)
    {
        const float h = std::pow (H.data[i], power), p = std::pow (P.data[i], power);
        const float tot = h + p;
        hMask.data[i] = tot > 0 ? h / tot : 0.5f;
        pMask.data[i] = tot > 0 ? p / tot : 0.5f;
    }
}

Signal applyMask (const Signal& y, const Matrix& mask, int nfft, int hop)
{
    const int frames = numFrames (y.size(), hop);
    const auto w = hann (nfft);
    Signal out (y.size() + (size_t) nfft, 0.0f);
    std::vector<float> norm (out.size(), 0.0f);
    std::vector<std::complex<float>> buf ((size_t) nfft);
    for (int f = 0; f < frames && f < mask.cols; ++f)
    {
        const long start = (long) f * hop - nfft / 2;
        for (int i = 0; i < nfft; ++i)
        {
            const long idx = start + i;
            const float s = (idx >= 0 && idx < (long) y.size()) ? y[(size_t) idx] : 0.0f;
            buf[(size_t) i] = { s * w[(size_t) i], 0.0f };
        }
        fft (buf);
        for (int b = 0; b < nfft; ++b)
        {
            const int bin = b <= nfft / 2 ? b : nfft - b;
            buf[(size_t) b] *= mask.at (bin, f);
        }
        // inverse FFT via conjugation
        for (auto& v : buf) v = std::conj (v);
        fft (buf);
        for (int i = 0; i < nfft; ++i)
        {
            const long idx = start + i + nfft / 2;
            if (idx < 0 || idx >= (long) out.size())
                continue;
            out[(size_t) idx] += std::conj (buf[(size_t) i]).real() / (float) nfft * w[(size_t) i];
            norm[(size_t) idx] += w[(size_t) i] * w[(size_t) i];
        }
    }
    Signal res (y.size());
    for (size_t i = 0; i < y.size(); ++i)
    {
        const size_t idx = i + (size_t) nfft / 2;
        res[i] = norm[idx] > 1e-6f ? out[idx] / norm[idx] : 0.0f;
    }
    return res;
}

double frameToTime (int frame, double sr, int hop) { return (double) frame * hop / sr; }
int timeToFrame (double t, double sr, int hop) { return (int) std::floor (t * sr / hop); }

std::vector<int> medianFilter (const std::vector<int>& x, int size)
{
    const int n = (int) x.size(), half = size / 2;
    std::vector<int> out (x.size());
    std::vector<int> buf;
    for (int i = 0; i < n; ++i)
    {
        buf.clear();
        for (int j = i - half; j <= i + half; ++j)
            buf.push_back (x[(size_t) std::clamp (j, 0, n - 1)]);
        std::nth_element (buf.begin(), buf.begin() + half, buf.end());
        out[(size_t) i] = buf[(size_t) half];
    }
    return out;
}

Signal trimSilence (const Signal& y, double topDb, size_t* startOut)
{
    const int frame = 2048, hop = 512;
    if (y.size() < (size_t) frame)
    {
        if (startOut) *startOut = 0;
        return y;
    }
    std::vector<double> rms;
    for (size_t s = 0; s + (size_t) frame <= y.size(); s += hop)
    {
        double e = 0;
        for (int i = 0; i < frame; ++i)
            e += (double) y[s + (size_t) i] * y[s + (size_t) i];
        rms.push_back (std::sqrt (e / frame));
    }
    const double peak = *std::max_element (rms.begin(), rms.end());
    if (peak <= 0)
    {
        if (startOut) *startOut = 0;
        return y;
    }
    const double thr = peak * std::pow (10.0, -topDb / 20.0);
    size_t first = 0, last = rms.size() - 1;
    while (first < rms.size() && rms[first] <= thr) ++first;
    while (last > first && rms[last] <= thr) --last;
    const size_t a = first * hop, b = std::min (y.size(), last * hop + (size_t) frame);
    if (startOut) *startOut = a;
    return Signal (y.begin() + (long) a, y.begin() + (long) b);
}

} // namespace zampler::dsp
