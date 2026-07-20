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
- `jq`
- Python 3 with `requests`, `click`, `rdflib`. A dedicated env is cleanest — it
  avoids polluting your base Python and survives `make clean` (which wipes
  `build/`):
  ```bash
  conda create -y -n patchstorage-uploader python=3.12 pip
  conda run -n patchstorage-uploader pip install requests click rdflib
  ```
  Then `conda activate patchstorage-uploader` before `make`, or pass
  `PYTHON="$(conda run -n patchstorage-uploader which python)"`. (A plain
  `pip install requests click rdflib`, or a venv, works too.)
- A modgui **screenshot** in the bundle (required to publish)

## Usage
- `make patchstorage-build` — build all three bundles into `build/patchstorage/`
- `make patchstorage-prepare` — assemble + prepare; inspect `build/ps-upload/dist/`
- `make patchstorage PS_USER=<username>` — build + prepare + publish
