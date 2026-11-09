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
