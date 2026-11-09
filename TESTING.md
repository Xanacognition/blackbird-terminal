# Testing Zampler

## What has already been tested (automatically)

| | How | Result |
|---|---|---|
| Python engine | `pytest`: 64 tests on synthesised audio | all pass |
| C++ engine | `plugin/tests/EngineTests.cpp`: the same signals | 53/53 checks pass |
| Plugin as a VST3 | `plugin/tests/HostTest.cpp` loads the built `.vst3` the way a DAW does, analyses a test loop (chords + melody + drums at 44.1 kHz), plays 4 bars with a fake host transport and checks the MIDI that comes out | all pass on Linux: 24/24 chord notes and 32/32 melody notes on the right lines, kicks/snares exact, bass/chords in key, no stuck notes, preview audible, state saved |
| Renoise tool | `main.lua` runs against a mock of the Renoise API | passes |

Test material used: a monophonic melody with repeated notes, a bassline with rests, an
Am-F-C-G chord loop, the same loop with a melody on top, a kick/snare/hat loop, and all of
these mixed together at 120 BPM. Results on the full mix: tempo exact, kicks/snares exact,
53 of 56 notes found with 3 extra (C++ engine) / 49 of 56 with 2 extra (Python).

**Known quirks:**
- On loops that are equally C major and A minor (the same notes), the key can come out as
  the relative major/minor. Pick the key by hand if it matters.
- The per-bar chord names include the melody, so Am plus a melody note G can read as `Am7`
  or `Fmaj7`. *From sample* progressions use these names.

**Not tested yet:** real recordings, a real Renoise session, CLAP hosts, Windows/macOS
builds on a real machine. That's what this round of testing is for.

## Get a build

- **Easiest:** open the repository's **Actions** tab, pick the latest *Build & test* run,
  and download `Zampler-windows`, `Zampler-macos` or `Zampler-linux`. Each zip contains
  `Zampler.vst3`, `Zampler.clap` (plus `Zampler.component` on macOS), the Standalone app
  and `renoise-tool/com.zampler.Import.xrnt`.
- Or build it yourself: see [plugin/README.md](plugin/README.md).

Install by copying into your plugin folder:
- Windows: `C:\Program Files\Common Files\VST3` (and `...\CLAP`)
- macOS: `~/Library/Audio/Plug-Ins/VST3` (and `/CLAP`, `/Components`)
- Linux: `~/.vst3` (and `~/.clap`)

macOS builds aren't signed. If the plugin is blocked, run
`xattr -dr com.apple.quarantine ~/Library/Audio/Plug-Ins/VST3/Zampler.vst3` (same for the
`.clap`/`.component`).

Web app: `pip install -r requirements.txt && streamlit run streamlit_app.py`

## Checklist

Tick each item and note anything odd. A screenshot of the pattern view helps a lot.

### 1. Loading (Renoise)
- [ ] Renoise finds **Zampler** after a plugin rescan (VST3, and CLAP if you use it).
- [ ] It loads as an instrument and the editor opens.
- [ ] Resizing the window works.

### 2. Dropping samples
Try at least one of each:
- [ ] a single-note melody (lead, vocal chop, whistle)
- [ ] a bassline
- [ ] a chord loop (piano/pad/guitar)
- [ ] a full loop with drums
- [ ] a drum-only loop

For each one, check:
- [ ] **Tempo**: is it right? Half or double the real tempo counts as a miss.
- [ ] **Key**: is it right? Note that the relative major/minor, e.g. C major vs A minor, contain the same notes.
- [ ] **Notes**: compare *Every note* vs *Melody line*. Are any obvious notes missing? Any extra ghost notes?
- [ ] **Drums**: are the kick, snare and hat lines right? Is there a drum guide on a sample with no drums (there shouldn't be)?
- [ ] Analysis time for a 30-60 s sample: _____ s

### 3. Building
- [ ] Bass styles, including **Follow the kick** on a loop with drums.
- [ ] Chord progressions (*From sample* and the presets) and rhythms.
- [ ] Lead styles: *Harmony* should sound like a harmony of the sample's melody.
- [ ] Sketch: type a wrong note, e.g. `G#4` in A minor. It should be fixed, with a message.
- [ ] Scale lock off: notes are no longer forced into the key.

### 4. Playback in Renoise
- [ ] Press play: Zampler's preview plays in time with the song and loops.
- [ ] Stop and start, and jump to another position: no stuck notes.
- [ ] *Preview sound* off mutes it.

### 5. Getting notes into the song
- [ ] *Save Renoise tool (.xrnt)*, then drag it onto Renoise: it installs.
- [ ] *Save Renoise pattern*, then **Tools → Zampler → Import Pattern File…**: new tracks appear with the notes; the tempo and LPB are set; patterns longer than one get added.
- [ ] *Drag MIDI out* onto Renoise or the desktop: you get a `.mid`.
- [ ] *Save MIDI*: the file opens in another DAW with one track per part, and drums on channel 10.

### 6. Save / reload
- [ ] Save the Renoise song, close it and reopen it: the settings come back and the sample is re-analysed.

## Reporting

Please send, for each problem:
1. The sample (or a short excerpt of it).
2. What Zampler said: the tempo, key, note count and drum count in the info strip.
3. What it should have been.
4. A screenshot of the pattern view.

For the best tuning data, include samples where Zampler got it wrong: missed notes, ghost
notes, wrong drums or the wrong tempo.
