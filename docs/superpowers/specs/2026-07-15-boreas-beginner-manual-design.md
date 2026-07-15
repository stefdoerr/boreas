# Boreas beginner manual — design

**Date:** 2026-07-15
**Status:** approved (pending final spec review)

## Goal

A beginner-facing PDF manual for the Boreas freeze pedal plugin. The reader is a
musician using MOD Desktop or a MOD Dwarf. They have no knowledge of — and no
interest in — the code or the DSP; the manual covers only how to install and
play the pedal.

## Non-goals

- No DSP explanations beyond one plain-language sentence on why the freeze
  sounds smooth. No mention of FFTs, oscillator banks, partials, or code files.
- No developer content (building from source, cross-compiling, modgui).
- Not a replacement for README.md, which stays developer-facing.

## Deliverables

1. `docs/manual/boreas-manual.html` — manual source. Single HTML file with
   inline CSS, print-oriented (`@page` A4, controlled page breaks). References
   the pedal screenshot by relative path
   (`../../plugins/Boreas/modgui/screenshot-boreas.png`); the HTML renders
   correctly only from inside the repo, which is acceptable because the PDF is
   the shipped artifact.
2. `docs/manual/boreas-manual.pdf` — generated output, kept in the repo so it
   can be attached to GitHub releases.
3. `make manual` target in the top-level Makefile: renders the PDF from the
   HTML with headless Chrome
   (`google-chrome --headless --no-pdf-header-footer --print-to-pdf=...`).
   The PDF is never hand-edited; regenerate via make.

## Content outline (~6 A4 pages)

1. **Cover** — pedal screenshot, name, tagline, edition date (July 2026).
2. **What Boreas does** — freezing explained for a first-timer: stomp, and the
   sound you were just making holds as a steady, endless drone under your
   playing. One sentence on smoothness ("it re-plays the tone of that moment,
   not a tape loop — so there is no seam, wobble, or repeat").
3. **Quick start** — five numbered steps: add to pedalboard → set Dry/Effect →
   play and hold a note → stomp Freeze while it rings → play over it; Clear to
   remove.
4. **The controls** — footswitches first (Freeze stacks up to six layers;
   Clear removes the newest — stack-of-plates metaphor; Hold freezes only
   while pressed), then knob-by-knob: what Speed, Layer, Gliss, Tone,
   Move Rate, Move Depth, Dry, Effect each do at min / default / max, phrased
   by ear, matching the semantics in README.md. Numbered callout badges over
   the pedal screenshot map names to positions.
5. **Recipes** — five named settings recipes (ambient pad bed / slow-swell
   cathedral / breathing drone / momentary swells on Hold / rising-octave
   intro with Gliss), given as knob settings plus what to listen for.
6. **Tips** — freeze while the note rings (capture is the last instant of
   sound, so don't stomp during the pick attack); build chords one note at a
   time; more layers = each slightly thinner (shared voice budget, phrased
   plainly); Movement depth 0 = perfectly static.
7. **Installing** — download the platform tarball from GitHub Releases.
   - MOD Desktop (Linux): unzip into `~/Documents/MOD Desktop/lv2/`, restart.
   - MOD Dwarf, primary method (no SSH): with the unit connected via USB,
     `base64 boreas-vX.Y.Z-dwarf-aarch64.tar.gz | curl -F 'package=@-'
     http://192.168.51.1/sdk/install` — then refresh the web UI; reboot the
     unit if it doesn't appear. (Command format verified against
     mod-plugin-builder's `publish` script: tar.gz must be base64-encoded,
     form field `package`, endpoint `/sdk/install`.)
   - MOD Dwarf, alternative: `scp -O` the bundle to `/root/.lv2/` and reboot.
8. **FAQ / troubleshooting + back page** — nothing happens on Freeze (Effect
   at 0? input silent? stack full — six layers?); sounds dull (Tone default is
   deliberately dark — open it); freeze wobbles (Move Depth up); is it stereo
   (mono in/out); specs table and credits/license.

## Visual design

Boreas-themed: dark glacial cover page; interior pages white (print-friendly)
with ice-blue/steel accents, styled control tables, callout badges. System
fonts only — deterministic offline rendering, no webfont fetches. A4.

## Acceptance criteria

- `make manual` produces the PDF with no errors on this machine.
- All controls documented; behavior statements match README.md (which matches
  the current engine, including the Hold-release fix).
- Install commands are copy-pasteable and syntactically verified.
- No code, file names, or DSP jargon in the body text.
- Roughly 6 pages; no orphaned headings across page breaks.
