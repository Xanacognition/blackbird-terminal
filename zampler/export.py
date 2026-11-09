"""Exports: Standard MIDI File and a Renoise pattern file for the Zampler
Import tool (``renoise_tool/``)."""

from __future__ import annotations

import io
import zipfile
from pathlib import Path

import mido

from .tracker import Grid, Part, layout_columns, song_length

TICKS_PER_BEAT = 480
TOOL_DIR = Path(__file__).resolve().parent.parent / "renoise_tool" / "com.zampler.Import.xrnt"


def to_midi(parts: list[Part], grid: Grid) -> bytes:
    """Multi-track type-1 MIDI file, one track and channel per part."""
    mid = mido.MidiFile(type=1, ticks_per_beat=TICKS_PER_BEAT)
    tpl = TICKS_PER_BEAT // grid.lpb
    meta = mido.MidiTrack()
    meta.append(mido.MetaMessage("set_tempo", tempo=mido.bpm2tempo(grid.bpm), time=0))
    meta.append(mido.MetaMessage("time_signature", numerator=grid.beats_per_bar, denominator=4, time=0))
    meta.append(mido.MetaMessage("end_of_track", time=0))
    mid.tracks.append(meta)
    melodic = 0
    for part in parts:
        if part.drums:
            channel = 9  # GM drum channel 10
        else:
            channel = melodic if melodic < 9 else melodic + 1  # skip channel 10
            melodic += 1
        events: list[tuple[int, int, mido.Message]] = []
        for s in part.steps:
            if not 0 <= s.pitch <= 127:
                continue
            events.append((s.line * tpl, 1, mido.Message("note_on", note=s.pitch, velocity=max(1, min(127, s.velocity)), channel=channel % 16)))
            events.append((s.end * tpl, 0, mido.Message("note_off", note=s.pitch, velocity=0, channel=channel % 16)))
        events.sort(key=lambda e: (e[0], e[1]))  # note-offs before note-ons on the same tick
        tr = mido.MidiTrack()
        tr.append(mido.MetaMessage("track_name", name=part.name, time=0))
        now = 0
        for tick, _, msg in events:
            tr.append(msg.copy(time=tick - now))
            now = tick
        tr.append(mido.MetaMessage("end_of_track", time=0))
        mid.tracks.append(tr)
    buf = io.BytesIO()
    mid.save(file=buf)
    return buf.getvalue()


def _lua_str(s: str) -> str:
    return '"' + s.replace("\\", "\\\\").replace('"', '\\"').replace("\n", " ") + '"'


def to_renoise(parts: list[Part], grid: Grid, pattern_lines: int = 64, name: str = "Zampler") -> str:
    """Lua data file (``*.zampler.lua``) read by the Zampler Import tool.

    Notes use Renoise note values (C-0 = 0, C-4 = 48, OFF = 120), volume is
    0x00..0x80 and instruments are 0-based. The song is split into patterns of
    ``pattern_lines`` lines.
    """
    pattern_lines = max(1, min(512, pattern_lines))
    total = song_length(parts, grid)
    layouts = [layout_columns(p, total) for p in parts]
    n_patterns = -(-total // pattern_lines)
    out = [
        "-- Zampler export. Load with Tools > Zampler > Import Pattern File...",
        "return {",
        f"  name = {_lua_str(name)},",
        f"  bpm = {grid.bpm:g},",
        f"  lpb = {grid.lpb},",
        f"  pattern_lines = {pattern_lines},",
        "  tracks = {",
    ]
    for part, cols in zip(parts, layouts):
        out.append(f"    {{ name = {_lua_str(part.name)}, columns = {len(cols)} }},")
    out.append("  },")
    out.append("  patterns = {")
    for p in range(n_patterns):
        lo, hi = p * pattern_lines, min(total, (p + 1) * pattern_lines)
        out.append(f"    {{ lines = {hi - lo}, notes = {{")
        for t, cols in enumerate(layouts):
            for c, col in enumerate(cols):
                for ln in range(lo, hi):
                    cell = col[ln]
                    if cell.off:
                        out.append(f"      {{{t + 1},{c + 1},{ln - lo},120}},")
                    elif cell.pitch is not None and 12 <= cell.pitch <= 131:
                        vol = min(0x80, round((cell.velocity or 100) / 127 * 0x80))
                        out.append(f"      {{{t + 1},{c + 1},{ln - lo},{cell.pitch - 12},{cell.instrument or 0},{vol}}},")
        out.append("    } },")
    out.append("  },")
    out.append("}")
    return "\n".join(out) + "\n"


def renoise_tool_bytes() -> bytes:
    """The Renoise tool packaged as an installable ``.xrnt`` (a zip)."""
    buf = io.BytesIO()
    with zipfile.ZipFile(buf, "w", zipfile.ZIP_DEFLATED) as z:
        for f in sorted(TOOL_DIR.iterdir()):
            if f.is_file():
                z.write(f, f.name)
    return buf.getvalue()
