from conftest import drum_loop, synth

from zampler import generators as gen
from zampler.analysis import SR, analyze
from zampler.drums import detect_drums
from zampler.export import to_midi
from zampler.poly import transcribe_poly
from zampler.theory import Key
from zampler.tracker import Grid, layout_columns, quantize_poly

CHORDS = [[57, 60, 64], [53, 57, 60], [48, 55, 64], [55, 59, 62]]  # Am F C G, one per 2 beats
MELODY = [72, 76, 74, 72, 77, 76, 72, 69, 76, 79, 76, 72, 74, 71, 74, 79]  # 8ths on top


def _chords_audio():
    return sum(synth([c[i] for c in CHORDS], bpm=120, note_beats=2) for i in range(3))


def _by_line(steps):
    out = {}
    for s in steps:
        out.setdefault(s.line, set()).add(s.pitch)
    return out


def test_every_chord_note_found():
    steps = quantize_poly(transcribe_poly(_chords_audio(), SR), Grid(120))
    assert _by_line(steps) == {0: {57, 60, 64}, 8: {53, 57, 60}, 16: {48, 55, 64}, 24: {55, 59, 62}}


def test_chords_plus_melody_on_top():
    y = _chords_audio()
    m = synth(MELODY, bpm=120, note_beats=0.5) * 0.8
    n = min(len(y), len(m))
    got = _by_line(quantize_poly(transcribe_poly(y[:n] + m[:n], SR), Grid(120)))
    want = {}
    for i, c in enumerate(CHORDS):
        want.setdefault(i * 8, set()).update(c)
    for i, p in enumerate(MELODY):
        want.setdefault(i * 2, set()).add(p)
    assert got == want  # incl. melody notes sitting on a chord note's overtone (F5 over F3, E5 over C3)


def test_poly_mode_still_handles_single_lines():
    seq = [57, 60, 64, 62, 60, 60, 59, 57]
    assert [n.pitch for n in transcribe_poly(synth(seq), SR)] == seq


def test_drum_hits():
    y, truth = drum_loop()
    hits = detect_drums(y, SR)
    grid = Grid(120)
    got = {k: sorted({grid.to_line(h.time) for h in hits if h.kind == k}) for k in truth}
    want = {k: sorted({grid.to_line(t) for t in v}) for k, v in truth.items()}
    assert got["kick"] == want["kick"]
    assert got["snare"] == want["snare"]
    # Hats under a snare are masked by the snare's noise; all others are found.
    assert set(want["hat"]) - set(want["snare"]) <= set(got["hat"]) <= set(want["hat"])


def test_drums_survive_a_bassline():
    y, truth = drum_loop()
    b = synth([45, 45, 48, 43] * 2, bpm=120, note_beats=1) * 0.6
    n = min(len(y), len(b))
    hits = detect_drums(y[:n] + b[:n], SR)
    assert sorted(round(h.time * 2) / 2 for h in hits if h.kind == "kick") == truth["kick"]
    assert sorted(round(h.time * 2) / 2 for h in hits if h.kind == "snare") == truth["snare"]


def test_analyze_fills_poly_and_drums():
    y, _ = drum_loop()
    a = analyze(y)
    assert a.drum_hits and a.bpm == 120


def test_drum_part_renoise_and_midi():
    y, _ = drum_loop()
    grid = Grid(120)
    part = gen.drum_part(detect_drums(y, SR), grid)
    assert part.drums and {s.pitch for s in part.steps} == {36, 38, 42}
    cols = layout_columns(part, 32)
    assert cols[0][0].pitch == 60  # kick shows as C-4 in Renoise
    assert not any(c.off for col in cols for c in col)  # one-shots, no OFFs
    import io

    import mido

    mid = mido.MidiFile(file=io.BytesIO(to_midi([part], grid)))
    assert {m.channel for m in mid.tracks[1] if m.type == "note_on"} == {9}


def test_bass_follows_the_kick():
    key = Key(9, "minor")
    grid = Grid(120)
    kicks = [0, 6, 10, 16, 22, 26]  # 2-bar kick pattern
    chords = gen.progression(key, "i - VI - III - VII", 4)
    part = gen.bassline(key, chords, grid, "Follow the kick", kick_lines=kicks)
    lines = [s.line for s in part.steps]
    assert lines == [0, 6, 10, 16, 22, 26, 32, 38, 42, 48, 54, 58]  # looped over 4 bars
    assert all(key.contains(s.pitch) for s in part.steps)
    assert part.steps[0].length == 6


def test_no_fake_drums_in_a_melody():
    assert detect_drums(synth([57, 60, 64, 62, 60, 60, 59, 57] * 2), SR) == []
    assert detect_drums(_chords_audio(), SR) == []


def test_full_mix_with_drums():
    """Chords + melody + drums together: drum tones must not turn into notes."""
    chords = CHORDS * 2
    melody = MELODY * 2
    y = sum(synth([c[i] for c in chords], bpm=120, note_beats=2) for i in range(3))
    m = synth(melody, bpm=120, note_beats=0.5) * 0.8
    d, _ = drum_loop(bars=4)
    n = min(len(y), len(m), len(d))
    a = analyze((y[:n] + m[:n] + d[:n]) * 0.5)
    want = {(i * 8, p) for i, c in enumerate(chords) for p in c} | {(i * 2, p) for i, p in enumerate(melody)}
    got = {(s.line, s.pitch) for s in quantize_poly(a.poly_notes, Grid(a.bpm))}
    assert a.bpm == 120
    assert len(want & got) >= 0.85 * len(want)
    assert len(got - want) <= 4
    assert all(p >= 40 for _, p in got - want) or not (got - want)  # no kick sub-tones
