"""Zampler: drop a sample, get it as tracker notes, build parts that fit."""

from __future__ import annotations

import html
from pathlib import Path

import altair as alt
import pandas as pd
import streamlit as st

from zampler import generators as gen
from zampler.analysis import analyze
from zampler.export import renoise_tool_bytes, to_midi, to_renoise
from zampler.theory import NOTE_NAMES, SCALES, Key, note_name
from zampler.tracker import Grid, Part, quantize, render_pattern, song_length

st.set_page_config(page_title="Zampler", page_icon="🎛️", layout="wide")

st.markdown(
    """
<style>
.zkeys {display:flex; gap:4px; margin:6px 0 2px 0; flex-wrap:wrap}
.zkey {width:52px; padding:8px 0 6px; text-align:center; border-radius:6px; font-family:monospace;
       font-size:13px; border:1px solid rgba(128,128,128,.35); position:relative; overflow:hidden}
.zkey.in {background:#1f6f4a; color:#e8fff2; border-color:#2fa36d}
.zkey.out {background:rgba(128,128,128,.12); color:rgba(128,128,128,.9); text-decoration:line-through}
.zkey.root {box-shadow: inset 0 0 0 2px #ffd166}
.zkey .bar {position:absolute; left:0; bottom:0; height:4px; background:#ffd166}
.zkey small {display:block; font-size:10px; opacity:.8}
.zchords {display:flex; gap:4px; flex-wrap:wrap; font-family:monospace}
.zchords span {padding:4px 8px; border-radius:4px; background:rgba(128,128,128,.15)}
</style>
""",
    unsafe_allow_html=True,
)

st.title("🎛️ Zampler")
st.caption("Drop a sample → get it as Renoise tracker notes → build bass, chords and leads that can't hit a wrong note.")


@st.cache_data(show_spinner=False, max_entries=8)
def run_analysis(data: bytes, bpm_hint: float | None, fmin: str, fmax: str):
    return analyze(data, bpm_hint=bpm_hint, fmin=fmin, fmax=fmax)


# --------------------------------------------------------------------------- sidebar
with st.sidebar:
    st.header("Settings")
    bpm_override = st.number_input("BPM (0 = detect)", 0.0, 999.0, 0.0, 1.0)
    key_mode = st.radio("Key", ["Detect", "Set manually"], horizontal=True)
    if key_mode == "Set manually":
        c1, c2 = st.columns(2)
        root_name = c1.selectbox("Root", NOTE_NAMES, index=9)
        scale_name = c2.selectbox("Scale", list(SCALES), index=1)
    else:
        scale_name = st.selectbox("Scale flavour", ["(as detected)"] + list(SCALES), index=0,
                                  help="Keep the detected root but use another scale/mode, e.g. dorian or pentatonic.")
    scale_lock = st.toggle("Scale lock", True, help="Every generated or typed note is snapped into the key.")
    fix_sample = st.toggle("Snap the sample's own notes too", False,
                           help="Off: the sample is transcribed exactly as heard (out-of-key notes are flagged).")
    st.divider()
    lpb = st.select_slider("Lines per beat (LPB)", [2, 4, 8, 16], value=4)
    pattern_lines = st.select_slider("Pattern length", [16, 32, 64, 128, 256, 512], value=64)
    with st.expander("Pitch range for detection"):
        fmin = st.selectbox("Lowest note", ["C1", "C2", "C3"], index=0)
        fmax = st.selectbox("Highest note", ["C5", "C6", "C7"], index=2)

uploaded = st.file_uploader("Drag and drop a sample", type=["wav", "flac", "ogg", "aif", "aiff", "mp3"])

if uploaded is None:
    st.info(
        "**How it works**\n\n"
        "1. Drop a sample (a loop, a vocal chop, a melody, a bassline).\n"
        "2. Zampler finds the tempo, the key and every note, and lays them out on a tracker grid.\n"
        "3. Use the note map and the builders to add bass, chords and leads, all locked to the key.\n"
        "4. Export MIDI, or a pattern file for the **Zampler Import** Renoise tool."
    )
    st.stop()

data = uploaded.getvalue()
st.audio(data)
with st.spinner("Listening to your sample…"):
    try:
        a = run_analysis(data, bpm_override or None, fmin, fmax)
    except Exception as e:  # unreadable/odd file
        st.error(f"Couldn't read that file: {e}")
        st.stop()

