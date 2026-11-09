"""Drum / rhythm guide: find kick, snare and hi-hat hits in a sample.

Hits are found on the percussive part of the signal (HPSS), then each hit is
labelled from how much energy *appears* in three bands right after it
(low < 150 Hz, mid 250 Hz-4 kHz, high > 6 kHz), measured against the moment
just before it. Using the jump rather than the absolute level stops a
sustained bassline from making every hit look like a kick. A hit can carry
more than one label (kick + hat on the same 16th is common).
"""

from __future__ import annotations

from dataclasses import dataclass

import librosa
import numpy as np

HOP = 256
KICK, SNARE, HAT = "kick", "snare", "hat"
# General MIDI drum notes (channel 10).
GM = {KICK: 36, SNARE: 38, HAT: 42}


@dataclass
class DrumHit:
    time: float
    kind: str
    velocity: int = 100


def detect_drums(y: np.ndarray, sr: int, sensitivity: float = 0.5,
                 min_percussive: float = 0.06) -> list[DrumHit]:
    """Kick/snare/hat hits. Returns nothing when the sample has no real drums
    (percussive energy below ``min_percussive`` of the total), so a plucked
    melody's attacks aren't mistaken for a beat."""
    if len(y) < sr * 0.05:
        return []
    yp = librosa.effects.percussive(y, margin=2.0)
    total_energy = float(np.sum(y.astype(float) ** 2)) or 1.0
    if float(np.sum(yp.astype(float) ** 2)) / total_energy < min_percussive:
        return []
    n_fft = 1024
    D = np.abs(librosa.stft(yp, n_fft=n_fft, hop_length=HOP)) ** 2
    freqs = librosa.fft_frequencies(sr=sr, n_fft=n_fft)
    bands = {
        KICK: (freqs >= 30) & (freqs < 150),
        SNARE: (freqs >= 250) & (freqs < 4000),
        HAT: freqs >= 6000,
    }
    E = {k: D[m].sum(axis=0) for k, m in bands.items()}

    env = librosa.onset.onset_strength(y=yp, sr=sr, hop_length=HOP)
    delta = 0.02 + 0.13 * (1 - sensitivity)
    frames = librosa.onset.onset_detect(onset_envelope=env, sr=sr, hop_length=HOP, delta=delta,
                                        normalize=True, backtrack=False)
    frames = list(frames)
    # A hit right at the start has no "before", so the onset detector can miss it.
    total = sum(E.values())
    if total.size and total[:4].max() > 0.1 * total.max() and (not frames or frames[0] > 4):
        frames.insert(0, 0)
    if not frames:
        return []
    frames = np.asarray(frames)
    strength = np.array([env[f: f + 4].max() if f < len(env) else 0.0 for f in frames])
    strength = strength / (strength.max() or 1)
    after_n = max(1, int(0.05 * sr / HOP))
    before_n = max(1, int(0.03 * sr / HOP))

    jumps = {k: np.zeros(len(frames)) for k in E}
    for i, f in enumerate(frames):
        for k, e in E.items():
            # The onset frame lands a frame or two after the attack, so compare
            # the peak around/after it with the quietest point just before.
            a = e[max(0, f - 2): f + after_n].max() if f < len(e) else 0.0
            b = e[max(0, f - before_n - 3): max(0, f - 2)].min() if f > 2 else 0.0
            jumps[k][i] = max(0.0, a - b)
    # Scale each band by its biggest jump so a hat (physically small) and a
    # kick (physically huge) are judged on the same footing.
    peak = {k: (v.max() or 1.0) for k, v in jumps.items()}
    floor = 0.15 + 0.2 * (1 - sensitivity)

    hits: list[DrumHit] = []
    times = librosa.frames_to_time(frames, sr=sr, hop_length=HOP)
    for i, t in enumerate(times):
        sc = {k: jumps[k][i] / peak[k] for k in E}
        labels = []
        if sc[KICK] >= floor + 0.1:
            labels.append(KICK)
        # Snare: a strong mid jump that isn't just the kick's click.
        if sc[SNARE] >= floor + 0.1 and (KICK not in labels or sc[SNARE] > 0.85):
            labels.append(SNARE)
        # Hat: the top band jumps clearly more than the mids (a snare's noise
        # also reaches the top band, but its mids jump too).
        if sc[HAT] >= floor * 0.4 and sc[HAT] >= 1.5 * sc[SNARE]:
            labels.append(HAT)
        if not labels:
            labels = [max(sc, key=sc.get)]
        vel = int(np.clip(50 + 77 * strength[i], 1, 127))
        hits.extend(DrumHit(float(t), k, vel) for k in labels)
    return hits


def kick_lines(hits: list[DrumHit], to_line) -> list[int]:
    """Grid lines of kick hits (for 'follow the kick' basslines)."""
    return sorted({to_line(h.time) for h in hits if h.kind == KICK})
