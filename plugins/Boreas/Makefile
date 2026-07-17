#!/usr/bin/make -f
# DPF inner Makefile for Boreas
ifeq ($(BETA),1)
BOREAS_BETA = 1
endif
ifeq ($(BOREAS_BETA),1)
NAME = boreas-beta
CXXFLAGS += -DBOREAS_BETA=1
else
NAME = boreas
endif

# Plugin version — the top-level VERSION file is the single source of
# truth (`make release` bumps and commits it to match the release tag).
# Injected as preprocessor defines so getVersion() and the generated LV2
# TTL (lv2:minorVersion / lv2:microVersion) always report the released
# version. Travels through CXXFLAGS because DPF's Makefile.base.mk
# hard-resets BUILD_CXX_FLAGS but appends $(CXXFLAGS) at the end.
PLUGIN_VERSION := $(strip $(shell cat ../../VERSION 2>/dev/null))
ifeq ($(PLUGIN_VERSION),)
$(error VERSION file not found at ../../VERSION)
endif
PLUGIN_VERSION_PARTS := $(subst ., ,$(PLUGIN_VERSION))
CXXFLAGS += -DPLUGIN_VERSION_MAJOR=$(word 1,$(PLUGIN_VERSION_PARTS)) \
            -DPLUGIN_VERSION_MINOR=$(word 2,$(PLUGIN_VERSION_PARTS)) \
            -DPLUGIN_VERSION_MICRO=$(word 3,$(PLUGIN_VERSION_PARTS))

FILES_DSP = \
	BoreasPlugin.cpp

include ../../dpf/Makefile.plugins.mk

TARGETS += lv2

all: $(TARGETS)
