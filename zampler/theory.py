"""Music theory helpers: note names, scales, key detection, scale lock, chords.

Note numbers are MIDI numbers throughout (60 = middle C). Renoise shows MIDI 60
as ``C-4`` (Renoise note value 48), so the Renoise octave is ``midi // 12 - 1``.
"""

from __future__ import annotations

import re
from dataclasses import dataclass

import numpy as np

NOTE_NAMES = ["C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"]
FLAT_ALIASES = {"DB": "C#", "EB": "D#", "GB": "F#", "AB": "G#", "BB": "A#",
                "CB": "B", "FB": "E", "E#": "F", "B#": "C"}

SCALES: dict[str, tuple[int, ...]] = {
    "major": (0, 2, 4, 5, 7, 9, 11),
    "minor": (0, 2, 3, 5, 7, 8, 10),
    "harmonic minor": (0, 2, 3, 5, 7, 8, 11),
    "melodic minor": (0, 2, 3, 5, 7, 9, 11),
    "dorian": (0, 2, 3, 5, 7, 9, 10),
    "phrygian": (0, 1, 3, 5, 7, 8, 10),
    "lydian": (0, 2, 4, 6, 7, 9, 11),
    "mixolydian": (0, 2, 4, 5, 7, 9, 10),
    "locrian": (0, 1, 3, 5, 6, 8, 10),
    "major pentatonic": (0, 2, 4, 7, 9),
    "minor pentatonic": (0, 3, 5, 7, 10),
    "blues": (0, 3, 5, 6, 7, 10),
    "chromatic": tuple(range(12)),
}

# Krumhansl-Kessler key profiles.
_MAJOR_PROFILE = np.array([6.35, 2.23, 3.48, 2.33, 4.38, 4.09, 2.52, 5.19, 2.39, 3.66, 2.29, 2.88])
_MINOR_PROFILE = np.array([6.33, 2.68, 3.52, 5.38, 2.60, 3.53, 2.54, 4.75, 3.98, 2.69, 3.34, 3.17])


def note_name(midi: int, renoise: bool = False) -> str:
    """``60 -> 'C4'`` or, with ``renoise=True``, ``60 -> 'C-4'``."""
    midi = int(midi)
    name = NOTE_NAMES[midi % 12]
    octave = midi // 12 - 1
    if renoise:
        if len(name) == 1:
            name += "-"
        return f"{name}{octave}"
    return f"{name}{octave}"


_NOTE_RE = re.compile(r"^([A-G])([#B]?)-?(-?\d+)$")


def parse_note(text: str) -> int:
    """Parse ``'C4'``, ``'c#3'``, ``'Eb2'``, ``'C-4'`` (Renoise) or a MIDI number."""
    t = text.strip().upper()
    if not t:
        raise ValueError("empty note")
    if t.isdigit():
        return int(t)
    m = _NOTE_RE.match(t)
    if not m:
        raise ValueError(f"can't read note {text!r} (try C4, F#3, Bb2 or C-4)")
    letter, acc, octave = m.group(1), m.group(2), int(m.group(3))
    base = (octave + 1) * 12 + NOTE_NAMES.index(letter)
    return base + (1 if acc == "#" else -1 if acc == "B" else 0)


def parse_root(text: str) -> int:
    """Parse a pitch class name such as ``'F#'`` or ``'Bb'`` into 0..11."""
    t = text.strip().upper()
    t = FLAT_ALIASES.get(t, t)
    if t not in NOTE_NAMES:
        raise ValueError(f"unknown key root: {text!r}")
    return NOTE_NAMES.index(t)


@dataclass(frozen=True)
class Key:
    root: int  # pitch class 0..11
    scale: str = "minor"

    @property
    def intervals(self) -> tuple[int, ...]:
        return SCALES[self.scale]

    @property
    def pitch_classes(self) -> frozenset[int]:
        return frozenset((self.root + i) % 12 for i in self.intervals)

    @property
    def name(self) -> str:
        return f"{NOTE_NAMES[self.root]} {self.scale}"

    def contains(self, midi: int) -> bool:
        return int(midi) % 12 in self.pitch_classes

    def snap(self, midi: int, prefer: str = "nearest") -> int:
        """Move ``midi`` to the closest in-key note.

        Ties (a note exactly between two scale notes) go down by default,
        ``prefer='up'`` sends them up.
        """
        midi = int(midi)
        if self.contains(midi):
            return midi
        for d in range(1, 12):
            down, up = midi - d, midi + d
            cand = [up, down] if prefer == "up" else [down, up]
            for c in cand:
                if self.contains(c):
                    return c
        return midi

    def degree_note(self, degree: int, octave_midi: int) -> int:
        """Scale degree (0-based, may exceed scale length or be negative) above
        the key root placed in the octave starting at ``octave_midi`` (a C)."""
        n = len(self.intervals)
        octs, idx = divmod(degree, n)
        return octave_midi + self.root + self.intervals[idx] + 12 * octs

    def degree_of(self, midi: int) -> int | None:
        """Absolute scale-degree index of an in-key note (inverse of degree_note
        with ``octave_midi=0``)."""
        if not self.contains(midi):
            return None
        rel = int(midi) - self.root
        octs, pc = divmod(rel, 12)
        return octs * len(self.intervals) + self.intervals.index(pc)

    def step(self, midi: int, steps: int) -> int:
        """Move an in-key note by ``steps`` scale steps."""
        d = self.degree_of(self.snap(midi))
        return self.degree_note(d + steps, 0)

    def triad(self, degree: int, octave_midi: int = 48, size: int = 3) -> list[int]:
        """Diatonic stacked-thirds chord on ``degree`` (size 3 = triad, 4 = 7th)."""
        return [self.degree_note(degree + 2 * i, octave_midi) for i in range(size)]


