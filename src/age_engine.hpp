/*
 * DopplerIt - Doppler delay for MOD devices, Raspberry Pi 5 and desktop hosts
 * Copyright (C) 2026 pilali
 * SPDX-License-Identifier: MIT
 *
 * "Age" engine: a time-invariant Doppler delay
 * --------------------------------------------
 * Every sound that enters lives its own pass-by while it echoes away: the
 * pitch of a repeat depends only on its age, never on a clock. A note played
 * at any moment gets exactly the same treatment, like a delay.
 *
 * Repeat k is a tap of the delay line at age k * time. Its pitch is the
 * Doppler pass-by curve taken at that age:
 *
 *   Pass-by  : arrival (higher) -> closest point -> departure (lower)
 *   Approach : arrival (higher)  -> back to the original pitch
 *   Recede   : original pitch    -> departure (lower)
 *
 * Speed sets the realistic transposition (12 log2(1 + v/c) semitones each
 * way); Depth multiplies it (up to one octave) to exaggerate the effect.
 *
 * The pass lasts "period" seconds, so it is spread over period / time
 * repeats (at most 24, within 16 seconds). After the pass, a
 * plain feedback delay keeps the repeats at the final pitch until they fade.
 *
 * Taps are parallel reads of one delay line, each transposed once from the
 * original (no compounding of artefacts). A transposing tap is a read head
 * moving at the pitch ratio; when it drifts out of its window around the
 * nominal delay it is moved back by about one window length, with a short
 * cross-fade at the position where the waveforms match best (normalized
 * cross-correlation). Windows are up to 30 ms, so a transposed repeat lands within
 * +/- 15 ms of its nominal time. A tap at the original pitch is a pure delay.
 */

#pragma once

#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>

namespace dopplerit {

enum AgeMode : int {
    kAgeApproach = 0,
    kAgeRecede   = 1,
    kAgePassBy   = 2,
};

struct AgeParams {
    int   mode;     // AgeMode
    float time;     // s, delay between two repeats
    float speedKmh; // vehicle speed: amount of transposition
    float depth;    // 1..8, exaggeration of the transposition (x1 = realistic), max one octave
    float distance; // m, closest distance: sharpness of the pitch drop
    float period;   // s, duration of the pass (in repeat age)
    float feedback; // 0..1, gain from one repeat to the next
    float tone;     // 0..1, dark .. bright
    float mix;      // 0..1, dry/wet balance
    bool  stereo;   // false: same signal on every output
};

static constexpr float kAgeMinTime     = 0.02f;
static constexpr float kAgeMaxTime     = 1.5f;
static constexpr float kAgeMinSpeedKmh = 5.f;
static constexpr float kAgeMaxSpeedKmh = 300.f;
static constexpr float kAgeMinDistance = 1.f;
static constexpr float kAgeMaxDistance = 50.f;
static constexpr float kAgeMinPeriod   = 0.1f;
static constexpr float kAgeMaxPeriod   = 16.f;
static constexpr float kAgeMinDepth    = 1.f;
static constexpr float kAgeMaxDepth    = 8.f;

class AgeDelayEngine {
public:
    static constexpr int    kMaxTaps      = 24;
    static constexpr int    kMaxChannels  = 2;
    static constexpr double kMaxSpan      = 16.0;  // s, age of the last tap
    static constexpr double kMaxSemitones = 12.0;  // transposition limit (one octave)
    static constexpr double kSpeedOfSound = 343.0;
    static constexpr double kMaxWindow    = 0.030; // s, transposition window (+/- 15 ms timing)
    static constexpr double kMinRead      = 0.006; // s, shortest read delay
    static constexpr double kSpliceFade   = 0.015; // s
    static constexpr double kSearch       = 0.006; // s, +/- correlation search
    static constexpr double kCorrLength   = 0.008; // s
    static constexpr double kGlideRate    = 0.5;   // delay change rate when Time jumps
    static constexpr double kStereoAge    = 0.04;  // right ear: curve offset (fraction of period)
    static constexpr double kPi           = 3.14159265358979323846;

    AgeDelayEngine() = default;
    ~AgeDelayEngine() { release(); }
    AgeDelayEngine(const AgeDelayEngine&) = delete;
    AgeDelayEngine& operator=(const AgeDelayEngine&) = delete;

