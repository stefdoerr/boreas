# Boreas Beginner Manual Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** A beginner-facing ~6-page A4 PDF manual for the Boreas freeze pedal, generated reproducibly from an HTML source via headless Chrome and a `make manual` target.

**Architecture:** One self-contained HTML file (inline print CSS, A4 `@page`, controlled page breaks) is the single source of truth; `google-chrome --headless --print-to-pdf` renders it; a Makefile target wires the two together. The pedal screenshot is referenced by relative path from the repo.

**Tech Stack:** HTML + print CSS, headless Google Chrome (installed at `google-chrome`), GNU make.

**Spec:** `docs/superpowers/specs/2026-07-15-boreas-beginner-manual-design.md`

## Global Constraints

- Audience: musicians with zero code/DSP knowledge. No FFT/oscillator/partial/file-name jargon anywhere in body text. The ONLY smoothness explanation allowed: "it re-plays the tone of that moment, not a tape loop — so there is no seam, wobble, or repeat."
- Paper: A4. System fonts only (no webfont fetches — render must work offline).
- All control behavior statements must match README.md semantics (which match the current engine incl. the Hold-release fix: Hold's release removes exactly the layer its press created).
- Repo/products facts: GitHub repo `https://github.com/stefdoerr/boreas`, latest release `v0.0.2`, release assets `boreas-v0.0.2-linux-x86_64.tar.gz` (MOD Desktop) and `boreas-v0.0.2-dwarf-aarch64.tar.gz` (Dwarf). Plugin appears under brand **Stefan** in MOD.
- Dwarf curl install command (verified against mod-plugin-builder `publish`): `base64 boreas-v0.0.2-dwarf-aarch64.tar.gz | curl -F 'package=@-' http://192.168.51.1/sdk/install`
- Git: stage files only, NEVER commit (user's standing rule overrides this skill's commit steps).
- Knob facts (from README/engine, use these numbers): Speed fades ~5 ms (max) to ~4 s (min), default ≈ 1 s; Layer default full; Gliss max = start one octave below, glide up over ~2 s, 0 = off; Tone high-cut ~500 Hz (min) to ~18 kHz (max), default ≈ 2 kHz; Move Rate ~0.05–8 Hz, default ≈ 0.25 Hz; Move Depth 0 = perfectly static freeze, max = ±60 % volume breathing + ~±10 cents shimmer; Dry/Effect defaults 0.5/0.5; six layers max; Freeze/Clear fire once per press, Hold only while held.

---

### Task 1: Manual HTML source

**Files:**
- Create: `docs/manual/boreas-manual.html`

**Interfaces:**
- Consumes: `plugins/Boreas/modgui/screenshot-boreas.png` via relative path `../../plugins/Boreas/modgui/screenshot-boreas.png`
- Produces: printable HTML whose Chrome-rendered PDF is ~6 A4 pages; Task 2's make target renders exactly this file.

- [ ] **Step 1: Inspect the pedal screenshot** — `Read plugins/Boreas/modgui/screenshot-boreas.png` and note the on-pedal positions of the 8 knobs and 3 buttons, to place the numbered callout badges (CSS `position:absolute` with `%` coordinates over the image).

- [ ] **Step 2: Write `docs/manual/boreas-manual.html`** with inline CSS and these sections in order (content per the spec outline; each `<section>` uses `break-before: page` where a new page must start):
  1. Cover (dark glacial background, screenshot, title "BOREAS", tagline "Hold a moment of sound forever.", "User Manual — July 2026 edition", brand Stefan).
  2. "What Boreas does" intro.
  3. "Quick start" — 5 numbered steps.
  4. "The controls" — screenshot with numbered badges; footswitch table (Freeze / Hold / Clear); knob-by-knob entries with min/default/max sweep descriptions using the Global Constraints numbers.
  5. "Recipes" — 5 recipe cards: Ambient Pad Bed / Slow-Swell Cathedral / Breathing Drone / Momentary Swells (Hold) / Rising Intro (Gliss), each = knob settings + what to listen for.
  6. "Tips" — freeze while the note rings; stack chords one note at a time; many layers = each slightly thinner; Move Depth 0 = static.
  7. "Installing Boreas" — GitHub release download; MOD Desktop unzip to `~/Documents/MOD Desktop/lv2/` + restart; Dwarf primary = the curl command from Global Constraints (device connected over USB, then refresh web UI / reboot if absent); Dwarf alternative = `scp -O -r boreas.lv2 root@192.168.51.1:/root/.lv2/` + reboot.
  8. FAQ (nothing on Freeze / dull / wobble / stereo / layer limit) + specs table + credits (DPF, MOD Audio, EHX Deep Freeze inspiration; ISC license).
  Print CSS: `@page { size: A4; margin: 18mm 16mm }`, `h2/h3 { break-after: avoid }`, tables/cards `break-inside: avoid`, ice-blue/steel accent palette on white body, dark cover page.

- [ ] **Step 3: Render and inspect** — `google-chrome --headless --disable-gpu --no-pdf-header-footer --print-to-pdf=/tmp/claude-1000/-home-sdoerr-Fun-boreas/52c377c9-d0c5-42ba-a00b-0cdab7952fa1/scratchpad/manual-draft.pdf docs/manual/boreas-manual.html`, then `Read` the PDF. Expected: ~6 pages, screenshot renders (not a broken-image icon), badges sit on knobs, no orphaned headings, no page overflow. Iterate on CSS until true.

- [ ] **Step 4: Jargon check** — `grep -iE 'FFT|oscillator|partial|DSP|\.hpp|\.cpp|LV2' docs/manual/boreas-manual.html` — LV2 may appear ONLY in the install/specs sections; the others must not appear at all. Fix any hits.

- [ ] **Step 5: Stage** — `git add docs/manual/boreas-manual.html` (no commit).

### Task 2: `make manual` target + shipped PDF

**Files:**
- Modify: `Makefile` (append after the `install` block, before the Dwarf section)
- Create (generated): `docs/manual/boreas-manual.pdf`

**Interfaces:**
- Consumes: `docs/manual/boreas-manual.html` from Task 1.
- Produces: `make manual` target; committed-quality PDF at `docs/manual/boreas-manual.pdf`.

- [ ] **Step 1: Add the target to the top-level Makefile:**

```make
# ---------------------------------------------------------------------------
# manual: render the beginner PDF manual from its HTML source via headless
# Chrome. The PDF is generated output — edit the HTML, then re-run this.

MANUAL_HTML := docs/manual/boreas-manual.html
MANUAL_PDF  := docs/manual/boreas-manual.pdf
CHROME      ?= google-chrome

manual:
	$(CHROME) --headless --disable-gpu --no-pdf-header-footer \
		--print-to-pdf=$(MANUAL_PDF) $(MANUAL_HTML)
	@echo "==> $(MANUAL_PDF)"

.PHONY: manual
```

- [ ] **Step 2: Run `make manual`** — expected: exits 0, prints `==> docs/manual/boreas-manual.pdf`, file exists and is non-trivially sized (>100 kB with the screenshot).

- [ ] **Step 3: Final acceptance pass** — `Read` the generated PDF end-to-end and check every acceptance criterion in the spec: ~6 pages; all 8 knobs + 3 footswitches documented; install commands copy-pasteable; no orphaned headings.

- [ ] **Step 4: Stage** — `git add Makefile docs/manual/boreas-manual.pdf` (no commit).

## Self-Review

- Spec coverage: deliverable 1 → Task 1; deliverables 2+3 → Task 2; all 8 content sections enumerated in Task 1 Step 2; both Dwarf install methods present; acceptance criteria mapped to Task 1 Step 3/4 and Task 2 Step 3. No gaps.
- Placeholders: none — commands, paths, palette, and copy facts are concrete.
- Consistency: file paths and target names identical across tasks (`docs/manual/boreas-manual.{html,pdf}`, `manual`).
- Deviation from skill template: commit steps replaced by stage-only steps (user's standing no-commit rule).
