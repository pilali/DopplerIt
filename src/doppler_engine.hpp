/*
 * DopplerIt - Doppler effect LV2 plugin for MOD devices and Raspberry Pi 5
 * Copyright (C) 2026 pilali
 * SPDX-License-Identifier: MIT
 *
 * Physical model
 * --------------
 * A virtual source moves on a straight line (x axis) at constant speed v,
 * passing at a closest distance d in front of the listener. The listener's
 * ear(s) sit on the x axis at y = 0 (mono: one ear at x = 0, stereo: two ears
 * at x = -e and x = +e, e being driven by the "Width" control).
 *
 * The signal heard at time t was emitted at time t - tau, where tau is the
 * propagation time. For a source moving linearly, tau has a closed form:
 *
 *     c^2 tau^2 = (a - v tau)^2 + d^2        (a = source x minus ear x, now)
 *  => tau = (-a v + sqrt(a^2 v^2 + (c^2 - v^2)(a^2 + d^2))) / (c^2 - v^2)
 *
 * This gives an exact Doppler pitch ratio c / (c - v_radial) without any
 * iterative solver. The constant part d / c is removed so that the source
 * "at the closest point" is heard with (almost) no latency.
 *
 * The time-varying delay is read from a single mono delay line with 4-point
 * Hermite interpolation. Distance also drives an optional 1/r attenuation and
 * an air-absorption low-pass filter. Restarting a cycle (loop, trigger, mode
 * change, end of a one-shot pass) cross-fades two "voices" (read heads) so
 * there is never a click.
 */

#pragma once

#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>

namespace dopplerit {

enum Mode : int {
    kModeApproach = 0, // source comes from far away towards the listener
    kModeRecede   = 1, // source moves away from the listener
    kModePassBy   = 2, // full fly-by: approach then recede
};

struct Params {
    int   mode;        // Mode
    float speedKmh;    // source speed in km/h
    float period;      // duration of one pass in seconds
    float distance;    // closest distance to the listener in meters
    float attenuation; // 0..1, amount of distance attenuation + air absorption
    float width;       // 0..1, stereo spread (ignored in mono)
    bool  stereo;      // output mode: false = same mono signal on every output
    float mix;         // 0..1, dry/wet balance
    bool  loop;        // repeat passes continuously
};

// Parameter ranges (must match the .ttl files)
static constexpr float kMinSpeedKmh  = 5.0f;
static constexpr float kMaxSpeedKmh  = 300.0f;
static constexpr float kMinPeriod    = 0.5f;
static constexpr float kMaxPeriod    = 10.0f;
static constexpr float kMinDistance  = 1.0f;
static constexpr float kMaxDistance  = 50.0f;

class Engine {
public:
    static constexpr double kPi           = 3.14159265358979323846;
    static constexpr double kSpeedOfSound = 343.0;  // m/s
    static constexpr double kMaxEarOffset = 2.0;    // m, at width = 100 %
    static constexpr float  kMaxPanDepth  = 0.7f;   // at width = 100 %
    static constexpr double kFadeTime     = 0.050;  // s, voice cross-fade
    static constexpr int    kMaxChannels  = 2;

    Engine() = default;
    ~Engine() { release(); }

    Engine(const Engine&) = delete;
    Engine& operator=(const Engine&) = delete;

