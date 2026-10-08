/*
 * DopplerIt - Doppler delay for MOD devices, Raspberry Pi 5 and desktop hosts
 * Copyright (C) 2026 pilali
 * SPDX-License-Identifier: MIT
 *
 * Principle
 * ---------
 * A multi-head echo, like a tape delay with 1 to 4 playback heads, where
 * every head applies a Doppler "pass-by" to what it reads.
 *
 * A changing delay time is a moving source: while the delay gets shorter the
 * signal is pitched up (arrival), while it gets longer it is pitched down
 * (departure). The pitch ratio of what a head reads is exactly 1 - dD/dt.
 *
 * Each LFO cycle is one vehicle passing by: the delay follows the distance
 * curve of a straight pass, sqrt(x^2 + d^2) (hyperbola), down to its closest
 * point then back up to exactly where it started. So at the end of a cycle
 * the delay is continuous (no click, no swoop) while its slope flips: the
 * pitch jumps straight from "departure" back to "arrival", the next vehicle.
 *
 *   speed    -> plateau pitch ratios 1 + v/c (arrival) and 1 - v/c (departure)
 *   distance -> how abrupt the pitch drop is at the closest point
 *   period   -> time between two vehicles
 *
 * Heads are in series: head k delays the output of head k-1 by "time" and
 * applies the pass-by again, so the transposition compounds from head to
 * head. All heads are heard (summed); the last one feeds back to the first.
 * "Stagger" offsets the pass-by of each head (0: all heads in sync).
 *
 * In each stage a tone low-pass (repeats get darker); in the feedback path a
 * rumble high-pass and a soft saturation that keeps the loop stable.
 * Nothing is cut or spliced: read heads only move.
 */

#pragma once

#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>

namespace dopplerit {

struct DelayParams {
    int   heads;    // 1..4
    float time;     // s, mean delay of each head (spacing between heads)
    float speedKmh; // vehicle speed: pitch amount
    float distance; // m, closest distance: sharpness of the pitch drop
    float period;   // s, one pass-by per period
    float stagger;  // 0..1, LFO offset between consecutive heads (fraction of a cycle)
    float feedback; // 0..1
    float tone;     // 0..1, dark .. bright
    float mix;      // 0..1, dry/wet balance
    bool  stereo;   // false: same signal on every output
    bool  loop;     // continuous pass-bys; off: one pass-by per trigger
};

// Parameter ranges (must match the .ttl files)
static constexpr int   kMaxHeads     = 4;
static constexpr float kMinTime      = 0.02f;
static constexpr float kMaxTime      = 1.5f;
static constexpr float kMinSpeedKmh  = 5.f;
static constexpr float kMaxSpeedKmh  = 300.f;
static constexpr float kMinDistance  = 1.f;
static constexpr float kMaxDistance  = 50.f;
static constexpr float kMinPeriod    = 0.1f;
static constexpr float kMaxPeriod    = 16.f;

class DelayEngine {
public:
    static constexpr int    kMaxChannels  = 2;
    static constexpr double kSpeedOfSound = 343.0;
    static constexpr double kMaxSwing     = 1.6;   // peak-to-peak delay excursion <= 1.6 x time
    static constexpr double kInertia      = 0.003; // s, smoothing of the delay time
    static constexpr double kMaxShrink    = 1.0;   // delay shrink rate limit: pitch <= 2x per head
    static constexpr double kMaxGrow      = 0.5;   // delay growth rate limit: pitch >= 0.5x per head
    static constexpr double kStereoOffset = 0.03;  // LFO lag of the right channel (cycle)
    static constexpr double kPi           = 3.14159265358979323846;

    DelayEngine() = default;
    ~DelayEngine() { release(); }
    DelayEngine(const DelayEngine&) = delete;
    DelayEngine& operator=(const DelayEngine&) = delete;