def detect_key(chroma_profile: np.ndarray) -> tuple[Key, float]:
    """Krumhansl-Schmuckler key finding on a 12-bin pitch-class profile.

    Returns the best :class:`Key` (major or minor) and a 0..1 confidence
    (correlation of the winner).
    """
    profile = np.asarray(chroma_profile, dtype=float)
    if profile.shape != (12,) or not np.any(profile > 0):
        return Key(0, "minor"), 0.0
    best: tuple[float, Key] = (-2.0, Key(0, "minor"))
    for root in range(12):
        for scale, ref in (("major", _MAJOR_PROFILE), ("minor", _MINOR_PROFILE)):
            r = np.corrcoef(profile, np.roll(ref, root))[0, 1]
            if np.isfinite(r) and r > best[0]:
                best = (r, Key(root, scale))
    return best[1], float(max(0.0, best[0]))


# Chord templates for per-bar chord recognition (root position pitch classes).
CHORD_QUALITIES: dict[str, tuple[int, ...]] = {
    "": (0, 4, 7),
    "m": (0, 3, 7),
    "dim": (0, 3, 6),
    "sus2": (0, 2, 7),
    "sus4": (0, 5, 7),
    "7": (0, 4, 7, 10),
    "maj7": (0, 4, 7, 11),
    "m7": (0, 3, 7, 10),
}


@dataclass(frozen=True)
class Chord:
    root: int
    quality: str

    @property
    def name(self) -> str:
        return f"{NOTE_NAMES[self.root]}{self.quality}"

    @property
    def pitch_classes(self) -> tuple[int, ...]:
        return tuple((self.root + i) % 12 for i in CHORD_QUALITIES[self.quality])

    def voicing(self, base_midi: int = 48) -> list[int]:
        """Close voicing starting from the root at or above ``base_midi``."""
        root = base_midi + ((self.root - base_midi) % 12)
        return [root + i for i in CHORD_QUALITIES[self.quality]]


def match_chord(chroma: np.ndarray, key: Key | None = None) -> tuple[Chord, float]:
    """Best chord template for a 12-bin chroma vector. When ``key`` is given,
    in-key chords get a small bonus so ambiguous bars resolve diatonically."""
    v = np.asarray(chroma, dtype=float)
    norm = np.linalg.norm(v)
    if norm == 0:
        return Chord(key.root if key else 0, "m" if key and "minor" in key.scale else ""), 0.0
    v = v / norm
    best = (-1.0, Chord(0, ""))
    for root in range(12):
        for q, ivs in CHORD_QUALITIES.items():
            t = np.zeros(12)
            for i in ivs:
                t[(root + i) % 12] = 1.0
            t /= np.linalg.norm(t)
            score = float(v @ t)
            if len(ivs) == 4:
                score -= 0.03  # prefer plain triads unless the 7th is clearly there
            if key is not None and all(key.contains(pc) for pc in (Chord(root, q).pitch_classes)):
                score += 0.05
            if score > best[0]:
                best = (score, Chord(root, q))
    return best[1], best[0]


def diatonic_chord(key: Key, degree: int, sevenths: bool = False) -> Chord:
    """Name the diatonic chord on a 0-based scale degree of a 7-note key."""
    notes = key.triad(degree, 0, 4 if sevenths else 3)
    root = notes[0] % 12
    ivs = tuple(sorted({(n - notes[0]) % 12 for n in notes}))
    for q, t in CHORD_QUALITIES.items():
        if tuple(sorted(t)) == ivs:
            return Chord(root, q)
    # Fallback for exotic scales: closest triad by third.
    third = (notes[1] - notes[0]) % 12
    return Chord(root, "m" if third == 3 else "")