    bool init(double sampleRate, int channels)
    {
        fs_       = sampleRate;
        channels_ = channels < 1 ? 1 : (channels > kMaxChannels ? kMaxChannels : channels);

        // Worst case delay: approach mode, max speed, max period, ear offset.
        const double vmax = kMaxSpeedKmh / 3.6;
        const double L    = vmax * kMaxPeriod + kMaxEarOffset + 1.0;
        const double tau  = propagationTime(-L, vmax, kMinDistance) - kMinDistance / kSpeedOfSound;
        const double need = tau * fs_ + 16.0;

        uint32_t size = 1;
        while ((double)size < need)
            size <<= 1;

        bufSize_ = size;
        bufMask_ = size - 1;
        buf_     = static_cast<float*>(std::calloc(size, sizeof(float)));

        fadeLen_ = (uint32_t)(kFadeTime * fs_);
        if (fadeLen_ < 16)
            fadeLen_ = 16;
        fadeIn_  = static_cast<float*>(std::malloc((fadeLen_ + 1) * sizeof(float)));

        if (buf_ == nullptr || fadeIn_ == nullptr) {
            release();
            return false;
        }

        // equal-power fade-in curve, fade-out uses the mirrored table
        for (uint32_t i = 0; i <= fadeLen_; ++i)
            fadeIn_[i] = (float)std::sin(0.5 * kPi * (double)i / (double)fadeLen_);

        smoothSlow_  = (float)(1.0 - std::exp(-1.0 / (0.150 * fs_)));
        smoothFast_  = (float)(1.0 - std::exp(-1.0 / (0.030 * fs_)));
        maxCutoff_   = std::fmin(18000.0, 0.45 * fs_);
        return true;
    }

    void release()
    {
        std::free(buf_);
        std::free(fadeIn_);
        buf_    = nullptr;
        fadeIn_ = nullptr;
    }

    // Call from activate() or whenever the stream restarts.
    void reset(const Params& p)
    {
        if (buf_ != nullptr)
            std::memset(buf_, 0, bufSize_ * sizeof(float));
        writePos_ = 0;

        speed_    = clampf(p.speedKmh, kMinSpeedKmh, kMaxSpeedKmh) / 3.6f;
        period_   = clampf(p.period, kMinPeriod, kMaxPeriod);
        distance_ = clampf(p.distance, kMinDistance, kMaxDistance);
        atten_    = clampf(p.attenuation, 0.f, 1.f);
        width_    = (channels_ > 1 && p.stereo) ? clampf(p.width, 0.f, 1.f) : 0.f;
        mix_      = clampf(p.mix, 0.f, 1.f);
        enabled_  = 1.f;

        const int mode = clampMode(p.mode);
        cur_  = Voice{ xStart(mode, pathLength()), mode, true, { 0.f, 0.f } };
        old_  = cur_;
        fadePos_        = 0;
        fading_         = false;
        triggerPending_ = false;
    }

    // Request a new pass from the start (footswitch / trigger button).
    void trigger() { triggerPending_ = true; }

    // in: mono input, out: channels_ output buffers. "enabled" is the LV2
    // enabled designation (0 = bypassed), it is smoothed to avoid clicks.
    void process(const float* in, float* const* out, uint32_t frames, const Params& p, bool enabled)
    {
        const float tSpeed = clampf(p.speedKmh, kMinSpeedKmh, kMaxSpeedKmh) / 3.6f;
        const float tPeriod = clampf(p.period, kMinPeriod, kMaxPeriod);
        const float tDist  = clampf(p.distance, kMinDistance, kMaxDistance);
        const float tAtt   = clampf(p.attenuation, 0.f, 1.f);
        const float tWidth = (channels_ > 1 && p.stereo) ? clampf(p.width, 0.f, 1.f) : 0.f;
        const float tMix   = clampf(p.mix, 0.f, 1.f);
        const float tEn    = enabled ? 1.f : 0.f;
        const int   tMode  = clampMode(p.mode);

        float wet[kMaxChannels];
        float wetOld[kMaxChannels];

        for (uint32_t i = 0; i < frames; ++i)
        {
            // parameter smoothing
            speed_    += smoothSlow_ * (tSpeed  - speed_);
            period_   += smoothSlow_ * (tPeriod - period_);
            distance_ += smoothSlow_ * (tDist   - distance_);
            atten_    += smoothFast_ * (tAtt    - atten_);
            width_    += smoothFast_ * (tWidth  - width_);
            mix_      += smoothFast_ * (tMix    - mix_);
            enabled_  += smoothFast_ * (tEn     - enabled_);

            // Mono output: once the width has faded out both ears are
            // identical, so only one is computed and copied.
            if (tWidth <= 0.f && width_ < 1e-4f)
                width_ = 0.f;
            ears_ = (channels_ > 1 && width_ > 0.f) ? channels_ : 1;

            const float x = in[i];
            buf_[writePos_] = x;

            const double L = pathLength();

            // voice management: only one transition at a time
            if (!fading_)
                updateVoiceState(tMode, p.loop, L);

            renderVoice(cur_, wet);

            if (fading_)
            {
                renderVoice(old_, wetOld);
                const float gIn  = fadeIn_[fadePos_];
                const float gOut = fadeIn_[fadeLen_ - fadePos_];
                for (int c = 0; c < ears_; ++c)
                    wet[c] = wet[c] * gIn + wetOld[c] * gOut;

                advanceVoice(old_);
                if (++fadePos_ > fadeLen_)
                    fading_ = false;
            }

            advanceVoice(cur_);

            if (ears_ < channels_)
            {
                wet[1] = wet[0];
                cur_.lp[1] = cur_.lp[0];
                old_.lp[1] = old_.lp[0];
            }

            const float dryG = mix_ < 0.5f ? 1.f : 2.f * (1.f - mix_);
            const float wetG = mix_ > 0.5f ? 1.f : 2.f * mix_;

            for (int c = 0; c < channels_; ++c)
            {
                const float processed = dryG * x + wetG * wet[c];
                out[c][i] = x + enabled_ * (processed - x);
            }

            writePos_ = (writePos_ + 1) & bufMask_;
        }

        // flush denormals in filter states
        for (int c = 0; c < kMaxChannels; ++c)
        {
            if (std::fabs(cur_.lp[c]) < 1e-20f) cur_.lp[c] = 0.f;
            if (std::fabs(old_.lp[c]) < 1e-20f) old_.lp[c] = 0.f;
        }
    }

