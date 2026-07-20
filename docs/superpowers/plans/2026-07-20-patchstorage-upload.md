# Patchstorage Publishing Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a local `make patchstorage` target that builds the plugin for the three Patchstorage LV2-platform targets (`linux-amd64`, `rpi-aarch64`, `patchbox-os-arm32`) and publishes it to patchstorage.com, plus attach those three bundles to the GitHub release.

**Architecture:** Reuse Patchstorage's prebuilt cross-toolchain Docker images (`patchstorage/lv2_builder-<platform>:latest`), driven by our proven two-phase DPF build (native pass for `.ttl`+modgui, then cross-compile the `.so`). The third-party `patchstorage-lv2-uploader` is vendored by copying the few files we use (not a submodule) and run from a disposable scratch tree assembled at publish time. Implemented and verified in **boreas first** (a real, testable plugin), then the generic pieces are ported to `mod-plugin-template`.

**Tech Stack:** GNU Make, Bash, Docker, Python 3 (`requests click rdflib`), `jq`, DPF, `gh` CLI.

**Design spec:** `docs/superpowers/specs/2026-07-20-patchstorage-upload-design.md`

## Global Constraints

These apply to **every** task:

- **Staging only — never `git commit`.** Repo convention (see project memory): stage changes with `git add`; committing is the user's call. Each task ends by staging, not committing.
- **CPU/opt flags match Patchstorage's defconfig exactly:** `linux-amd64` → `-msse -msse2 -mfpmath=sse`; `rpi-aarch64` → `-mcpu=cortex-a72`; `patchbox-os-arm32` → `-mcpu=cortex-a53 -mfpu=neon-fp-armv8 -mfloat-abi=hard`.
- **Toolchain tuples (compiler prefixes):** `linux-amd64` → `x86_64-mod-linux-gnu`; `rpi-aarch64` → `aarch64-rpi4-linux-gnu`; `patchbox-os-arm32` → `armv8-rpi3-linux-gnueabihf`.
- **Builder platform ↔ target slug:** `x86_64`→`linux-amd64`, `raspberrypi4_aarch64`→`rpi-aarch64`, `raspberrypi3_armv8`→`patchbox-os-arm32`.
- **Docker images referenced by `:latest`** (not digest-pinned).
- **`patchbox-os-arm32` is 32-bit armhf**, not 64-bit.
- **DSP-only DPF plugins only** (no heavy native deps). Boreas qualifies.
- A **modgui screenshot is mandatory** to publish (uploader errors without one). Boreas ships `screenshot-boreas.png`.
- **Do not touch** `make dwarf`, `make dwarf-deploy`, `make all`, `make install`, `make beta` behavior. The native `make all` build stays as the local/dev build.
- Boreas plugin identity: `PLUGIN=boreas`, plugin dir `plugins/Boreas`, bundle folder `boreas.lv2`.

---

## File Structure

**boreas (Tasks 1–9):**
- Create: `patchstorage-build/build-target.sh` — in-container two-phase cross-build for one target.
- Create: `patchstorage-build/prepare.sh` — host-side: assemble scratch uploader tree, generate `plugins.json`, stage bundles, run `prepare`.
- Create: `patchstorage-build/README.md` — brief notes (mirrors `mod-build/README.md`).
- Create: `patchstorage.json` — per-plugin metadata (`source_code_url`, `donate_url`).
- Create: `patchstorage-build/uploader/` — vendored copy of the uploader (4 files + GPL `LICENSE` + `PROVENANCE`).
- Modify: `plugins/Boreas/Makefile` — add `EXTRA_CFLAGS`/`EXTRA_CXXFLAGS` passthrough.
- Modify: `Makefile` — add `patchstorage-build`, `patchstorage-prepare`, `patchstorage` targets; rework `release-build`/`release` to build+attach the three bundles (replacing `linux-x86_64` with `linux-amd64`).
- Modify: `INSTRUCTIONS.md`, `README.md` — docs.

**mod-plugin-template (Task 10):**
- Same set, with placeholder `patchstorage.json` and `plugins/MyPlugin/Makefile` edited.

---

## Task 1: Vendor the uploader by copying the four files we use

**Why copy, not submodule:** the upstream repo is 436K (mostly `.git` history + a
60K community `plugins.json` we never use); we need only ~56K of stable code. A
submodule would add a `git submodule update --init` step to every fork, against the
template's self-contained goal. GPL-3.0 is fine as mere aggregation (separate
publish tool, not linked into the ISC plugin) provided the `LICENSE` + attribution
travel with the copy.

**Files:**
- Create: `patchstorage-build/uploader/{uploader.py,bundles.py,licenses.json,categories.json,LICENSE,PROVENANCE}`

**Interfaces:**
- Produces: `patchstorage-build/uploader/{uploader.py,bundles.py,licenses.json,categories.json}` available for `prepare.sh` (Task 6).

Upstream source: `https://github.com/patchstorage/patchstorage-lv2-uploader` @ commit `4141b23c201834a10382275918b97cf166e07274` (2024-05-05). A local clone exists at `/home/sdoerr/Fun/patchstorage-lv2-uploader`.

- [ ] **Step 1: Confirm the target directory is absent (the gap)**

Run: `ls patchstorage-build/uploader 2>&1`
Expected: `ls: cannot access 'patchstorage-build/uploader': No such file or directory`

- [ ] **Step 2: Copy the four files + GPL LICENSE from the local clone**

