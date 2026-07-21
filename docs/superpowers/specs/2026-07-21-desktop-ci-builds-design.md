# Desktop VST3/CLAP via GitHub Actions CI — design

**Date:** 2026-07-21
**Status:** Approved (design); pending implementation plan
**Applies to:** `mod-plugin-template` (primary) + `boreas` + `sitar`

## Summary

Move the **desktop** plugin builds (VST3 + CLAP, for Linux + macOS + Windows) off the
local machine and into **GitHub Actions**, using DISTRHO's official
`distrho/dpf-makefile-action`. Keep the **MOD Dwarf** and **Patchstorage** builds
**local** (they need the vendored/prebuilt Docker toolchains, which the action can't
produce). One `make release` still creates the GitHub release with the local
MOD/Patchstorage artifacts; the tag push triggers CI, which appends the desktop
builds to that same release.

## Motivation

- Our plugins are DSP-only (`DISTRHO_PLUGIN_HAS_UI 0`) — in a DAW they get the host's
  generic parameter UI, so no custom-UI code is needed (LVGL was evaluated and
  declined). We just want the desktop **VST3/CLAP binaries** on releases.
- macOS binaries can't be built locally (no Mac); Windows cross-compiles but CI is
  cleaner; Linux desktop builds are trivial in CI. `dpf-makefile-action` handles all
  three with ~15 lines of YAML and **no custom Docker images**.
- MOD Dwarf and Patchstorage need their specific toolchains (moddwarf ABI; Patchstorage
  glibc-2.27 platform images) and their own distribution (Patchstorage uploader) — so
  they stay local, unchanged.

## Goals

- Desktop **VST3 + CLAP (+ LV2)** for `linux-x86_64`, `win64`, `macos-universal`
  built in CI and attached to the GitHub release on tag `v*`.
- Local dev/test workflow **unchanged**.
- Local `make release` keeps producing the MOD/Patchstorage artifacts; CI adds the
  desktop ones to the same release.
- Applied consistently to template + boreas + sitar.

## Non-goals

- **macOS code-signing / notarization** — ship **unsigned** (users right-click→Open
  past Gatekeeper). Not now, not planned.
- **win32** (32-bit Windows) — win64 only.
- Building MOD Dwarf or Patchstorage targets in CI — they stay local (Docker toolchains).
- Custom UI (LVGL/ImGui/webview) — declined; host generic UI is sufficient.

## Decisions (locked)

- Linux desktop CI uses **DISTRHO's convention**: `ubuntu-latest` runner **with a
  `container: ubuntu:20.04`** → glibc **2.31** floor (runs on Ubuntu 20.04+/Debian 11+).
  A small standard image, distinct from our custom toolchain Docker.
- **win64 only**, cross-compiled on `ubuntu-24.04`.
- **macOS unsigned, permanently.** `macos-universal` on `macos-14`.
- Trigger: **tag `v*`** (+ manual `workflow_dispatch`).

## Architecture — division of labor

| Artifact | Built by | Where |
|---|---|---|
| MOD Dwarf LV2 | `make dwarf` (mod-build Docker) | local |
| Patchstorage LV2 (linux-amd64 / rpi-aarch64 / patchbox-os-arm32) | `make patchstorage` (Patchstorage Docker) | local → uploader |
| Desktop VST3/CLAP (+LV2) — linux-x86_64, win64, macos-universal | `distrho/dpf-makefile-action` | **GitHub CI** |
| Local dev: modgui in MOD Desktop | `make install` | local |
| Local dev: VST3/CLAP in a DAW | `make` → copy to `~/.vst3`/`~/.clap` | local |

## Components

### 1. CI workflow — `.github/workflows/desktop-release.yml` (new, each repo)

```yaml
name: desktop-release
on:
  push:
    tags: ['v*']
  workflow_dispatch: {}
jobs:
  build:
    strategy:
      fail-fast: false
      matrix:
        include:
          - { target: linux-x86_64,    os: ubuntu-latest, container: 'ubuntu:20.04' }
          - { target: win64,           os: ubuntu-24.04 }
          - { target: macos-universal, os: macos-14 }
    runs-on: ${{ matrix.os }}
    container: ${{ matrix.container }}
    steps:
      - uses: actions/checkout@v4
        with:
          submodules: recursive        # dpf is a submodule; required
      - uses: distrho/dpf-makefile-action@v1
        with:
          target: ${{ matrix.target }}
          release: true                # upload built archives to the tag's release
```

