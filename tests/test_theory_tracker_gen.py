import io

import mido
import pytest

from zampler import generators as gen
from zampler.analysis import Note
from zampler.export import to_midi
from zampler.theory import Key, detect_key, note_name, parse_note
from zampler.tracker import Grid, Part, Step, layout_columns, quantize, render_pattern

A_MINOR = Key(9, "minor")


@pytest.mark.parametrize("text,midi", [("C4", 60), ("c#3", 49), ("Eb2", 39), ("C-4", 60), ("A-1", 33),
                                       ("Cb4", 59), ("B#3", 60), ("60", 60)])
def test_parse_note(text, midi):
    assert parse_note(text) == midi


def test_renoise_note_names():
    assert note_name(60, renoise=True) == "C-4"
    assert note_name(61, renoise=True) == "C#4"


def test_snap_and_steps():
    assert A_MINOR.snap(68) == 67  # G# -> G (tie goes down)
    assert A_MINOR.snap(66) == 65  # F# -> F
    assert A_MINOR.step(57, 2) == 60  # A3 + a third = C4


def test_detect_key_from_profile():
    profile = [0] * 12
    for pc in (2, 4, 6, 7, 9, 11, 1):  # D major scale
        profile[pc] = 1
    profile[2] = 3
    profile[9] = 2
    key, conf = detect_key(profile)
    assert key.name == "D major" and conf > 0.5


def test_quantize_never_drops_colliding_notes():
    notes = [Note(0.0, 0.1, 60), Note(0.01, 0.1, 62), Note(0.02, 0.1, 64)]
    steps = quantize(notes, Grid(120, 4))
    assert [(s.line, s.pitch) for s in steps] == [(0, 60), (1, 62), (2, 64)]


@pytest.mark.parametrize("style", gen.BASS_STYLES)
def test_bass_styles_stay_in_key(style):
    grid = Grid(120)
    chords = gen.progression(A_MINOR, "i - VI - III - VII", 4)
    part = gen.bassline(A_MINOR, chords, grid, style)
    assert part.steps and all(A_MINOR.contains(s.pitch) for s in part.steps)


@pytest.mark.parametrize("style", gen.CHORD_STYLES)
def test_chord_styles_stay_in_key(style):
    chords = gen.progression(A_MINOR, "i - iv - v - i", 4, sevenths=True)
    part = gen.chord_part(A_MINOR, chords, Grid(120), style)
    assert part.steps and all(A_MINOR.contains(s.pitch) for s in part.steps)


@pytest.mark.parametrize("style", gen.LEAD_STYLES)
@pytest.mark.parametrize("seed", range(5))
def test_lead_styles_stay_in_key(style, seed):
    sample = [Step(i * 2, 2, p) for i, p in enumerate([57, 60, 64, 62, 60, 59, 57, 52])]
    chords = gen.progression(A_MINOR, "From sample", 4)
    part = gen.lead(A_MINOR, chords, Grid(120), style, seed=seed, sample_steps=sample)
    assert part.steps and all(A_MINOR.contains(s.pitch) for s in part.steps)


def test_sketch_fixes_wrong_notes():
    part, fixes = gen.sketch("A3 G#3 - . C4", A_MINOR, Grid(120))
    assert [s.pitch for s in part.steps] == [57, 55, 60]
    assert part.steps[1].length == 4  # held with '-'
    assert part.steps[2].line == 8  # after a rest
    assert fixes == ["G#3 is not in A minor -> G3"]


def test_chords_spread_over_columns_with_offs():
    part = Part("Chords", [Step(0, 4, 57), Step(0, 4, 60), Step(0, 4, 64), Step(4, 4, 59)])
    cols = layout_columns(part, 16)
    assert len(cols) == 3
    assert cols[0][4].pitch == 59 and cols[1][4].off and cols[2][4].off
    assert "A-3 00 65 C-4 00 65 E-4 00 65" in render_pattern([part], Grid(120))


def test_midi_roundtrip():
    grid = Grid(140, 4)
    parts = [Part("Lead", [Step(0, 2, 60, 100), Step(2, 2, 62, 90)]), Part("Bass", [Step(0, 8, 36)])]
    mid = mido.MidiFile(file=io.BytesIO(to_midi(parts, grid)))
    assert [t.name for t in mid.tracks[1:]] == ["Lead", "Bass"]
    ons = [(m.note, m.velocity) for m in mid.tracks[1] if m.type == "note_on"]
    assert ons == [(60, 100), (62, 90)]
    assert abs(mido.tempo2bpm(mid.tracks[0][0].tempo) - 140) < 0.01
