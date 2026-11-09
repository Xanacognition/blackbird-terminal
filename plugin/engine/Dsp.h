// Zampler engine: DSP building blocks (FFT, STFT, semitone CQT, onsets, HPSS).
#pragma once

#include <complex>
#include <vector>

namespace zampler::dsp
{

using Signal = std::vector<float>;
/** Row-major matrix: rows x cols, data[r * cols + c]. */
struct Matrix
{
    int rows = 0, cols = 0;
    std::vector<float> data;

    Matrix() = default;
    Matrix (int r, int c, float v = 0.0f) : rows (r), cols (c), data ((size_t) r * (size_t) c, v) {}
    float& at (int r, int c) { return data[(size_t) r * (size_t) cols + (size_t) c]; }
    float at (int r, int c) const { return data[(size_t) r * (size_t) cols + (size_t) c]; }
};

constexpr int kSampleRate = 22050;

/** Windowed-sinc resampling to kSampleRate (no-op if already there). */
Signal resample (const Signal& in, double inRate, double outRate = kSampleRate);

/** In-place radix-2 FFT. size must be a power of two. */
void fft (std::vector<std::complex<float>>& x);

/** |STFT|, Hann window, centred frames (zero padded). rows = nfft/2+1 bins, cols = frames. */
Matrix stftMagnitude (const Signal& y, int nfft, int hop);

/** Constant-Q magnitude with one bin per semitone (Hann-windowed direct kernels).
    rows = nSemitones starting at midiLow, cols = frames at hop. */
Matrix semitoneSpectrogram (const Signal& y, double sr, int midiLow, int nSemitones, int hop);

/** Spectral-flux onset strength (log-compressed, half-wave rectified), one value per STFT frame. */
std::vector<float> onsetStrength (const Matrix& stftMag);

/** Peak-picked onset frames (librosa-style local max + local mean + delta, with minimum gap). */
std::vector<int> pickOnsets (const std::vector<float>& env, double sr, int hop, float delta = 0.07f,
                             double waitSeconds = 0.03);

/** STFT-based onsets land early (the window sees an attack before it happens).
    Move each onset time to the steepest rise of a fine (~3 ms) energy envelope
    within [t - before, t + after]. Onsets with no real energy rise are dropped. */
std::vector<double> refineOnsetTimes (const Signal& y, double sr, const std::vector<double>& times,
                                      double before = 0.02, double after = 0.07);

/** Harmonic/percussive split of a magnitude spectrogram by median filtering
    (Fitzgerald 2010). Returns soft masks raised to `power`. */
void hpssMasks (const Matrix& mag, Matrix& harmonicMask, Matrix& percussiveMask, int kernel = 17, float power = 2.0f);

/** Inverse STFT of `mag * mask` using the original phase (recomputed). */
Signal applyMask (const Signal& y, const Matrix& mask, int nfft, int hop);

double frameToTime (int frame, double sr, int hop);
int timeToFrame (double t, double sr, int hop);

/** Running median over a vector of ints (odd size), edges use nearest. */
std::vector<int> medianFilter (const std::vector<int>& x, int size);

/** Trim leading/trailing silence below topDb relative to peak RMS. Returns the offset of the trimmed start. */
Signal trimSilence (const Signal& y, double topDb = 50.0, size_t* startOut = nullptr);

} // namespace zampler::dsp
