/*
 * DopplerIt - Doppler delay for MOD devices, Raspberry Pi 5 and desktop hosts
 * Copyright (C) 2026 pilali
 * SPDX-License-Identifier: MIT
 *
 * Principle
 * ---------
 * An echo delay with feedback whose delay time is swept by a saw-tooth LFO,
 * as if someone was turning the "time" knob of an analog delay while it is
 * playing. A changing delay time is a moving source: while the time gets
 * shorter the repeats are pitched up (approaching source), while it gets
 * longer they are pitched down (receding source). The pitch ratio of what
 * is read is exactly 1 - dD/dt. Repeats travel through the line again and
 * again, so each one is transposed once more: the echoes spiral up or down.
 *
 * Nothing is cut or spliced: the read head simply moves, like the clock of a
 * bucket-brigade delay or the head of a tape echo. The delay time follows the
 * LFO through an "inertia" (one-pole smoothing plus a slew-rate limit), so
 * the fly-back of the saw-tooth becomes a fast swoop instead of a click.
 *
 * In the loop: a tone low-pass (each repeat gets darker), a DC/rumble
 * high-pass and a soft saturation that keeps high feedback settings stable.
 */

#pragma once

#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>

namespace dopplerit {

enum Shape : int {
    kShapeApproach = 0, // delay time decreases (saw down): repeats pitched up
    kShapeRecede   = 1, // delay time increases (saw up): repeats pitched down
    kShapePassBy   = 2, // triangle: up then down
};

struct DelayParams {
    int   shape;    // Shape
    bool  stereo;   // false: same signal on every output
    float time;     // s, base delay time (longest point of the sweep)
    float depth;    // 0..1, sweep range as a fraction of time
    float period;   // s, LFO period
    float feedback; // 0..1
    float tone;     // 0..1, dark .. bright
    float mix;      // 0..1, dry/wet balance
    bool  loop;     // LFO free running; off: one cycle per trigger
};

// Parameter ranges (must match the .ttl files)
static constexpr float kMinTime   = 0.02f;
static constexpr float kMaxTime   = 1.5f;
static constexpr float kMinPeriod = 0.1f;
static constexpr float kMaxPeriod = 16.0f;

class DelayEngine {
public:
    static constexpr int    kMaxChannels   = 2;
    static constexpr double kInertia       = 0.012; // s, smoothing of the delay time
    static constexpr double kMaxRiseRate   = 0.75;  // delay growth: pitch >= 0.25x (-2 oct)
    static constexpr double kMaxFallRate   = 3.0;   // delay shrink: pitch <= 4x (+2 oct)
    static constexpr double kStereoOffset  = 0.125; // LFO phase offset of the right channel
    static constexpr double kPi            = 3.14159265358979323846;

    DelayEngine() = default;
    ~DelayEngine() { release(); }
    DelayEngine(const DelayEngine&) = delete;
    DelayEngine& operator=(const DelayEngine&) = delete;

    bool init(double sampleRate, int channels)
    {
        release();
        fs_       = sampleRate;
        channels_ = channels < 1 ? 1 : (channels > kMaxChannels ? kMaxChannels : channels);

        const double need = kMaxTime * 1.05 * fs_ + 16.0;
        uint32_t size = 1;
        while ((double)size < need)
            size <<= 1;
        size_ = size;
        mask_ = size - 1;

        for (int c = 0; c < kMaxChannels; ++c) {
            line_[c].buf = static_cast<float*>(std::calloc(size_, sizeof(float)));
            if (line_[c].buf == nullptr) {
                release();
                return false;
            }
        }

        smooth_  = (float)(1.0 - std::exp(-1.0 / (0.030 * fs_)));
        inertia_ = 1.0 - std::exp(-1.0 / (kInertia * fs_));
        hpCoef_  = (float)(1.0 - std::exp(-2.0 * kPi * 40.0 / fs_));
        return true;
    }

    void release()
    {
        for (int c = 0; c < kMaxChannels; ++c) {
            std::free(line_[c].buf);
            line_[c].buf = nullptr;
        }
    }