    // Exact propagation time for a source currently at relative position a
    // (along the path), moving at speed v, at perpendicular distance d.
    static double propagationTime(double a, double v, double d)
    {
        const double c2  = kSpeedOfSound * kSpeedOfSound;
        const double c2v = c2 - v * v;
        return (-a * v + std::sqrt(a * a * v * v + c2v * (a * a + d * d))) / c2v;
    }

    uint32_t bufferSize() const { return bufSize_; }

private:
    struct Voice {
        double x;      // source position along the path, meters
        int    mode;
        bool   moving; // false once a one-shot pass is over
        float  lp[kMaxChannels];
    };

    static float clampf(float v, float lo, float hi)
    {
        return v < lo ? lo : (v > hi ? hi : v);
    }

    static int clampMode(int m)
    {
        return m < kModeApproach ? kModeApproach : (m > kModePassBy ? kModePassBy : m);
    }

    double pathLength() const { return (double)speed_ * (double)period_; }

    static double xStart(int mode, double L)
    {
        switch (mode) {
        case kModeApproach: return -L;
        case kModeRecede:   return 0.0;
        default:            return -0.5 * L;
        }
    }

    static double xEnd(int mode, double L)
    {
        switch (mode) {
        case kModeApproach: return 0.0;
        case kModeRecede:   return L;
        default:            return 0.5 * L;
        }
    }

    void startFade(const Voice& next)
    {
        old_     = cur_;
        cur_     = next;
        // keep the filter memory so the new head does not start from silence
        cur_.lp[0] = old_.lp[0];
        cur_.lp[1] = old_.lp[1];
        fadePos_ = 0;
        fading_  = true;
    }

    void updateVoiceState(int mode, bool loop, double L)
    {
        if (triggerPending_)
        {
            triggerPending_ = false;
            startFade(Voice{ xStart(mode, L), mode, true, { 0.f, 0.f } });
            return;
        }

        if (cur_.mode != mode)
        {
            // keep the same progress along the new path
            const double s0 = xStart(cur_.mode, L), s1 = xEnd(cur_.mode, L);
            double frac = (s1 > s0) ? (cur_.x - s0) / (s1 - s0) : 0.0;
            frac = frac < 0.0 ? 0.0 : (frac > 1.0 ? 1.0 : frac);
            const double n0 = xStart(mode, L), n1 = xEnd(mode, L);
            startFade(Voice{ n0 + frac * (n1 - n0), mode, cur_.moving, { 0.f, 0.f } });
            return;
        }

        if (cur_.moving)
        {
            const double end = xEnd(cur_.mode, L);
            if (cur_.x >= end)
            {
                if (loop)
                    startFade(Voice{ xStart(mode, L), mode, true, { 0.f, 0.f } });
                else // one-shot: the source stops at the end of its path
                    startFade(Voice{ end, mode, false, { 0.f, 0.f } });
            }
        }
        else if (loop)
        {
            startFade(Voice{ xStart(mode, L), mode, true, { 0.f, 0.f } });
        }
    }

