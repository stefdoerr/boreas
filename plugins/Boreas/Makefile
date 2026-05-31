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

FILES_DSP = \
	BoreasPlugin.cpp

include ../../dpf/Makefile.plugins.mk

TARGETS += lv2

all: $(TARGETS)