- Builds the plugin's declared `TARGETS` (lv2 vst3 clap) for each platform and, with
  `release: true`, attaches the archives to the release for the pushed tag.
- `submodules: recursive` is required (our repos carry `dpf` as a submodule).
- `pawpaw` for win64: **start with DISTRHO's `pawpaw: true`** (self-contained
  Windows binaries), but drop it if the verification run shows a plain `win64` already
  produces a self-contained `.vst3`/`.clap` for our dependency-free plugins.

### 2. Local `make release` (each repo) — trimmed

- **Remove**: the `desktop-build` invocation and the `VST3_TARBALL` / `CLAP_TARBALL`
  build + `gh release create` attach lines.
- **Keep**: build + attach the 3 Patchstorage LV2 tarballs (`linux-amd64`,
  `rpi-aarch64`, `patchbox-os-arm32`) + `dwarf-aarch64` + `manual.pdf`; still runs
  `gh release create` to create the release with those local artifacts.

### 3. Removed targets/files (each repo)

- Delete the `desktop-build` Makefile target + `DESKTOP_DIR` var.
- Delete `patchstorage-build/build-desktop.sh` (its only role was local desktop
  release builds → CI's job now).

### 4. Unchanged

- Plain `make` still builds `lv2 vst3 clap` (inner `TARGETS`) with the host toolchain
  — the local dev/test path (load `bin/<plugin>.{vst3,clap}` in a DAW).
- `make install`, `make dwarf`, `make dwarf-deploy`, `make patchstorage*` — untouched.
- `patchstorage-build/build-target.sh` (the lv2 cross-build) — untouched.

## Data flow — one release, two producers

1. `make release version=x.y.z` (local): bump VERSION, commit, `git push`, tag,
   `git push origin v$(version)` (**triggers CI**), then `gh release create v$(version)`
   with the local MOD/Patchstorage artifacts + manual.
2. CI (on the tag): builds desktop VST3/CLAP for the three platforms and **appends**
   them to the release.

**Coordination / race:** the tag push (step 1) fires CI before `gh release create` runs.
The implementation must ensure CI's upload targets an existing release without racing
the local create. Approach: CI's upload step **creates-or-updates** the release (the
action / `softprops/action-gh-release` upsert semantics), and/or a short
"wait-for-release" guard. Nail this in the plan; verify on a throwaway tag.

## Error handling / edge cases

- `fail-fast: false` so one platform's failure doesn't cancel the others.
- Missing `dpf` submodule → build fails; `submodules: recursive` prevents it.
- Windows cross-compile TTL generation runs the target binary under `wine` — the action
  installs it; verify the LV2 ttl step succeeds under wine for our Makefile.
- Our **customized** top-level Makefile (modgui/manual/ttl/patchstorage) is what the
  action drives via `make`. Risk: the modgui/manual steps or ttl-for-MOD interfere with
  a desktop CI build. Mitigation: verify on a throwaway tag; if needed, add a
  CI-friendly make path that builds only the desktop formats.

## Testing / verification

- **Throwaway-tag CI run** on one repo (boreas) first: confirm the workflow builds
  linux-x86_64 + win64 + macos-universal and attaches `.vst3`/`.clap` to the release.
- Confirm the produced Linux binary's glibc floor (~2.31) via `objdump -T`.
- Confirm `make release` (dry-run `make -n`) no longer references desktop-build/vst3/clap
  and still attaches Patchstorage LV2 + dwarf + manual.
- Confirm plain `make` still builds `bin/<plugin>.{lv2,vst3,clap}` locally.
- Delete the throwaway tag/release after verification.

## Rollout

1. Implement + verify on **boreas** (throwaway tag) first — it's the real, releasable
   plugin and exercises the full path.
