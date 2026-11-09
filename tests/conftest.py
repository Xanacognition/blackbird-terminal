import sys
from pathlib import Path

import numpy as np
import pytest

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

from zampler.analysis import SR  # noqa: E402


def synth(midis, bpm=120.0, sr=SR, note_beats=0.5):
    """Plucky harmonic tone per note (eighth notes by default)."""
    dur = 60.0 / bpm * note_beats
    out = []
    for m in midis:
        n = int(dur * sr)
        t = np.arange(n) / sr
        if m is None:
            out.append(np.zeros(n))
            continue
        f = 440.0 * 2 ** ((m - 69) / 12)
        w = sum(np.sin(2 * np.pi * f * k * t) / k for k in (1, 2, 3))
        env = np.minimum(1, t / 0.005) * np.exp(-t * 3) * np.minimum(1, (dur - t) / 0.01)
        out.append(w * env * 0.3)
    return np.concatenate(out).astype(np.float32)


@pytest.fixture(scope="session")
def melody():
    seq = [57, 60, 64, 62, 60, 60, 59, 57, 52, 55, 57, 60, 64, 67, 65, 64] * 2
    return seq, synth(seq)


def drum_loop(bpm=120.0, bars=2, sr=SR, seed=0):
    """Kick on 1 & 3, snare on 2 & 4, closed hat on every 8th.

    Returns (audio, {kind: [hit times]}).
    """
    rng = np.random.default_rng(seed)
    beat = 60.0 / bpm
    n = int(bars * 4 * beat * sr) + sr // 2
    y = np.zeros(n)

    def put(t, x):
        i = int(t * sr)
        y[i:i + len(x)] += x[: n - i]

    tt = np.arange(int(0.35 * sr)) / sr
    kick = np.sin(2 * np.pi * (45 * tt + (110 / 25) * (1 - np.exp(-25 * tt)))) * np.exp(-tt * 9)
    ts = np.arange(int(0.2 * sr)) / sr
    snare = (0.5 * np.sin(2 * np.pi * 190 * ts) + rng.standard_normal(len(ts)) * 0.6) * np.exp(-ts * 22)
    th = np.arange(int(0.05 * sr)) / sr
    hat_noise = np.diff(rng.standard_normal(len(th) + 1))  # crude high-pass
    hat = hat_noise * np.exp(-th * 70) * 0.25
    truth = {"kick": [], "snare": [], "hat": []}
    for b in range(bars * 4):
        t = b * beat
        if b % 2 == 0:
            put(t, kick * 0.9)
            truth["kick"].append(t)
        else:
            put(t, snare * 0.6)
            truth["snare"].append(t)
        for half in (0, 0.5):
            put(t + half * beat, hat)
            truth["hat"].append(t + half * beat)
    return (y * 0.8).astype(np.float32), truth
