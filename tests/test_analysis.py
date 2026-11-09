from zampler.analysis import analyze, refine_tempo
from zampler.tracker import Grid, quantize

from conftest import synth


def test_every_note_is_found_including_repeats(melody):
    seq, y = melody
    a = analyze(y)
    assert [n.pitch for n in a.notes] == seq  # incl. the repeated C4 C4
    assert a.bpm == 120
    assert a.key.name == "A minor"


def test_quantized_to_eighth_note_lines(melody):
    seq, y = melody
    a = analyze(y)
    steps = quantize(a.notes, Grid(a.bpm, lpb=4))
    assert [s.line for s in steps] == list(range(0, 2 * len(seq), 2))
    assert [s.pitch for s in steps] == seq


def test_bassline_sample_and_rests():
    seq = [45, None, 45, 48, None, 43, 45, None] * 2
    a = analyze(synth(seq, bpm=100), fmin="C1")
    assert [n.pitch for n in a.notes] == [m for m in seq if m is not None]


def test_refine_tempo_snaps_to_grid():
    onsets = [i * 60 / 128 / 2 for i in range(16)]
    assert refine_tempo(125.0, onsets) == 128
