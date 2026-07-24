#!/usr/bin/env python3
"""
Render screenshot-boreas.png via headless Chromium so it matches MOD-UI's
actual rendering of the pedal (real fonts, real knob sprite, real CSS
gradients). Ported from the sitar repo's render_screenshot.py.

Pipeline: resolve the Mustache tokens MOD-UI fills at runtime, point the knob
sprite URL at a local file, drop the audio/MIDI/CV port loops (the jacks sit
outside the frame and are cropped away), then screenshot at WIDTH x HEIGHT.

Run inside any Python env that has Playwright + Chromium:
    pip install playwright && playwright install chromium
    python3 render_screenshot.py
"""

import os
import re
import shutil
import tempfile

from playwright.sync_api import sync_playwright

HERE   = os.path.dirname(os.path.abspath(__file__))
ICON   = os.path.join(HERE, "icon-boreas.html")
STYLE  = os.path.join(HERE, "stylesheet-boreas.css")
SPRITE = os.path.join(HERE, "knobs", "black.png")
OUT    = os.path.join(HERE, "screenshot-boreas.png")

WIDTH  = 520
HEIGHT = 324
# Device pixel ratio for the render. 2x keeps the same 520x324 CSS layout but
# outputs 1040x648 — crisper on HiDPI screens and in the printed manual.
SCALE  = 2

# Same values you put in modgui.ttl.
SUBSTITUTIONS = {
    "{{brand}}":   "Stefan",
    "{{label}}":   "Boreas",
    "{{color}}":   "metal2",
    "{{knob}}":    "gold",
    "{{{cns}}}":   "",   # cache-bust class suffix; harmless to drop
    "{{{ns}}}":    "",   # cache-bust query string; not needed for local files
    "{{instancename}}": "boreas-render",
}


def render_template(html: str) -> str:
    for k, v in SUBSTITUTIONS.items():
        html = html.replace(k, v)
    # Drop the audio/midi/cv port iteration blocks. The jacks are positioned
    # outside the pedal frame and cropped by overflow:hidden, so the screenshot
    # doesn't need them (and their Mustache loops can't render statically).
    for kind in ("audio", "midi", "cv"):
        for direction in ("input", "output"):
            html = re.sub(
                r"\{\{#effect\.ports\." + kind + r"\." + direction + r"\}\}.*?\{\{/effect\.ports\." + kind + r"\." + direction + r"\}\}",
                "", html, flags=re.DOTALL,
            )
    return html


def render_stylesheet(css: str, sprite_url: str) -> str:
    for k, v in SUBSTITUTIONS.items():
        css = css.replace(k, v)
    css = css.replace("/resources/knobs/black.png", sprite_url)
    return css


def main():
    with open(ICON,  "r", encoding="utf-8") as f: icon_html = f.read()
    with open(STYLE, "r", encoding="utf-8") as f: css       = f.read()

    sprite_url = "file://" + SPRITE
    css        = render_stylesheet(css, sprite_url)
    pedal_html = render_template(icon_html)

    page = f"""<!doctype html>
<html><head>
<meta charset="utf-8">
<style>
  html, body {{
    margin: 0; padding: 0;
    width: {WIDTH}px; height: {HEIGHT}px;
    overflow: hidden;
    background: transparent;
  }}
  .pedal-host {{ position: relative; width: {WIDTH}px; height: {HEIGHT}px; }}
{css}
</style></head>
<body><div class="pedal-host">{pedal_html}</div></body></html>"""

    with tempfile.TemporaryDirectory(prefix="boreas-render-") as tmp:
        page_path = os.path.join(tmp, "render.html")
        with open(page_path, "w", encoding="utf-8") as f:
            f.write(page)

        with sync_playwright() as p:
            browser = p.chromium.launch()
            context = browser.new_context(
                viewport={"width": WIDTH, "height": HEIGHT},
                device_scale_factor=SCALE,
            )
            tab = context.new_page()
            tab.goto("file://" + page_path)
            tab.wait_for_load_state("networkidle")

            tmp_out = os.path.join(tmp, "screenshot.png")
            tab.screenshot(path=tmp_out, omit_background=False, full_page=False)
            browser.close()

        shutil.copyfile(tmp_out, OUT)
        print(f"Wrote {OUT} ({WIDTH*SCALE}x{HEIGHT*SCALE} @ {SCALE}x, rendered by headless Chromium)")


if __name__ == "__main__":
    main()