    void advanceVoice(Voice& v) const
    {
        if (v.moving)
            v.x += (double)speed_ / fs_;
    }

    void renderVoice(Voice& vc, float* outs)
    {
        const double v    = vc.moving ? (double)speed_ : 0.0;
        const double d    = (double)distance_;
        const double tau0 = d / kSpeedOfSound;
        const double ear  = (double)width_ * kMaxEarOffset;
        const float  pan  = width_ * kMaxPanDepth;

        for (int c = 0; c < ears_; ++c)
        {
            const double earX = ears_ == 1 ? 0.0 : (c == 0 ? -ear : ear);
            const double a    = vc.x - earX;
            const double tau  = propagationTime(a, v, d);
            const double r    = kSpeedOfSound * tau; // distance at emission time, >= d

            double delay = (tau - tau0) * fs_ + 2.0;
            const double maxDelay = (double)(bufSize_ - 4);
            if (delay > maxDelay) delay = maxDelay;
            if (delay < 2.0)      delay = 2.0;

            float s = readHermite(delay);

            const float k = (float)(d / r); // 1 at the closest point, -> 0 far away

            // air absorption: cutoff decreases with distance
            const double fc    = maxCutoff_ * std::sqrt((double)k);
            const float  alpha = (float)(1.0 - std::exp(-2.0 * kPi * fc / fs_));
            vc.lp[c] += alpha * (s - vc.lp[c]);
            s += atten_ * (vc.lp[c] - s);

            // distance attenuation (1/r law, normalized at the closest point)
            float g = 1.f - atten_ + atten_ * k;

            // stereo: attenuate the ear opposite to the source
            if (ears_ > 1)
            {
                const float sinTheta = (float)((a - v * tau) / r);
                if (c == 0)
                    g *= 1.f - pan * (sinTheta > 0.f ? sinTheta : 0.f);
                else
                    g *= 1.f - pan * (sinTheta < 0.f ? -sinTheta : 0.f);
            }

            outs[c] = s * g;
        }
    }

    float readHermite(double delay) const
    {
        const uint32_t di   = (uint32_t)delay;
        const float    t    = (float)(delay - (double)di);
        const uint32_t base = writePos_ - di;

        const float p0 = buf_[(base + 1) & bufMask_];
        const float p1 = buf_[base & bufMask_];
        const float p2 = buf_[(base - 1) & bufMask_];
        const float p3 = buf_[(base - 2) & bufMask_];

        const float c1 = 0.5f * (p2 - p0);
        const float c2 = p0 - 2.5f * p1 + 2.f * p2 - 0.5f * p3;
        const float c3 = 0.5f * (p3 - p0) + 1.5f * (p1 - p2);
        return ((c3 * t + c2) * t + c1) * t + p1;
    }

    double   fs_       = 48000.0;
    int      channels_ = 1;
    int      ears_     = 1; // ears actually computed (1 in mono output mode)

    float*   buf_      = nullptr;
    uint32_t bufSize_  = 0;
    uint32_t bufMask_  = 0;
    uint32_t writePos_ = 0;

    float*   fadeIn_   = nullptr;
    uint32_t fadeLen_  = 0;
    uint32_t fadePos_  = 0;
    bool     fading_   = false;
    bool     triggerPending_ = false;

    Voice    cur_ {};
    Voice    old_ {};

    // smoothed parameters
    float speed_ = 0.f, period_ = 1.f, distance_ = 1.f;
    float atten_ = 0.f, width_ = 0.f, mix_ = 0.f, enabled_ = 1.f;

    float  smoothSlow_ = 0.f;
    float  smoothFast_ = 0.f;
    double maxCutoff_  = 18000.0;
};

} // namespace dopplerit
