#!/usr/bin/make -f
#
# DopplerIt - Doppler delay LV2 plugin
#
#   make                     build build/dopplerit.lv2 (native optimizations)
#   make NOOPT=true          build without CPU specific flags (cross builds,
#                            mod-plugin-builder, packages)
#   make install             install to $(DESTDIR)$(PREFIX)/lib/lv2
#   make install-user        install to ~/.lv2
#   make test                build and run the offline DSP tests
#   make demo                render demo WAV files of the engine into build/demo
#   make presets             regenerate the LV2 presets from src/presets.hpp
#   make presets-demo        render every preset into build/presets
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

$(TARGET): src/dopplerit.cpp src/age_engine.hpp
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

test: tests/test_age check-presets
	./tests/test_age

tests/test_age: tests/test_age.cpp src/age_engine.hpp
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -fno-fast-math -Isrc tests/test_age.cpp -o $@ -lm

# LV2 presets, generated from the table shared with the JUCE version
$(BUILDDIR)/gen_presets: tools/gen_presets.cpp src/presets.hpp src/age_engine.hpp
	@mkdir -p $(BUILDDIR)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -Isrc tools/gen_presets.cpp -o $@

presets: $(BUILDDIR)/gen_presets
	$(BUILDDIR)/gen_presets $(BUNDLE)

# fails if dopplerit.lv2 does not match src/presets.hpp (run "make presets")
check-presets: $(BUILDDIR)/gen_presets
	@mkdir -p $(BUILDDIR)/tmp/check-presets
	$(BUILDDIR)/gen_presets $(BUILDDIR)/tmp/check-presets > /dev/null
	cmp $(BUILDDIR)/tmp/check-presets/manifest.ttl $(BUNDLE)/manifest.ttl
	cmp $(BUILDDIR)/tmp/check-presets/presets.ttl $(BUNDLE)/presets.ttl

demo: tools/render_age_demo.cpp tools/demo_signal.hpp src/age_engine.hpp
	@mkdir -p $(BUILDDIR)/demo
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -Isrc tools/render_age_demo.cpp -o $(BUILDDIR)/render_age_demo -lm
	$(BUILDDIR)/render_age_demo $(BUILDDIR)/demo

presets-demo: tools/render_presets.cpp tools/demo_signal.hpp src/presets.hpp src/age_engine.hpp
	@mkdir -p $(BUILDDIR)/presets
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -Isrc tools/render_presets.cpp -o $(BUILDDIR)/render_presets -lm
	$(BUILDDIR)/render_presets $(BUILDDIR)/presets

clean:
	rm -rf $(BUILDDIR) tests/test_age

.PHONY: all bundle install install-user uninstall test check-presets presets presets-demo demo clean
