"""Audio -> notes, key, tempo and chords.

The transcription is monophonic (the most prominent pitch at each moment,
via librosa's pYIN). Key and chords use the full harmonic content (CQT chroma),
so they also work on chordal loops where the melody tracker only sees the top
voice.
"""

from __future__ import annotations

import io
from dataclasses import dataclass, field

import librosa
import numpy as np
import scipy.ndimage

from .theory import Chord, Key, detect_key, match_chord

SR = 22050
HOP = 256


@dataclass
class Note:
    start: float  # seconds
    end: float  # seconds
    pitch: int  # MIDI
    velocity: int = 100
    in_key: bool = True

    @property
    def duration(self) -> float:
        return self.end - self.start


@dataclass
class Analysis:
    duration: float
    bpm: float
    key: Key
    key_confidence: float
    notes: list[Note]
    pitch_profile: np.ndarray  # 12 bins, what the sample actually uses
    bar_chords: list[tuple[float, Chord]] = field(default_factory=list)  # (start sec, chord)
    beats: np.ndarray = field(default_factory=lambda: np.zeros(0))

    @property
    def seconds_per_beat(self) -> float:
        return 60.0 / self.bpm


def load_audio(source, max_seconds: float | None = 90.0) -> tuple[np.ndarray, int]:
    """Load any file librosa/soundfile can read (path, bytes or file-like) as
    mono float32 at :data:`SR`."""
    if isinstance(source, (bytes, bytearray)):
        source = io.BytesIO(source)
    y, sr = librosa.load(source, sr=SR, mono=True, duration=max_seconds)
    return y.astype(np.float32), int(sr)


def estimate_tempo(y: np.ndarray, sr: int, hint: float | None = None) -> tuple[float, np.ndarray]:
    """Tempo (BPM) folded into 70..180 and beat times in seconds."""
    onset_env = librosa.onset.onset_strength(y=y, sr=sr, hop_length=512)
    if hint:
        _, beats = librosa.beat.beat_track(onset_envelope=onset_env, sr=sr, hop_length=512,
                                           bpm=float(hint), units="time")
        return float(hint), np.asarray(beats)
    tempo = librosa.feature.tempo(onset_envelope=onset_env, sr=sr, hop_length=512)
    bpm = float(np.atleast_1d(tempo)[0]) if np.size(tempo) else 120.0
    bpm = bpm if bpm > 0 else 120.0
    while bpm < 70:
        bpm *= 2
    while bpm > 180:
        bpm /= 2
    _, beats = librosa.beat.beat_track(onset_envelope=onset_env, sr=sr, hop_length=512, bpm=bpm, units="time")
    return bpm, np.asarray(beats)


def refine_tempo(bpm: float, onsets: list[float], search: float = 0.06) -> float:
    """Nudge a rough tempo so note onsets land on a 16th-note grid.

    Tries every BPM (0.1 steps, integers preferred) within +-``search`` of the
    estimate and keeps the one with the smallest mean grid error, measured
    relative to the first onset. Trackers love integer tempos, so integer
    candidates win ties.
    """
    if len(onsets) < 4:
        return round(bpm, 1)
    on = np.asarray(onsets) - onsets[0]
    best = (np.inf, bpm)
    for cand in np.arange(bpm * (1 - search), bpm * (1 + search), 0.1):
        cand = round(float(cand), 1)
        step = 60.0 / cand / 4
        phase = on / step
        err = float(np.mean(np.abs(phase - np.round(phase)))) + (0.0 if cand.is_integer() else 0.004)
        if err < best[0] - 1e-9:
            best = (err, cand)
    for c in (round(best[1]),):  # snap to integer if it's essentially as good
        step = 60.0 / c / 4
        phase = on / step
        if float(np.mean(np.abs(phase - np.round(phase)))) <= best[0] + 0.01:
            return float(c)
    return best[1]


