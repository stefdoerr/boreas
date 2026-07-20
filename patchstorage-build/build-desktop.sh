#!/bin/bash
# Build the desktop plugin formats (VST3 + CLAP) with Patchstorage's portable
# x86_64 toolchain (glibc 2.27), so the binaries load on a wide range of Linux
# DAWs regardless of the build host's glibc — consistent with the linux-amd64
# LV2 build. Run INSIDE a patchstorage/lv2_builder-x86_64 image (via
# `make desktop-build`). The host passes:
#   /src         — source tree, mounted read-only
#   /out         — host output dir (build/desktop); we drop <plugin>.{vst3,clap} here
#   $PLUGIN      — plugin name
#   $TUPLE       — cross toolchain prefix (x86_64-mod-linux-gnu)
#   $CPUFLAGS    — CPU/opt flags matching patchstorage's defconfig
#   $HOST_UID/$HOST_GID — chown output back to the host user
#
# Single-phase: VST3/CLAP don't use the LV2 TTL generator or modgui, so there's
# no native pass — just cross-compile the two formats with the toolchain.
set -euo pipefail

for v in PLUGIN TUPLE CPUFLAGS; do
  if [ -z "${!v:-}" ]; then echo "build-desktop.sh: \$$v not set" >&2; exit 1; fi
done
[ -d /src ] || { echo "build-desktop.sh: /src not mounted" >&2; exit 1; }
[ -d /out ] || { echo "build-desktop.sh: /out not mounted" >&2; exit 1; }

GCC="$(ls /home/builder/lv2-workdir/*/toolchain/bin/${TUPLE}-gcc 2>/dev/null | head -n1 || true)"
if [ -z "$GCC" ]; then
  GCC="$(find / -name "${TUPLE}-gcc" -type f 2>/dev/null | head -n1 || true)"
fi
if [ -z "$GCC" ]; then
  echo "build-desktop.sh: toolchain '${TUPLE}-gcc' not found in image." >&2
  echo "Toolchains present:" >&2
  ls /home/builder/lv2-workdir/*/toolchain/bin/*-gcc 2>/dev/null >&2 || echo "  (none at expected path)" >&2
  exit 1
fi
BIN_DIR="$(dirname "$GCC")"
echo "==> Toolchain: $BIN_DIR/${TUPLE}-{gcc,g++}"

WORK=/tmp/desktopbuild/$PLUGIN
rm -rf "$WORK"; mkdir -p "$WORK"
rsync -a --exclude bin --exclude build --exclude '.git' /src/ "$WORK/"
cd "$WORK"

PLUGIN_DIR="$(find plugins -mindepth 2 -maxdepth 2 -name Makefile -printf '%h\n' | head -n1)"
[ -n "$PLUGIN_DIR" ] || { echo "build-desktop.sh: no plugin Makefile under plugins/*/" >&2; exit 1; }

echo "==> Building VST3 + CLAP for ${PLUGIN} (${TUPLE}, portable glibc)"
make -s -C "$PLUGIN_DIR" vst3 clap \
  CC="$BIN_DIR/${TUPLE}-gcc" \
  CXX="$BIN_DIR/${TUPLE}-g++" \
  AR="$BIN_DIR/${TUPLE}-ar" \
  STRIP="$BIN_DIR/${TUPLE}-strip" \
  EXTRA_CFLAGS="$CPUFLAGS" \
  EXTRA_CXXFLAGS="$CPUFLAGS" \
  NOOPT=false

[ -d "bin/${PLUGIN}.vst3" ] || { echo "build-desktop.sh: VST3 bundle not produced" >&2; exit 1; }
[ -f "bin/${PLUGIN}.clap" ] || { echo "build-desktop.sh: CLAP not produced" >&2; exit 1; }

echo "==> Publishing to /out"
rm -rf "/out/${PLUGIN}.vst3" "/out/${PLUGIN}.clap"
cp -rL "bin/${PLUGIN}.vst3" "/out/"
cp -L  "bin/${PLUGIN}.clap" "/out/"

if [ -n "${HOST_UID:-}" ] && [ -n "${HOST_GID:-}" ]; then
  chown -R "$HOST_UID:$HOST_GID" /out
fi
echo "==> Done: $(file -b /out/${PLUGIN}.clap)"
