#!/usr/bin/make -f
# Top-level Makefile for a MOD Desktop / MOD Dwarf LV2 plugin.
# -----------------------------------------------------------
#
# This is the single source of truth for the plugin's identity in builds:
# change PLUGIN, BRAND, LABEL, and PLUGIN_URI_BASE below when you rename
# the plugin. Everything else (TTL patching, bundle naming, install paths,
# release tarball names) derives from these.
#
# Targets:
#   make                 - build the LV2 bundle into bin/<plugin>.lv2
#   make beta            - same, but builds the side-by-side beta variant
#                          (distinct URI/brand/unique-id; can coexist with
#                          the stable plugin in any LV2 host)
#   make install         - copy bin/<plugin>.lv2 into $(LV2_DIR)
#                          (default /usr/lib/lv2; sudo for system install)
#   make install-beta    - build beta + install via ./install.sh
#   make dwarf           - cross-compile + scp to a connected MOD Dwarf
#   make release version=x.y.z  - build, package, tag, push, gh release

# ---------------------------------------------------------------------------
# Plugin identity — change these when forking the template.
#
# PLUGIN     — lowercase identifier; the LV2 bundle becomes <PLUGIN>.lv2
#              and the cross-compiled .so is named <PLUGIN>.so.
# PLUGIN_DIR — path to the DPF inner-plugin source dir. The convention
#              is CamelCase under plugins/ to match DPF examples; rename
#              the directory when forking and update this var.
# BRAND      — modgui brand string shown on the pedal frame.
# LABEL      — modgui label string shown on the pedal frame.
# PLUGIN_URI_BASE — stable LV2 URI prefix (does NOT need to resolve over
#              HTTP — LV2 URIs are pure identifiers — but pick something
#              unique so plugins from different vendors never collide).
PLUGIN          := myplugin
PLUGIN_DIR      := plugins/MyPlugin
BRAND           := myplugin
LABEL           := My Plugin
PLUGIN_URI_BASE := http://myplugin.local/plugins
# ---------------------------------------------------------------------------

# Set BETA=1 to produce a side-by-side beta build: distinct LV2 URI,
# bundle name, brand, and unique id. Same source, different identity —
# install with `make BETA=1 install` (or the `make beta` shortcut) and
# it'll coexist with the stable plugin in MOD Desktop. Used to A/B test
# a work-in-progress against the released plugin.
#
# The conditional `-D<PLUGIN_UPPER>_BETA=1` macro is propagated to the
# DPF sub-make via $(CXXFLAGS) (BUILD_CXX_FLAGS is hard-reset by DPF's
# Makefile.base.mk so user flags must travel via CXXFLAGS).
PLUGIN_UPPER := $(shell echo $(PLUGIN) | tr a-z A-Z)
ifeq ($(BETA),1)
export $(PLUGIN_UPPER)_BETA := 1
BUNDLE_NAME  := $(PLUGIN)-beta
BUNDLE_LABEL := $(LABEL) (Beta)
PLUGIN_URI   := $(PLUGIN_URI_BASE)/$(PLUGIN)-beta
else
BUNDLE_NAME  := $(PLUGIN)
BUNDLE_LABEL := $(LABEL)
PLUGIN_URI   := $(PLUGIN_URI_BASE)/$(PLUGIN)
endif
BUNDLE := bin/$(BUNDLE_NAME).lv2

.PHONY: all plugin ttl modgui clean distclean

all: plugin ttl modgui

# ---------------------------------------------------------------------------
# Build the plugin .so

plugin:
	$(MAKE) -C $(PLUGIN_DIR)

# ---------------------------------------------------------------------------
# Generate manifest.ttl and <plugin>.ttl via DPF's TTL generator.
# DPF dlopens the .so to introspect ports — this must run on the same
# architecture as the plugin (i.e., x86_64 host build, not the cross
# build).

ttl: plugin dpf/utils/lv2_ttl_generator
	@$(CURDIR)/dpf/utils/generate-ttl.sh

dpf/utils/lv2_ttl_generator:
	$(MAKE) -C dpf/utils/lv2-ttl-generator

# ---------------------------------------------------------------------------
# Copy modgui assets into the bundle and patch the modgui.ttl with the
# current build's URI / brand / label. The sed substitutions are no-ops
# in the stable build (identity transform) and only kick in for the beta.

