"""Polyphonic note extraction: every note inside chords, not just the top line.

Method (no ML model, so it runs anywhere):

1. Signal (optionally with drums removed by HPSS) -> constant-Q
   spectrum, 3 bins per semitone pooled to 1 bin per semitone.
2. Cut the audio into segments at onsets. For each segment take the spectrum
   just after the attack.
3. Iteratively pick the pitch with the highest harmonic salience (energy at the
   fundamental + weighted overtones), then subtract that note's expected
   overtones so they aren't mistaken for extra notes. Stop when what's left is
   too quiet or ``max_poly`` notes are found.
4. Stitch segments: a pitch that keeps sounding across a segment boundary
   without being re-attacked is one held note.
"""

from __future__ import annotations

import librosa
import numpy as np

from .analysis import Note

HOP = 512
BINS_PER_SEMI = 3
HARMONICS = 6
_HARM_OFFS = [int(round(12 * np.log2(h))) for h in range(1, HARMONICS + 1)]  # 0,12,19,24,28,31
_HARM_W = [0.9 ** (h - 1) for h in range(1, HARMONICS + 1)]


def semitone_spectrogram(y: np.ndarray, sr: int, fmin: str = "C1", octaves: int = 7) -> tuple[np.ndarray, int]:
    """|CQT| pooled to one row per semitone. Returns (S[semitones, frames], midi of row 0)."""
    lo = librosa.note_to_midi(fmin)
    C = np.abs(librosa.cqt(y, sr=sr, hop_length=HOP, fmin=librosa.midi_to_hz(lo - 1 / BINS_PER_SEMI),
                           n_bins=12 * octaves * BINS_PER_SEMI, bins_per_octave=12 * BINS_PER_SEMI))
    S = C.reshape(12 * octaves, BINS_PER_SEMI, -1).max(axis=1)
    return S, int(lo)


def _is_peak(s: np.ndarray) -> np.ndarray:
    left = np.concatenate([[0.0], s[:-1]])
    right = np.concatenate([s[1:], [0.0]])
    return (s >= left) & (s >= right) & (s > 1.3 * np.minimum(left, right) + 1e-12)


def _pick_notes(spec: np.ndarray, floor: float, max_poly: int, rel: float,
                peaks_only: bool = True) -> list[tuple[int, float]]:
    """Iterative harmonic-salience peak picking on one semitone spectrum."""
    s = spec.astype(float).copy()
    n = len(s)
    picked: list[tuple[int, float]] = []
    loudest = 0.0
    for _ in range(max_poly):
        sal = np.zeros(n)
        for off, w in zip(_HARM_OFFS, _HARM_W):
            sal[: n - off] += w * s[off:]
        # A note needs its own fundamental, not just overtones, and that
        # fundamental must be a local peak (noise and drum wash are flat).
        sal[s < floor] = 0
        sal[peaks_only & ~_is_peak(s)] = 0
        p = int(np.argmax(sal))
        if sal[p] <= 0:
            break
        amp = s[p]
        # Judge on the note's own (residual) fundamental: quiet leftovers stop the search.
        if amp < rel * loudest:
            break
        loudest = max(loudest, amp)
        picked.append((p, float(amp)))
        # Remove the note's overtones, but only as much as a smooth harmonic
        # series explains (spectral smoothness, Klapuri 2003). An overtone bin
        # that sticks out above its neighbouring harmonics is kept: that's
        # another note sitting on top (e.g. a melody an octave above a chord).
        idx = [p + off for off in _HARM_OFFS if p + off < n]
        obs = [s[i] for i in idx]
        for h in range(1, len(idx)):
            nbrs = [obs[h - 1]] + ([obs[h + 1]] if h + 1 < len(obs) else [])
            s[idx[h]] = max(0.0, s[idx[h]] - min(obs[h], sum(nbrs) / len(nbrs)))
        s[p] = 0
        for nb in (p - 1, p + 1):  # spectral leakage
            if 0 <= nb < n:
                s[nb] = max(0.0, s[nb] - 0.5 * amp)
    return picked


def transcribe_poly(y: np.ndarray, sr: int, fmin: str = "C1", fmax: str = "C7", max_poly: int = 6,
                    rel: float = 0.18, min_note: float = 0.06, remove_drums: bool = False) -> list[Note]:
    if len(y) < sr * 0.05:
        return []
    # HPSS would strip drums, but it also eats short plucky notes, so it's opt-in.
    S, lo = semitone_spectrogram(librosa.effects.harmonic(y, margin=1.0) if remove_drums else y, sr, fmin)
    hi = librosa.note_to_midi(fmax)
    n_frames = S.shape[1]
    peak = S.max() if S.size and S.max() > 0 else 1.0
    floor = 0.04 * peak

    onset_env = librosa.onset.onset_strength(y=y, sr=sr, hop_length=HOP)
    bounds = librosa.onset.onset_detect(onset_envelope=onset_env, sr=sr, hop_length=HOP, backtrack=False)
    bounds = sorted({0, *[int(b) for b in bounds if 0 < b < n_frames], n_frames})
    min_frames = max(1, int(round(min_note * sr / HOP)))
    times = librosa.frames_to_time(np.arange(n_frames + 1), sr=sr, hop_length=HOP)

    notes: list[Note] = []
    active: dict[int, Note] = {}  # pitch -> note still sounding at the previous boundary
    prev_level: dict[int, float] = {}
    for a, b in zip(bounds[:-1], bounds[1:]):
        if b - a < min_frames:
            continue
        # Look just after the attack (skip the noisy transient frame).
        w0 = min(b - 1, a + 1)
        w1 = max(w0 + 1, min(b, a + max(2, (b - a) // 2)))
        seg = S[:, w0:w1].mean(axis=1)
        picks = _pick_notes(seg, floor, max_poly, rel)
        current: dict[int, Note] = {}
        for row, amp in picks:
            pitch = lo + row
            if pitch > hi:
                continue
            # Where does this note stop sounding inside the segment?
            env = S[row, a:b]
            alive = np.nonzero(env >= 0.15 * env.max())[0]
            end_f = a + (int(alive[-1]) + 1 if alive.size else b - a)
            start_level = float(S[row, a:min(b, a + 3)].max())
            held = (pitch in active and active[pitch].end >= times[a] - 1e-6
                    and start_level <= 1.25 * prev_level.get(pitch, 0.0))
            if held:
                note = active[pitch]
                note.end = float(times[end_f])
            else:
                vel = int(np.clip(40 + 87 * (amp / peak) ** 0.5, 1, 127))
                note = Note(float(times[a]), float(times[end_f]), int(pitch), vel)
                notes.append(note)
            current[pitch] = note
            prev_level[pitch] = float(S[row, max(a, b - 3):b].mean())
        # Only notes that sounded right up to the boundary can be held over.
        active = {p: n for p, n in current.items() if n.end >= times[b] - 2 * HOP / sr}
    return [n for n in sorted(notes, key=lambda n: (n.start, n.pitch)) if n.duration >= min_note]