    bool init(double sampleRate, int channels)
    {
        release();
        fs_       = sampleRate;
        channels_ = channels < 1 ? 1 : (channels > kMaxChannels ? kMaxChannels : channels);

        size_ = pow2((kMaxSpan + kMaxWindow + 0.1) * fs_);
        mask_ = size_ - 1;
        tailSize_ = pow2((kAgeMaxTime + 0.1) * fs_);
        tailMask_ = tailSize_ - 1;
        for (int c = 0; c < kMaxChannels; ++c) {
            ch_[c].buf  = static_cast<float*>(std::calloc(size_, sizeof(float)));
            ch_[c].tail = static_cast<float*>(std::calloc(tailSize_, sizeof(float)));
            if (ch_[c].buf == nullptr || ch_[c].tail == nullptr) {
                release();
                return false;
            }
        }
        spliceLen_  = (int)(kSpliceFade * fs_);
        if (spliceLen_ > kMaxFadeLen) spliceLen_ = kMaxFadeLen;
        for (int i = 0; i <= spliceLen_; ++i)
            fadeCurve_[i] = (float)(0.5 - 0.5 * std::cos(kPi * (double)i / (double)spliceLen_));
        searchHalf_ = (int)(kSearch * fs_);
        corrHalf_   = (int)(0.5 * kCorrLength * fs_);
        searchStep_ = fs_ > 60000.0 ? 2 : 1;
        smooth_     = (float)(1.0 - std::exp(-1.0 / (0.030 * fs_)));
        return true;
    }

    void release()
    {
        for (int c = 0; c < kMaxChannels; ++c) {
            std::free(ch_[c].buf);
            std::free(ch_[c].tail);
            ch_[c].buf = ch_[c].tail = nullptr;
        }
    }

    void reset(const AgeParams& p)
    {
        for (int c = 0; c < kMaxChannels; ++c) {
            Chan& C = ch_[c];
            if (C.buf)  std::memset(C.buf, 0, size_ * sizeof(float));
            if (C.tail) std::memset(C.tail, 0, tailSize_ * sizeof(float));
            C.tailLp = 0.f;
        }
        writePos_ = 0;
        time_     = clampf(p.time, kAgeMinTime, kAgeMaxTime);
        speed_    = clampf(p.speedKmh, kAgeMinSpeedKmh, kAgeMaxSpeedKmh) / 3.6f;
        distance_ = clampf(p.distance, kAgeMinDistance, kAgeMaxDistance);
        period_   = clampf(p.period, kAgeMinPeriod, kAgeMaxPeriod);
        depth_    = clampf(p.depth, kAgeMinDepth, kAgeMaxDepth);
        feedback_ = clampf(p.feedback, 0.f, 0.98f);
        tone_     = clampf(p.tone, 0.f, 1.f);
        mix_      = clampf(p.mix, 0.f, 1.f);
        stereo_   = (channels_ > 1 && p.stereo) ? 1.f : 0.f;
        enabled_  = 1.f;
        mode_     = clampMode(p.mode);
        lastKey_[0] = -1.0;
        updateTaps();
        for (int k = 0; k < kMaxTaps; ++k)
            active_[k] = k < taps_ ? 1.f : 0.f;
        for (int c = 0; c < kMaxChannels; ++c)
            for (int k = 0; k < kMaxTaps; ++k) {
                Tap& t = ch_[c].tap[k];
                t.e = delay_[k];
                t.fade = 0;
                t.lp = 0.f;
            }
    }