if key_mode == "Set manually":
    key = Key(NOTE_NAMES.index(root_name), scale_name)
else:
    key = Key(a.key.root, a.key.scale if scale_name == "(as detected)" else scale_name)

grid = Grid(bpm=a.bpm, lpb=lpb)
sample_steps = quantize(a.notes, grid, key, scale_lock=fix_sample)
sample_part = Part("Sample", sample_steps, instrument=0)
bars = max(1, song_length([sample_part], grid) // grid.lines_per_bar)
out_of_key = [n for n in a.notes if not key.contains(n.pitch)]

m1, m2, m3, m4 = st.columns(4)
m1.metric("Tempo", f"{a.bpm:g} BPM")
m2.metric("Key", key.name, f"{a.key_confidence:.0%} sure" if key_mode == "Detect" else "manual",
          delta_color="off", delta_arrow="off")
m3.metric("Notes found", len(a.notes), f"{len(out_of_key)} out of key" if out_of_key else "all in key",
          delta_color="inverse" if out_of_key else "off", delta_arrow="off")
m4.metric("Length", f"{bars} bar{'s' if bars != 1 else ''}", f"{a.duration:.1f}s", delta_color="off", delta_arrow="off")

# --------------------------------------------------------------------------- note map
st.subheader("Note map")
usage = a.pitch_profile / a.pitch_profile.max() if a.pitch_profile.max() > 0 else a.pitch_profile
keys_html = []
for pc in range(12):
    cls = "in" if pc in key.pitch_classes else "out"
    if pc == key.root:
        cls += " root"
    deg = ""
    if pc in key.pitch_classes:
        deg = str(key.intervals.index((pc - key.root) % 12) + 1)
    keys_html.append(
        f'<div class="zkey {cls}" title="used {usage[pc]:.0%}">{NOTE_NAMES[pc]}'
        f'<small>{deg or "✕"}</small><div class="bar" style="width:{usage[pc] * 100:.0f}%"></div></div>'
    )
st.markdown(f'<div class="zkeys">{"".join(keys_html)}</div>', unsafe_allow_html=True)
st.caption("Green = safe notes in the key (number = scale degree, gold outline = root). "
           "Gold bar = how much the sample uses that note. Struck-out notes are blocked by scale lock.")

if a.bar_chords:
    st.markdown("**Chords heard in the sample (per bar)**")
    st.markdown(
        '<div class="zchords">' + "".join(f"<span>{i + 1}: {html.escape(c.name)}</span>"
                                          for i, (_, c) in enumerate(a.bar_chords)) + "</div>",
        unsafe_allow_html=True,
    )
if out_of_key and not fix_sample:
    names = sorted({note_name(n.pitch) for n in out_of_key})
    st.warning(f"The sample itself plays {len(out_of_key)} note(s) outside {key.name}: {', '.join(names)}. "
               "They're kept as-is in the Sample track. Try another scale flavour, or turn on "
               "*Snap the sample's own notes too*.")

# --------------------------------------------------------------------------- builders
st.subheader("Build on it")
sample_chords = [c for _, c in a.bar_chords]
tabs = st.tabs(["🎸 Bass", "🎹 Chords", "🎺 Lead", "✏️ Sketch"])
parts: list[Part] = [sample_part]

with tabs[0]:
    c = st.columns([1, 2, 2, 1])
    on_bass = c[0].toggle("Bass", True)
    bass_prog = c[1].selectbox("Progression", list(gen.PROGRESSIONS), key="bprog")
    bass_style = c[2].selectbox("Style", gen.BASS_STYLES, index=1)
    bass_oct = c[3].number_input("Octave", 0, 6, 2, key="boct")
with tabs[1]:
    c = st.columns([1, 2, 2, 1, 1])
    on_chords = c[0].toggle("Chords", True)
    chord_prog = c[1].selectbox("Progression", list(gen.PROGRESSIONS), key="cprog",
                                help="'From sample' uses the chords Zampler heard in each bar.")
    chord_style = c[2].selectbox("Rhythm", gen.CHORD_STYLES, index=0)
    chord_oct = c[3].number_input("Octave", 1, 7, 3, key="coct")
    sevenths = c[4].toggle("7ths", False)
with tabs[2]:
    c = st.columns([1, 2, 1, 1, 1])
    on_lead = c[0].toggle("Lead", False)
    lead_style = c[1].selectbox("Style", gen.LEAD_STYLES)
    lead_oct = c[2].number_input("Octave", 2, 8, 5, key="loct")
    density = c[3].slider("Busy", 0.0, 1.0, 0.5)
    seed = c[4].number_input("Variation #", 0, 9999, 1)
with tabs[3]:
    sketch_text = st.text_input(
        "Type notes (space separated). `.` = rest, `-` = hold. Wrong notes get fixed for you.",
        placeholder="A3 C4 E4 - G4 . F4 E4 D#4 C4",
    )
    sketch_step = st.select_slider("Each token lasts", ["1/16", "1/8", "1/4"], value="1/8")

prog_cache: dict[tuple[str, bool], list] = {}


def prog(name: str, sev: bool = False):
    if (name, sev) not in prog_cache:
        prog_cache[(name, sev)] = gen.progression(key, name, bars, sample_chords, sev)
    return prog_cache[(name, sev)]


if on_bass:
    parts.append(gen.bassline(key, prog(bass_prog), grid, bass_style, int(bass_oct), scale_lock=scale_lock))
if on_chords:
    parts.append(gen.chord_part(key, prog(chord_prog, sevenths), grid, chord_style, int(chord_oct),
                                scale_lock=scale_lock))
if on_lead:
    parts.append(gen.lead(key, prog(chord_prog), grid, lead_style, int(lead_oct), density, int(seed),
                          sample_steps=sample_steps))
if sketch_text.strip():
    step_lines = {"1/16": lpb // 4 or 1, "1/8": max(1, lpb // 2), "1/4": lpb}[sketch_step]
    sk, fixes = gen.sketch(sketch_text, key, grid, step_lines, scale_lock=scale_lock)
    parts.append(sk)
    with tabs[3]:
        for f in fixes:
            st.caption(f"🔒 {f}")

# --------------------------------------------------------------------------- piano roll
rows = []
for p in parts:
    for s in p.steps:
        rows.append({"part": p.name, "start": s.line / grid.lines_per_bar + 1, "end": s.end / grid.lines_per_bar + 1,
                     "pitch": s.pitch, "top": s.pitch + 0.9, "note": note_name(s.pitch), "in key": key.contains(s.pitch)})
if rows:
    df = pd.DataFrame(rows)
    roll = (
        alt.Chart(df)
        .mark_rect(cornerRadius=2)
        .encode(
            x=alt.X("start:Q", title="bar", scale=alt.Scale(domain=[1, bars + 1])),
            x2="end:Q",
            y=alt.Y("pitch:Q", title="note", scale=alt.Scale(zero=False)),
            y2="top:Q",
            color=alt.Color("part:N", legend=alt.Legend(orient="top")),
            opacity=alt.condition("datum['in key']", alt.value(0.9), alt.value(0.35)),
            tooltip=["part", "note", "start", "in key"],
        )
        .properties(height=320)
    )
    st.altair_chart(roll.interactive(bind_y=False), use_container_width=True)

# --------------------------------------------------------------------------- tracker view
st.subheader("Pattern")
total = song_length(parts, grid)
n_patterns = -(-total // pattern_lines)
pat = st.number_input(f"Pattern (of {n_patterns})", 1, max(1, n_patterns), 1) - 1
st.code(render_pattern(parts, grid, pat * pattern_lines, pattern_lines), language=None)

# --------------------------------------------------------------------------- export
st.subheader("Export")
stem = Path(uploaded.name).stem or "zampler"
e1, e2, e3 = st.columns(3)
e1.download_button("⬇️ MIDI (all tracks)", to_midi(parts, grid), f"{stem}.mid", "audio/midi",
                   use_container_width=True)
e2.download_button("⬇️ Renoise pattern file", to_renoise(parts, grid, pattern_lines, stem),
                   f"{stem}.zampler.lua", "text/plain", use_container_width=True)
e3.download_button("⬇️ Zampler Import tool (.xrnt)", renoise_tool_bytes(), "com.zampler.Import.xrnt",
                   "application/zip", use_container_width=True)
st.caption(
    "**Into Renoise:** drag the `.xrnt` onto Renoise once to install the tool, then "
    "*Tools → Zampler → Import Pattern File…* and pick the `.zampler.lua`. Each part becomes its own track "
    "(instrument 00 = sample, 01 = bass, 02 = chords, 03 = lead, 04 = sketch), starting at the selected pattern. "
    "The MIDI file works in any DAW."
)
