# Patchstorage publishing for MOD/DPF LV2 plugins — design

**Date:** 2026-07-20
**Status:** Approved (design); pending implementation plan
**Applies to:** `mod-plugin-template` (primary) and `boreas` (first consumer)

## Summary

Add a self-contained, local `make patchstorage` target that builds a plugin for
the three targets Patchstorage's "LV2 plugins" platform supports
(`linux-amd64`, `rpi-aarch64`, `patchbox-os-arm32`) and publishes it to
[patchstorage.com](https://patchstorage.com/platform/lv2-plugins/) using the
existing `patchstorage-lv2-uploader`. The same three bundles are also attached to
the GitHub release. The build reuses Patchstorage's own prebuilt cross-toolchain
Docker images, driven by our proven two-phase DPF build pattern (the one already
used for the Dwarf).

This is a developer-run, local flow. There is **no CI and no stored credentials**;
Patchstorage credentials are entered at run time.

## Goals

- One command (`make patchstorage`) to publish the current plugin to Patchstorage
  for all three platform targets.
- Byte-for-byte ABI/glibc compatibility with what Patchstorage expects, by using
  Patchstorage's own toolchains (no hand-rolled cross-compilers).
- Attach the same three bundles to the GitHub release so users can download them
  directly.
- Keep everything self-contained and in the template's established style
  (vendored deps, `make` targets, developer-driven), so every forked plugin
  inherits the capability.
- No stored secrets.

## Non-goals

- Publishing the **Dwarf** build to Patchstorage. MOD devices pull plugins from
  MOD's own store, not Patchstorage. The existing `make dwarf` / `make dwarf-deploy`
  device-deploy flow is unrelated and stays exactly as-is.
- CI / GitHub Actions automation. Explicitly a local target (may be revisited
  later; nothing here precludes wrapping it in CI).
- Supporting plugins with heavy native dependencies (fftw, cairo, libsndfile,
  etc.). The chosen build path assumes **DSP-only DPF plugins** (see Risks).

## Background / key facts

Established during research (sources: `patchstorage/patchstorage-lv2-builder`,
`patchstorage/patchstorage-lv2-uploader`, `mod-audio/mod-plugin-builder`, DPF):

### The three Patchstorage targets

| Patchstorage target | Builder platform | Arch / ABI | Toolchain tuple | glibc | GCC |
|---|---|---|---|---|---|
| `linux-amd64` | `x86_64` | x86-64, SSE2 baseline | `x86_64-mod-linux-gnu` | 2.27 | Linaro 7.5 |
| `rpi-aarch64` | `raspberrypi4_aarch64` | AArch64 (ARMv8 64-bit) | `aarch64-rpi4-linux-gnu` | 2.27 | Linaro 7.5 |
| `patchbox-os-arm32` | `raspberrypi3_armv8` | **32-bit ARM hard-float + NEON** | `armv8-rpi3-linux-gnueabihf` | 2.31 | 10.3.0 |

Gotchas: `raspberrypi3_armv8` is a **32-bit armhf** build despite the name; and a
modern (bookworm) cross-compiler is unsafe (glibc too new — binaries won't load on
the target OSes). Using Patchstorage's toolchains sidesteps both.

The target-slug mapping is authoritative from the uploader's `TARGETS_MAP`.

### Patchstorage's prebuilt images expose a directly-callable toolchain

`patchstorage/lv2_builder-<platform>:latest` images (Docker Hub) contain a
standalone crosstool-ng toolchain at a fixed path:

```
/home/builder/lv2-workdir/<platform>/toolchain/bin/<tuple>-gcc   (and -g++)
sysroot: /home/builder/lv2-workdir/<platform>/toolchain/<tuple>/sysroot
```

The stock `./build` entrypoint is strictly Buildroot-recipe-driven (needs
`plugins/package/<name>/<name>.mk`), but the toolchain can be invoked directly on a
bind-mounted source tree, bypassing Buildroot entirely. For DSP-only DPF plugins
this is sufficient: DPF vendors its own LV2 headers, and the toolchain's own
sysroot carries libc/libstdc++/libm.

> Caveat: the toolchain path above is inferred from the images' Dockerfile +
> scripts, **not yet confirmed by running a pulled image**. Implementation step 1
> verifies it; the build script auto-discovers the toolchain and fails loudly if
> the layout differs. Fallback: the Buildroot-recipe path (same images, more
> boilerplate).

### The uploader

`patchstorage-lv2-uploader` (`uploader.py` + `bundles.py`) reads an LV2 bundle's
`manifest.ttl` + modgui screenshot and auto-derives almost all Patchstorage
metadata: title, categories, tags, version/revision, license, description,
stability, artwork. It requires:

- Input layout `plugins/<target-slug>/<plugin>.lv2/` (target slugs fetched live
  from the Patchstorage API).
- A **modgui screenshot** (`modgui:screenshot`) — hard error without one.
  (Boreas ships `screenshot-boreas.png` ✓.)
- Two mandatory per-plugin override fields it cannot derive from TTL:
  `source_code_url` and `donate_url` (the latter may be `null`). Read from its
  `plugins.json`, keyed by bundle folder name (e.g. `boreas.lv2`).
- A license mapping (`licenses.json`) — Boreas's `getLicense()` returns `ISC`,
  which maps to a known Patchstorage license id, so no override needed.
- `push` authenticates with a Patchstorage username + password to mint an API
  token, and is idempotent (skips if a plugin with matching UIDs was uploaded by
  another user; updates own uploads when revision/targets/state differ).

## Architecture

Three stages, with a shared build step feeding both the Patchstorage upload and
the GitHub release.

```
                    patchstorage-build  (new; 3 targets)
                   /                    \
     make patchstorage                   make release
   (arrange + prepare + push)          (+ dwarf-build, package, tag,
        -> patchstorage.com              gh release attach)
                                          -> GitHub release assets
```

### Stage 1 — build (shared)

New `patchstorage-build/` directory (parallel to `mod-build/`), containing
`build-target.sh` — a platform-parameterized generalization of
`mod-build/build-plugin.sh`. No Dockerfile of our own: we `docker pull`
Patchstorage's prebuilt images.

A Makefile platform table drives it. Each row: builder-platform, target-slug,
toolchain tuple, and CPU/opt flags (matched exactly to Patchstorage's defconfigs):

| builder-platform | target-slug | tuple | opt flags |
|---|---|---|---|
| `x86_64` | `linux-amd64` | `x86_64-mod-linux-gnu` | `-msse -msse2 -mfpmath=sse` |
| `raspberrypi4_aarch64` | `rpi-aarch64` | `aarch64-rpi4-linux-gnu` | `-mcpu=cortex-a72` |
| `raspberrypi3_armv8` | `patchbox-os-arm32` | `armv8-rpi3-linux-gnueabihf` | `-mcpu=cortex-a53 -mfpu=neon-fp-armv8 -mfloat-abi=hard` |

`make patchstorage-build` loops the three, `docker run`-ing
`patchstorage/lv2_builder-<platform>` with the source mounted read-only and an
output dir mounted, invoking `build-target.sh`. Output:
`build/patchstorage/<target-slug>/<plugin>.lv2/`.

`build-target.sh` reuses the proven two-phase pattern:

1. rsync source into a writable scratch dir (exclude `bin`, `build`, `.git`).
2. **Native pass**: `make all` with the image's host x86_64 compiler — produces
   the `.ttl` (DPF's `lv2_ttl_generator` must `dlopen` a native `.so`) and the
   modgui bundle layout. Stash the populated `.lv2`. (Assumes each image provides a
   host x86_64 `g++`; the images are Debian x86_64 with build tooling, but this is
   confirmed in implementation step 1. The `.ttl` is architecture-independent, so
   the same native pass is valid for every target.)
3. **Target pass**: auto-discover the cross toolchain (glob
   `/home/builder/lv2-workdir/*/toolchain/bin/*-gcc`; assert exactly the expected
   tuple is present), then rebuild the `.so` with that `CC/CXX` + the platform's
   opt flags; strip with the toolchain's `strip`.
4. Assert the resulting `.so` is the expected architecture (`file` check).
5. Overlay the target `.so` onto the stashed bundle; copy to `/out`; chown back to
   the host user.

For `linux-amd64` the "target" toolchain is same-arch (x86_64) but still
Patchstorage's glibc-2.27 toolchain — so the shipped `.so` is the portable build,
while the throwaway native pass just generates the TTL.

### Stage 2 — arrange + prepare + push (upload only)

The uploader is **vendored by copying** the four files we actually use
(`uploader.py`, `bundles.py`, `licenses.json`, `categories.json` — ~56K total)
into `patchstorage-build/uploader/`, alongside the upstream GPL-3.0 `LICENSE` and a
one-line provenance note (source URL + commit SHA). We deliberately do **not** use
a git submodule: the whole upstream repo is 436K (mostly `.git` history and the
60K community `plugins.json` we never use), and a submodule would add a
`git submodule update --init` step to every fork — the opposite of the template's
self-contained goal. The uploader is a stable proof-of-concept (recent upstream
commits only touch `plugins.json`), so there is little to track. Copying a GPL tool
in is fine as mere aggregation — it is a separate publish script, not linked into
the ISC plugin — provided the GPL `LICENSE` and attribution travel with it.

At run time `make patchstorage` assembles a scratch working tree at
`build/ps-upload/` so the vendored copy stays pristine and we control inputs:

- Copy the vendored uploader code + its generic `licenses.json` / `categories.json`.
- Write a generated `plugins.json` containing **only this plugin's** entry, built
  from the repo's committed `patchstorage.json` (see Stage 3), keyed by the bundle
  folder name (`<plugin>.lv2`).
- Copy the three built bundles into `plugins/<target-slug>/<plugin>.lv2/`.
- Run `python uploader.py prepare all` — generates `dist/<plugin>/patchstorage.json`
  + per-target tarballs + `artwork.png` (from the modgui screenshot).
- Run `python uploader.py push all --username <user>` — authenticates and
  publishes/updates.

### Stage 3 — per-plugin metadata

The template ships a committed `patchstorage.json` at the repo root with the fields
the TTL cannot provide:

```json
{
  "source_code_url": "https://github.com/<owner>/<plugin>",
  "donate_url": null
}
```

Optional keys the uploader also honors (usually unnecessary): `categories`,
`tags`, `license`. Template = placeholders; Boreas =
`source_code_url: https://github.com/stefdoerr/boreas`, `donate_url: null`.

### Stage 4 — release integration

`make release` gains the shared `patchstorage-build` step and attaches the three
bundles. Final GitHub release asset set:

- `<plugin>-vX-linux-amd64.tar.gz` — **replaces** the old `linux-x86_64` asset
  (same arch, but the portable glibc-2.27 build).
- `<plugin>-vX-rpi-aarch64.tar.gz`
- `<plugin>-vX-patchbox-os-arm32.tar.gz`
- `<plugin>-vX-dwarf-aarch64.tar.gz` — kept, for Dwarf/MOD users downloading
  directly.
- `manual.pdf`

The native `make all` build is unchanged and still used for local dev and
`make install`; only the *release asset* for x86_64 changes to the portable build.

## Make targets (final surface)

| Target | Action |
|---|---|
| `make patchstorage-build` | Build all three bundles into `build/patchstorage/<slug>/`. |
| `make patchstorage-prepare` | `patchstorage-build` + arrange + `prepare` (inspect `dist/` before publishing). |
| `make patchstorage` | `patchstorage-build` + prepare + `push` to patchstorage.com. |
| `make release` | `patchstorage-build` + `dwarf-build` + package + tag/push + `gh release` with all assets. |
| `make dwarf` / `make dwarf-deploy` | **Unchanged.** |
| `make install` / `make all` / `make beta` | **Unchanged.** |

`make patchstorage` builds fresh from current source + `VERSION` (guarantees the
uploaded binaries match the tagged commit); it does not download release assets.

## Credentials

- `--username` supplied on the command line (or via a `PATCHSTORAGE_USER` env/Make
  variable).
- Password via the uploader's interactive prompt, or `PATCHSTORAGE_PASS` env for
  non-interactive runs.
- Nothing is written to disk or committed.

## Dependencies added

- `patchstorage-lv2-uploader` vendored by copy into `patchstorage-build/uploader/`
  (four files ~56K + GPL `LICENSE` + provenance note). No submodule.
- Python 3 + `requests click rdflib` (documented; one-time `pip install`).
- Docker (already required for the Dwarf build).
- Network at run time (Docker Hub image pulls; Patchstorage API for `prepare`/`push`).

## Error handling

- **Toolchain not found in image** → `build-target.sh` asserts the expected
  tuple's `-gcc` exists after globbing; hard error with the discovered layout, so a
  Patchstorage image change is caught immediately (not silently mis-built).
- **Wrong architecture** → post-compile `file` check on the `.so` per target.
- **Missing modgui screenshot** → surfaces as the uploader's existing
  `PluginFieldMissing`; documented as a hard requirement to publish.
- **Missing `source_code_url` / `donate_url`** → uploader's existing
  `BundleBadContents`; the committed `patchstorage.json` prevents this.
- **Already uploaded by another user** → uploader skips (existing behavior).
- **Auth failure / network error** → uploader's existing handling; no partial
  state on our side (scratch tree is disposable).

## Testing / verification

- **Implementation step 1**: pull one image, confirm the toolchain path, and build
  one target end-to-end before wiring the rest.
- Per-target `file` assertion on every built `.so`.
- Optional QEMU (`qemu-user-static`, already used in the mod-build image) load
  check for the ARM `.so`s.
- `make patchstorage-prepare` lets a human inspect the generated
  `dist/<plugin>/patchstorage.json` + artwork before any upload.
- First real upload done against a single plugin (Boreas) and verified on
  patchstorage.com before relying on the flow.

## Risks & mitigations

| Risk | Mitigation |
|---|---|
| Toolchain path inferred, not run-verified | Verify in impl step 1; auto-discover + assert in the build script; documented fallback to the Buildroot-recipe path. |
| Patchstorage restructures their images | Auto-discovery fails loudly; images tracked via `:latest` (a fresh `docker pull` gets the current build), so a breaking change surfaces on the next pull rather than silently. |
| Plugin needs native deps beyond libc/libstdc++/libm | Out of scope for now; such plugins would use the Buildroot-recipe path (their `staging` tree). Documented as a non-goal. |
| Docker Hub image size / first-pull time | Acceptable for a release-time, local operation; images are cached after first pull. |
| Uploader is third-party PoC and may drift | Vendored by copy at a recorded commit SHA; we control when to re-sync. |

## Rollout

1. Implement in `mod-plugin-template` (placeholders): `patchstorage-build/`,
   Makefile targets, vendored uploader copy, `patchstorage.json` example, docs in
   `INSTRUCTIONS.md` / `README.md`.
2. Verify end-to-end on `boreas` with real metadata; publish once and confirm on
   patchstorage.com.
3. Keep `mod-plugin-template` and `boreas` in sync as with prior template work.

## Decisions

- Docker images are referenced by `:latest` (not digest-pinned) — a fresh
  `docker pull` always gets Patchstorage's current toolchain build.

## Open questions

None blocking. Deferred: whether to later add a CI wrapper.
