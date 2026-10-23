from pathlib import Path

import streamlit as st
import streamlit.components.v1 as components

st.set_page_config(page_title="Z-NOISE", layout="wide")

html = (Path(__file__).parent / "z-noise" / "index.html").read_text(encoding="utf-8")
components.html(html, height=1900, scrolling=True)