    void reset(const DelayParams& p)
    {
        for (int c = 0; c < kMaxChannels; ++c) {
            if (line_[c].buf != nullptr)
                std::memset(line_[c].buf, 0, size_ * sizeof(float));
            line_[c].lp = line_[c].hp = 0.f;
        }
        writePos_ = 0;

        time_     = clampf(p.time, kMinTime, kMaxTime);
        depth_    = clampf(p.depth, 0.f, 1.f);
        period_   = clampf(p.period, kMinPeriod, kMaxPeriod);
        feedback_ = clampf(p.feedback, 0.f, 1.f);
        tone_     = clampf(p.tone, 0.f, 1.f);
        mix_      = clampf(p.mix, 0.f, 1.f);
        enabled_  = 1.f;
        stereo_   = (channels_ > 1 && p.stereo) ? 1.f : 0.f;

        phase_   = 0.0;
        running_ = p.loop;
        triggerPending_ = false;
        for (int c = 0; c < kMaxChannels; ++c)
            line_[c].d = target(p.shape, lfoPos(c, p.loop));
    }

    // Start one LFO cycle from its beginning (footswitch / trigger button)
    void trigger() { triggerPending_ = true; }

    void process(const float* in, float* const* out, uint32_t frames, const DelayParams& p, bool enabled)
    {
        const float tTime  = clampf(p.time, kMinTime, kMaxTime);
        const float tDepth = clampf(p.depth, 0.f, 1.f);
        const float tPer   = clampf(p.period, kMinPeriod, kMaxPeriod);
        const float tFb    = clampf(p.feedback, 0.f, 1.f);
        const float tTone  = clampf(p.tone, 0.f, 1.f);
        const float tMix   = clampf(p.mix, 0.f, 1.f);
        const float tEn    = enabled ? 1.f : 0.f;
        const int   shape  = p.shape < kShapeApproach ? kShapeApproach : (p.shape > kShapePassBy ? kShapePassBy : p.shape);
        const float tSt    = (channels_ > 1 && p.stereo) ? 1.f : 0.f;
        const double stereoEnd = 1.0 + (channels_ > 1 ? kStereoOffset : 0.0);

        if (triggerPending_) {
            triggerPending_ = false;
            phase_   = 0.0;
            running_ = true;
        }
        if (p.loop && !running_)
            running_ = true;

        // tone: one-pole low-pass from 700 Hz to 16 kHz (exponential)
        for (uint32_t i = 0; i < frames; ++i)
        {
            time_     = tTime; // time changes go through the delay inertia, like a real knob
            depth_   += smooth_ * (tDepth - depth_);
            period_  += smooth_ * (tPer   - period_);
            feedback_+= smooth_ * (tFb    - feedback_);
            tone_    += smooth_ * (tTone  - tone_);
            mix_     += smooth_ * (tMix   - mix_);
            enabled_ += smooth_ * (tEn    - enabled_);
            stereo_  += smooth_ * (tSt    - stereo_);
            if (tSt <= 0.f && stereo_ < 1e-4f)
                stereo_ = 0.f;
            // the second line runs as long as stereo is (partly) on
            const int ch = (channels_ > 1 && stereo_ > 0.f) ? channels_ : 1;

            // LFO
            if (running_) {
                phase_ += 1.0 / ((double)period_ * fs_);
                if (p.loop) {
                    if (phase_ >= 1.0)
                        phase_ -= 1.0;
                } else if (phase_ >= stereoEnd) {
                    phase_   = 0.0;
                    running_ = false;
                }
            }

            if ((i & 15) == 0) {
                const double fc = 700.0 * std::pow(16000.0 / 700.0, (double)tone_);
                lpCoef_ = (float)(1.0 - std::exp(-2.0 * kPi * std::fmin(fc, 0.45 * fs_) / fs_));
            }

            const float x = in[i];
            float wet[kMaxChannels];

            for (int c = 0; c < ch; ++c)
            {
                Line& L = line_[c];

                // delay time follows the LFO with inertia and a slew-rate limit
                const double tgt = target(shape, lfoPos(c, p.loop));
                double step = inertia_ * (tgt - L.d);
                const double up = kMaxRiseRate / fs_, down = kMaxFallRate / fs_;
                step = step > up ? up : (step < -down ? -down : step);
                L.d += step;

                const float y = read(L.buf, L.d * fs_);

                // tone (also tames the aliasing of fast upward sweeps)
                L.lp += lpCoef_ * (y - L.lp);
                wet[c] = L.lp;

                // feedback path: rumble high-pass + soft saturation
                L.hp += hpCoef_ * (L.lp - L.hp);
                const float fb = softClip((L.lp - L.hp) * feedback_);
                L.buf[writePos_] = x + fb;
            }

            if (ch < channels_) {
                // mono: keep the second line in sync for a click-free switch to stereo
                line_[1].d = line_[0].d; line_[1].lp = line_[0].lp; line_[1].hp = line_[0].hp;
                line_[1].buf[writePos_] = line_[0].buf[writePos_];
                wet[1] = wet[0];
            } else {
                wet[1] = wet[0] + stereo_ * (wet[1] - wet[0]);
            }

            const float dryG = mix_ < 0.5f ? 1.f : 2.f * (1.f - mix_);
            const float wetG = mix_ > 0.5f ? 1.f : 2.f * mix_;
            for (int c = 0; c < channels_; ++c) {
                const float processed = dryG * x + wetG * wet[c];
                out[c][i] = x + enabled_ * (processed - x);
            }

            writePos_ = (writePos_ + 1) & mask_;
        }

        for (int c = 0; c < kMaxChannels; ++c) {
            if (std::fabs(line_[c].lp) < 1e-20f) line_[c].lp = 0.f;
            if (std::fabs(line_[c].hp) < 1e-20f) line_[c].hp = 0.f;
        }
    }

