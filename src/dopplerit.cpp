/*
 * DopplerIt - Doppler effect LV2 plugin for MOD devices and Raspberry Pi 5
 * Copyright (C) 2026 pilali
 * SPDX-License-Identifier: MIT
 *
 * One plugin, 1 audio input -> 2 audio outputs, with an "Output" mode:
 *   - Mono   : simple effect, the same signal on both outputs
 *   - Stereo : slightly offset ears for a wider image
 */

#if defined(__has_include)
# if __has_include(<lv2/core/lv2.h>)
#  include <lv2/core/lv2.h>
# else
#  include <lv2/lv2plug.in/ns/lv2core/lv2.h>
# endif
#else
# include <lv2/core/lv2.h>
#endif

#include <cstdlib>
#include <new>

#include "doppler_engine.hpp"

#define DOPPLERIT_URI "https://github.com/pilali/dopplerit"

namespace {

enum Control {
    kCtlMode = 0,
    kCtlSpeed,
    kCtlPeriod,
    kCtlDistance,
    kCtlAttenuation,
    kCtlOutput,
    kCtlWidth,
    kCtlMix,
    kCtlLoop,
    kCtlTrigger,
    kCtlEnabled,
    kCtlCount
};

// Port layout, see dopplerit.ttl
enum PortKind { kPortIn, kPortOutL, kPortOutR, kPortControl };

struct PortDef {
    PortKind kind;
    int      control;
};

const PortDef kPorts[] = {
    { kPortIn, -1 },
    { kPortOutL, -1 },
    { kPortOutR, -1 },
    { kPortControl, kCtlMode },
    { kPortControl, kCtlSpeed },
    { kPortControl, kCtlPeriod },
    { kPortControl, kCtlDistance },
    { kPortControl, kCtlAttenuation },
    { kPortControl, kCtlOutput },
    { kPortControl, kCtlWidth },
    { kPortControl, kCtlMix },
    { kPortControl, kCtlLoop },
    { kPortControl, kCtlTrigger },
    { kPortControl, kCtlEnabled },
};

// Defaults, used until the host connects a control port
const float kDefaults[kCtlCount] = {
    (float)dopplerit::kModePassBy, // mode
    60.f,                          // speed (km/h)
    4.f,                           // period (s)
    4.f,                           // distance (m)
    60.f,                          // attenuation (%)
    1.f,                           // output (0 = mono, 1 = stereo)
    50.f,                          // width (%)
    70.f,                          // mix (%)
    1.f,                           // loop
    0.f,                           // trigger
    1.f,                           // enabled
};

struct DopplerIt {
    dopplerit::Engine engine;

    const float* in      = nullptr;
    float*       out[2]  = { nullptr, nullptr };
    const float* ctl[kCtlCount] = {};

    bool lastTrigger = false;

    float value(int c) const
    {
        return ctl[c] != nullptr ? *ctl[c] : kDefaults[c];
    }

    dopplerit::Params params() const
    {
        dopplerit::Params p;
        p.mode        = (int)(value(kCtlMode) + 0.5f);
        p.speedKmh    = value(kCtlSpeed);
        p.period      = value(kCtlPeriod);
        p.distance    = value(kCtlDistance);
        p.attenuation = value(kCtlAttenuation) * 0.01f;
        p.width       = value(kCtlWidth) * 0.01f;
        p.stereo      = value(kCtlOutput) > 0.5f;
        p.mix         = value(kCtlMix) * 0.01f;
        p.loop        = value(kCtlLoop) > 0.5f;
        return p;
    }
};

LV2_Handle instantiate(const LV2_Descriptor*, double rate, const char*, const LV2_Feature* const*)
{
    DopplerIt* self = new (std::nothrow) DopplerIt();
    if (self == nullptr)
        return nullptr;

    if (!self->engine.init(rate, 2)) {
        delete self;
        return nullptr;
    }

    self->engine.reset(self->params());
    return (LV2_Handle)self;
}

void connectPort(LV2_Handle instance, uint32_t port, void* data)
{
    DopplerIt* self = (DopplerIt*)instance;
    if (port >= sizeof(kPorts) / sizeof(kPorts[0]))
        return;

    const PortDef& def = kPorts[port];
    switch (def.kind) {
    case kPortIn:      self->in     = (const float*)data; break;
    case kPortOutL:    self->out[0] = (float*)data;       break;
    case kPortOutR:    self->out[1] = (float*)data;       break;
    case kPortControl: self->ctl[def.control] = (const float*)data; break;
    }
}

void activate(LV2_Handle instance)
{
    DopplerIt* self = (DopplerIt*)instance;
    self->engine.reset(self->params());
    self->lastTrigger = self->value(kCtlTrigger) > 0.5f;
}

void run(LV2_Handle instance, uint32_t frames)
{
    DopplerIt* self = (DopplerIt*)instance;

    if (self->in == nullptr || self->out[0] == nullptr || self->out[1] == nullptr)
        return;

    const bool trig = self->value(kCtlTrigger) > 0.5f;
    if (trig && !self->lastTrigger)
        self->engine.trigger();
    self->lastTrigger = trig;

    self->engine.process(self->in, self->out, frames, self->params(),
                         self->value(kCtlEnabled) > 0.5f);
}

void cleanup(LV2_Handle instance)
{
    delete (DopplerIt*)instance;
}

const void* extensionData(const char*)
{
    return nullptr;
}

const LV2_Descriptor kDescriptor = {
    DOPPLERIT_URI,
    instantiate,
    connectPort,
    activate,
    run,
    nullptr,
    cleanup,
    extensionData
};

} // namespace

LV2_SYMBOL_EXPORT const LV2_Descriptor* lv2_descriptor(uint32_t index)
{
    return index == 0 ? &kDescriptor : nullptr;
}