    bool init(double sampleRate, int channels)
    {
        release();
        fs_       = sampleRate;
        channels_ = channels < 1 ? 1 : (channels > kMaxChannels ? kMaxChannels : channels);

        const double need = kMaxTime * (1.0 + 0.5 * kMaxSwing) * fs_ + 16.0;
        uint32_t size = 1;
        while ((double)size < need)
            size <<= 1;
        size_ = size;
        mask_ = size - 1;

        for (int c = 0; c < kMaxChannels; ++c)
            for (int h = 0; h < kMaxHeads; ++h) {
                stage_[c][h].buf = static_cast<float*>(std::calloc(size_, sizeof(float)));
                if (stage_[c][h].buf == nullptr) {
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
        for (int c = 0; c < kMaxChannels; ++c)
            for (int h = 0; h < kMaxHeads; ++h) {
                std::free(stage_[c][h].buf);
                stage_[c][h].buf = nullptr;
            }
    }

    void reset(const DelayParams& p)
    {
        for (int c = 0; c < kMaxChannels; ++c) {
            for (int h = 0; h < kMaxHeads; ++h) {
                Stage& s = stage_[c][h];
                if (s.buf != nullptr)
                    std::memset(s.buf, 0, size_ * sizeof(float));
                s.lp = 0.f;
            }
            fbHp_[c] = 0.f;
            fbOut_[c] = 0.f;
        }
        writePos_ = 0;

        time_     = clampf(p.time, kMinTime, kMaxTime);
        speed_    = clampf(p.speedKmh, kMinSpeedKmh, kMaxSpeedKmh) / 3.6f;
        distance_ = clampf(p.distance, kMinDistance, kMaxDistance);
        period_   = clampf(p.period, kMinPeriod, kMaxPeriod);
        stagger_  = clampf(p.stagger, 0.f, 1.f);
        feedback_ = clampf(p.feedback, 0.f, 1.f);
        tone_     = clampf(p.tone, 0.f, 1.f);
        mix_      = clampf(p.mix, 0.f, 1.f);
        stereo_   = (channels_ > 1 && p.stereo) ? 1.f : 0.f;
        enabled_  = 1.f;
        heads_    = clampHeads(p.heads);
        for (int h = 0; h < kMaxHeads; ++h)
            headGain_[h] = h < heads_ ? 1.f : 0.f;

        phase_   = 0.0;
        running_ = p.loop;
        triggerPending_ = false;
        updateCurve();
        lpCoef_ = toneCoef();
        for (int c = 0; c < kMaxChannels; ++c)
            for (int h = 0; h < kMaxHeads; ++h)
                stage_[c][h].d = targetDelay(c, h, p.loop);
    }

    // Start one pass-by from its beginning (footswitch / trigger button)
    void trigger() { triggerPending_ = true; }

    void process(const float* in, float* const* out, uint32_t frames, const DelayParams& p, bool enabled)
    {
        const float tTime  = clampf(p.time, kMinTime, kMaxTime);
        const float tSpeed = clampf(p.speedKmh, kMinSpeedKmh, kMaxSpeedKmh) / 3.6f;
        const float tDist  = clampf(p.distance, kMinDistance, kMaxDistance);
        const float tPer   = clampf(p.period, kMinPeriod, kMaxPeriod);
        const float tStag  = clampf(p.stagger, 0.f, 1.f);
        const float tFb    = clampf(p.feedback, 0.f, 1.f);
        const float tTone  = clampf(p.tone, 0.f, 1.f);
        const float tMix   = clampf(p.mix, 0.f, 1.f);
        const float tSt    = (channels_ > 1 && p.stereo) ? 1.f : 0.f;
        const float tEn    = enabled ? 1.f : 0.f;
        const int   tHeads = clampHeads(p.heads);

        if (triggerPending_) {
            triggerPending_ = false;
            phase_   = 0.0;
            running_ = true;
        }
        if (p.loop && !running_)
            running_ = true;

        // a one-shot pass-by lasts until the most delayed head has finished
        const double oneShotEnd = 1.0 + (double)(kMaxHeads - 1) * (double)tStag + kStereoOffset;

        for (uint32_t i = 0; i < frames; ++i)
        {
            time_     = tTime; // goes through the delay inertia, like turning a real knob
            speed_   += smooth_ * (tSpeed - speed_);
            distance_+= smooth_ * (tDist  - distance_);
            period_  += smooth_ * (tPer   - period_);
            stagger_ += smooth_ * (tStag  - stagger_);
            feedback_+= smooth_ * (tFb    - feedback_);
            tone_    += smooth_ * (tTone  - tone_);
            mix_     += smooth_ * (tMix   - mix_);
            enabled_ += smooth_ * (tEn    - enabled_);
            stereo_  += smooth_ * (tSt    - stereo_);
            if (tSt <= 0.f && stereo_ < 1e-4f)
                stereo_ = 0.f;
            for (int h = 0; h < kMaxHeads; ++h)
                headGain_[h] += smooth_ * ((h < tHeads ? 1.f : 0.f) - headGain_[h]);

            if (running_) {
                phase_ += 1.0 / ((double)period_ * fs_);
                if (p.loop) {
                    if (phase_ >= 1.0)
                        phase_ -= 1.0;
                } else if (phase_ >= oneShotEnd) {
                    phase_   = 0.0;
                    running_ = false;
                }
            }

            if ((i & 15) == 0) {
                lpCoef_ = toneCoef();
                updateCurve();
            }

            const float x = in[i];
            const int ch = (channels_ > 1 && stereo_ > 0.f) ? channels_ : 1;
            float wet[kMaxChannels] = { 0.f, 0.f };
            float norm = 0.f;
            for (int h = 0; h < kMaxHeads; ++h)
                norm += headGain_[h] * headGain_[h];
            norm = 1.f / std::sqrt(norm > 1e-6f ? norm : 1e-6f);

            for (int c = 0; c < ch; ++c)
            {
                // the first stage receives the input plus the feedback of the last head
                // all stages always run (an idle head must not keep stale audio)
                float sig = x + fbOut_[c];
                float sum = 0.f, last = 0.f;
                for (int h = 0; h < kMaxHeads; ++h)
                {
                    Stage& s = stage_[c][h];
                    s.buf[writePos_] = sig;

                    const double tgt = targetDelay(c, h, p.loop);
                    double step = inertia_ * (tgt - s.d);
                    const double up = kMaxGrow / fs_, down = kMaxShrink / fs_;
                    step = step > up ? up : (step < -down ? -down : step);
                    s.d += step;

                    const float y = read(s.buf, s.d * fs_);
                    s.lp += lpCoef_ * (y - s.lp);
                    sig = s.lp;
                    sum += headGain_[h] * sig;
                    // weight of "last active head", cross-faded when the head count changes
                    const float next = h + 1 < kMaxHeads ? headGain_[h + 1] : 0.f;
                    last += (headGain_[h] - next) * sig;
                }
                wet[c] = sum * norm;

                // feedback from the last active head
                fbHp_[c] += hpCoef_ * (last - fbHp_[c]);
                fbOut_[c] = softClip((last - fbHp_[c]) * feedback_);
            }

            if (ch < channels_) {
                // mono: keep the right channel in sync for a click-free switch to stereo
                for (int h = 0; h < kMaxHeads; ++h) {
                    stage_[1][h].d  = stage_[0][h].d;
                    stage_[1][h].lp = stage_[0][h].lp;
                    stage_[1][h].buf[writePos_] = stage_[0][h].buf[writePos_];
                }
                fbHp_[1] = fbHp_[0];
                fbOut_[1] = fbOut_[0];
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
            for (int h = 0; h < kMaxHeads; ++h)
                if (std::fabs(stage_[c][h].lp) < 1e-20f) stage_[c][h].lp = 0.f;
            if (std::fabs(fbHp_[c]) < 1e-20f) fbHp_[c] = 0.f;
        }
    }

    // diagnostics
    double currentDelay(int c, int h) const { return stage_[c][h].d; }
    uint32_t bufferSize() const { return size_; }

private:
    struct Stage {
        float* buf = nullptr;
        double d   = 0.1; // s, current (smoothed) delay
        float  lp  = 0.f;
    };

    static float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
    static int clampHeads(int h) { return h < 1 ? 1 : (h > kMaxHeads ? kMaxHeads : h); }

    // smooth saturation, ~linear below 0.5, bounded by +/-1
    static float softClip(float x)
    {
        if (x > 3.f) return 1.f;
        if (x < -3.f) return -1.f;
        const float x2 = x * x;
        return x * (27.f + x2) / (27.f + 9.f * x2);
    }

    // tone: one-pole low-pass from 700 Hz to 16 kHz (exponential)
    float toneCoef() const
    {
        const double fc = 700.0 * std::pow(16000.0 / 700.0, (double)tone_);
        return (float)(1.0 - std::exp(-2.0 * kPi * std::fmin(fc, 0.45 * fs_) / fs_));
    }

    // Pass-by curve: the delay follows sqrt(u^2 + s^2) - s (normalized), u
    // going from -1 to 1 along the cycle, s = closest distance / half path.
    // Excursion chosen so that the plateau slopes give pitch ratios 1 +/- v/c.
    void updateCurve()
    {
        const double v     = (double)speed_;
        const double half  = v * (double)period_ * 0.5;            // half path length (m)
        curveS_            = (double)distance_ / (half > 1e-6 ? half : 1e-6);
        curveH1_           = std::sqrt(1.0 + curveS_ * curveS_) - curveS_;
        // plateau slope of D: dD/dt = swing * 2 / (period * h1) = v / c
        double swing       = (v / kSpeedOfSound) * (double)period_ * curveH1_ * 0.5;
        const double maxSw = kMaxSwing * (double)time_;
        swing_             = swing > maxSw ? maxSw : swing;
    }

    // LFO position (0..1) of a head of a channel
    double lfoPos(int c, int h, bool loop) const
    {
        double p = phase_ - (double)h * (double)stagger_ - (c == 1 ? kStereoOffset * (double)stereo_ : 0.0);
        if (loop) {
            p -= std::floor(p);
            return p;
        }
        if (!running_ || p < 0.0 || p >= 1.0)
            return 0.0; // at rest (start of the curve: no motion)
        return p;
    }

    double targetDelay(int c, int h, bool loop) const
    {
        const double u  = 2.0 * lfoPos(c, h, loop) - 1.0;
        const double hn = (std::sqrt(u * u + curveS_ * curveS_) - curveS_) / (curveH1_ > 1e-9 ? curveH1_ : 1e-9);
        // hn: 1 at both ends of the cycle, 0 at the closest point
        return (double)time_ + swing_ * (hn - 0.5);
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
    Stage    stage_[kMaxChannels][kMaxHeads];
    float    fbHp_[kMaxChannels]  = { 0.f, 0.f };
    float    fbOut_[kMaxChannels] = { 0.f, 0.f };
    uint32_t size_     = 0;
    uint32_t mask_     = 0;
    uint32_t writePos_ = 0;

    double phase_   = 0.0;
    bool   running_ = true;
    bool   triggerPending_ = false;

    int   heads_ = 1;
    float headGain_[kMaxHeads] = { 1.f, 0.f, 0.f, 0.f };
    float time_ = 0.3f, speed_ = 20.f, distance_ = 10.f, period_ = 2.f, stagger_ = 0.f;
    float feedback_ = 0.f, tone_ = 0.5f, mix_ = 0.5f, stereo_ = 1.f, enabled_ = 1.f;
    float smooth_ = 0.f, lpCoef_ = 0.5f, hpCoef_ = 0.f;
    double inertia_ = 0.0;
    double curveS_ = 0.1, curveH1_ = 0.9, swing_ = 0.0;
};

} // namespace dopplerit