def transcribe(
    y: np.ndarray,
    sr: int,
    fmin: str = "C1",
    fmax: str = "C7",
    min_note: float = 0.06,
    voicing_threshold: float = 0.1,
) -> list[Note]:
    """Monophonic note transcription.

    pYIN gives a pitch track; the track is rounded to semitones, median-smoothed
    to remove vibrato/glide flicker, then cut into notes wherever the pitch
    changes, voicing drops out, or an onset (re-attack of the same pitch) occurs.
    """
    if len(y) < sr * 0.05:
        return []
    f0, voiced, vprob = librosa.pyin(
        y, fmin=librosa.note_to_hz(fmin), fmax=librosa.note_to_hz(fmax), sr=sr,
        frame_length=2048, hop_length=HOP, fill_na=np.nan,
    )
    voiced = voiced & (vprob >= voicing_threshold)
    midi = np.full(f0.shape, -1, dtype=int)
    ok = voiced & np.isfinite(f0)
    midi[ok] = np.round(librosa.hz_to_midi(f0[ok])).astype(int)
    # Smooth: 5-frame median (~58 ms) on voiced regions only.
    smooth = scipy.ndimage.median_filter(midi, size=5, mode="nearest")
    smooth[~ok] = -1

    rms = librosa.feature.rms(y=y, frame_length=2048, hop_length=HOP)[0]
    rms = rms[: len(smooth)] if len(rms) >= len(smooth) else np.pad(rms, (0, len(smooth) - len(rms)))
    peak = rms.max() if rms.size and rms.max() > 0 else 1.0

    onset_env = librosa.onset.onset_strength(y=y, sr=sr, hop_length=HOP)
    onset_times = librosa.onset.onset_detect(onset_envelope=onset_env, sr=sr, hop_length=HOP,
                                             backtrack=True, units="time")

    frame_t = librosa.frames_to_time(np.arange(len(smooth) + 1), sr=sr, hop_length=HOP)
    notes: list[Note] = []
    start = None
    cur = -1

    def close(end_idx: int):
        if start is None or cur < 0:
            return
        t0, t1 = frame_t[start], frame_t[end_idx]
        if t1 - t0 >= min_note:
            level = float(np.sqrt(np.mean(rms[start:end_idx] ** 2)) / peak) if end_idx > start else 0.5
            vel = int(np.clip(40 + 87 * level, 1, 127))
            notes.append(Note(float(t0), float(t1), int(cur), vel))

    for i, p in enumerate(smooth):
        if p != cur:
            close(i)
            start, cur = (i, int(p)) if p >= 0 else (None, -1)
    close(len(smooth))
    return _split_at_onsets(_merge_short_gaps(notes), onset_times, min_note)


def _split_at_onsets(notes: list[Note], onset_times: np.ndarray, min_note: float) -> list[Note]:
    """Split held notes where a new attack happens on the same pitch, so
    repeated notes (C C C) are not collapsed into one long note."""
    out: list[Note] = []
    for n in notes:
        cuts = [t for t in onset_times if n.start + min_note <= t <= n.end - min_note]
        edges = [n.start, *cuts, n.end]
        for a, b in zip(edges[:-1], edges[1:]):
            out.append(Note(float(a), float(b), n.pitch, n.velocity))
    return out


def _merge_short_gaps(notes: list[Note], gap: float = 0.03) -> list[Note]:
    """Join same-pitch fragments separated by tiny unvoiced dropouts."""
    out: list[Note] = []
    for n in notes:
        if out and out[-1].pitch == n.pitch and n.start - out[-1].end <= gap:
            out[-1].end = n.end
            out[-1].velocity = max(out[-1].velocity, n.velocity)
        else:
            out.append(n)
    return out


def chroma_profile(y: np.ndarray, sr: int) -> np.ndarray:
    chroma = librosa.feature.chroma_cqt(y=y, sr=sr, hop_length=512)
    return chroma.sum(axis=1)


def bar_chords(y: np.ndarray, sr: int, bpm: float, key: Key, beats_per_bar: int = 4,
               offset: float = 0.0) -> list[tuple[float, Chord]]:
    """Best-matching chord per bar."""
    chroma = librosa.feature.chroma_cqt(y=y, sr=sr, hop_length=512)
    times = librosa.frames_to_time(np.arange(chroma.shape[1]), sr=sr, hop_length=512)
    bar_len = 60.0 / bpm * beats_per_bar
    out = []
    t = offset
    dur = len(y) / sr
    while t < dur - bar_len * 0.25:
        sel = (times >= t) & (times < t + bar_len)
        if sel.any():
            chord, _ = match_chord(chroma[:, sel].mean(axis=1), key)
            out.append((t, chord))
        t += bar_len
    return out


def analyze(source, bpm_hint: float | None = None, max_seconds: float | None = 90.0,
            fmin: str = "C1", fmax: str = "C7") -> Analysis:
    y, sr = load_audio(source, max_seconds) if not isinstance(source, np.ndarray) else (source, SR)
    y, _ = librosa.effects.trim(y, top_db=50)
    duration = len(y) / sr
    bpm, beats = estimate_tempo(y, sr, bpm_hint)
    notes = transcribe(y, sr, fmin=fmin, fmax=fmax)
    if not bpm_hint:
        bpm = refine_tempo(bpm, [n.start for n in notes])

    # Key: blend harmonic chroma with the transcribed notes (duration weighted)
    harm = chroma_profile(y, sr)
    harm = harm / harm.sum() if harm.sum() > 0 else harm
    mel = np.zeros(12)
    for n in notes:
        mel[n.pitch % 12] += n.duration
    mel = mel / mel.sum() if mel.sum() > 0 else mel
    profile = 0.5 * harm + 0.5 * mel if mel.sum() > 0 else harm
    key, conf = detect_key(profile)
    for n in notes:
        n.in_key = key.contains(n.pitch)

    chords = bar_chords(y, sr, bpm, key)
    return Analysis(duration=duration, bpm=bpm, key=key, key_confidence=conf, notes=notes,
                    pitch_profile=profile, bar_chords=chords, beats=beats)