```bash
mkdir -p patchstorage-build/uploader
UP=/home/sdoerr/Fun/patchstorage-lv2-uploader
cp "$UP/uploader.py" "$UP/bundles.py" "$UP/licenses.json" "$UP/categories.json" "$UP/LICENSE" \
   patchstorage-build/uploader/
```
(If the local clone is gone: `git clone https://github.com/patchstorage/patchstorage-lv2-uploader /tmp/psup && UP=/tmp/psup` first.)

- [ ] **Step 3: Write the provenance note**

`patchstorage-build/uploader/PROVENANCE`:
```
Vendored (copied, not a submodule) from:
  https://github.com/patchstorage/patchstorage-lv2-uploader
  commit 4141b23c201834a10382275918b97cf166e07274 (2024-05-05)

Files: uploader.py, bundles.py, licenses.json, categories.json
License: GNU GPL-3.0 (see LICENSE in this directory).

Only these four files are used. The upstream community plugins.json is NOT
vendored — this repo generates a per-plugin plugins.json at publish time from the
repo-root patchstorage.json (see patchstorage-build/prepare.sh).

To re-sync: copy the four files from a fresh checkout and update the commit above.
```

- [ ] **Step 4: Verify the files exist, are GPL, and the CLI's deps import**

Run:
```bash
ls patchstorage-build/uploader/{uploader.py,bundles.py,licenses.json,categories.json,LICENSE,PROVENANCE}
head -2 patchstorage-build/uploader/LICENSE | grep -q 'GENERAL PUBLIC LICENSE' && echo "GPL LICENSE present"
python3 -c "import requests, click, rdflib" && echo "python deps OK"
```
Expected: all six paths listed; `GPL LICENSE present`; `python deps OK`.
If the import fails: `pip install requests click rdflib` (document, don't fail the task).

- [ ] **Step 5: Stage**

```bash
git add patchstorage-build/uploader
```

---

## Task 2: Per-plugin metadata file

**Files:**
- Create: `patchstorage.json`

**Interfaces:**
- Produces: `patchstorage.json` — a JSON object with at least `source_code_url` and `donate_url`, consumed by `prepare.sh` (Task 6) which wraps it as `{"boreas.lv2": <this object>}`.

- [ ] **Step 1: Write the metadata file**

`patchstorage.json`:
```json
{
    "source_code_url": "https://github.com/stefdoerr/boreas",
    "donate_url": null
}
```

- [ ] **Step 2: Verify it is valid JSON with the required keys and produces the right uploader shape**

Run:
```bash
jq -e 'has("source_code_url") and has("donate_url")' patchstorage.json
jq '{("boreas.lv2"): .}' patchstorage.json
```
Expected: first prints `true`; second prints `{ "boreas.lv2": { "source_code_url": "...", "donate_url": null } }`.

- [ ] **Step 3: Stage**

```bash
git add patchstorage.json
```

---

## Task 3: `EXTRA_CFLAGS`/`EXTRA_CXXFLAGS` passthrough in the inner DPF Makefile

**Why:** The cross-build must inject CPU/opt flags without clobbering the version defines. Passing `CXXFLAGS=...` on the make command line would override the makefile's `CXXFLAGS +=` lines (command-line wins), dropping `-DPLUGIN_VERSION_*`. A dedicated `EXTRA_*` variable, appended inside the makefile, injects flags while preserving the version defines.

**Files:**
- Modify: `plugins/Boreas/Makefile`

**Interfaces:**
- Produces: inner Makefile honors command-line `EXTRA_CFLAGS` and `EXTRA_CXXFLAGS`, appending them to `CFLAGS`/`CXXFLAGS`. Consumed by `build-target.sh` (Task 4).

- [ ] **Step 1: Verify the gap — an injected flag is NOT currently applied without clobbering**

Run:
```bash
make -C plugins/Boreas clean >/dev/null 2>&1
make -n -C plugins/Boreas EXTRA_CXXFLAGS=-DPS_SENTINEL 2>/dev/null \
  | grep BoreasPlugin | grep -- '-DPS_SENTINEL' || echo "SENTINEL ABSENT (expected before change)"
```
Expected: `SENTINEL ABSENT (expected before change)`.

- [ ] **Step 2: Add the passthrough**

In `plugins/Boreas/Makefile`, immediately after the `CXXFLAGS += -DPLUGIN_VERSION_MAJOR=...` block (the version-defines block ending around line 26) and before `FILES_DSP =`, add:

```makefile
# Extra flags injected by the Patchstorage cross-build (CPU/opt flags matching
# patchstorage's defconfig). Kept in a separate variable so passing them on the
# make command line doesn't clobber the version defines above — a command-line
# CXXFLAGS=... would override the makefile's `CXXFLAGS +=` lines entirely.
CFLAGS   += $(EXTRA_CFLAGS)
CXXFLAGS += $(EXTRA_CXXFLAGS)
```

- [ ] **Step 3: Verify the injected flag now appears alongside the version define**

Run:
```bash
make -C plugins/Boreas clean >/dev/null 2>&1
make -n -C plugins/Boreas EXTRA_CXXFLAGS=-DPS_SENTINEL 2>/dev/null \
  | grep BoreasPlugin | grep -- '-DPS_SENTINEL' | grep -- '-DPLUGIN_VERSION_MAJOR' \
  && echo "PASS: sentinel + version defines both present"
```
Expected: a g++ line printed, then `PASS: sentinel + version defines both present`.

- [ ] **Step 4: Stage**

```bash
git add plugins/Boreas/Makefile
```

---

## Task 4: `build-target.sh` + `patchstorage-build` target — validate on `linux-amd64`

This is the load-bearing task: it confirms the prebuilt image exposes a directly-callable toolchain at the expected path and that the two-phase DPF build works inside it. Validate with `linux-amd64` first (smallest/fastest image).

**Files:**
- Create: `patchstorage-build/build-target.sh`
- Modify: `Makefile` (add platform table + `patchstorage-build` target)

**Interfaces:**
- Consumes: `EXTRA_CFLAGS`/`EXTRA_CXXFLAGS` from Task 3; `patchstorage.json` not needed here.
- Produces: `build/patchstorage/<slug>/<plugin>.lv2/` for each slug in `PS_TARGETS`. `build-target.sh` reads env: `PLUGIN`, `TARGET_SLUG`, `TUPLE`, `CPUFLAGS`, `EXPECT_ARCH`, `HOST_UID`, `HOST_GID`; mounts `/src` (ro) and `/out`.

- [ ] **Step 1: Create the build script**

`patchstorage-build/build-target.sh`:
```bash
#!/bin/bash
# Cross-build one Patchstorage target, run INSIDE a patchstorage/lv2_builder-<platform> image.
#
# Invoked by `make patchstorage-build` via `docker run`. The host passes:
#   /src         — source tree, mounted read-only
#   /out         — host output dir (build/patchstorage/<slug>); we drop <plugin>.lv2 here
#   $PLUGIN      — plugin name (bundle is <plugin>.lv2, shared object is <plugin>.so)
#   $TARGET_SLUG — patchstorage target slug (linux-amd64 | rpi-aarch64 | patchbox-os-arm32)
#   $TUPLE       — cross toolchain prefix (e.g. aarch64-rpi4-linux-gnu)
#   $CPUFLAGS    — CPU/opt flags matching patchstorage's defconfig
#   $EXPECT_ARCH — substring `file` must report for the built .so
#   $HOST_UID/$HOST_GID — chown output back to the host user
#
# Two-phase build, mirroring the proven mod-build/build-plugin.sh (Dwarf) pattern:
#   1. Native x86_64 build -> .ttl + modgui bundle (DPF's ttl generator dlopens a native .so).
#   2. Cross-compile the .so with the image's toolchain + CPUFLAGS; overlay onto the stash.
set -euo pipefail

for v in PLUGIN TARGET_SLUG TUPLE CPUFLAGS EXPECT_ARCH; do
  if [ -z "${!v:-}" ]; then echo "build-target.sh: \$$v not set" >&2; exit 1; fi
done
[ -d /src ] || { echo "build-target.sh: /src not mounted" >&2; exit 1; }
[ -d /out ] || { echo "build-target.sh: /out not mounted" >&2; exit 1; }

# Locate the cross toolchain inside the image (auto-discover; fail loudly if the
# image layout ever changes). Expected path is the crosstool-ng prefix dir.
GCC="$(ls /home/builder/lv2-workdir/*/toolchain/bin/${TUPLE}-gcc 2>/dev/null | head -n1 || true)"
if [ -z "$GCC" ]; then
  GCC="$(find / -name "${TUPLE}-gcc" -type f 2>/dev/null | head -n1 || true)"
fi
if [ -z "$GCC" ]; then
  echo "build-target.sh: toolchain '${TUPLE}-gcc' not found in image." >&2
  echo "Toolchains present:" >&2
  ls /home/builder/lv2-workdir/*/toolchain/bin/*-gcc 2>/dev/null >&2 || echo "  (none at expected path)" >&2
  exit 1
fi
BIN_DIR="$(dirname "$GCC")"
echo "==> Toolchain: $BIN_DIR/${TUPLE}-{gcc,g++}"

WORK=/tmp/psbuild/$PLUGIN
rm -rf "$WORK"; mkdir -p "$WORK"
rsync -a --exclude bin --exclude build --exclude '.git' /src/ "$WORK/"
cd "$WORK"

PLUGIN_DIR="$(find plugins -mindepth 2 -maxdepth 2 -name Makefile -printf '%h\n' | head -n1)"
[ -n "$PLUGIN_DIR" ] || { echo "build-target.sh: no plugin Makefile under plugins/*/" >&2; exit 1; }

echo "==> [1/3] Native build (.ttl + modgui assets)"
make -s all
STASH=/tmp/${PLUGIN}-bundle-stash
rm -rf "$STASH"
cp -rL "bin/${PLUGIN}.lv2" "$STASH"

echo "==> [2/3] Cross-compiling ${PLUGIN}.so for ${TARGET_SLUG} (${TUPLE})"
make -s -C "$PLUGIN_DIR" clean
make -s -C "$PLUGIN_DIR" \
  CC="$BIN_DIR/${TUPLE}-gcc" \
  CXX="$BIN_DIR/${TUPLE}-g++" \
  AR="$BIN_DIR/${TUPLE}-ar" \
  STRIP="$BIN_DIR/${TUPLE}-strip" \
  EXTRA_CFLAGS="$CPUFLAGS" \
  EXTRA_CXXFLAGS="$CPUFLAGS" \
  NOOPT=false
"$BIN_DIR/${TUPLE}-strip" "bin/${PLUGIN}.lv2/${PLUGIN}.so"

if ! file "bin/${PLUGIN}.lv2/${PLUGIN}.so" | grep -q "$EXPECT_ARCH"; then
  echo "build-target.sh: unexpected arch for ${TARGET_SLUG} (want '$EXPECT_ARCH')" >&2
  file "bin/${PLUGIN}.lv2/${PLUGIN}.so" >&2
  exit 1
fi

echo "==> [3/3] Publishing bundle to /out/${PLUGIN}.lv2"
cp -f "bin/${PLUGIN}.lv2/${PLUGIN}.so" "$STASH/${PLUGIN}.so"
rm -rf "/out/${PLUGIN}.lv2"
cp -rL "$STASH" "/out/${PLUGIN}.lv2"

if [ -n "${HOST_UID:-}" ] && [ -n "${HOST_GID:-}" ]; then
  chown -R "$HOST_UID:$HOST_GID" /out
fi
echo "==> Done: $(file -b /out/${PLUGIN}.lv2/${PLUGIN}.so)"
```

Then: `chmod +x patchstorage-build/build-target.sh`

- [ ] **Step 2: Add the platform table + `patchstorage-build` target to `Makefile`**

Append to `Makefile` (after the `dwarf` section, before the `release` section):

```makefile
# ---------------------------------------------------------------------------
# Patchstorage build — cross-compile for the three targets patchstorage.com's
# LV2-plugins platform supports, using Patchstorage's own prebuilt toolchain
# images (patchstorage/lv2_builder-<platform>:latest). Our build-target.sh runs
# the same two-phase build as the Dwarf cross-build, but pulls the toolchain
# from their image instead of building one. Output: build/patchstorage/<slug>/.

PS_TARGETS := linux-amd64 rpi-aarch64 patchbox-os-arm32
PS_DIR     := build/patchstorage

patchstorage-build:
	@set -e; for slug in $(PS_TARGETS); do \
	  case $$slug in \
	    linux-amd64) plat=x86_64; tuple=x86_64-mod-linux-gnu; \
	      flags="-msse -msse2 -mfpmath=sse"; arch="x86-64";; \
	    rpi-aarch64) plat=raspberrypi4_aarch64; tuple=aarch64-rpi4-linux-gnu; \
	      flags="-mcpu=cortex-a72"; arch="ARM aarch64";; \
	    patchbox-os-arm32) plat=raspberrypi3_armv8; tuple=armv8-rpi3-linux-gnueabihf; \
	      flags="-mcpu=cortex-a53 -mfpu=neon-fp-armv8 -mfloat-abi=hard"; arch="ARM, EABI5";; \
	    *) echo "unknown slug $$slug"; exit 1;; \
	  esac; \
	  echo "==> Building $(PLUGIN) for $$slug ($$plat)"; \
	  mkdir -p "$(PS_DIR)/$$slug"; \
	  docker run --rm --user root \
	    -e HOST_UID=$$(id -u) -e HOST_GID=$$(id -g) \
	    -e PLUGIN=$(PLUGIN) -e TARGET_SLUG=$$slug \
	    -e TUPLE=$$tuple -e CPUFLAGS="$$flags" -e EXPECT_ARCH="$$arch" \
	    -v "$(CURDIR):/src:ro" \
	    -v "$(CURDIR)/$(PS_DIR)/$$slug:/out" \
	    patchstorage/lv2_builder-$$plat:latest \
	    bash /src/patchstorage-build/build-target.sh; \
	done
	@echo "==> Patchstorage bundles built under $(PS_DIR)/"

.PHONY: patchstorage-build
```

- [ ] **Step 3: Validate the whole approach on `linux-amd64` only**

Run (pulls the x86_64 image on first use):
```bash
docker run --rm --user root \
  -e HOST_UID=$(id -u) -e HOST_GID=$(id -g) \
  -e PLUGIN=boreas -e TARGET_SLUG=linux-amd64 \
  -e TUPLE=x86_64-mod-linux-gnu -e CPUFLAGS="-msse -msse2 -mfpmath=sse" \
  -e EXPECT_ARCH="x86-64" \
  -v "$(pwd):/src:ro" -v "$(pwd)/build/patchstorage/linux-amd64:/out" \
  patchstorage/lv2_builder-x86_64 \
  bash /src/patchstorage-build/build-target.sh
```
Expected: ends with `==> Done: ELF 64-bit LSB shared object, x86-64 ...`.

Then confirm the bundle and ownership:
```bash
file build/patchstorage/linux-amd64/boreas.lv2/boreas.so
ls build/patchstorage/linux-amd64/boreas.lv2/   # boreas.so, manifest.ttl, boreas.ttl, modgui.ttl, modgui/
stat -c '%U' build/patchstorage/linux-amd64/boreas.lv2/boreas.so   # your user, not root
```
Expected: `x86-64`; bundle contents listed; owner is you.

> If the toolchain is NOT found at `/home/builder/lv2-workdir/*/toolchain/bin/`, the script's `find /` fallback prints the real path — record it and, if the layout differs, tighten the glob. If `--user root` is rejected by the image, drop it and instead `sudo chown` the output on the host afterward. This step is where the one unverified assumption in the spec gets resolved.

- [ ] **Step 4: Stage**

```bash
git add patchstorage-build/build-target.sh Makefile
```

---

## Task 5: Verify the two ARM targets and finalize `patchstorage-build`

**Files:** none new (exercises Task 4's script + target for the remaining platforms).

**Interfaces:**
- Produces: `build/patchstorage/rpi-aarch64/boreas.lv2/` and `build/patchstorage/patchbox-os-arm32/boreas.lv2/` with correct-arch `.so`s.

- [ ] **Step 1: Build all three via the Make target**

Run (pulls the two ARM images on first use — several GB, minutes):
```bash
make patchstorage-build
```
Expected: three `==> Done:` lines, no errors.

- [ ] **Step 2: Assert each `.so` is the correct architecture**

Run:
```bash
file build/patchstorage/linux-amd64/boreas.lv2/boreas.so       | grep -q 'x86-64'      && echo "amd64 OK"
file build/patchstorage/rpi-aarch64/boreas.lv2/boreas.so       | grep -q 'ARM aarch64' && echo "aarch64 OK"
file build/patchstorage/patchbox-os-arm32/boreas.lv2/boreas.so | grep -q 'ARM, EABI5'  && echo "arm32 OK"
```
Expected: `amd64 OK`, `aarch64 OK`, `arm32 OK`.

- [ ] **Step 3: Confirm the arm32 binary is hard-float**

Run (use `readelf`, not `file` — `file` does not reliably print the float ABI on a stripped `.so`; the ELF header flags and `.ARM.attributes` section are authoritative and survive stripping):
```bash
SO=build/patchstorage/patchbox-os-arm32/boreas.lv2/boreas.so
readelf -h "$SO" | grep -q 'hard-float ABI' && echo "arm32 hard-float OK (e_flags)"
readelf -A "$SO" | grep -q 'Tag_ABI_VFP_args: VFP registers' && echo "arm32 VFP-args OK"
```
Expected: `arm32 hard-float OK (e_flags)` and `arm32 VFP-args OK`.

> If the cross `make` fails during TTL generation (attempting to run a cross binary), change the cross invocation in `build-target.sh` from the default target to DPF's DSP-only target: `make -s -C "$PLUGIN_DIR" lv2_dsp ...` (the native pass already produced the `.ttl`). Re-run this task. The default target mirrors the proven Dwarf script, so try it first.

- [ ] **Step 4: Stage (no file changes; records task completion)**

```bash
git status --short   # working tree already staged from Task 4
```

---

## Task 6: `prepare.sh` + `patchstorage-prepare` target

**Files:**
- Create: `patchstorage-build/prepare.sh`
- Modify: `Makefile` (add `patchstorage-prepare` target)

**Interfaces:**
- Consumes: built bundles under `build/patchstorage/<slug>/` (Tasks 4–5); `patchstorage.json` (Task 2); the vendored uploader at `patchstorage-build/uploader/` (Task 1).
- Produces: `build/ps-upload/` scratch tree with `dist/boreas.lv2/patchstorage.json`, per-target `*.tar.gz`, and `artwork.png`. Consumed by the push step (Task 7).

- [ ] **Step 1: Create the prepare script**

`patchstorage-build/prepare.sh`:
```bash
#!/bin/bash
# Assemble a disposable uploader working tree and run `prepare`. Runs on the HOST.
# Requires: python3 + requests click rdflib; jq; network (Patchstorage API).
#
# Reads env: $PLUGIN. Keeps the vendored uploader copy pristine by copying into a
# scratch dir and generating a plugins.json containing only this plugin.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
PLUGIN="${PLUGIN:?PLUGIN not set}"
UPLOADER="$ROOT/patchstorage-build/uploader"
SRC_BUNDLES="$ROOT/build/patchstorage"
SCRATCH="$ROOT/build/ps-upload"
META="$ROOT/patchstorage.json"

[ -f "$UPLOADER/uploader.py" ] || { echo "vendored uploader missing at $UPLOADER"; exit 1; }
[ -f "$META" ] || { echo "missing $META"; exit 1; }
[ -d "$SRC_BUNDLES" ] || { echo "no bundles — run 'make patchstorage-build' first"; exit 1; }

rm -rf "$SCRATCH"
mkdir -p "$SCRATCH/plugins"
cp "$UPLOADER/uploader.py" "$UPLOADER/bundles.py" \
   "$UPLOADER/licenses.json" "$UPLOADER/categories.json" "$SCRATCH/"

# plugins.json keyed by the bundle folder name ("<plugin>.lv2"), from our metadata.
jq '{("'"$PLUGIN"'.lv2"): .}' "$META" > "$SCRATCH/plugins.json"

# Stage each built bundle into plugins/<slug>/<plugin>.lv2
found=0
for slug_dir in "$SRC_BUNDLES"/*/; do
  slug="$(basename "$slug_dir")"
  if [ -d "$slug_dir/$PLUGIN.lv2" ]; then
    mkdir -p "$SCRATCH/plugins/$slug"
    cp -rL "$slug_dir/$PLUGIN.lv2" "$SCRATCH/plugins/$slug/"
    found=$((found + 1))
  fi
done
[ "$found" -gt 0 ] || { echo "no <plugin>.lv2 bundles found under $SRC_BUNDLES"; exit 1; }
echo "==> Staged $found target bundle(s) for $PLUGIN"

cd "$SCRATCH"
python3 uploader.py prepare all
echo "==> Prepared. Inspect: $SCRATCH/dist/$PLUGIN.lv2/patchstorage.json"
```

Then: `chmod +x patchstorage-build/prepare.sh`

- [ ] **Step 2: Add the `patchstorage-prepare` target to `Makefile`**

After the `patchstorage-build` target:
```makefile
# Assemble the uploader working tree and generate patchstorage.json + tarballs +
# artwork under build/ps-upload/dist/ for inspection. Assumes bundles are already
# built (run `make patchstorage-build` first). Hits the Patchstorage API.
patchstorage-prepare:
	PLUGIN=$(PLUGIN) bash patchstorage-build/prepare.sh

.PHONY: patchstorage-prepare
```

- [ ] **Step 3: Run prepare and inspect the generated metadata**

Run (requires network):
```bash
make patchstorage-prepare
cat build/ps-upload/dist/boreas.lv2/patchstorage.json
```
Expected: valid JSON containing `uids` (the Boreas LV2 URI), `title`, `content`, `categories`, `tags`, `revision` (matches `VERSION`), `license` (ISC's numeric id), `source_code_url` = the GitHub URL, an `artwork` path, and a `files` array with **3** entries (one per target).

- [ ] **Step 4: Assert the three targets and required fields are present**

Run:
```bash
jq -e '.files | length == 3' build/ps-upload/dist/boreas.lv2/patchstorage.json && echo "3 targets OK"
jq -e 'has("uids") and has("source_code_url") and has("artwork")' build/ps-upload/dist/boreas.lv2/patchstorage.json && echo "required fields OK"
test -f build/ps-upload/dist/boreas.lv2/artwork.png && echo "artwork OK"
```
Expected: `3 targets OK`, `required fields OK`, `artwork OK`.

- [ ] **Step 5: Stage**

```bash
git add patchstorage-build/prepare.sh Makefile
```

---

## Task 7: `make patchstorage` (build + prepare + push)

**Files:**
- Modify: `Makefile` (add `patchstorage` target)

**Interfaces:**
- Consumes: everything from Tasks 4–6.
- Produces: the `make patchstorage` target. Requires `PS_USER=<username>`; password entered at the uploader's interactive prompt.

- [ ] **Step 1: Add the `patchstorage` target to `Makefile`**

After `patchstorage-prepare` (note: `PS_USER` is checked in a FIRST prerequisite so a
bare `make patchstorage` fails fast instead of running the full build/prepare first;
`$(PYTHON)` is the overridable interpreter added in Task 6):
```makefile
# Full publish: build the three bundles, prepare, and push to patchstorage.com.
# Provide your Patchstorage username; the uploader prompts for the password
# (nothing is stored). Idempotent: skips/updates per the uploader's own logic.
#
#   make patchstorage PS_USER=<patchstorage-username>
patchstorage: patchstorage-check-user patchstorage-build patchstorage-prepare
	cd build/ps-upload && $(PYTHON) uploader.py push all --username "$(PS_USER)"

# Fail fast if PS_USER is unset — BEFORE the expensive build/prepare prerequisites
# run (a bare `make patchstorage` must not do a full 3-target Docker build only to
# then complain about a missing username).
patchstorage-check-user:
	@if [ -z "$(PS_USER)" ]; then \
		echo "error: set PS_USER=<patchstorage-username>"; \
		echo "       usage: make patchstorage PS_USER=yourname"; \
		exit 1; \
	fi

.PHONY: patchstorage patchstorage-check-user
```

- [ ] **Step 2: Verify the guard and target wiring without publishing**

Run:
```bash
make patchstorage 2>&1 | head -3        # no PS_USER -> must refuse
make -n patchstorage PS_USER=testuser | grep -q 'uploader.py push all --username' && echo "push wiring OK"
```
Expected: first prints the `error: set PS_USER=...` message; second prints `push wiring OK`.

- [ ] **Step 3: Real publish (deliberate, side-effecting — user-initiated)**

> This creates/updates a **public** listing on patchstorage.com. Run only when intending to publish. The uploader is idempotent (skips if owned by another user; updates your own when revision/targets/state changed).

Run:
```bash
make patchstorage PS_USER=<your-patchstorage-username>
# enter password at the prompt
```
Expected: `Published: https://patchstorage.com/... (ID:...)`. Open the URL and confirm title, description, license, categories, artwork, and that all three target files are attached.

- [ ] **Step 4: Stage**

```bash
git add Makefile
```

---

## Task 8: Release integration — attach the three bundles, replace `linux-x86_64`

**Files:**
- Modify: `Makefile` (`release-build` and `release` sections)

**Interfaces:**
- Consumes: `patchstorage-build` (Task 4) and the existing `dwarf-build`.
- Produces: `dist/` tarballs `boreas-v<version>-{linux-amd64,rpi-aarch64,patchbox-os-arm32,dwarf-aarch64}.tar.gz`, all attached to the GitHub release along with the manual.

- [ ] **Step 1: Replace the tarball name vars**

In `Makefile`, in the release section, replace:
```makefile
LINUX_TARBALL := $(PLUGIN)-v$(version)-linux-x86_64.tar.gz
DWARF_TARBALL := $(PLUGIN)-v$(version)-dwarf-aarch64.tar.gz
```
with:
```makefile
AMD64_TARBALL := $(PLUGIN)-v$(version)-linux-amd64.tar.gz
RPI_TARBALL   := $(PLUGIN)-v$(version)-rpi-aarch64.tar.gz
ARM32_TARBALL := $(PLUGIN)-v$(version)-patchbox-os-arm32.tar.gz
DWARF_TARBALL := $(PLUGIN)-v$(version)-dwarf-aarch64.tar.gz
```

- [ ] **Step 2: Rework `release-build` to build + package all four**

Replace the body of `release-build` (the `@echo "==> Building desktop bundle..."` through the final `@ls` line) with:
```makefile
	@echo "==> Building Patchstorage bundles (linux-amd64, rpi-aarch64, patchbox-os-arm32)"
	$(MAKE) patchstorage-build
	@mkdir -p $(DIST_DIR)
	tar -C $(PS_DIR)/linux-amd64       -czf $(DIST_DIR)/$(AMD64_TARBALL) $(PLUGIN).lv2
	tar -C $(PS_DIR)/rpi-aarch64       -czf $(DIST_DIR)/$(RPI_TARBALL)   $(PLUGIN).lv2
	tar -C $(PS_DIR)/patchbox-os-arm32 -czf $(DIST_DIR)/$(ARM32_TARBALL) $(PLUGIN).lv2
	@echo "==> Building Dwarf bundle (aarch64)"
	$(MAKE) dwarf-build
	tar -C build/dwarf -czf $(DIST_DIR)/$(DWARF_TARBALL) $(PLUGIN).lv2
	@echo
	@echo "Built release artefacts in $(DIST_DIR)/:"
	@ls -lh $(DIST_DIR)/$(PLUGIN)-v$(version)-*.tar.gz
```

- [ ] **Step 3: Update the `gh release create` asset list in `release`**

In the `release` target, replace the `gh release create` invocation's asset lines so it attaches all four tarballs plus the manual:
```makefile
	gh release create "v$(version)" \
		"$(DIST_DIR)/$(AMD64_TARBALL)" \
		"$(DIST_DIR)/$(RPI_TARBALL)" \
		"$(DIST_DIR)/$(ARM32_TARBALL)" \
		"$(DIST_DIR)/$(DWARF_TARBALL)" \
		"$(MANUAL_PDF)" \
		--title "v$(version)" \
		--generate-notes
```
Also update the `@echo "==> Creating GitHub release..."` comment line to say "with all bundles + manual attached".

- [ ] **Step 4: Verify packaging locally (no release cut)**

Run (rebuilds all bundles incl. Dwarf; heavy — needs the Dwarf image):
```bash
make release-build version=$(cat VERSION)
ls dist/boreas-v$(cat VERSION)-{linux-amd64,rpi-aarch64,patchbox-os-arm32,dwarf-aarch64}.tar.gz
```
Expected: all four tarballs listed; no `linux-x86_64` tarball produced.

- [ ] **Step 5: Verify the release command references all four assets + manual**

Run:
```bash
make -n release version=$(cat VERSION) 2>/dev/null | grep 'gh release create' -A6
```
Expected: the printed `gh release create` includes `linux-amd64`, `rpi-aarch64`, `patchbox-os-arm32`, `dwarf-aarch64`, and the manual PDF — and does NOT include `linux-x86_64`.

- [ ] **Step 6: Stage**

```bash
git add Makefile
```

---

## Task 9: Documentation

**Files:**
- Create: `patchstorage-build/README.md`
- Modify: `INSTRUCTIONS.md`, `README.md`

**Interfaces:** none (docs).

- [ ] **Step 1: Write `patchstorage-build/README.md`**

```markdown
# patchstorage-build

Cross-builds this plugin for the three targets patchstorage.com's LV2-plugins
platform supports and publishes it, reusing Patchstorage's own prebuilt
toolchain images (`patchstorage/lv2_builder-<platform>:latest`).

| Target slug | Builder image platform | Arch / ABI | glibc |
|---|---|---|---|
| `linux-amd64` | `x86_64` | x86-64, SSE2 | 2.27 |
| `rpi-aarch64` | `raspberrypi4_aarch64` | AArch64 | 2.27 |
| `patchbox-os-arm32` | `raspberrypi3_armv8` | 32-bit armhf + NEON | 2.31 |

- `build-target.sh` runs *inside* an image: two-phase build (native pass for the
  `.ttl` + modgui, then cross-compile the `.so`), same pattern as the Dwarf build.
- `prepare.sh` runs on the host: assembles a disposable uploader tree from the
  vendored `uploader/` copy, generates `plugins.json` from the repo's
  `patchstorage.json`, stages the built bundles, and runs the uploader's `prepare`.

## Prerequisites
- Docker
- Python 3 with `pip install requests click rdflib`, and `jq`
- A modgui **screenshot** in the bundle (required to publish)

## Usage
- `make patchstorage-build` — build all three bundles into `build/patchstorage/`
- `make patchstorage-prepare` — assemble + prepare; inspect `build/ps-upload/dist/`
- `make patchstorage PS_USER=<username>` — build + prepare + publish
```

- [ ] **Step 2: Add a "Publishing to Patchstorage" section to `INSTRUCTIONS.md`**

Add a section documenting: the three targets and their ABI/glibc; the prerequisites (`pip install requests click rdflib`, `jq`, Docker — no submodule init needed, the uploader is vendored in `patchstorage-build/uploader/`); the mandatory modgui screenshot; the per-plugin `patchstorage.json` fields (`source_code_url`, `donate_url`); the three make targets; that credentials are entered interactively and never stored; and that the three bundles are also attached to GitHub releases (with `linux-amd64` replacing the old `linux-x86_64`).

- [ ] **Step 3: Add a short Patchstorage note to `README.md`**

Add a brief bullet list under the build/release docs pointing to the three `make patchstorage*` targets and `patchstorage-build/README.md`.

- [ ] **Step 4: Verify docs mention the key facts**

Run:
```bash
grep -q 'patchstorage' README.md && grep -q 'source_code_url' INSTRUCTIONS.md \
  && grep -q 'screenshot' INSTRUCTIONS.md && echo "docs OK"
```
Expected: `docs OK`.

- [ ] **Step 5: Stage**

```bash
git add patchstorage-build/README.md INSTRUCTIONS.md README.md
```

---

## Task 10: Port the generic mechanism to `mod-plugin-template`

Apply the same changes to the template (path `/home/sdoerr/Fun/mod-plugin-template`), with placeholder metadata and the template's plugin identity (`PLUGIN=myplugin`, dir `plugins/MyPlugin`, bundle `myplugin.lv2`).

**Files (all under `/home/sdoerr/Fun/mod-plugin-template`):**
- Create: `patchstorage-build/build-target.sh` (identical to boreas), `patchstorage-build/README.md`
- Create: `patchstorage-build/prepare.sh` (identical to boreas)
- Create: `patchstorage-build/uploader/` (identical vendored copy — 4 files + LICENSE + PROVENANCE)
- Create: `patchstorage.json` (placeholders)
- Modify: `plugins/MyPlugin/Makefile` (`EXTRA_*` passthrough), `Makefile` (targets + release), `INSTRUCTIONS.md`, `README.md`

**Interfaces:**
- Produces: the template gains all `make patchstorage*` targets, parameterized by the existing `PLUGIN`/`PLUGIN_DIR` identity vars, so forks inherit them.

- [ ] **Step 1: Copy the generic files verbatim (incl. the vendored uploader)**

```bash
T=/home/sdoerr/Fun/mod-plugin-template
mkdir -p "$T/patchstorage-build"
cp patchstorage-build/build-target.sh patchstorage-build/prepare.sh patchstorage-build/README.md "$T/patchstorage-build/"
cp -r patchstorage-build/uploader "$T/patchstorage-build/"
chmod +x "$T/patchstorage-build/build-target.sh" "$T/patchstorage-build/prepare.sh"
```
(`build-target.sh`/`prepare.sh` are plugin-agnostic — they read `$PLUGIN` and auto-detect the plugin dir. The vendored `uploader/` is identical across repos.)

- [ ] **Step 2: (removed — no submodule; the uploader is copied in Step 1)**

- [ ] **Step 3: Add placeholder `patchstorage.json`**

`/home/sdoerr/Fun/mod-plugin-template/patchstorage.json`:
```json
{
    "source_code_url": "https://github.com/youruser/yourplugin",
    "donate_url": null
}
```

- [ ] **Step 4: Add the `EXTRA_*` passthrough to `plugins/MyPlugin/Makefile`**

Add the same two lines as Task 3, after the template's version-defines `CXXFLAGS +=` block:
```makefile
CFLAGS   += $(EXTRA_CFLAGS)
CXXFLAGS += $(EXTRA_CXXFLAGS)
```

- [ ] **Step 5: Add the Make targets and release changes**

Apply the same additions as Tasks 4, 6, 7, 8 to the template's `Makefile` (the snippets use `$(PLUGIN)`/`$(PS_DIR)` and are identity-agnostic — paste them verbatim). Update the template's `INSTRUCTIONS.md`/`README.md` as in Task 9.

- [ ] **Step 6: Verify the template builds one target for its placeholder plugin**

Run:
```bash
cd /home/sdoerr/Fun/mod-plugin-template
make patchstorage-build PS_TARGETS=linux-amd64
file build/patchstorage/linux-amd64/myplugin.lv2/myplugin.so | grep -q 'x86-64' && echo "template build OK"
```
Expected: `template build OK`.
(Full publish needs a real screenshot + metadata; not exercised for the placeholder.)

- [ ] **Step 7: Stage in the template repo**

```bash
git -C /home/sdoerr/Fun/mod-plugin-template add \
  patchstorage-build patchstorage.json \
  plugins/MyPlugin/Makefile Makefile INSTRUCTIONS.md README.md
```

---

## Self-Review

**Spec coverage:**
- Local `make patchstorage`, separate from `make dwarf` → Tasks 4–7 (targets), Global Constraints (dwarf untouched). ✓
- Three targets via Patchstorage prebuilt images, hybrid build → Tasks 4–5. ✓
- CPU flags match defconfig; `:latest` images → Global Constraints, Task 4 table. ✓
- Toolchain auto-discovery + step-1 verification → Task 4 Step 3. ✓
- Uploader vendored by copy (not submodule); scratch-tree assembly; generated `plugins.json` → Tasks 1, 6. ✓
- Per-plugin `source_code_url`/`donate_url` → Task 2. ✓
- Credentials at run time, nothing stored → Task 7. ✓
- Modgui screenshot requirement → Global Constraints, Task 9. ✓
- Release integration: attach three bundles, replace `linux-x86_64` with `linux-amd64`, keep Dwarf + manual → Task 8. ✓
- Native `make all` unchanged for dev/install → Global Constraints. ✓
- Template-then/first rollout → boreas-first (Tasks 1–9) then template (Task 10); deviation from the spec's stated order, chosen for testability (the template's placeholder plugin can't be published end-to-end). Same end state. ✓
- Docs → Task 9 (boreas), Task 10 (template). ✓

**Placeholder scan:** No TBD/TODO; every code/script/Makefile step contains full content; verification commands have expected output. ✓

**Type/name consistency:** `PS_TARGETS`, `PS_DIR`, `build/patchstorage/<slug>/`, `build/ps-upload/`, tuples, and slugs are used identically across Tasks 4–8. `EXTRA_CFLAGS`/`EXTRA_CXXFLAGS` defined in Task 3, consumed in Task 4. `patchstorage.json` shape defined in Task 2, consumed in Task 6. ✓

**Fallbacks documented** for the two real risks: toolchain path / `--user root` (Task 4 Step 3) and cross-build TTL generation (Task 5 Step 3).
