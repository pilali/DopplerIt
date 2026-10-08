/*
 * DopplerIt - Doppler delay LV2 plugin for MOD devices and Raspberry Pi 5
 * Copyright (C) 2026 pilali
 * SPDX-License-Identifier: MIT
 *
 * 1 audio input -> 2 audio outputs, with an "Output" mode:
 *   - Mono   : the same signal on both outputs
 *   - Stereo : the effect is slightly offset between left and right
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

#include "delay_engine.hpp"

#define DOPPLERIT_URI "https://github.com/pilali/dopplerit"

namespace {

enum Control {
    kCtlHeads = 0,
    kCtlTime,
    kCtlSpeed,
    kCtlDistance,
    kCtlPeriod,
    kCtlStagger,
    kCtlFeedback,
    kCtlTone,
    kCtlMix,
    kCtlOutput,
    kCtlLoop,
    kCtlTrigger,
    kCtlEnabled,
    kCtlMode,
    kCtlCount
};

// Port layout, see dopplerit.ttl: 0 in, 1 out_l, 2 out_r, then the controls (mode last)
enum { kPortIn = 0, kPortOutL = 1, kPortOutR = 2, kPortFirstControl = 3 };

// Defaults, used until the host connects a control port
const float kDefaults[kCtlCount] = {
    2.f,    // heads
    0.25f,  // time (s)
    100.f,  // speed (km/h)
    5.f,    // distance (m)
    2.5f,   // period (s)
    0.f,    // stagger (%)
    35.f,   // feedback (%)
    60.f,   // tone (%)
    50.f,   // mix (%)
    1.f,    // output (0 = mono, 1 = stereo)
    1.f,    // loop
    0.f,    // trigger
    1.f,    // enabled
    2.f,    // mode (0 approach, 1 recede, 2 pass-by)
};

struct DopplerIt {
    dopplerit::DelayEngine engine;

    const float* in     = nullptr;
    float*       out[2] = { nullptr, nullptr };
    const float* ctl[kCtlCount] = {};

    bool lastTrigger = false;

    float value(int c) const
    {
        return ctl[c] != nullptr ? *ctl[c] : kDefaults[c];
    }

    dopplerit::DelayParams params() const
    {
        dopplerit::DelayParams p;
        p.mode     = (int)(value(kCtlMode) + 0.5f);
        p.heads    = (int)(value(kCtlHeads) + 0.5f);
        p.time     = value(kCtlTime);
        p.speedKmh = value(kCtlSpeed);
        p.distance = value(kCtlDistance);
        p.period   = value(kCtlPeriod);
        p.stagger  = value(kCtlStagger) * 0.01f;
        p.feedback = value(kCtlFeedback) * 0.01f;
        p.tone     = value(kCtlTone) * 0.01f;
        p.mix      = value(kCtlMix) * 0.01f;
        p.stereo   = value(kCtlOutput) > 0.5f;
        p.loop     = value(kCtlLoop) > 0.5f;
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
    switch (port) {
    case kPortIn:   self->in     = (const float*)data; return;
    case kPortOutL: self->out[0] = (float*)data;       return;
    case kPortOutR: self->out[1] = (float*)data;       return;
    default:
        if (port >= kPortFirstControl && port < kPortFirstControl + kCtlCount)
            self->ctl[port - kPortFirstControl] = (const float*)data;
        return;
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
