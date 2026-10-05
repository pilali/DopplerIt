#!/usr/bin/make -f
#
# DopplerIt - Doppler effect LV2 plugin
#
#   make                     build build/dopplerit.lv2 (native optimizations)
#   make NOOPT=true          build without CPU specific flags (cross builds,
#                            mod-plugin-builder, packages)
#   make install             install to $(DESTDIR)$(PREFIX)/lib/lv2
#   make install-user        install to ~/.lv2
#   make test                build and run the offline DSP tests
#

BUNDLE   = dopplerit.lv2
PLUGIN   = dopplerit
BUILDDIR = build
TARGET   = $(BUILDDIR)/$(BUNDLE)/$(PLUGIN).so

PREFIX  ?= /usr/local
LV2DIR  ?= $(PREFIX)/lib/lv2
USERLV2 ?= $(HOME)/.lv2

CXX     ?= g++
CXXFLAGS ?= -O3

# ---------------------------------------------------------------------------
# CPU specific optimizations
#
# Native builds pick the best flags for the build machine (Raspberry Pi 5 =
# Cortex-A76). mod-plugin-builder passes its own flags (Duo: Cortex-A7,
# Duo X / Dwarf: Cortex-A53), use NOOPT=true there.

ifneq ($(NOOPT),true)
MACHINE := $(shell uname -m)
ifneq (,$(filter aarch64 arm64,$(MACHINE)))
OPT_FLAGS = -mcpu=native
else ifneq (,$(filter armv7l armv7 armhf,$(MACHINE)))
OPT_FLAGS = -mcpu=native -mfpu=neon-vfpv4 -mfloat-abi=hard
else ifneq (,$(filter x86_64 i686 i386,$(MACHINE)))
OPT_FLAGS = -march=native
endif
endif

override CXXFLAGS += $(OPT_FLAGS) -std=c++11 -fPIC -fvisibility=hidden \
                     -ffast-math -fno-finite-math-only -fdata-sections -ffunction-sections \
                     -Wall -Wextra -Wno-unused-parameter
override LDFLAGS  += -shared -Wl,--no-undefined -Wl,--gc-sections -Wl,-O1 -Wl,--as-needed

LV2_CFLAGS := $(shell pkg-config --cflags lv2 2>/dev/null)

TTL_FILES    = $(wildcard $(BUNDLE)/*.ttl)
MODGUI_FILES = $(wildcard $(BUNDLE)/modgui/*)

# ---------------------------------------------------------------------------

all: $(TARGET) bundle

$(TARGET): src/dopplerit.cpp src/doppler_engine.hpp
	@mkdir -p $(BUILDDIR)/$(BUNDLE)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(LV2_CFLAGS) src/dopplerit.cpp -o $@ $(LDFLAGS)

bundle: $(TTL_FILES) $(MODGUI_FILES)
	@mkdir -p $(BUILDDIR)/$(BUNDLE)/modgui
	cp $(TTL_FILES) $(BUILDDIR)/$(BUNDLE)/
	cp -r $(BUNDLE)/modgui/. $(BUILDDIR)/$(BUNDLE)/modgui/

install: all
	install -d $(DESTDIR)$(LV2DIR)/$(BUNDLE)/modgui
	install -m 644 $(BUILDDIR)/$(BUNDLE)/*.ttl $(DESTDIR)$(LV2DIR)/$(BUNDLE)/
	install -m 755 $(TARGET) $(DESTDIR)$(LV2DIR)/$(BUNDLE)/
	cp -r $(BUILDDIR)/$(BUNDLE)/modgui/. $(DESTDIR)$(LV2DIR)/$(BUNDLE)/modgui/

install-user:
	$(MAKE) install LV2DIR=$(USERLV2) DESTDIR=

uninstall:
	rm -rf $(DESTDIR)$(LV2DIR)/$(BUNDLE)

test: tests/test_engine
	./tests/test_engine

tests/test_engine: tests/test_engine.cpp src/doppler_engine.hpp
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -fno-fast-math -Isrc tests/test_engine.cpp -o $@ -lm

clean:
	rm -rf $(BUILDDIR) tests/test_engine

.PHONY: all bundle install install-user uninstall test clean