2. Propagate the workflow + Makefile trim to **mod-plugin-template** and **sitar**
   (identical workflow; identity-agnostic Makefile edits).
3. Keep the three repos in sync as with prior work.

## Risks & mitigations

| Risk | Mitigation |
|---|---|
| Action doesn't mesh with our custom Makefile | Throwaway-tag test first; add a CI-only make path if needed |
| CI ↔ local `gh release create` race | Upsert semantics + wait-for-release guard; verify on the test tag |
| Linux CI glibc floor rises vs local (2.31 vs 2.15) | Accepted (DISTRHO's convention; 2.31 covers modern distros) |
| `pawpaw: true` build-time overhead for dep-free plugins | Drop pawpaw if plain win64 is already self-contained (verify) |
| Unsigned macOS Gatekeeper friction | Accepted (documented: right-click→Open); notarization out of scope |

## Docs

- Template `INSTRUCTIONS.md`: a "Desktop builds (CI)" section — desktop VST3/CLAP are
  built by GitHub Actions on tag; MOD Dwarf + Patchstorage stay local; local dev uses
  plain `make` / `make install`; macOS is unsigned (right-click→Open).
- Note the release model (local `make release` + CI appends desktop).

## Open questions

None blocking.

## Implementation notes (as-built — boreas, 2026-07-21)

Verified on boreas (CI run `29835053785`, all three platforms green). Two deltas
from the design above emerged during implementation:

1. **Desktop CI builds VST3 + CLAP only — LV2 dropped.** The inner DPF build
   emits only the plugin `.so`; a valid desktop LV2 bundle needs the top-level
   `ttl` step (`generate-ttl.sh` + a MOD-specific `sed -i` patch), which is
   MOD-oriented and uses GNU-only `sed -i` that breaks on the macOS runner. The
   desktop-Linux LV2 is already shipped as the Patchstorage `linux-amd64` release
   asset, so a CI desktop LV2 would be redundant. This matches the original
   intent ("just the VST3 and CLAP builds on releases").

2. **The action drives the build via `make features` then `make`.** Our
   top-level Makefile isn't a DPF Makefile, so:
   - Added a top-level `features:` target that delegates to the inner plugin
     Makefile (which includes `dpf/Makefile.base.mk`, where `features` lives).
   - Added a `DESKTOP_ONLY=1` switch (passed through the action's `extraargs`):
     it sets `PLUGIN_FORMATS := vst3 clap` and makes `all: plugin` (skipping the
     MOD-specific `ttl`/`modgui`). Local `make` (DESKTOP_ONLY unset) is unchanged.
   The workflow stays identical across repos (`extraargs: DESKTOP_ONLY=1`); the
   plugin-dir specificity lives in each repo's top-level Makefile (`PLUGIN_DIR`).

3. **`ubuntu:20.04` container needs git installed before checkout.** The base
   image has no git, so `actions/checkout` fell back to a git-less REST download
   that can't fetch the `dpf` submodule. A guarded pre-checkout step
   (`if: matrix.container != ''`) installs `git ca-certificates`.

4. **pawpaw confirmed unnecessary.** The win64 cross-build is self-contained —
   `boreas.clap` imports only system DLLs (KERNEL32/msvcrt/SHELL32); DPF
   statically links the MinGW runtime. No `pawpaw` input needed.

5. **macOS artifact is a `.pkg` installer** (DPF's `package-osx-installer`),
   containing the VST3 + CLAP; Linux/Windows ship the bare bundles.

### Propagation status

- **boreas**: done + verified (commit `9360430`).
- **mod-plugin-template**: pending — has no workflow yet; add `desktop-release.yml`
  + the `features`/`DESKTOP_ONLY` Makefile hooks + trim + docs.
- **sitar**: pending — **already has an older `release.yml`** that builds a
  linux-x86_64 LV2 *in CI* via plain `make` and creates the release itself. Under
  this design that LV2 is produced *locally* by `make release` (Patchstorage
  `linux-amd64`), so the old workflow should be **replaced** by
  `desktop-release.yml`. Confirm before replacing (outward-facing release change).
