/*
 * DopplerIt - factory presets
 * Copyright (C) 2026 pilali
 * SPDX-License-Identifier: MIT
 *
 * Single source of the presets of every version:
 *   - JUCE (VST3 / AU): factory programs
 *   - LV2: dopplerit.lv2/presets.ttl and manifest.ttl, generated from this
 *     table by tools/gen_presets.cpp ("make presets")
 *
 * Values are in the units of the controls (the LV2 ports). Presets leave the
 * Output (Mono / Stereo) choice alone: it depends on the routing.
 * Times are given at 120 BPM where they are musical (0.5 s = quarter note).
 */

#pragma once

#include "age_engine.hpp"

namespace dopplerit {

struct Preset {
    const char* id;     // LV2 preset URI fragment
    const char* name;
    int   mode;         // AgeMode
    float time;         // s
    float speedKmh;     // km/h
    float depth;        // x
    float shape;        // %
    float period;       // s
    float feedback;     // %
    float tone;         // %
    float mix;          // %
};

static const Preset kPresets[] = {
    //  id                    name                    mode           time   speed depth shape period  fb  tone  mix
    // the default settings
    { "default",            "Default",              kAgePassBy,   0.25f, 100.f, 2.0f,  50.f, 2.0f,  70.f, 60.f, 50.f },

    // realistic: the transposition of a real vehicle (Depth x1 to x2)
    { "highway-siren",      "Highway Siren",        kAgePassBy,   0.25f, 110.f, 1.0f,  50.f, 2.0f,  70.f, 60.f, 50.f },
    { "ambulance",          "Ambulance",            kAgePassBy,   0.20f,  80.f, 2.0f,  30.f, 1.6f,  70.f, 65.f, 50.f },
    { "approaching-train",  "Approaching Train",    kAgeApproach, 0.30f, 140.f, 1.5f,  60.f, 3.0f,  75.f, 50.f, 45.f },
    { "fading-away",        "Fading Away",          kAgeRecede,   0.35f,  90.f, 1.5f,  70.f, 3.5f,  75.f, 45.f, 45.f },

    // subtle: detuned echoes, slow drift
    { "detuned-slapback",   "Detuned Slapback",     kAgePassBy,   0.09f,  25.f, 1.0f, 100.f, 0.5f,  40.f, 70.f, 35.f },
    { "slow-drift",         "Slow Drift",           kAgePassBy,   0.40f,  30.f, 2.0f, 100.f, 8.0f,  80.f, 50.f, 35.f },
    { "pitch-drop-slap",    "Pitch-Drop Slap",      kAgeRecede,   0.12f, 150.f, 3.0f,  40.f, 0.6f,  35.f, 60.f, 40.f },

    // musical intervals: a fifth, then one octave each way
    { "fifth-pass",         "Fifth Up, Fifth Down", kAgePassBy,   0.20f, 200.f, 2.7f,  60.f, 2.4f,  72.f, 60.f, 45.f },
    { "octave-swap",        "Octave Swap",          kAgePassBy,   0.25f, 300.f, 8.0f,   5.f, 2.0f,  70.f, 60.f, 50.f },
    { "octave-fly-by",      "Octave Fly-By",        kAgePassBy,   0.10f, 300.f, 8.0f,  60.f, 2.4f,  70.f, 60.f, 45.f },
    { "long-glide",         "Long Glide",           kAgePassBy,   0.25f, 300.f, 8.0f,  70.f, 6.0f,  65.f, 60.f, 50.f },
    { "rising-ghosts",      "Rising Ghosts",        kAgeApproach, 0.20f, 300.f, 8.0f, 100.f, 3.0f,  72.f, 70.f, 45.f },
    { "dive-bomb",          "Dive Bomb",            kAgeRecede,   0.08f, 300.f, 8.0f,  20.f, 1.9f,  70.f, 55.f, 45.f },

    // in time at 120 BPM (sync Time and Period to the tempo in mod-ui)
    { "quarter-note-pass",  "Quarter-Note Pass",    kAgePassBy,   0.50f, 150.f, 3.0f,  60.f, 4.0f,  72.f, 55.f, 40.f },
    { "dotted-eighth-siren","Dotted Eighth Siren",  kAgePassBy,   0.375f,200.f, 4.0f,  50.f, 3.0f,  65.f, 60.f, 40.f },
    { "triplet-cascade",    "Triplet Cascade",      kAgeApproach, 0.1667f,200.f,5.0f,  80.f, 2.0f,  70.f, 65.f, 40.f },

    // textures: dense swarms, long tails
    { "rush-hour",          "Rush Hour",            kAgePassBy,   0.05f, 200.f, 4.0f,  80.f, 1.2f,  60.f, 55.f, 40.f },
    { "comet-tail",         "Comet Tail",           kAgeApproach, 0.30f, 300.f, 8.0f, 100.f, 7.2f,  80.f, 85.f, 40.f },
    { "deep-space",         "Deep Space",           kAgeRecede,   0.60f, 250.f, 6.0f, 100.f, 9.6f,  88.f, 35.f, 45.f },
};

static const int kNumPresets = (int)(sizeof(kPresets) / sizeof(kPresets[0]));

// sections of the list above (first preset of each), for preset menus
struct PresetGroup {
    int first;
    const char* name;
};

static const PresetGroup kPresetGroups[] = {
    {  0, "Default" },
    {  1, "Realistic" },
    {  5, "Subtle" },
    {  8, "Intervals" },
    { 14, "In time (120 BPM)" },
    { 17, "Textures" },
};

static const int kNumPresetGroups = (int)(sizeof(kPresetGroups) / sizeof(kPresetGroups[0]));

} // namespace dopplerit