modgui: ttl
	@mkdir -p $(BUNDLE)/modgui
	cp -f $(PLUGIN_DIR)/modgui/*.html $(BUNDLE)/modgui/
	cp -f $(PLUGIN_DIR)/modgui/*.css  $(BUNDLE)/modgui/
	cp -f $(PLUGIN_DIR)/modgui/*.js   $(BUNDLE)/modgui/
	@# PNGs are optional during early development (modgui still renders
	@# without a screenshot; only the knob sprite is essential).
	@if ls $(PLUGIN_DIR)/modgui/*.png >/dev/null 2>&1; then \
		cp -f $(PLUGIN_DIR)/modgui/*.png $(BUNDLE)/modgui/; \
	fi
	@if [ -d $(PLUGIN_DIR)/modgui/knobs ]; then \
		cp -rf $(PLUGIN_DIR)/modgui/knobs $(BUNDLE)/modgui/; \
	fi
	sed -e 's|$(PLUGIN_URI_BASE)/$(PLUGIN)|$(PLUGIN_URI)|g' \
	    -e 's|modgui:brand "$(BRAND)"|modgui:brand "$(BUNDLE_NAME)"|' \
	    -e 's|modgui:label "$(LABEL)"|modgui:label "$(BUNDLE_LABEL)"|' \
	    $(PLUGIN_DIR)/modgui.ttl > $(BUNDLE)/modgui.ttl
	@if ! grep -q 'modgui.ttl' $(BUNDLE)/manifest.ttl; then \
		printf '\n<%s>\n    rdfs:seeAlso <modgui.ttl> .\n' \
			"$(PLUGIN_URI)" >> $(BUNDLE)/manifest.ttl; \
	fi

# ---------------------------------------------------------------------------

clean:
	$(MAKE) clean -C $(PLUGIN_DIR)
	$(MAKE) clean -C dpf/utils/lv2-ttl-generator
	rm -rf bin build

distclean: clean
	rm -rf dpf/utils/lv2_ttl_generator dpf/utils/lv2_ttl_generator.exe

# ---------------------------------------------------------------------------
# install: copy the built bundle into $(LV2_DIR). Use PREFIX/LV2_DIR for
# system installs (sudo make install PREFIX=/usr/local); use ./install.sh
# for the MOD Desktop user-plugin dir.

PREFIX     ?= /usr
LV2_DIR    ?= $(DESTDIR)$(PREFIX)/lib/lv2

install: all
	@mkdir -p "$(LV2_DIR)"
	cp -rL "$(BUNDLE)" "$(LV2_DIR)/"

beta:
	$(MAKE) BETA=1

install-beta:
	$(MAKE) BETA=1
	BETA=1 ./install.sh

.PHONY: install beta install-beta

# ---------------------------------------------------------------------------
# MOD Dwarf cross-build — self-contained, no host workdir or external clones.
#
# Vendored Docker image (mod-build/Dockerfile) bakes in the
# mod-plugin-builder cross-toolchain (aarch64, glibc 2.27, gcc 9.4.0 —
# matching Dwarf firmware). First `make dwarf-image` is slow (~30-60 min,
# one-time per machine). After that, `make dwarf-build` is ~10 s.

CROSS_IMAGE  ?= $(PLUGIN)-cross
DWARF_HOST   ?= 192.168.51.1
DWARF_USER   ?= root
DWARF_LV2DIR ?= /root/.lv2

DWARF_BUNDLE := build/dwarf/$(PLUGIN).lv2

dwarf-image:
	docker build -t $(CROSS_IMAGE) mod-build/

dwarf-build:
	@if ! docker image inspect $(CROSS_IMAGE) >/dev/null 2>&1; then \
		echo "==> $(CROSS_IMAGE) image not built yet — building (~30-60 min, one-time)"; \
		$(MAKE) dwarf-image; \
	fi
	@mkdir -p build/dwarf
	docker run --rm \
		-e HOST_UID=$$(id -u) -e HOST_GID=$$(id -g) \
		-e PLUGIN=$(PLUGIN) \
		-v "$(CURDIR):/src:ro" \
		-v "$(CURDIR)/build/dwarf:/out" \
		$(CROSS_IMAGE) \
		bash /src/mod-build/build-plugin.sh

# Push the bundle to a connected Dwarf via scp. /root/.lv2/ is the per-
# user plugin dir and survives firmware updates.
#
# scp -O: Dwarf runs Dropbear, which has no SFTP subsystem. Modern OpenSSH
# scp (>= 9.0) defaults to SFTP and fails with "subsystem request failed
# on channel 0". -O forces the legacy scp protocol.
#
# After scp, we restart BOTH jack2 and mod-ui — they maintain independent
# lilv plugin caches. Restarting jack2 alone makes the plugin instantiable
# but mod-ui still shows the cached old port list ("No such symbol: ..."
# errors in the UI).
dwarf-deploy:
	@if [ ! -d "$(DWARF_BUNDLE)" ]; then \
		echo "error: no bundle at $(DWARF_BUNDLE) — run 'make dwarf-build' first."; \
		exit 1; \
	fi
	scp -O -r "$(DWARF_BUNDLE)" "$(DWARF_USER)@$(DWARF_HOST):$(DWARF_LV2DIR)/"
	@echo "==> Restarting jack2 + mod-ui so the new bundle is picked up"
	ssh "$(DWARF_USER)@$(DWARF_HOST)" 'systemctl restart jack2 mod-ui'
	@echo "==> After deploy, hard-refresh the MOD-UI browser tab (Ctrl-Shift-R)"
	@echo "    so its JS-side plugin metadata isn't served from the browser cache."

dwarf: dwarf-build dwarf-deploy

.PHONY: dwarf dwarf-build dwarf-image dwarf-deploy

# ---------------------------------------------------------------------------
# release: build desktop + dwarf bundles locally, package them, tag the
# current commit as v$(version), push, and create a GitHub release with
# both bundles attached as downloadable assets.
#
#   make release version=0.0.1
#
# Local build (instead of CI) because the cross-toolchain image takes
# ~30-60 min to assemble from scratch and is hard to cache reliably on a
# fresh GH Actions runner. Locally the image is already there and
# `make dwarf-build` is ~10 s.

DIST_DIR := dist
LINUX_TARBALL := $(PLUGIN)-v$(version)-linux-x86_64.tar.gz
DWARF_TARBALL := $(PLUGIN)-v$(version)-dwarf-aarch64.tar.gz

release-build:
	@if [ -z "$(version)" ]; then \
		echo "error: version is required."; \
		echo "       usage: make release-build version=x.y.z"; \
		exit 1; \
	fi
	@echo "==> Building desktop bundle (Linux x86_64)"
	$(MAKE) clean all
	@mkdir -p $(DIST_DIR)
	tar -C bin -czf $(DIST_DIR)/$(LINUX_TARBALL) $(PLUGIN).lv2
	@echo "==> Building Dwarf bundle (aarch64)"
	$(MAKE) dwarf-build
	tar -C build/dwarf -czf $(DIST_DIR)/$(DWARF_TARBALL) $(PLUGIN).lv2
	@echo
	@echo "Built release artefacts in $(DIST_DIR)/:"
	@ls -lh $(DIST_DIR)/$(PLUGIN)-v$(version)-*.tar.gz

release: release-build
	@if [ -n "$$(git status --porcelain)" ]; then \
		echo "error: working tree is dirty. Commit or stash first."; \
		git status --short; \
		exit 1; \
	fi
	@if git rev-parse -q --verify "refs/tags/v$(version)" >/dev/null 2>&1; then \
		echo "error: tag v$(version) already exists locally."; \
		exit 1; \
	fi
	@echo "==> Pushing branch (so the tagged commit is reachable on origin)"
	git push
	@echo "==> Tagging v$(version)"
	git tag -a "v$(version)" -m "Release v$(version)"
	git push origin "v$(version)"
	@echo "==> Creating GitHub release v$(version) with both bundles attached"
	gh release create "v$(version)" \
		"$(DIST_DIR)/$(LINUX_TARBALL)" \
		"$(DIST_DIR)/$(DWARF_TARBALL)" \
		--title "v$(version)" \
		--generate-notes
	@echo
	@echo "Release published:"
	@echo "  https://github.com/$$(git config --get remote.origin.url | sed -E 's|.*[:/]([^:/]+/[^/]+?)(\.git)?$$|\1|')/releases/tag/v$(version)"

.PHONY: release release-build
