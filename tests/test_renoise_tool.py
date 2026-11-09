"""Runs the Renoise tool's main.lua against a small mock of the Renoise API."""

from pathlib import Path

import pytest

from zampler.export import renoise_tool_bytes, to_renoise
from zampler.tracker import Grid, Part, Step

lupa = pytest.importorskip("lupa")
try:
    from lupa.luajit21 import LuaRuntime  # Renoise embeds LuaJIT (Lua 5.1)
except ImportError:  # pragma: no cover
    from lupa.lua51 import LuaRuntime

MAIN = Path(__file__).resolve().parent.parent / "renoise_tool" / "com.zampler.Import.xrnt" / "main.lua"

MOCK = r"""
local function new_col() return {note_value = 121, instrument_value = 255, volume_value = 255} end
local function new_line() local l = {cols = {}}
  function l:note_column(i) self.cols[i] = self.cols[i] or new_col(); return self.cols[i] end
  return l end
local function new_ptrack() local t = {lines = {}}
  function t:line(i) self.lines[i] = self.lines[i] or new_line(); return self.lines[i] end
  return t end
local function new_pattern() local p = {number_of_lines = 64, tracks = {}}
  function p:track(i) self.tracks[i] = self.tracks[i] or new_ptrack(); return self.tracks[i] end
  return p end
song = {
  transport = {bpm = 125, lpb = 4},
  sequencer_track_count = 1,
  tracks = {{name = "Track 01"}, {name = "Master"}},
  instruments = {{}, {}, {}},
  patterns = {new_pattern()},
  selected_sequence_index = 1,
}
song.sequencer = {pattern_sequence = {1}}
function song.sequencer:insert_new_pattern_at(pos)
  table.insert(song.patterns, new_pattern()); table.insert(self.pattern_sequence, pos, #song.patterns)
  return #song.patterns end
function song:insert_track_at(i)
  local t = {name = "", visible_note_columns = 1}; table.insert(self.tracks, i, t)
  self.sequencer_track_count = self.sequencer_track_count + 1; return t end
function song:pattern(i) return self.patterns[i] end
renoise = {
  song = function() return song end,
  app = function() return {} end,
  tool = function() return {add_menu_entry = function() end} end,
}
"""


def test_tool_writes_notes_into_new_tracks():
    grid = Grid(bpm=140, lpb=4)
    parts = [
        Part("Sample", [Step(0, 2, 60, 127), Step(2, 2, 62, 100)], 0),
        Part("Chords", [Step(0, 16, 57, 90), Step(0, 16, 60, 90), Step(0, 16, 64, 90)], 4),
        Part("Bass", [Step(70, 4, 33, 110)], 1),
    ]
    data = to_renoise(parts, grid, pattern_lines=64)

    L = LuaRuntime()
    L.execute(MOCK)
    L.execute(MAIN.read_text())
    loader = L.eval("function(src) return (loadstring or load)(src)() end")
    count = L.globals().zampler_import_data(L.globals().song, loader(data))

    song = L.globals().song
    assert count == 6
    assert song.transport.bpm == 140
    names = [song.tracks[i].name for i in range(1, len(song.tracks) + 1)]
    assert names == ["Track 01", "Sample", "Chords", "Bass", "Master"]
    assert song.tracks[3].visible_note_columns == 3
    p1 = song.patterns[1]
    # Sample track is song track 2; lines are 1-based in the Lua API.
    assert p1.tracks[2].lines[1].cols[1].note_value == 48  # C-4
    assert p1.tracks[2].lines[3].cols[1].note_value == 50  # D-4
    assert p1.tracks[2].lines[5].cols[1].note_value == 120  # OFF
    assert p1.tracks[3].lines[1].cols[3].note_value == 52  # E-4 in 3rd column
    assert p1.tracks[3].lines[1].cols[1].instrument_value == 2  # clamped to last instrument
    # Bass note at line 70 spilled into a second, newly created pattern.
    assert len(song.sequencer.pattern_sequence) == 2
    assert song.patterns[2].tracks[4].lines[7].cols[1].note_value == 21  # A-1


def test_tool_package_contains_manifest_and_main():
    import io
    import zipfile

    names = zipfile.ZipFile(io.BytesIO(renoise_tool_bytes())).namelist()
    assert sorted(names) == ["main.lua", "manifest.xml"]
