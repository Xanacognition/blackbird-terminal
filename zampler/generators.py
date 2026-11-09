"""Write basslines, chords and leads that fit the sample.

Everything here is scale-locked: generated pitches always go through
``Key.snap`` so a wrong note can't sneak in.
"""

from __future__ import annotations

import random

from .theory import Chord, Key, diatonic_chord
from .tracker import Grid, Part, Step

# Degree lists (0-based scale degrees), one chord per bar.
PROGRESSIONS: dict[str, list[int]] = {
    "From sample": [],
    "i - VI - III - VII": [0, 5, 2, 6],
    "i - iv - v - i": [0, 3, 4, 0],
    "I - V - vi - IV": [0, 4, 5, 3],
    "vi - IV - I - V": [5, 3, 0, 4],
    "ii - V - I - I": [1, 4, 0, 0],
    "i - VII - VI - VII": [0, 6, 5, 6],
    "i - i - iv - v": [0, 0, 3, 4],
}

BASS_STYLES = ["Root notes", "Eighths", "Root + fifth", "Octave bounce", "Walking", "Syncopated", "Follow the kick"]
CHORD_STYLES = ["Whole bar", "Half notes", "Off-beat stabs", "Quarter pulse", "Arp up", "Arp up/down"]
LEAD_STYLES = ["Motif", "Harmony (3rd above sample)", "Counter-melody", "Octave double"]


def progression(key: Key, name: str, bars: int, sample_chords: list[Chord] | None = None,
                sevenths: bool = False) -> list[Chord]:
    """One chord per bar, looped to ``bars`` bars."""
    if name == "From sample" or name not in PROGRESSIONS:
        base = list(sample_chords or []) or [diatonic_chord(key, 0, sevenths)]
    else:
        n = len(key.intervals)
        base = [diatonic_chord(key, d % n, sevenths) for d in PROGRESSIONS[name]]
    return [base[i % len(base)] for i in range(bars)]


def _root_in(chord: Chord, low: int) -> int:
    return low + ((chord.root - low) % 12)