    // diagnostics
    double currentDelay(int c) const { return line_[c].d; }
    uint32_t bufferSize() const { return size_; }

private:
    struct Line {
        float* buf = nullptr;
        double d   = 0.1;  // s, current (smoothed) delay time
        float  lp  = 0.f;
        float  hp  = 0.f;
    };

    static float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

    // smooth saturation, ~linear below 0.5, bounded by +/-1
    static float softClip(float x)
    {
        if (x > 3.f) return 1.f;
        if (x < -3.f) return -1.f;
        const float x2 = x * x;
        return x * (27.f + x2) / (27.f + 9.f * x2);
    }

    // LFO position (0..1) of a channel; the right channel lags in stereo
    double lfoPos(int c, bool loop) const
    {
        double p = phase_ - (c == 1 ? kStereoOffset * (double)stereo_ : 0.0);
        if (loop) {
            if (p < 0.0) p += 1.0;
            return p;
        }
        if (!running_ || p < 0.0 || p >= 1.0)
            return 0.0; // at rest
        return p;
    }

    // delay time for an LFO position: longest = time, shortest = time * (1 - depth)
    double target(int shape, double pos) const
    {
        const double hi = time_, lo = time_ * (1.0 - 0.95 * depth_);
        switch (shape) {
        case kShapeApproach: return hi - (hi - lo) * pos;
        case kShapeRecede:   return lo + (hi - lo) * pos;
        default:             return hi - (hi - lo) * (1.0 - std::fabs(2.0 * pos - 1.0));
        }
    }

    float read(const float* buf, double delay) const
    {
        if (delay < 2.0) delay = 2.0;
        const double maxD = (double)(size_ - 4);
        if (delay > maxD) delay = maxD;
        const uint32_t di   = (uint32_t)delay;
        const float    t    = (float)(delay - (double)di);
        const uint32_t base = writePos_ - di;
        const float p0 = buf[(base + 1) & mask_];
        const float p1 = buf[base & mask_];
        const float p2 = buf[(base - 1) & mask_];
        const float p3 = buf[(base - 2) & mask_];
        const float c1 = 0.5f * (p2 - p0);
        const float c2 = p0 - 2.5f * p1 + 2.f * p2 - 0.5f * p3;
        const float c3 = 0.5f * (p3 - p0) + 1.5f * (p1 - p2);
        return ((c3 * t + c2) * t + c1) * t + p1;
    }

    double   fs_       = 48000.0;
    int      channels_ = 1;
    Line     line_[kMaxChannels];
    uint32_t size_     = 0;
    uint32_t mask_     = 0;
    uint32_t writePos_ = 0;

    double phase_   = 0.0;
    bool   running_ = true;
    bool   triggerPending_ = false;

    float stereo_ = 1.f;
    float time_ = 0.4f, depth_ = 0.f, period_ = 1.f, feedback_ = 0.f, tone_ = 0.5f, mix_ = 0.5f, enabled_ = 1.f;
    float smooth_ = 0.f, lpCoef_ = 0.5f, hpCoef_ = 0.f;
    double inertia_ = 0.0;
};

} // namespace dopplerit
