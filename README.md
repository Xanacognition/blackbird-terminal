# 🎛️ Zampler (for now)

Drop a sample in and Zampler maps it out as tracker notes. You can then build
basslines, chords and leads on top that are **locked to the sample's key**, so you can't
play a wrong note. It's aimed at Renoise and works with any tracker or DAW through MIDI.

Inspired by *Notegrabber* by Navie D.

## What it does

| | |
|---|---|
| **Drag and drop** | WAV / FLAC / OGG / AIFF / MP3 (the first 90 s are analysed) |
| **Note detection** | Finds every note (pYIN pitch tracking + onset splitting, so repeated notes like `C C C` stay separate), plus tempo (snapped to the note grid) and key (Krumhansl key profiles over harmonic + melodic content) |
| **Note map** | A 12-key map of the safe notes in the key with scale degrees, how much the sample uses each note, and the chords it hears in each bar |
| **Builders** | Bass (6 styles), Chords (6 rhythms incl. arps, smooth voice-leading, 7ths), Lead (motif, harmony a 3rd above the sample, counter-melody, octave double), and a Sketch box where you type notes like `A3 C4 E4 - G#4` and wrong notes get corrected with a message |
| **Scale lock** | Every generated or typed note goes through the key. The sample itself is transcribed as heard, and any out-of-key notes are flagged (you can snap them too) |
| **Tracker view** | A Renoise-style pattern (`C-4 01 7F`, `OFF`, hex line numbers) with chords spread across note columns |
| **Export** | Multi-track `.mid`, or a `.zampler.lua` pattern file plus the **Zampler Import** Renoise tool (`.xrnt`) |

## Run it

```bash
pip install -r requirements.txt
streamlit run streamlit_app.py
```

## Getting it into Renoise

1. In Zampler, download **Zampler Import tool (.xrnt)** and drag it onto Renoise once to install it.
2. Download the **Renoise pattern file** (`yoursample.zampler.lua`).
3. In Renoise, go to *Tools → Zampler → Import Pattern File…* (it's also in the pattern editor's right-click menu).

Each part becomes a new track starting at the selected pattern. New patterns are added if
the export is longer than one pattern, and nothing that's already in your song is
overwritten. Tempo and LPB are set from the export. Instrument numbers: `00` sample,
`01` bass, `02` chords, `03` lead, `04` sketch. They're capped at the last instrument you
have, so load your sounds into those slots.

Or drag the `.mid` into any DAW, or use a Renoise MIDI import tool.

## Layout

```
zampler/theory.py      notes, scales, key detection, scale lock, chords
zampler/analysis.py    audio -> notes / tempo / key / per-bar chords
zampler/tracker.py     quantise to lines, note columns, OFFs, pattern text
zampler/generators.py  bass, chords, lead, typed sketch (all scale-locked)
zampler/export.py      MIDI + Renoise pattern file + .xrnt packaging
renoise_tool/          the Renoise tool (manifest.xml + main.lua)
streamlit_app.py       the drag-and-drop UI
```

## Tests

```bash
pip install -r requirements-dev.txt
pytest
```

The tests use synthesised melodies to check that every note is found, including repeats
and notes after rests, and that the tempo, key and grid positions are right. They also
check that every builder style stays in key, and they run the Renoise tool's Lua against
a mock of the Renoise API.

## Limits / next steps

- Note detection follows **one melody line**: the loudest pitch at each moment. Key and
  chords use the full harmony, but individual notes inside dense chords aren't extracted yet.
- It's a standalone app for now, not a VST/CLAP plugin. The engine is kept separate from
  the UI so it can later be ported into a plugin shell (e.g. JUCE) that renders the same
  note map.
- The Renoise tool has been tested against a mock of the Renoise API, not inside Renoise
  itself yet.
