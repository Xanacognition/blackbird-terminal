"""Tracker grid: quantise notes to pattern lines and lay them out in note
columns the way Renoise does (one note per column per line, polyphony spread
across columns, explicit note-offs)."""

from __future__ import annotations

from dataclasses import dataclass, field

from .analysis import Note
from .theory import Key, note_name

MAX_PATTERN_LINES = 512  # Renoise limit
MAX_NOTE_COLUMNS = 12  # Renoise limit


@dataclass
class Step:
    """A note on the tracker grid. ``line`` / ``length`` are in pattern lines."""

    line: int
    length: int
    pitch: int  # MIDI
    velocity: int = 100

    @property
    def end(self) -> int:
        return self.line + self.length


@dataclass
class Part:
    name: str
    steps: list[Step] = field(default_factory=list)
    instrument: int = 0  # Renoise instrument index (00..FE)

    def transposed(self, semitones: int) -> "Part":
        return Part(self.name, [Step(s.line, s.length, s.pitch + semitones, s.velocity) for s in self.steps],
                    self.instrument)


@dataclass
class Grid:
    bpm: float
    lpb: int = 4  # lines per beat
    beats_per_bar: int = 4

    @property
    def lines_per_bar(self) -> int:
        return self.lpb * self.beats_per_bar

    @property
    def seconds_per_line(self) -> float:
        return 60.0 / self.bpm / self.lpb

    def to_line(self, seconds: float) -> int:
        return int(round(seconds / self.seconds_per_line))

    def to_seconds(self, line: float) -> float:
        return line * self.seconds_per_line


def quantize(notes: list[Note], grid: Grid, key: Key | None = None, scale_lock: bool = False,
             min_length: int = 1) -> list[Step]:
    """Convert timed notes to grid steps.

    No note is ever dropped: if two notes land on the same line, the later one
    is pushed to the next free line so every detected note survives. With
    ``scale_lock`` every pitch is snapped into ``key``.
    """
    steps: list[Step] = []
    used: set[int] = set()
    for n in sorted(notes, key=lambda n: n.start):
        line = grid.to_line(n.start)
        while line in used:
            line += 1
        end = max(line + min_length, grid.to_line(n.end))
        pitch = key.snap(n.pitch) if (scale_lock and key is not None) else n.pitch
        steps.append(Step(line, end - line, pitch, n.velocity))
        used.add(line)
    # Trim lengths so monophonic lines don't overlap the following note.
    for a, b in zip(steps, steps[1:]):
        if a.end > b.line:
            a.length = max(1, b.line - a.line)
    return steps


def song_length(parts: list[Part], grid: Grid) -> int:
    """Total lines rounded up to whole bars."""
    end = max((s.end for p in parts for s in p.steps), default=grid.lines_per_bar)
    bar = grid.lines_per_bar
    return max(bar, -(-end // bar) * bar)


# ---------------------------------------------------------------------------
# Column layout

@dataclass
class Cell:
    pitch: int | None = None  # MIDI, or None
    off: bool = False
    velocity: int | None = None
    instrument: int | None = None


def layout_columns(part: Part, total_lines: int) -> list[list[Cell]]:
    """Distribute a part's steps over note columns (greedy interval colouring)
    and insert OFFs where a note ends without the next note in that column
    starting on the same line. Returns ``columns[col][line]``."""
    col_free_at: list[int] = []  # first line each column is free again
    assignment: list[tuple[int, Step]] = []
    for s in sorted(part.steps, key=lambda s: (s.line, s.pitch)):
        for c, free in enumerate(col_free_at):
            if free <= s.line:
                col_free_at[c] = s.end
                assignment.append((c, s))
                break
        else:
            if len(col_free_at) >= MAX_NOTE_COLUMNS:
                # Out of columns: steal the one that frees soonest (truncate it).
                c = min(range(len(col_free_at)), key=col_free_at.__getitem__)
            else:
                c = len(col_free_at)
                col_free_at.append(0)
            col_free_at[c] = s.end
            assignment.append((c, s))

    cols = [[Cell() for _ in range(total_lines)] for _ in range(max(1, len(col_free_at)))]
    for c, s in assignment:
        if 0 <= s.line < total_lines:
            cols[c][s.line] = Cell(s.pitch, False, s.velocity, part.instrument)
    for c, s in assignment:
        if s.end < total_lines and cols[c][s.end].pitch is None:
            cols[c][s.end] = Cell(None, True)
    return cols


def _fmt_cell(cell: Cell, show_vol: bool) -> str:
    if cell.off:
        note, ins, vol = "OFF", "..", ".."
    elif cell.pitch is None:
        note, ins, vol = "---", "..", ".."
    else:
        note = note_name(cell.pitch, renoise=True) if 0 <= cell.pitch <= 119 else "???"
        ins = f"{cell.instrument:02X}" if cell.instrument is not None else ".."
        vol = f"{min(0x80, round(cell.velocity / 127 * 0x80)):02X}" if cell.velocity is not None else ".."
    return f"{note} {ins} {vol}" if show_vol else f"{note} {ins}"


def render_pattern(parts: list[Part], grid: Grid, start: int = 0, length: int | None = None,
                   show_vol: bool = True) -> str:
    """Render parts as a Renoise-style text pattern (one track per part)."""
    total = song_length(parts, grid)
    length = length if length is not None else total - start
    layouts = [layout_columns(p, total) for p in parts]
    widths = []
    header = "    |"
    for p, cols in zip(parts, layouts):
        w = len(cols) * (len(_fmt_cell(Cell(), show_vol)) + 1) - 1
        widths.append(w)
        header += " " + p.name[:w].ljust(w) + " |"
    lines = [header, "-" * len(header)]
    for ln in range(start, min(total, start + length)):
        row = f"{ln - start:03X} |" if length <= 0x1000 else f"{ln - start:04X}|"
        for cols in layouts:
            row += " " + " ".join(_fmt_cell(col[ln], show_vol) for col in cols) + " |"
        marker = "" if (ln - start) % grid.lines_per_bar else f"  <- bar {(ln // grid.lines_per_bar) + 1}"
        lines.append(row + marker)
    return "\n".join(lines)
