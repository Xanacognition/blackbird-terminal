# Wonderblue-OS

A Renoise-style tracker firmware concept for the FM-1, aimed at ambient breakbeat, breakcore, trip-hop and boom bap.

`index.html` is a self-contained, playable browser build (Web Audio, no dependencies besides Google Fonts). Open it on a phone or desktop, tap **POWER ON**, pick a vibe and press **PLAY** or **SIT**.

`z-noise.html` is the same page without the document wrapper; it is the source for the published artifact. Edit it, then regenerate `index.html` by wrapping it in a doctype/head/body.

## Engine

- 8 tracks: two break slicers (BRK, CHP), FM kick, snare, hats, FM bass, FM keys (chord stacking, EP tine), grain cloud.
- Breaks are synthesized at power-on and crushed to 12-bit / 26 kHz; 16 slices each. LOAD WAV slices your own.
- Effect column: `R` retrig, `G` chance, `D` delay, `B` reverse, `P` pitch, `OFF`.
- Akai-style pads with REC punch-in (quantized to the nearest row), note repeat, swing.
- Hold-to-engage punch-ins: rolls, reverse, half-time, scatter, dub, wash, filter.
- Generative: per-track chance, mutate, drift and density; FILL/DICE/RESTORE.
- SIT mode: plays itself, moving between sections, mutating parts and drifting FM/grain timbre.
- Master: tape wow/flutter, vinyl crackle, bitcrush, reverb, dotted delay, glue compressor.