    void process(const float* in, float* const* out, uint32_t frames, const AgeParams& p, bool enabled)
    {
        const float tTime  = clampf(p.time, kAgeMinTime, kAgeMaxTime);
        const float tSpeed = clampf(p.speedKmh, kAgeMinSpeedKmh, kAgeMaxSpeedKmh) / 3.6f;
        const float tDist  = clampf(p.distance, kAgeMinDistance, kAgeMaxDistance);
        const float tPer   = clampf(p.period, kAgeMinPeriod, kAgeMaxPeriod);
        const float tDepth = clampf(p.depth, kAgeMinDepth, kAgeMaxDepth);
        const float tFb    = clampf(p.feedback, 0.f, 0.98f);
        const float tTone  = clampf(p.tone, 0.f, 1.f);
        const float tMix   = clampf(p.mix, 0.f, 1.f);
        const float tSt    = (channels_ > 1 && p.stereo) ? 1.f : 0.f;
        const float tEn    = enabled ? 1.f : 0.f;
        mode_ = clampMode(p.mode);

        for (uint32_t i = 0; i < frames; ++i)
        {
            time_     = tTime; // Time moves the taps with a glide, like a tape delay
            // the curve is only recomputed while these move: snap when close
            glideTo(speed_, tSpeed);
            glideTo(distance_, tDist);
            glideTo(period_, tPer);
            glideTo(depth_, tDepth);
            glideTo(tone_, tTone);
            feedback_+= smooth_ * (tFb    - feedback_);
            mix_     += smooth_ * (tMix   - mix_);
            enabled_ += smooth_ * (tEn    - enabled_);
            glideTo(stereo_, tSt);

            if ((i & 15) == 0)
                updateTaps();
            for (int k = 0; k < kMaxTaps; ++k)
                active_[k] += smooth_ * ((k < taps_ ? 1.f : 0.f) - active_[k]);

            const float x = in[i];
            const int ch = (channels_ > 1 && stereo_ > 0.f) ? channels_ : 1;
            float wet[kMaxChannels] = { 0.f, 0.f };

            for (int c = 0; c < ch; ++c)
            {
                Chan& C = ch_[c];
                C.buf[writePos_] = x;

                float sum = 0.f, tailIn = 0.f, tailW = 0.f, g = 1.f;
                for (int k = 0; k < kMaxTaps; ++k)
                {
                    const float a = active_[k];
                    if (k >= taps_ && a < 1e-5f) {
                        // idle tap: taps read the shared line, so it can simply
                        // restart at its place when it becomes active again
                        Tap& t = C.tap[k];
                        t.e = delay_[k];
                        t.fade = 0;
                        t.lp = 0.f;
                        g *= feedback_;
                        continue;
                    }
                    const float y = runTap(C, C.tap[k], k, ratio_[c][k]);
                    sum += a * g * y;
                    const float last = a - (k + 1 < kMaxTaps ? active_[k + 1] : 0.f);
                    tailIn += last * y;
                    tailW  += last * g;
                    g *= feedback_;
                }

                // after the pass: plain feedback delay at the final pitch
                const float tOut = readTail(C.tail, (double)time_ * fs_);
                C.tailLp += tapCoef_[kMaxTaps - 1] * (tOut - C.tailLp);
                C.tail[tailPos_] = feedback_ * (tailIn + C.tailLp);
                sum += tailW * C.tailLp;
                wet[c] = sum;
            }

            if (ch < channels_) {
                // mono: keep the right channel in sync for a click-free switch to stereo
                ch_[1].buf[writePos_] = x;
                ch_[1].tail[tailPos_] = ch_[0].tail[tailPos_];
                ch_[1].tailLp = ch_[0].tailLp;
                for (int k = 0; k < kMaxTaps; ++k)
                    ch_[1].tap[k] = ch_[0].tap[k];
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
            tailPos_  = (tailPos_ + 1) & tailMask_;
        }

        for (int c = 0; c < kMaxChannels; ++c) {
            for (int k = 0; k < kMaxTaps; ++k)
                if (std::fabs(ch_[c].tap[k].lp) < 1e-20f) ch_[c].tap[k].lp = 0.f;
            if (std::fabs(ch_[c].tailLp) < 1e-20f) ch_[c].tailLp = 0.f;
        }
    }

    // pitch ratio of a repeat of a given age (s), left ear: for tests and plots
    double pitchAtAge(double age) const { return curve(age, 0.0); }
    int    taps() const { return taps_; }
    uint32_t splices() const { return splices_; }
    uint32_t bufferSize() const { return size_; }

private:
    struct Tap {
        double e = 0.1;     // s, current read delay
        double eOld = 0.1;  // s, previous position while cross-fading
        int    fade = 0;
        float  lp = 0.f;
    };
    struct Chan {
        float* buf  = nullptr;
        float* tail = nullptr;
        float  tailLp = 0.f;
        Tap    tap[kMaxTaps];
    };

    static float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

    void glideTo(float& v, float target) const
    {
        const float d = target - v;
        if (std::fabs(d) <= 1e-6f * (std::fabs(target) + 1e-3f))
            v = target;
        else
            v += smooth_ * d;
    }
    static int clampMode(int m) { return m < kAgeApproach ? kAgeApproach : (m > kAgePassBy ? kAgePassBy : m); }
    static uint32_t pow2(double need)
    {
        uint32_t s = 1;
        while ((double)s < need) s <<= 1;
        return s;
    }

    // Doppler pass-by curve as a function of age (s = closest distance / half
    // path). The plateaus are +/- A semitones, A = depth x 12 log2(1 + v/c)
    // (depth 1: the realistic arrival pitch), limited to one octave.
    double curve(double age, double offset) const
    {
        const double v = (double)speed_, T = (double)period_;
        const double half = mode_ == kAgePassBy ? 0.5 * v * T : v * T;
        const double s = (double)distance_ / (half > 1e-6 ? half : 1e-6);
        double f = age / T + offset;              // 0..1 along the pass
        f = f < 0.0 ? 0.0 : (f > 1.0 ? 1.0 : f);
        const double u = mode_ == kAgePassBy ? 2.0 * f - 1.0
                       : (mode_ == kAgeApproach ? f - 1.0 : f);
        const double w = u / std::sqrt(u * u + s * s) * std::sqrt(1.0 + s * s); // -1 .. 1
        double A = (double)depth_ * 12.0 * std::log2(1.0 + v / kSpeedOfSound);
        A = A > kMaxSemitones ? kMaxSemitones : A;
        return std::exp2(-A * w / 12.0);
    }

    void updateTaps()
    {
        // nothing to do while no parameter of the curve moves
        const double key[8] = { time_, speed_, distance_, period_, depth_, tone_, stereo_, (double)mode_ };
        if (std::memcmp(key, lastKey_, sizeof(key)) == 0)
            return;
        std::memcpy(lastKey_, key, sizeof(key));
        const double t = (double)time_;
        int n = (int)std::floor((double)period_ / t + 0.5);
        const int maxN = (int)std::floor(kMaxSpan / t);
        n = n < 1 ? 1 : n;
        n = n > kMaxTaps ? kMaxTaps : n;
        n = n > maxN ? (maxN < 1 ? 1 : maxN) : n;
        taps_ = n;
        for (int k = 0; k < kMaxTaps; ++k) {
            const double age = (double)(k + 1) * t;
            delay_[k] = age < kMaxSpan ? age : kMaxSpan;
            double w = 2.0 * (delay_[k] - kMinRead);
            window_[k] = w < kMaxWindow ? w : kMaxWindow;
            ratio_[0][k] = curve(age, 0.0);
            ratio_[1][k] = curve(age, -kStereoAge * (double)stereo_);
            // tone: each repeat a bit darker than the previous one
            const double fc = 700.0 * std::pow(16000.0 / 700.0, (double)tone_) / std::sqrt((double)(k + 1));
            tapCoef_[k] = (float)(1.0 - std::exp(-2.0 * kPi * std::fmin(std::fmax(fc, 200.0), 0.45 * fs_) / fs_));
        }
    }

    float runTap(Chan& C, Tap& t, int k, double ratio)
    {
        const double D = delay_[k], W = window_[k];
        const double rate = (1.0 - ratio) / fs_;

        const bool still = std::fabs(1.0 - ratio) < 1e-5;
        if (std::fabs(t.e - D) > W || (still && t.fade == 0 && std::fabs(t.e - D) > 0.5 / fs_)) {
            // Time changed (or an untransposed tap is off its place): glide the
            // head to its new position, a tape-like pitch bend
            const double lim = kGlideRate / fs_;
            double step = D - t.e;
            step = step > lim ? lim : (step < -lim ? -lim : step);
            t.e += step;
        } else {
            t.e += rate;
            if (t.fade == 0 && W > 0.004) {
                if (t.e < D - 0.5 * W)
                    splice(C, t, D, W, +1.0);
                else if (t.e > D + 0.5 * W)
                    splice(C, t, D, W, -1.0);
            }
        }

        float y = read(C.buf, t.e * fs_);
        if (t.fade > 0) {
            t.eOld += rate;
            const float yo = read(C.buf, t.eOld * fs_);
            const float g = fadeCurve_[spliceLen_ - t.fade];
            y = yo + g * (y - yo);
            --t.fade;
        }
        t.lp += tapCoef_[k] * (y - t.lp);
        return t.lp;
    }

    // move a drifting head back by about one window, where the waveforms match
    void splice(Chan& C, Tap& t, double D, double W, double dir)
    {
        const double S = (double)searchHalf_ / fs_;
        double shift = dir * (W - S);
        const double lo = D - 0.5 * W, hi = D + 0.5 * W;
        const uint32_t from = (uint32_t)(t.e * fs_ + 2.0);
        // coarse search (every 4th position, decimated window), then refine
        int bestS = 0;
        float bestScore = -2.f;
        for (int pass = 0; pass < 2; ++pass) {
            const int step   = pass == 0 ? 4 * searchStep_ : 1;
            const int from_s = pass == 0 ? -searchHalf_ : bestS - 3 * searchStep_;
            const int to_s   = pass == 0 ?  searchHalf_ : bestS + 3 * searchStep_;
            const int stride = pass == 0 ? 4 : 2;
            if (pass == 1) bestScore = -2.f;
            const int coarseBest = bestS;
            for (int sIdx = from_s; sIdx <= to_s; sIdx += step) {
                if (sIdx < -searchHalf_ || sIdx > searchHalf_)
                    continue;
                const double cand = t.e + shift + (double)sIdx / fs_;
                if (cand < lo || cand > hi || cand < kMinRead)
                    continue;
                const float score = correlation(C.buf, from, (uint32_t)(cand * fs_ + 2.0), stride);
                if (score > bestScore) {
                    bestScore = score;
                    bestS = sIdx;
                }
            }
            if (pass == 1 && bestScore <= -2.f)
                bestS = coarseBest;
        }
        const double best = shift + (double)bestS / fs_;
        t.eOld = t.e;
        t.e += best;
        t.fade = spliceLen_;
        ++splices_;
    }

    float correlation(const float* buf, uint32_t da, uint32_t db, int stride) const
    {
        float ab = 0.f, aa = 0.f, bb = 0.f;
        const uint32_t pa = writePos_ - da, pb = writePos_ - db;
        for (int k = -corrHalf_; k <= corrHalf_; k += stride) {
            const float a = buf[(pa + (uint32_t)k) & mask_];
            const float b = buf[(pb + (uint32_t)k) & mask_];
            ab += a * b;
            aa += a * a;
            bb += b * b;
        }
        return ab / std::sqrt(aa * bb + 1e-12f);
    }

    float read(const float* buf, double delay) const { return hermite(buf, mask_, writePos_, delay); }
    float readTail(const float* buf, double delay) const { return hermite(buf, tailMask_, tailPos_, delay); }

    static float hermite(const float* buf, uint32_t mask, uint32_t wpos, double delay)
    {
        if (delay < 2.0) delay = 2.0;
        if (delay > (double)(mask - 4)) delay = (double)(mask - 4);
        const uint32_t di   = (uint32_t)delay;
        const float    t    = (float)(delay - (double)di);
        const uint32_t base = wpos - di;
        const float p0 = buf[(base + 1) & mask];
        const float p1 = buf[base & mask];
        const float p2 = buf[(base - 1) & mask];
        const float p3 = buf[(base - 2) & mask];
        const float c1 = 0.5f * (p2 - p0);
        const float c2 = p0 - 2.5f * p1 + 2.f * p2 - 0.5f * p3;
        const float c3 = 0.5f * (p3 - p0) + 1.5f * (p1 - p2);
        return ((c3 * t + c2) * t + c1) * t + p1;
    }

    double   fs_ = 48000.0;
    int      channels_ = 1;
    Chan     ch_[kMaxChannels];
    uint32_t size_ = 0, mask_ = 0, tailSize_ = 0, tailMask_ = 0;
    uint32_t writePos_ = 0, tailPos_ = 0;

    int    mode_ = kAgePassBy;
    int    taps_ = 1;
    float  active_[kMaxTaps] = {};
    double delay_[kMaxTaps] = {};
    double window_[kMaxTaps] = {};
    double ratio_[kMaxChannels][kMaxTaps] = {};
    float  tapCoef_[kMaxTaps] = {};

    static constexpr int kMaxFadeLen = 4096;
    float fadeCurve_[kMaxFadeLen + 1] = {};
    int spliceLen_ = 720, searchHalf_ = 288, corrHalf_ = 192, searchStep_ = 1;
    uint32_t splices_ = 0;

    double lastKey_[8] = { -1.0 };
    float time_ = 0.25f, speed_ = 27.8f, distance_ = 5.f, period_ = 2.f, depth_ = 1.f;
    float feedback_ = 0.5f, tone_ = 0.6f, mix_ = 0.5f, stereo_ = 1.f, enabled_ = 1.f, smooth_ = 0.f;
};

} // namespace dopplerit