def bassline(key: Key, chords: list[Chord], grid: Grid, style: str = "Root notes", octave: int = 2,
             velocity: int = 110, scale_lock: bool = True, kick_lines: list[int] | None = None) -> Part:
    """``kick_lines`` (grid lines of the sample's kicks) drives the
    'Follow the kick' style; without kicks it falls back to root notes."""
    low = (octave + 1) * 12  # C of that octave
    bar = grid.lines_per_bar
    beat = grid.lpb
    eighth = max(1, grid.lpb // 2)
    steps: list[Step] = []

    def add(line, length, pitch, vel=velocity):
        steps.append(Step(line, max(1, length), key.snap(pitch) if scale_lock else pitch, vel))

    for b, chord in enumerate(chords):
        t0 = b * bar
        root = _root_in(chord, low)
        fifth = root + 7
        nxt = _root_in(chords[(b + 1) % len(chords)], low)
        if style == "Follow the kick" and kick_lines:
            # Kick pattern of the matching bar of the sample (looped if the
            # sample is shorter), each bass note held until the next kick.
            n_sample_bars = max(1, -(-(max(kick_lines) + 1) // bar))
            src = (b % n_sample_bars) * bar
            hits = sorted({k - src for k in kick_lines if src <= k < src + bar})
            if not hits:
                add(t0, bar, root)
            for i, off in enumerate(hits):
                nxt_hit = hits[i + 1] if i + 1 < len(hits) else bar
                add(t0 + off, nxt_hit - off, root)
        elif style == "Root notes" or style == "Follow the kick":
            add(t0, bar, root)
        elif style == "Eighths":
            for i in range(0, bar, eighth):
                add(t0 + i, eighth, root, velocity if i % beat == 0 else velocity - 20)
        elif style == "Root + fifth":
            for i in range(grid.beats_per_bar):
                add(t0 + i * beat, beat, root if i % 2 == 0 else fifth)
        elif style == "Octave bounce":
            for i in range(0, bar, eighth):
                add(t0 + i, eighth, root if (i // eighth) % 2 == 0 else root + 12)
        elif style == "Walking":
            # root, two scale steps toward the next root, then a leading tone
            direction = 1 if nxt >= root else -1
            line = [root, key.step(root, direction), key.step(root, 2 * direction), nxt - direction]
            for i, p in enumerate(line[: grid.beats_per_bar]):
                add(t0 + i * beat, beat, p)
        elif style == "Syncopated":
            # 3+3+2 feel in 8ths
            for off, ln, p in ((0, 3, root), (3, 3, root), (6, 2, fifth)):
                add(t0 + off * eighth, ln * eighth, p)
        else:
            add(t0, bar, root)
    return Part("Bass", steps, instrument=1)


def chord_part(key: Key, chords: list[Chord], grid: Grid, style: str = "Whole bar", octave: int = 3,
               velocity: int = 90, scale_lock: bool = True) -> Part:
    base = (octave + 1) * 12
    bar = grid.lines_per_bar
    beat = grid.lpb
    eighth = max(1, grid.lpb // 2)
    steps: list[Step] = []
    prev: list[int] | None = None
    for b, chord in enumerate(chords):
        voicing = _smooth_voicing(chord.voicing(base), prev)
        if scale_lock:
            voicing = sorted({key.snap(p) for p in voicing})
        prev = voicing
        t0 = b * bar
        hits: list[tuple[int, int, list[int]]] = []
        if style == "Whole bar":
            hits = [(0, bar, voicing)]
        elif style == "Half notes":
            hits = [(0, bar // 2, voicing), (bar // 2, bar // 2, voicing)]
        elif style == "Off-beat stabs":
            hits = [(i * beat + eighth, eighth, voicing) for i in range(grid.beats_per_bar)]
        elif style == "Quarter pulse":
            hits = [(i * beat, beat, voicing) for i in range(grid.beats_per_bar)]
        elif style in ("Arp up", "Arp up/down"):
            seq = voicing + [voicing[0] + 12]
            if style == "Arp up/down":
                seq = seq + seq[-2:0:-1]
            hits = [(i, eighth, [seq[(i // eighth) % len(seq)]]) for i in range(0, bar, eighth)]
        for off, length, notes in hits:
            for p in notes:
                steps.append(Step(t0 + off, max(1, length), p, velocity))
    return Part("Chords", steps, instrument=2)


def _smooth_voicing(voicing: list[int], prev: list[int] | None) -> list[int]:
    """Pick the inversion closest to the previous chord (less jumping around)."""
    if not prev:
        return voicing
    centre = sum(prev) / len(prev)
    options = []
    v = list(voicing)
    for _ in range(len(voicing)):
        for shift in (-12, 0, 12):
            cand = [p + shift for p in v]
            options.append(cand)
        v = v[1:] + [v[0] + 12]
    return min(options, key=lambda c: abs(sum(c) / len(c) - centre))


def lead(key: Key, chords: list[Chord], grid: Grid, style: str = "Motif", octave: int = 5,
         density: float = 0.5, seed: int = 0, sample_steps: list[Step] | None = None,
         velocity: int = 100) -> Part:
    rng = random.Random(seed)
    bar = grid.lines_per_bar
    eighth = max(1, grid.lpb // 2)
    low = (octave + 1) * 12
    steps: list[Step] = []

    if style in ("Harmony (3rd above sample)", "Octave double") and sample_steps:
        # Re-voice the sample's own melody. Snap first, then move by scale steps,
        # so the harmony is diatonic (a 3rd above can be major or minor).
        src_centre = sum(s.pitch for s in sample_steps) / len(sample_steps)
        shift = 12 * round((low + 6 - src_centre) / 12)
        for s in sample_steps:
            p = key.snap(s.pitch)
            p = key.step(p, 2) if style.startswith("Harmony") else p + 12
            steps.append(Step(s.line, s.length, key.snap(p + shift), velocity))
        return Part("Lead", steps, instrument=3)

    sample_lines = {s.line for s in (sample_steps or [])}
    # Motif: write 2 bars, then repeat with a variation on the second half.
    motif_len = 2 * bar
    motif: list[tuple[int, int, int]] = []  # (offset, length, degree delta relative to chord root)
    pos = 0
    cur = 0
    while pos < motif_len:
        on_beat = pos % grid.lpb == 0
        if rng.random() < (0.35 + 0.6 * density if on_beat else 0.15 + 0.6 * density):
            length = rng.choice([eighth, eighth, grid.lpb, grid.lpb * 2] if density < 0.6 else [eighth, eighth, grid.lpb])
            cur = max(-4, min(5, cur + rng.choice([-2, -1, -1, 1, 1, 2, 0, 3])))
            motif.append((pos, length, cur))
            pos += length
        else:
            pos += eighth

    for b in range(0, len(chords), 2):
        variation = (b // 2) % 2 == 1
        for off, length, deg in motif:
            bar_idx = b + off // bar
            if bar_idx >= len(chords):
                break
            line = b * bar + off
            if style == "Counter-melody" and line in sample_lines:
                continue  # leave room: only play in the sample's gaps
            chord = chords[bar_idx]
            root = key.snap(_root_in(chord, low - 5))  # chord root nearest the chosen octave
            d = deg + (rng.choice([-1, 1]) if variation and off >= bar else 0)
            pitch = key.step(root, d)
            # Strong beats land on chord tones.
            if off % grid.lpb == 0 and pitch % 12 not in chord.pitch_classes:
                pitch = min((key.step(pitch, k) for k in (-1, 1)),
                            key=lambda p: (p % 12 not in chord.pitch_classes, abs(p - pitch)))
            steps.append(Step(line, length, key.snap(pitch), velocity - (0 if off % grid.lpb == 0 else 15)))
    return Part("Lead", steps, instrument=3)


def sketch(text: str, key: Key, grid: Grid, step_lines: int | None = None, scale_lock: bool = True,
           velocity: int = 100) -> tuple[Part, list[str]]:
    """Turn typed notes (``"A3 C4 E4 . G4 -"``) into a part.

    ``.`` is a rest, ``-`` holds the previous note. Returns the part and a list
    of messages describing every note the scale lock corrected.
    """
    from .theory import note_name, parse_note

    step_lines = step_lines or max(1, grid.lpb // 2)
    steps: list[Step] = []
    fixes: list[str] = []
    line = 0
    for tok in text.replace(",", " ").split():
        if tok == ".":
            line += step_lines
            continue
        if tok == "-":
            if steps:
                steps[-1].length += step_lines
            line += step_lines
            continue
        try:
            p = parse_note(tok)
        except ValueError as e:
            fixes.append(f"skipped {tok!r}: {e}")
            line += step_lines
            continue
        if scale_lock and not key.contains(p):
            q = key.snap(p)
            fixes.append(f"{note_name(p)} is not in {key.name} -> {note_name(q)}")
            p = q
        steps.append(Step(line, step_lines, p, velocity))
        line += step_lines
    return Part("Sketch", steps, instrument=4), fixes


def drum_part(hits, grid: Grid, kinds: tuple[str, ...] = ("kick", "snare", "hat")) -> Part:
    """The sample's drum hits on the grid (General MIDI notes 36/38/42;
    shown as C-4/D-4/F#4 in Renoise). One-line hits; drums don't need OFFs."""
    from .drums import GM

    steps: dict[tuple[int, int], Step] = {}
    for h in hits:
        if h.kind not in kinds:
            continue
        line = grid.to_line(h.time)
        key = (line, GM[h.kind])
        if key not in steps or steps[key].velocity < h.velocity:
            steps[key] = Step(line, 1, GM[h.kind], h.velocity)
    return Part("Drums", sorted(steps.values(), key=lambda s: (s.line, s.pitch)), instrument=5, drums=True)
